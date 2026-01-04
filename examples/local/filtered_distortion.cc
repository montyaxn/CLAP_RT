// Distortion with lowpass filter - uses lib/utils.h
#include "utils.h"

extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  const float drive = 4.0f;

  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      float x = inputs[ch][i] * drive;
      float clipped = soft_clip(x);
      outputs[ch][i] = lowpass(clipped, 0.1f);
    }
  }
}
