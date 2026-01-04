#include "JIT.h"
#include "Error.h"

#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/Version.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Job.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/HeaderSearchOptions.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/ExecutionEngine/Orc/EPCDynamicLibrarySearchGenerator.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>

namespace clap_rt {

namespace {
namespace orc = llvm::orc;

// Auto-detect libstdc++ include path
std::string detectLibstdcxxPath() {
  // Find the latest GCC version's include path
  const std::filesystem::path base = "/usr/include/c++";

  if (!std::filesystem::exists(base))
    return "";

  std::string latestVersion;
  for (const auto &entry : std::filesystem::directory_iterator(base)) {
    if (entry.is_directory()) {
      std::string name = entry.path().filename().string();
      // Skip non-version directories (like "v1" from libc++)
      if (!name.empty() && std::isdigit(name[0])) {
        if (latestVersion.empty() || name > latestVersion)
          latestVersion = name;
      }
    }
  }

  if (!latestVersion.empty())
    return (base / latestVersion).string();

  return "";
}

// Auto-detect clang builtin include path
std::string detectClangIncludePath() {
  std::string version = std::to_string(CLANG_VERSION_MAJOR);

  std::vector<std::string> candidates = {
      "/usr/lib/clang/" + version + "/include",
      "/usr/local/lib/clang/" + version + "/include",
  };

  for (const auto &path : candidates) {
    if (std::filesystem::exists(path))
      return path;
  }
  return "/usr/lib/clang/" + version + "/include"; // Fallback
}

} // anonymous namespace

void ClapJIT::initializeLLVM() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();
}

llvm::Expected<ClapJIT> ClapJIT::create(JITOptions opts) {
  ClapJIT jit;
  jit.options_ = std::move(opts);

  auto JITOrErr = orc::LLJITBuilder().create();
  if (!JITOrErr)
    return JITOrErr.takeError();

  jit.llJIT_ = std::move(*JITOrErr);

  // Allow JIT to resolve symbols from the host process and C++ runtime
  auto &ES = jit.llJIT_->getExecutionSession();
  auto &MainJD = jit.llJIT_->getMainJITDylib();

  // Load libstdc++ for STL support
  auto LibStdCxxGen = orc::EPCDynamicLibrarySearchGenerator::Load(ES, "libstdc++.so.6");
  if (!LibStdCxxGen) {
    LibStdCxxGen = orc::EPCDynamicLibrarySearchGenerator::Load(ES, "libstdc++.so");
  }
  if (LibStdCxxGen)
    MainJD.addGenerator(std::move(*LibStdCxxGen));

  // Also search in the host process
  auto DLSG = orc::EPCDynamicLibrarySearchGenerator::GetForTargetProcess(ES);
  if (!DLSG)
    return DLSG.takeError();
  MainJD.addGenerator(std::move(*DLSG));

  return jit;
}

llvm::Expected<std::unique_ptr<llvm::Module>>
ClapJIT::compileSingleFile(llvm::StringRef FilePath, llvm::LLVMContext &Ctx) {
  // Build command-line arguments for clang
  std::vector<std::string> argStorage;
  std::vector<const char *> Args;

  argStorage.push_back("clang++");

  // Language standard
  switch (options_.langStandard) {
  case LangStandard::CXX14:
    argStorage.push_back("-std=c++14");
    break;
  case LangStandard::CXX17:
    argStorage.push_back("-std=c++17");
    break;
  case LangStandard::CXX20:
    argStorage.push_back("-std=c++20");
    break;
  }

  // Linux: Use libstdc++ (system default)
  std::string libstdcxxPath = detectLibstdcxxPath();
  if (!libstdcxxPath.empty()) {
    argStorage.push_back("-I" + libstdcxxPath);
    // Platform-specific headers (e.g., bits/c++config.h)
    std::string platformPath = libstdcxxPath + "/../../x86_64-linux-gnu/" +
                               std::filesystem::path(libstdcxxPath).filename().string();
    if (std::filesystem::exists(platformPath)) {
      argStorage.push_back("-I" + platformPath);
    }
  }

  // Clang builtins
  std::string clangPath = detectClangIncludePath();
  if (!clangPath.empty()) {
    argStorage.push_back("-I" + clangPath);
  }

  // System headers
  argStorage.push_back("-I/usr/include/x86_64-linux-gnu");
  argStorage.push_back("-I/usr/include");

  // Add user include paths
  for (const auto &path : options_.includePaths) {
    argStorage.push_back("-I" + path);
  }

  // File to compile
  argStorage.push_back(FilePath.str());

  // Output as LLVM IR (not used, but needed for driver)
  argStorage.push_back("-emit-llvm");
  argStorage.push_back("-c");

  // Build pointer array from storage
  for (const auto &arg : argStorage) {
    Args.push_back(arg.c_str());
  }

  // Set up diagnostics
  std::string diagOutput;
  llvm::raw_string_ostream diagStream(diagOutput);
  clang::DiagnosticOptions diagOpts;
  auto diagPrinter = new clang::TextDiagnosticPrinter(diagStream, diagOpts);
  llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> Diags =
      new clang::DiagnosticsEngine(
          llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs>(
              new clang::DiagnosticIDs()),
          diagOpts, diagPrinter, false);

  std::string triple = options_.targetTriple.empty()
                           ? llvm::sys::getProcessTriple()
                           : options_.targetTriple;

  clang::driver::Driver Driver("clang++", triple, *Diags);
  Driver.setCheckInputsExist(false);

  std::unique_ptr<clang::driver::Compilation> C(Driver.BuildCompilation(Args));
  if (!C) {
    return makeError(ErrorCode::CompilationFailed,
                     "Failed to create compilation", FilePath);
  }

  const clang::driver::JobList &Jobs = C->getJobs();
  if (Jobs.size() != 1 || !clang::isa<clang::driver::Command>(*Jobs.begin())) {
    return makeError(ErrorCode::CompilationFailed,
                     "Expected exactly one compile job", FilePath);
  }

  const clang::driver::Command &Cmd =
      clang::cast<clang::driver::Command>(*Jobs.begin());
  const llvm::opt::ArgStringList &CCArgs = Cmd.getArguments();

  // Create compiler invocation from driver-generated args
  clang::CompilerInstance CI;
  if (!clang::CompilerInvocation::CreateFromArgs(CI.getInvocation(), CCArgs, *Diags)) {
    diagStream.flush();
    return makeError(ErrorCode::CompilationFailed,
                     "Failed to create CompilerInvocation: " + diagOutput,
                     FilePath);
  }

  CI.createDiagnostics(diagPrinter, false);
  if (!CI.hasDiagnostics()) {
    return makeError(ErrorCode::CompilationFailed,
                     "Failed to create diagnostics", FilePath);
  }

  // Execute the EmitLLVMOnlyAction
  auto Act = std::make_unique<clang::EmitLLVMOnlyAction>(&Ctx);

  if (!CI.ExecuteAction(*Act)) {
    diagStream.flush();
    std::string errMsg = "Compilation failed";
    if (!diagOutput.empty()) {
      errMsg += ": " + diagOutput;
    }
    return makeError(ErrorCode::CompilationFailed, errMsg, FilePath);
  }

  std::unique_ptr<llvm::Module> M = Act->takeModule();
  if (!M) {
    return makeError(ErrorCode::ModuleGenerationFailed,
                     "No module generated after compilation", FilePath);
  }

  return M;
}

llvm::Error ClapJIT::addModule(llvm::StringRef FilePath) {
  // Check if we have a valid cache
  std::string cachePath = getCachePath(FilePath);
  std::string symPath = cachePath.empty() ? "" : cachePath + ".sym";

  if (isCacheValid(FilePath, cachePath)) {
    // Try to load symbols from cache
    if (std::filesystem::exists(symPath)) {
      std::ifstream symFile(symPath);
      std::string line;
      while (std::getline(symFile, line)) {
        auto sep = line.find('\t');
        if (sep != std::string::npos) {
          symbols_.emplace_back(line.substr(0, sep), line.substr(sep + 1));
        }
      }
      // Load cached object - full cache hit
      return loadCachedObject(cachePath);
    }
  }

  // Compile from source
  auto Ctx = std::make_unique<llvm::LLVMContext>();
  auto IROrErr = compileSingleFile(FilePath, *Ctx);
  if (!IROrErr)
    return IROrErr.takeError();

  // Collect symbols
  std::vector<SymbolEntry> newSymbols;
  for (const auto &F : **IROrErr) {
    if (!F.isDeclaration()) {
      std::string mangled = F.getName().str();
      std::string demangled = llvm::demangle(mangled);
      newSymbols.emplace_back(demangled, mangled);
    }
  }
  symbols_.insert(symbols_.end(), newSymbols.begin(), newSymbols.end());

  // Save to cache if caching is enabled
  if (!cachePath.empty()) {
    if (auto Err = compileAndCache(**IROrErr, cachePath)) {
      llvm::consumeError(std::move(Err));
    } else {
      // Save symbols to cache
      std::ofstream symFile(symPath);
      for (const auto &[demangled, mangled] : newSymbols) {
        symFile << demangled << '\t' << mangled << '\n';
      }
    }
  }

  auto TSM = orc::ThreadSafeModule(std::move(*IROrErr), std::move(Ctx));

  if (auto Err = llJIT_->addIRModule(std::move(TSM)))
    return Err;

  return llvm::Error::success();
}

llvm::Error ClapJIT::addModules(llvm::ArrayRef<llvm::StringRef> FilePaths) {
  for (const auto &FilePath : FilePaths) {
    if (auto Err = addModule(FilePath))
      return Err;
  }
  return llvm::Error::success();
}

llvm::Error ClapJIT::defineSymbol(llvm::StringRef Name, void *Addr) {
  auto &MainJD = llJIT_->getMainJITDylib();
  auto Symbol = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(Addr),
      llvm::JITSymbolFlags::Exported);
  return MainJD.define(
      llvm::orc::absoluteSymbols({{llJIT_->mangleAndIntern(Name), Symbol}}));
}

std::optional<std::string>
ClapJIT::findSymbol(llvm::StringRef FunctionName) const {
  for (const auto &[demangled, mangled] : symbols_) {
    // Check if demangled name starts with the function name
    // e.g., "add_cxx(int, int)" starts with "add_cxx"
    if (demangled.starts_with(FunctionName.str()) &&
        (demangled.size() == FunctionName.size() ||
         demangled[FunctionName.size()] == '(')) {
      return mangled;
    }
  }
  return std::nullopt;
}

llvm::Expected<orc::ExecutorAddr>
ClapJIT::lookup(llvm::StringRef SymbolName) const {
  return llJIT_->lookup(SymbolName);
}

llvm::Expected<orc::ExecutorAddr>
ClapJIT::lookupFunction(llvm::StringRef FunctionName) const {
  // First, try to find the mangled name (for C++ functions)
  if (auto mangled = findSymbol(FunctionName)) {
    return lookup(*mangled);
  }
  // Fall back to direct lookup (for extern "C" functions)
  return lookup(FunctionName);
}

std::string ClapJIT::getCachePath(llvm::StringRef SourcePath) const {
  if (options_.cacheDir.empty())
    return "";

  // Create cache filename from source path hash
  std::filesystem::path srcPath(SourcePath.str());
  std::string filename = srcPath.filename().string();

  // Simple hash of full path to avoid collisions
  std::size_t hash = std::hash<std::string>{}(SourcePath.str());

  std::filesystem::path cachePath = options_.cacheDir;
  cachePath /= filename + "." + std::to_string(hash) + ".o";

  return cachePath.string();
}

bool ClapJIT::isCacheValid(llvm::StringRef SourcePath,
                            llvm::StringRef CachePath) const {
  if (CachePath.empty())
    return false;

  std::filesystem::path srcPath(SourcePath.str());
  std::filesystem::path cachePath(CachePath.str());

  if (!std::filesystem::exists(cachePath))
    return false;

  auto srcTime = std::filesystem::last_write_time(srcPath);
  auto cacheTime = std::filesystem::last_write_time(cachePath);

  return cacheTime >= srcTime;
}

llvm::Error ClapJIT::compileAndCache(llvm::Module &M,
                                      llvm::StringRef CachePath) {
  // Get target machine
  llvm::Triple triple(options_.targetTriple.empty()
                          ? llvm::sys::getProcessTriple()
                          : options_.targetTriple);

  std::string Error;
  const llvm::Target *Target = llvm::TargetRegistry::lookupTarget(triple, Error);
  if (!Target) {
    return llvm::make_error<llvm::StringError>(
        "Failed to get target: " + Error, llvm::inconvertibleErrorCode());
  }

  llvm::TargetOptions opt;
  auto RM = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
  auto TM = std::unique_ptr<llvm::TargetMachine>(
      Target->createTargetMachine(triple, "generic", "", opt, RM));

  if (!TM) {
    return llvm::make_error<llvm::StringError>(
        "Failed to create target machine", llvm::inconvertibleErrorCode());
  }

  M.setDataLayout(TM->createDataLayout());
  M.setTargetTriple(triple);

  // Ensure cache directory exists
  std::filesystem::path cacheDir = std::filesystem::path(CachePath.str()).parent_path();
  std::filesystem::create_directories(cacheDir);

  // Emit object file
  std::error_code EC;
  llvm::raw_fd_ostream dest(CachePath.str(), EC, llvm::sys::fs::OF_None);
  if (EC) {
    return llvm::make_error<llvm::StringError>(
        "Could not open cache file: " + EC.message(),
        llvm::inconvertibleErrorCode());
  }

  llvm::legacy::PassManager pass;
  if (TM->addPassesToEmitFile(pass, dest, nullptr,
                               llvm::CodeGenFileType::ObjectFile)) {
    return llvm::make_error<llvm::StringError>(
        "Target machine can't emit object file",
        llvm::inconvertibleErrorCode());
  }

  pass.run(M);
  dest.flush();

  return llvm::Error::success();
}

llvm::Error ClapJIT::loadCachedObject(llvm::StringRef CachePath) {
  auto BufferOrErr = llvm::MemoryBuffer::getFile(CachePath);
  if (!BufferOrErr) {
    return llvm::make_error<llvm::StringError>(
        "Failed to load cached object: " + BufferOrErr.getError().message(),
        llvm::inconvertibleErrorCode());
  }

  return llJIT_->addObjectFile(std::move(*BufferOrErr));
}

} // namespace clap_rt
