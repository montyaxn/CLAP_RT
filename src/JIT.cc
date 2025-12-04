#include "JIT.h"
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Error.h>
#include <llvm/TargetParser/Host.h>

llvm::Expected<ClapJIT> ClapJIT::create() {
  auto JIT = ClapJIT();
  auto JITOrErr = orc::LLJITBuilder().create();
  if (!JITOrErr)
    return JITOrErr.takeError();
  JIT.llJIT = std::move(*JITOrErr);
  return JIT;
}

llvm::Expected<std::unique_ptr<llvm::Module>>
ClapJIT::compileSingleFile(llvm::StringRef FilePath, llvm::LLVMContext *Ctx) {
  clang::CompilerInstance CI;
  CI.createDiagnostics();

  auto &TO = CI.getInvocation().getTargetOpts();
  TO.Triple = llvm::sys::getProcessTriple();
  if (TO.Triple.empty())
    TO.Triple = llvm::sys::getDefaultTargetTriple();

  clang::TargetInfo *TI = clang::TargetInfo::CreateTargetInfo(
      CI.getDiagnostics(), CI.getInvocation().getTargetOpts());
  if (!TI)
    return llvm::make_error<llvm::StringError>("TargetInfo failed",
                                               llvm::inconvertibleErrorCode());
  CI.setTarget(TI);

  CI.getLangOpts().CPlusPlus = 1;
  CI.getLangOpts().CPlusPlus17 = 1;

  CI.createFileManager();
  CI.createSourceManager();

  auto &FrontendOpts = CI.getInvocation().getFrontendOpts();
  FrontendOpts.Inputs.clear();
  FrontendOpts.Inputs.push_back(
      clang::FrontendInputFile(FilePath, clang::Language::CXX));

  auto Act = std::make_unique<clang::EmitLLVMOnlyAction>(Ctx);

  if (!CI.ExecuteAction(*Act)) {
    return llvm::make_error<llvm::StringError>(
        "Failed to compile file: " + FilePath, llvm::inconvertibleErrorCode());
  }

  std::unique_ptr<llvm::Module> M = Act->takeModule();
  if (!M) {
    return llvm::make_error<llvm::StringError>("No module generated",
                                               llvm::inconvertibleErrorCode());
  }

  return M;
}

llvm::Error ClapJIT::addModule(llvm::StringRef FilePath) {
  auto Ctx = std::make_unique<llvm::LLVMContext>();
  auto IROrErr = compileSingleFile(FilePath, Ctx.get());
  if (!IROrErr)
    return IROrErr.takeError();
  auto TSM = orc::ThreadSafeModule(std::move(IROrErr.get()), std::move(Ctx));

  if (auto Err = llJIT->addIRModule(std::move(TSM)))
    return Err;
  return llvm::Error::success();
}

llvm::Error ClapJIT::addModule(std::vector<llvm::StringRef> FilePaths) {
  for (auto FilePath : FilePaths) {
    auto Ctx = std::make_unique<llvm::LLVMContext>();
    auto IROrErr = compileSingleFile(FilePath, Ctx.get());
    if (!IROrErr)
      return IROrErr.takeError();
    auto TSM = orc::ThreadSafeModule(std::move(IROrErr.get()), std::move(Ctx));

    if (auto Err = llJIT->addIRModule(std::move(TSM)))
      return Err;
  }
  return llvm::Error::success();
}

llvm::Expected<orc::ExecutorAddr>
ClapJIT::lookup(llvm::StringRef UnmangledName) {
  return llJIT->lookup(UnmangledName);
}
