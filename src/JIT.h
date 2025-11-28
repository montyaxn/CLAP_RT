#pragma once

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
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
public:
  ClapJIT();

  void compileFileToIR(llvm::StringRef FilePath);

  int runAdd(int r, int l);

private:
  std::unique_ptr<llvm::orc::LLJIT> JIT;

  llvm::Expected<orc::ThreadSafeModule> tryCompileFileToIR(llvm::StringRef FilePath);
};
