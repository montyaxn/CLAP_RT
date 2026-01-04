find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)

add_library(jit_dsp MODULE
    clap_plugin.cc
    gui.cc
)

# Link with whole-archive to ensure all LLVM symbols are included
target_link_libraries(jit_dsp
    PRIVATE
    -Wl,--whole-archive
    CLAP_RT_core
    -Wl,--no-whole-archive
    ${llvm_libs}
    clap
    imgui
    OpenGL::GL
    OpenGL::GLX
    X11::X11
)

# Set output name and extension
set_target_properties(jit_dsp PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

