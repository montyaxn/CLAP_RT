#include <string>

#include "JIT.h"


int main(int argc, char** argv) {
    std::string InputFile = (argc > 1) ? argv[1] : "test.cc";

    init_llvm();

    auto JIT = ClapJIT();

    llvm::outs() << "Compiling file: " << InputFile << " ...\n";
    JIT.compileFileToIR(InputFile);

    llvm::outs() << "Result: 100 + 200 = " << JIT.runAdd(100, 200) << "\n";

    return 0;
}
