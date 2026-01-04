cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(CLAP_RT)

# LLVM is built without RTTI, so we must match
if(MSVC)
  add_compile_options(/GR-)
  # Suppress some common MSVC warnings
  add_compile_options(/wd4624 /wd4291)
else()
  add_compile_options(-fno-rtti)
  # Use mold linker on Linux if available
  if(UNIX AND NOT APPLE)
    add_link_options("-fuse-ld=mold")
  endif()
endif()

# LTO disabled - LLVM static libs weren't built with LTO, so minimal benefit
# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)


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

# ---- CLAP setup ----
include(FetchContent)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG main
)
FetchContent_MakeAvailable(clap)
# ---- CLAP setup end ----

# ---- GLFW setup ----
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)
# ---- GLFW setup end ----

# ---- Dear ImGui setup ----
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.6
)
FetchContent_MakeAvailable(imgui)

# Create ImGui library
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)
set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
# ---- Dear ImGui setup end ----

add_library(CLAP_RT_core
    jit/JIT.cc
)

set_target_properties(CLAP_RT_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(CLAP_RT_core
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

add_executable(CLAP_RT_core_test
    test/test.cc
)

target_link_libraries(CLAP_RT_core_test
    PRIVATE
    CLAP_RT_core
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(CLAP_RT_core_test)

# ---- Plugin ----
add_subdirectory(plugin)

