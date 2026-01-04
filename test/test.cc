#include <gtest/gtest.h>
#include <llvm/Support/Error.h>

#include "../src/JIT.h"

class ClapJITTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() { clap_rt::ClapJIT::initializeLLVM(); }
};

TEST_F(ClapJITTest, CompileSingleFile) {
  auto JITOrErr = clap_rt::ClapJIT::create();
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  auto Err = JIT.addModule("test/add.cc");
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  auto AddOrErr = JIT.lookupAs<int(int, int)>("add");
  ASSERT_TRUE(!!AddOrErr) << llvm::toString(AddOrErr.takeError());
  auto Add = *AddOrErr;

  EXPECT_EQ(Add(1, 2), 3);
}

TEST_F(ClapJITTest, CompileMultipleFiles) {
  auto JITOrErr = clap_rt::ClapJIT::create();
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  auto Err = JIT.addModules({"test/add.cc", "test/mul.cc"});
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  auto AddOrErr = JIT.lookupAs<int(int, int)>("add");
  ASSERT_TRUE(!!AddOrErr) << llvm::toString(AddOrErr.takeError());
  auto Add = *AddOrErr;
  EXPECT_EQ(Add(1, 2), 3);

  auto MulOrErr = JIT.lookupAs<int(int, int)>("mul");
  ASSERT_TRUE(!!MulOrErr) << llvm::toString(MulOrErr.takeError());
  auto Mul = *MulOrErr;
  EXPECT_EQ(Mul(2, 5), 10);
}

TEST_F(ClapJITTest, CustomLanguageStandard) {
  clap_rt::JITOptions opts;
  opts.langStandard = clap_rt::LangStandard::CXX17;

  auto JITOrErr = clap_rt::ClapJIT::create(opts);
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  auto Err = JIT.addModule("test/add.cc");
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  auto AddOrErr = JIT.lookupAs<int(int, int)>("add");
  ASSERT_TRUE(!!AddOrErr) << llvm::toString(AddOrErr.takeError());
  auto Add = *AddOrErr;

  EXPECT_EQ(Add(3, 4), 7);
}

TEST_F(ClapJITTest, MangledCxxFunction) {
  auto JITOrErr = clap_rt::ClapJIT::create();
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  auto Err = JIT.addModule("test/cxx_func.cc");
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  // Find mangled name from function name
  auto mangled = JIT.findSymbol("add_cxx");
  ASSERT_TRUE(mangled.has_value()) << "Function add_cxx not found";

  auto AddOrErr = JIT.lookupAs<int(int, int)>(*mangled);
  ASSERT_TRUE(!!AddOrErr) << llvm::toString(AddOrErr.takeError());
  auto Add = *AddOrErr;

  EXPECT_EQ(Add(10, 20), 30);
}

TEST_F(ClapJITTest, CrossModuleLinking) {
  auto JITOrErr = clap_rt::ClapJIT::create();
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  // helper.cc defines square(), uses_helper.cc calls square()
  auto Err = JIT.addModules({"test/helper.cc", "test/uses_helper.cc"});
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  auto SumOfSquaresOrErr = JIT.lookupAs<int(int, int)>("sum_of_squares");
  ASSERT_TRUE(!!SumOfSquaresOrErr) << llvm::toString(SumOfSquaresOrErr.takeError());
  auto SumOfSquares = *SumOfSquaresOrErr;

  // 3^2 + 4^2 = 9 + 16 = 25
  EXPECT_EQ(SumOfSquares(3, 4), 25);
}
