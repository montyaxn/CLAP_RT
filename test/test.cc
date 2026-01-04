#include <gtest/gtest.h>
#include <llvm/Support/Error.h>
#include <filesystem>

#include "../jit/JIT.h"

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

TEST_F(ClapJITTest, STLSupport) {
  auto JITOrErr = clap_rt::ClapJIT::create();
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  auto Err = JIT.addModule("test/stl_test.cc");
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  auto AbsOrErr = JIT.lookupAs<int(int)>("abs_value");
  ASSERT_TRUE(!!AbsOrErr) << llvm::toString(AbsOrErr.takeError());
  auto Abs = *AbsOrErr;

  EXPECT_EQ(Abs(5), 5);
  EXPECT_EQ(Abs(-5), 5);
  EXPECT_EQ(Abs(0), 0);
}

TEST_F(ClapJITTest, VectorSupport) {
#ifdef _WIN32
  // MSVC STL has complex dependencies on exception handling infrastructure
  // (stdext::exception, RTTI vtables) that are not easily resolved in JIT.
  // Skip this test on Windows until full STL support is implemented.
  GTEST_SKIP() << "std::vector not yet supported on Windows (MSVC STL dependencies)";
#endif

  auto JITOrErr = clap_rt::ClapJIT::create();
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  auto Err = JIT.addModule("test/vector_test.cc");
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  auto SumOrErr = JIT.lookupAs<int(int)>("vector_sum");
  ASSERT_TRUE(!!SumOrErr) << llvm::toString(SumOrErr.takeError());
  auto Sum = *SumOrErr;

  // 1+2+3+4+5 = 15
  EXPECT_EQ(Sum(5), 15);
  // 1+2+...+10 = 55
  EXPECT_EQ(Sum(10), 55);
}

TEST_F(ClapJITTest, LifecycleSupport) {
  auto JITOrErr = clap_rt::ClapJIT::create();
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  auto Err = JIT.addModule("test/lifecycle_test.cc");
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  // Lookup init, destroy, and test functions
  auto InitOrErr = JIT.lookupAs<bool(double, uint32_t, uint32_t)>("init");
  ASSERT_TRUE(!!InitOrErr) << llvm::toString(InitOrErr.takeError());
  auto Init = *InitOrErr;

  auto DestroyOrErr = JIT.lookupAs<void()>("destroy");
  ASSERT_TRUE(!!DestroyOrErr) << llvm::toString(DestroyOrErr.takeError());
  auto Destroy = *DestroyOrErr;

  auto IsInitOrErr = JIT.lookupAs<bool()>("is_initialized");
  ASSERT_TRUE(!!IsInitOrErr) << llvm::toString(IsInitOrErr.takeError());
  auto IsInit = *IsInitOrErr;

  auto GetRateOrErr = JIT.lookupAs<double()>("get_sample_rate");
  ASSERT_TRUE(!!GetRateOrErr) << llvm::toString(GetRateOrErr.takeError());
  auto GetRate = *GetRateOrErr;

  // Initially not initialized
  EXPECT_FALSE(IsInit());
  EXPECT_EQ(GetRate(), 0.0);

  // Call init
  EXPECT_TRUE(Init(48000.0, 64, 1024));
  EXPECT_TRUE(IsInit());
  EXPECT_EQ(GetRate(), 48000.0);

  // Call destroy
  Destroy();
  EXPECT_FALSE(IsInit());
  EXPECT_EQ(GetRate(), 0.0);
}

TEST_F(ClapJITTest, CxxFunctionsWithoutExternC) {
  auto JITOrErr = clap_rt::ClapJIT::create();
  ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
  auto JIT = std::move(*JITOrErr);

  // This file has NO extern "C" - pure C++ functions
  auto Err = JIT.addModule("test/cxx_process.cc");
  ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

  // Lookup C++ mangled functions by name
  auto InitOrErr = JIT.lookupAs<bool(double, uint32_t, uint32_t)>("init");
  ASSERT_TRUE(!!InitOrErr) << llvm::toString(InitOrErr.takeError());
  auto Init = *InitOrErr;

  auto DestroyOrErr = JIT.lookupAs<void()>("destroy");
  ASSERT_TRUE(!!DestroyOrErr) << llvm::toString(DestroyOrErr.takeError());
  auto Destroy = *DestroyOrErr;

  auto ProcessOrErr = JIT.lookupAs<void(const float *const *, float *const *,
                                        uint32_t, uint32_t)>("process");
  ASSERT_TRUE(!!ProcessOrErr) << llvm::toString(ProcessOrErr.takeError());
  auto Process = *ProcessOrErr;

  auto GetGainOrErr = JIT.lookupAs<float()>("get_gain");
  ASSERT_TRUE(!!GetGainOrErr) << llvm::toString(GetGainOrErr.takeError());
  auto GetGain = *GetGainOrErr;

  // Before init, gain is 1.0
  EXPECT_FLOAT_EQ(GetGain(), 1.0f);

  // Init sets gain to 0.5
  EXPECT_TRUE(Init(48000.0, 64, 1024));
  EXPECT_FLOAT_EQ(GetGain(), 0.5f);

  // Test process
  float in_data[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  float out_data[4] = {0};
  const float *in_ptr = in_data;
  float *out_ptr = out_data;
  Process(&in_ptr, &out_ptr, 1, 4);

  EXPECT_FLOAT_EQ(out_data[0], 0.5f);
  EXPECT_FLOAT_EQ(out_data[1], 1.0f);
  EXPECT_FLOAT_EQ(out_data[2], 1.5f);
  EXPECT_FLOAT_EQ(out_data[3], 2.0f);

  // Destroy resets gain
  Destroy();
  EXPECT_FLOAT_EQ(GetGain(), 1.0f);
}

TEST_F(ClapJITTest, ObjectCaching) {
  // Create a temp cache directory
  auto cache_dir = std::filesystem::temp_directory_path() / "clap_jit_test_cache";
  std::filesystem::create_directories(cache_dir);

  // Clean up any existing cache
  for (const auto &entry : std::filesystem::directory_iterator(cache_dir)) {
    std::filesystem::remove(entry.path());
  }

  // First compile - should create cache
  {
    clap_rt::JITOptions opts;
    opts.cacheDir = cache_dir.string();

    auto JITOrErr = clap_rt::ClapJIT::create(opts);
    ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
    auto JIT = std::move(*JITOrErr);

    auto Err = JIT.addModule("test/add.cc");
    ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

    auto AddOrErr = JIT.lookupAs<int(int, int)>("add");
    ASSERT_TRUE(!!AddOrErr) << llvm::toString(AddOrErr.takeError());
    EXPECT_EQ((*AddOrErr)(1, 2), 3);
  }

  // Check cache file was created
  bool cache_exists = false;
  for (const auto &entry : std::filesystem::directory_iterator(cache_dir)) {
    if (entry.path().extension() == ".o") {
      cache_exists = true;
      break;
    }
  }
  EXPECT_TRUE(cache_exists) << "Cache file not created";

  // Second compile - should use cache
  {
    clap_rt::JITOptions opts;
    opts.cacheDir = cache_dir.string();

    auto JITOrErr = clap_rt::ClapJIT::create(opts);
    ASSERT_TRUE(!!JITOrErr) << llvm::toString(JITOrErr.takeError());
    auto JIT = std::move(*JITOrErr);

    auto Err = JIT.addModule("test/add.cc");
    ASSERT_FALSE(!!Err) << llvm::toString(std::move(Err));

    auto AddOrErr = JIT.lookupAs<int(int, int)>("add");
    ASSERT_TRUE(!!AddOrErr) << llvm::toString(AddOrErr.takeError());
    EXPECT_EQ((*AddOrErr)(5, 7), 12);
  }

  // Clean up
  std::filesystem::remove_all(cache_dir);
}
