#include <iostream>
#include <memory>
#include <string>
#include <vector>

// LLVM Includes
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h" 

// Clang Includes
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/FrontendOptions.h"

namespace orc = llvm::orc;

void exitOnErr(llvm::Error Err) {
    if (Err) {
        llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "Error: ");
        exit(1);
    }
}

llvm::Expected<orc::ThreadSafeModule> compileFileToIR(llvm::StringRef FilePath) {
    auto Ctx = std::make_unique<llvm::LLVMContext>();
    clang::CompilerInstance CI;
    CI.createDiagnostics();

    auto &TO = CI.getInvocation().getTargetOpts();
    TO.Triple = llvm::sys::getProcessTriple(); 
    if (TO.Triple.empty()) TO.Triple = llvm::sys::getDefaultTargetTriple();

    clang::TargetInfo *TI = clang::TargetInfo::CreateTargetInfo(
        CI.getDiagnostics(), CI.getInvocation().getTargetOpts()
    );
    if (!TI) return llvm::make_error<llvm::StringError>("TargetInfo failed", llvm::inconvertibleErrorCode());
    CI.setTarget(TI);

    CI.getLangOpts().CPlusPlus = 1;
    CI.getLangOpts().CPlusPlus17 = 1;

    CI.createFileManager();
    CI.createSourceManager();

    auto &FrontendOpts = CI.getInvocation().getFrontendOpts();
    FrontendOpts.Inputs.clear();
    FrontendOpts.Inputs.push_back(clang::FrontendInputFile(
        FilePath, 
        clang::Language::CXX
    ));

    auto Act = std::make_unique<clang::EmitLLVMOnlyAction>(Ctx.get());

    if (!CI.ExecuteAction(*Act)) {
        return llvm::make_error<llvm::StringError>(
            "Failed to compile file: " + FilePath, 
            llvm::inconvertibleErrorCode()
        );
    }

    std::unique_ptr<llvm::Module> M = Act->takeModule();
    if (!M) {
        return llvm::make_error<llvm::StringError>(
            "No module generated", llvm::inconvertibleErrorCode()
        );
    }

    return orc::ThreadSafeModule(std::move(M), std::move(Ctx));
}

int main(int argc, char** argv) {
    // ターゲット初期化
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();

    std::string InputFile = (argc > 1) ? argv[1] : "test.cc";
    
    llvm::outs() << "Compiling file: " << InputFile << " ...\n";

    auto JITOrErr = orc::LLJITBuilder().create();
    if (!JITOrErr) exitOnErr(JITOrErr.takeError());
    auto JIT = std::move(*JITOrErr);

    auto TSMOrErr = compileFileToIR(InputFile);
    if (!TSMOrErr) exitOnErr(TSMOrErr.takeError());

    if (auto Err = JIT->addIRModule(std::move(*TSMOrErr))) {
        exitOnErr(std::move(Err));
    }

    auto SymOrErr = JIT->lookup("add");
    if (!SymOrErr) exitOnErr(SymOrErr.takeError());

    orc::ExecutorAddr Addr = *SymOrErr;
    auto *AddFn = Addr.toPtr<int(int, int)>();

    int result = AddFn(100, 200);
    llvm::outs() << "Result: 100 + 200 = " << result << "\n";

    return 0;
}
