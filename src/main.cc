#include <string>

#include "JIT.h"

void exitOnErr(llvm::Error Err) {
    if (Err) {
        llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "Error: ");
        exit(1);
    }
}


int main(int argc, char** argv) {
    std::string InputFile = (argc > 1) ? argv[1] : "test.cc";

    init_llvm();

    auto JIT = ClapJIT();
    if(auto Err = JIT.initialize()){
        exitOnErr(std::move(Err));
    }

    llvm::outs() << "Compiling file: " << InputFile << " ...\n";
    if(auto Err = JIT.addModule(InputFile)){
        exitOnErr(std::move(Err));
    }

    auto AddrOrErr = JIT.lookup("add");
    if(!AddrOrErr){
        exitOnErr(std::move(AddrOrErr).takeError());
    }
    auto Addr = AddrOrErr.get().toPtr<int(*)(int,int)>();

    llvm::outs() << "Result: 100 + 200 = " << Addr(100,200) << "\n";

    return 0;
}
