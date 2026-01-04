// DSP processing code - JIT compiled at plugin initialization
// Edit this file to change the audio processing

// Parameter array from plugin
extern float g_params[];

// Parameter definitions
int param_count() { return 1; }

const char *param_name(int i) {
  static const char *names[] = {"Gain"};
  return (i < 1) ? names[i] : "?";
}

float param_min(int) { return 0.0f; }
float param_max(int) { return 1.0f; }
float param_default(int) { return 1.0f; }

void process(const float *const *inputs, float *const *outputs,
             unsigned int num_channels, unsigned int num_frames) {
  const float gain = g_params[0];

  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      outputs[ch][i] = inputs[ch][i] * gain;
    }
  }
}
