// DSP processing code - JIT compiled at plugin initialization
// Edit this file to change the audio processing

extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  const float gain = 0.5f;

  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      outputs[ch][i] = inputs[ch][i] * gain;
    }
  }
}
