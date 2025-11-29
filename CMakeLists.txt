cmake_minimum_required(VERSION 3.20.0)

project(CLAP_RT)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ---- llvm setup ----
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

llvm_map_components_to_libnames(llvm_libs 
    support 
    core 
    irreader 
    orcjit 
    native         
    executionengine
    demangle
)
# ---- llvm setup end ----

# ---- clang setup ----
find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")

include_directories(${CLANG_INCLUDE_DIRS})
# ---- clang setup end ----

# ---- GTest setup ----
find_package(GTest REQUIRED)
# ---- GTest setup end ----

add_library(clap_rt_core
    src/JIT.cc
)

target_link_libraries(clap_rt_core
    PUBLIC

    #llvm libs
    ${llvm_libs}

    PRIVATE
    
    # clang libs
    clangFrontend
    clangDriver
    clangSerialization
    clangParse
    clangSema
    clangAnalysis
    clangEdit
    clangAST
    clangCodeGen
    clangLex
    clangBasic
)

enable_testing()

add_executable(clap_rt_core_test
    test/test.cc
)

target_link_libraries(clap_rt_core_test
    PRIVATE
    clap_rt_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(clap_rt_core_test)

# add_executable(CLAP_RT 
#     src/main.cc
# )

# target_link_libraries(CLAP_RT
#     PRIVATE
#     CLAP_JIT
# )



