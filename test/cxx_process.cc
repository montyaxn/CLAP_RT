// C++ DSP functions - no extern "C"
#include <cstdint>

static float gain = 1.0f;

bool init(double sample_rate, uint32_t min_frames, uint32_t max_frames) {
  (void)sample_rate;
  (void)min_frames;
  (void)max_frames;
  gain = 0.5f;
  return true;
}

void destroy() {
  gain = 1.0f;
}

void process(const float *const *inputs, float *const *outputs,
             uint32_t num_channels, uint32_t num_frames) {
  for (uint32_t ch = 0; ch < num_channels; ++ch) {
    for (uint32_t i = 0; i < num_frames; ++i) {
      outputs[ch][i] = inputs[ch][i] * gain;
    }
  }
}

float get_gain() {
  return gain;
}
