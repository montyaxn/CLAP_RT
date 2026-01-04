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

  // Find mangled name by function name (searches all added modules)
  [[nodiscard]] std::optional<std::string>
  findSymbol(llvm::StringRef FunctionName) const;

  [[nodiscard]] llvm::Expected<llvm::orc::ExecutorAddr>
  lookup(llvm::StringRef UnmangledName) const;

  template <typename FuncT>
  [[nodiscard]] llvm::Expected<FuncT *>
  lookupAs(llvm::StringRef UnmangledName) const {
    auto AddrOrErr = lookup(UnmangledName);
    if (!AddrOrErr)
      return AddrOrErr.takeError();
    return AddrOrErr->toPtr<FuncT *>();
  }

private:
  ClapJIT() = default;

  [[nodiscard]] llvm::Expected<std::unique_ptr<llvm::Module>>
  compileSingleFile(llvm::StringRef FilePath, llvm::LLVMContext &Ctx);

  // Symbol info: pair of (demangled name, mangled name)
  using SymbolEntry = std::pair<std::string, std::string>;

  std::unique_ptr<llvm::orc::LLJIT> llJIT_;
  JITOptions options_;
  std::vector<SymbolEntry> symbols_;
};

} // namespace clap_rt
