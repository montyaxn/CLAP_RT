#pragma once

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace clap_rt {

enum class LangStandard { CXX14, CXX17, CXX20 };

struct JITOptions {
  LangStandard langStandard = LangStandard::CXX20;
  std::string targetTriple; // empty = auto-detect
  std::vector<std::string> includePaths; // additional include directories

  // Object file cache directory (empty = no caching)
  std::string cacheDir;
};

class ClapJIT {
public:
  [[nodiscard]] static llvm::Expected<ClapJIT> create(JITOptions opts = {});

  static void initializeLLVM();

  // Move semantics
  ClapJIT(ClapJIT &&) noexcept = default;
  ClapJIT &operator=(ClapJIT &&) noexcept = default;

  // No copy
  ClapJIT(const ClapJIT &) = delete;
  ClapJIT &operator=(const ClapJIT &) = delete;

  ~ClapJIT() = default;

  [[nodiscard]] llvm::Error addModule(llvm::StringRef FilePath);
  [[nodiscard]] llvm::Error addModules(llvm::ArrayRef<llvm::StringRef> FilePaths);

  /// Define an external symbol that JIT code can reference
  [[nodiscard]] llvm::Error defineSymbol(llvm::StringRef Name, void *Addr);

  // Find mangled name by function name (searches all added modules)
  [[nodiscard]] std::optional<std::string>
  findSymbol(llvm::StringRef FunctionName) const;

  [[nodiscard]] llvm::Expected<llvm::orc::ExecutorAddr>
  lookup(llvm::StringRef SymbolName) const;

  /// Lookup by function name - handles both C++ mangled and extern "C" functions.
  /// First tries findSymbol() to resolve mangled name, falls back to direct lookup.
  [[nodiscard]] llvm::Expected<llvm::orc::ExecutorAddr>
  lookupFunction(llvm::StringRef FunctionName) const;

  template <typename FuncT>
  [[nodiscard]] llvm::Expected<FuncT *>
  lookupAs(llvm::StringRef FunctionName) const {
    auto AddrOrErr = lookupFunction(FunctionName);
    if (!AddrOrErr)
      return AddrOrErr.takeError();
    return AddrOrErr->toPtr<FuncT *>();
  }

private:
  ClapJIT() = default;

  [[nodiscard]] llvm::Expected<std::unique_ptr<llvm::Module>>
  compileSingleFile(llvm::StringRef FilePath, llvm::LLVMContext &Ctx);

  // Compile module to object file and save to cache
  [[nodiscard]] llvm::Error compileAndCache(llvm::Module &M,
                                            llvm::StringRef CachePath);

  // Load cached object file
  [[nodiscard]] llvm::Error loadCachedObject(llvm::StringRef CachePath);

  // Get cache path for a source file (empty if caching disabled)
  std::string getCachePath(llvm::StringRef SourcePath) const;

  // Check if cache is valid (exists and newer than source)
  bool isCacheValid(llvm::StringRef SourcePath,
                    llvm::StringRef CachePath) const;

  // Symbol info: pair of (demangled name, mangled name)
  using SymbolEntry = std::pair<std::string, std::string>;

  std::unique_ptr<llvm::orc::LLJIT> llJIT_;
  JITOptions options_;
  std::vector<SymbolEntry> symbols_;
};

} // namespace clap_rt
