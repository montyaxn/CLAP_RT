// Stereo delay with independent L/R delay times

extern float g_params[];

static float delay_buffer_l[48000];
static float delay_buffer_r[48000];
static unsigned int write_pos = 0;

// Parameters: [0] = Delay L, [1] = Delay R, [2] = Feedback, [3] = Mix
int param_count() { return 4; }

const char *param_name(int i) {
  static const char *names[] = {"Delay L", "Delay R", "Feedback", "Mix"};
  return (i < 4) ? names[i] : "?";
}

float param_min(int) { return 0.0f; }
float param_max(int) { return 1.0f; }

float param_default(int i) {
  static float defaults[] = {0.5f, 0.5f, 0.4f, 0.5f};
  return (i < 4) ? defaults[i] : 0.5f;
}

void process(const float *const *inputs, float *const *outputs,
             unsigned int num_channels, unsigned int num_frames) {
  const unsigned int BUFFER_SIZE = 48000;

  // Delay times: 0.0 = 0ms, 1.0 = 1000ms (at 48kHz)
  unsigned int delay_samples[2] = {
      static_cast<unsigned int>(g_params[0] * BUFFER_SIZE),
      static_cast<unsigned int>(g_params[1] * BUFFER_SIZE)
  };
  const float feedback = g_params[2];
  const float mix = g_params[3];

  float *buffers[2] = {delay_buffer_l, delay_buffer_r};

  for (unsigned int i = 0; i < num_frames; ++i) {
    for (unsigned int ch = 0; ch < num_channels && ch < 2; ++ch) {
      unsigned int read_pos = (write_pos + BUFFER_SIZE - delay_samples[ch]) % BUFFER_SIZE;

      float dry = inputs[ch][i];
      float wet = buffers[ch][read_pos];

      buffers[ch][write_pos] = dry + wet * feedback;
      outputs[ch][i] = dry * (1.0f - mix) + wet * mix;
    }
    write_pos = (write_pos + 1) % BUFFER_SIZE;
  }
}
