# RT-CLAP

JIT-compiled CLAP audio plugin. Write DSP in C++ and hot-reload without restarting your DAW.
JITできるとこまで書いたら全部claudeがやってくれた。LLVMの新しいORC-JITには慣れてないみたいで自分で書かなきゃだったけど、
GUIとかホットリロードとかはほとんどclaude codeが書いてくれた。すごすぎるぜ。

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Plugin: `build/plugin/jit_dsp.clap`

## Usage

Create DSP files in `~/.local/share/rt-clap/local/`:

```cpp
// gain.cc
extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      outputs[ch][i] = inputs[ch][i] * 0.5f;
    }
  }
}
```

Select the file from the plugin GUI dropdown. Files auto-reload on save.

## Folder Structure

```
~/.local/share/rt-clap/
  local/        # your DSP files
  @username/    # community files
```
そのうちDSP書いたファイルをウェブサイトを通じて配布できたらなーみたいな
