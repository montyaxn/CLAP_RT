// Test STL support - using cmath
#include <cmath>

extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      float x = inputs[ch][i];
      outputs[ch][i] = std::abs(x);
    }
  }
}
