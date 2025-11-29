#include "JIT.h"
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/Error.h>
#include <llvm/TargetParser/Host.h>


llvm::Error ClapJIT::initialize(){
    auto JITOrErr = orc::LLJITBuilder().create();
    if (!JITOrErr) return JITOrErr.takeError();
    JIT = std::move(*JITOrErr);
    return llvm::Error::success();
}

llvm::Expected<orc::ThreadSafeModule> ClapJIT::tryCompileFileToIR(llvm::StringRef FilePath) {
  auto Ctx = std::make_unique<llvm::LLVMContext>();
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

  auto Act = std::make_unique<clang::EmitLLVMOnlyAction>(Ctx.get());

  if (!CI.ExecuteAction(*Act)) {
    return llvm::make_error<llvm::StringError>(
        "Failed to compile file: " + FilePath, llvm::inconvertibleErrorCode());
  }

  std::unique_ptr<llvm::Module> M = Act->takeModule();
  if (!M) {
    return llvm::make_error<llvm::StringError>("No module generated",
                                               llvm::inconvertibleErrorCode());
  }

  return orc::ThreadSafeModule(std::move(M), std::move(Ctx));
}

llvm::Error ClapJIT::addModule(llvm::StringRef FilePath){
    auto TSMOrErr = tryCompileFileToIR(FilePath);
    if (!TSMOrErr) return TSMOrErr.takeError();

    if (auto Err = JIT->addIRModule(std::move(*TSMOrErr)))
        return Err;
    return llvm::Error::success();
}


llvm::Expected<orc::ExecutorAddr> ClapJIT::lookup(llvm::StringRef UnmangledName){
    return JIT->lookup(UnmangledName);
}



