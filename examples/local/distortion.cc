// Soft-clip distortion with drive and output controls

extern float g_params[];

// Parameters: [0] = Drive, [1] = Output
int param_count() { return 2; }

const char *param_name(int i) {
  static const char *names[] = {"Drive", "Output"};
  return (i < 2) ? names[i] : "?";
}

float param_min(int) { return 0.0f; }
float param_max(int) { return 1.0f; }

float param_default(int i) {
  static float defaults[] = {0.5f, 0.5f};
  return (i < 2) ? defaults[i] : 0.5f;
}

void process(const float *const *inputs, float *const *outputs,
             unsigned int num_channels, unsigned int num_frames) {
  // Drive: 0.0 = 1x, 1.0 = 10x
  const float drive = 1.0f + g_params[0] * 9.0f;
  const float output_gain = g_params[1];

  for (unsigned int ch = 0; ch < num_channels; ++ch) {
    for (unsigned int i = 0; i < num_frames; ++i) {
      float x = inputs[ch][i] * drive;

      // Soft clipping using tanh approximation
      float x2 = x * x;
      float out = x * (27.0f + x2) / (27.0f + 9.0f * x2);

      outputs[ch][i] = out * output_gain;
    }
  }
}
