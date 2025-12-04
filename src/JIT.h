#pragma once

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/StringSaver.h>
#include <llvm/Support/TargetSelect.h>
#include <memory>

namespace orc = llvm::orc;

inline void init_llvm() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();
}

class ClapJIT {
private:
  ClapJIT() {}

public:
  static llvm::Expected<ClapJIT> create();

  llvm::Error addModule(llvm::StringRef FilePath);
  llvm::Error addModule(std::vector<llvm::StringRef> FilePaths);
  llvm::Expected<orc::ExecutorAddr> lookup(llvm::StringRef UnmangledName);

private:
  std::unique_ptr<llvm::orc::LLJIT> llJIT;

  llvm::Expected<std::unique_ptr<llvm::Module>>
  compileSingleFile(llvm::StringRef FilePath, llvm::LLVMContext *Ctx);
};
