find_package(OpenGL REQUIRED)

add_library(jit_dsp MODULE
    clap_plugin.cc
    gui.cc
)

if(WIN32)
  # Windows linking
  target_link_libraries(jit_dsp
      PRIVATE
      CLAP_RT_core
      ${llvm_libs}
      clap
      imgui
      OpenGL::GL
  )
else()
  # Linux/Unix linking
  find_package(X11 REQUIRED)

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
endif()

# Set output name and extension
set_target_properties(jit_dsp PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

