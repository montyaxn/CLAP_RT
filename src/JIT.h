#pragma once

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
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
  ClapJIT();
public:
  static llvm::Expected<ClapJIT> create();

  llvm::Error addModule(llvm::StringRef FilePath);
  llvm::Expected<orc::ExecutorAddr> lookup(llvm::StringRef UnmangledName);

private:
  std::unique_ptr<llvm::orc::LLJIT> llJIT;

  llvm::Expected<orc::ThreadSafeModule> tryCompileFileToIR(llvm::StringRef FilePath);
};
