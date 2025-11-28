cmake_minimum_required(VERSION 3.20.0)

project(CLAP_RT)

# --- 1. 基本設定 ---
# LLVM/Clang のヘッダーは C++17 以上の機能を多用するため必須です
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 2. LLVM パッケージのセットアップ ---
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# LLVMのプリプロセッサ定義読み込み
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# インクルードパスとライブラリパスの設定
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

# RTTI (実行時型情報) の設定
# LLVMは通常 -fno-rtti でビルドされているため、それに合わせないとリンクエラーになります
if(NOT LLVM_ENABLE_RTTI)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# --- 3. Clang パッケージのセットアップ ---
find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")

include_directories(${CLANG_INCLUDE_DIRS})

# --- 4. ターゲット定義 ---
set(SRC src/main.cc)

add_executable(CLAP_RT ${SRC})

target_include_directories(CLAP_RT PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)


llvm_map_components_to_libnames(llvm_libs 
    support 
    core 
    irreader 
    orcjit 
    native         
    executionengine
    demangle
)

target_link_libraries(CLAP_RT
    PRIVATE

    #llvm libs
    ${llvm_libs}
    
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
