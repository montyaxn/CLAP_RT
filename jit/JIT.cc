#include "JIT.h"
#include "Error.h"

#include <clang/Basic/DiagnosticOptions.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/HeaderSearchOptions.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>

namespace clap_rt {

namespace {
namespace orc = llvm::orc;
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
  return jit;
}

llvm::Expected<std::unique_ptr<llvm::Module>>
ClapJIT::compileSingleFile(llvm::StringRef FilePath, llvm::LLVMContext &Ctx) {
  clang::CompilerInstance CI;

  // Set up diagnostics - just use default behavior
  CI.createDiagnostics();
  if (!CI.hasDiagnostics()) {
    return makeError(ErrorCode::CompilationFailed,
                     "Failed to create diagnostics", FilePath);
  }

  // Configure target
  auto &TO = CI.getInvocation().getTargetOpts();
  if (options_.targetTriple.empty()) {
    TO.Triple = llvm::sys::getProcessTriple();
    if (TO.Triple.empty())
      TO.Triple = llvm::sys::getDefaultTargetTriple();
  } else {
    TO.Triple = options_.targetTriple;
  }

  clang::TargetInfo *TI = clang::TargetInfo::CreateTargetInfo(
      CI.getDiagnostics(), CI.getInvocation().getTargetOpts());
  if (!TI) {
    return makeError(ErrorCode::TargetCreationFailed,
                     "Failed to create TargetInfo for triple: " + TO.Triple,
                     FilePath);
  }
  CI.setTarget(TI);

  // Configure language options
  auto &LO = CI.getLangOpts();
  LO.CPlusPlus = 1;
  switch (options_.langStandard) {
  case LangStandard::CXX14:
    LO.CPlusPlus14 = 1;
    break;
  case LangStandard::CXX17:
    LO.CPlusPlus17 = 1;
    break;
  case LangStandard::CXX20:
    LO.CPlusPlus20 = 1;
    break;
  }

  CI.createFileManager();
  CI.createSourceManager();

  // Add include paths
  auto &HSO = CI.getInvocation().getHeaderSearchOpts();
  for (const auto &path : options_.includePaths) {
    HSO.AddPath(path, clang::frontend::Angled, false, true);
  }

  auto &FrontendOpts = CI.getInvocation().getFrontendOpts();
  FrontendOpts.Inputs.clear();
  FrontendOpts.Inputs.push_back(
      clang::FrontendInputFile(FilePath, clang::Language::CXX));

  auto Act = std::make_unique<clang::EmitLLVMOnlyAction>(&Ctx);

  if (!CI.ExecuteAction(*Act)) {
    return makeError(ErrorCode::CompilationFailed,
                     "Compilation failed (check stderr for diagnostics)",
                     FilePath);
  }

  std::unique_ptr<llvm::Module> M = Act->takeModule();
  if (!M) {
    return makeError(ErrorCode::ModuleGenerationFailed,
                     "No module generated after compilation", FilePath);
  }

  return M;
}

llvm::Error ClapJIT::addModule(llvm::StringRef FilePath) {
  auto Ctx = std::make_unique<llvm::LLVMContext>();
  auto IROrErr = compileSingleFile(FilePath, *Ctx);
  if (!IROrErr)
    return IROrErr.takeError();

  // Collect symbols before moving the module
  for (const auto &F : **IROrErr) {
    if (!F.isDeclaration()) {
      std::string mangled = F.getName().str();
      std::string demangled = llvm::demangle(mangled);
      symbols_.emplace_back(demangled, mangled);
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
ClapJIT::lookup(llvm::StringRef UnmangledName) const {
  return llJIT_->lookup(UnmangledName);
}

} // namespace clap_rt
