# RT-CLAP

A real-time JIT-compiled CLAP audio plugin. Write DSP code in C++ and hot-reload it without restarting your DAW.

## Features

- **JIT Compilation**: Compile C++ DSP code at runtime using LLVM/Clang
- **Hot Reload**: Edit your DSP code and see changes instantly
- **Embedded GUI**: X11/ImGui interface embedded in your DAW
- **File Watching**: Auto-recompile when files change
- **Multi-file Support**: Switch between different DSP effects via dropdown
- **Community Ready**: Folder structure for sharing DSP files (`local/`, `@username/`)

## Building

### Dependencies

- CMake 3.20+
- LLVM/Clang (with development headers)
- GTest
- X11, OpenGL

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

The plugin will be at `build/plugin/jit_dsp.clap`.

## Installation

Copy or symlink the plugin to your CLAP plugin folder:

```bash
# Option 1: Copy
cp build/plugin/jit_dsp.clap ~/.clap/

# Option 2: Add build folder to DAW's plugin paths
# Add: /path/to/RT_CLAP/build/plugin/
```

## Usage

### DSP Folder Structure

DSP files live in `~/.local/share/rt-clap/`:

```
~/.local/share/rt-clap/
  local/           # Your own DSP files
    dsp.cc
    my_effect.cc
  @username/       # Downloaded from community
    cool_delay.cc
```

### Writing DSP Code

Create a `.cc` file with a `process` function:

```cpp
// Simple gain effect
extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  const float gain = 0.5f;

  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      outputs[ch][i] = inputs[ch][i] * gain;
    }
  }
}
```

### Function Signature

```cpp
extern "C" void process(
    const float *const *inputs,   // Input audio buffers [channel][sample]
    float *const *outputs,        // Output audio buffers [channel][sample]
    unsigned int num_channels,    // Number of channels (usually 2)
    unsigned int num_frames       // Number of samples in this block
);
```

### Example Effects

**Bypass (pass-through)**
```cpp
extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      outputs[ch][i] = inputs[ch][i];
    }
  }
}
```

**Soft Clipping Distortion**
```cpp
extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  const float drive = 4.0f;

  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      float x = inputs[ch][i] * drive;
      float x2 = x * x;
      outputs[ch][i] = x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
  }
}
```

**Simple Delay** (uses static buffers)
```cpp
static float delay_buffer_l[48000];
static float delay_buffer_r[48000];
static unsigned int write_pos = 0;

extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  const unsigned int DELAY_SAMPLES = 24000;  // ~500ms at 48kHz
  const float FEEDBACK = 0.5f;
  const float MIX = 0.5f;

  float *buffers[2] = {delay_buffer_l, delay_buffer_r};

  for (unsigned int i = 0; i < num_frames; ++i) {
    for (unsigned int ch = 0; ch < num_channels && ch < 2; ++ch) {
      unsigned int read_pos = (write_pos + 48000 - DELAY_SAMPLES) % 48000;

      float dry = inputs[ch][i];
      float wet = buffers[ch][read_pos];

      buffers[ch][write_pos] = dry + wet * FEEDBACK;
      outputs[ch][i] = dry * (1.0f - MIX) + wet * MIX;
    }
    write_pos = (write_pos + 1) % 48000;
  }
}
```

### Tips

- **No `= {0}` on large arrays**: Use `static float buf[N];` not `static float buf[N] = {0};`
- **Static variables**: Use `static` for state that persists between process calls
- **Keep it simple**: Complex C++ features may not JIT compile correctly
- **Check compile.log**: Errors are logged to `~/.local/share/rt-clap/compile.log`

## GUI Controls

- **Dropdown**: Select which DSP file to use
- **Recompile**: Manually trigger recompilation
- **Status**: Shows compile success/error

Files are auto-recompiled when saved. New files are auto-detected.

## License

MIT
