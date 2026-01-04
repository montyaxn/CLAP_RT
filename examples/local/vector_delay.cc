// Multi-tap delay with CLAP-style lifecycle using std::vector
#include <cstdint>
#include <vector>

static constexpr unsigned int NUM_TAPS = 3;
static constexpr unsigned int tap_offsets[NUM_TAPS] = {4, 8, 12}; // In 1000ths of buffer
static constexpr float tap_gains[NUM_TAPS] = {0.6f, 0.4f, 0.25f};

// State allocated in init(), freed in destroy()
static std::vector<float> *buffer_l = nullptr;
static std::vector<float> *buffer_r = nullptr;
static unsigned int buffer_size = 0;
static unsigned int write_pos = 0;
static double g_sample_rate = 0;

extern "C" bool init(double sample_rate, uint32_t min_frames, uint32_t max_frames) {
  (void)min_frames;
  (void)max_frames;

  g_sample_rate = sample_rate;
  buffer_size = static_cast<unsigned int>(sample_rate);  // 1 second buffer

  buffer_l = new std::vector<float>(buffer_size, 0.0f);
  buffer_r = new std::vector<float>(buffer_size, 0.0f);
  write_pos = 0;

  return true;
}

extern "C" void destroy() {
  delete buffer_l;
  delete buffer_r;
  buffer_l = buffer_r = nullptr;
  buffer_size = 0;
}

extern "C" void process(const float *const *inputs, float *const *outputs,
                        unsigned int num_channels, unsigned int num_frames) {
  if (!buffer_l || !buffer_r) return;

  const float feedback = 0.4f;
  const float dry_gain = 0.7f;
  float *buffers[2] = {buffer_l->data(), buffer_r->data()};

  for (unsigned int i = 0; i < num_frames; ++i) {
    for (unsigned int ch = 0; ch < num_channels && ch < 2; ++ch) {
      float dry = inputs[ch][i];
      float wet = 0.0f;

      // Sum all delay taps
      for (unsigned int t = 0; t < NUM_TAPS; ++t) {
        unsigned int delay = (buffer_size * tap_offsets[t]) / 1000;
        unsigned int read_pos = (write_pos + buffer_size - delay) % buffer_size;
        wet += buffers[ch][read_pos] * tap_gains[t];
      }

      // Write input + feedback to buffer
      buffers[ch][write_pos] = dry + wet * feedback;

      outputs[ch][i] = dry * dry_gain + wet;
    }
    write_pos = (write_pos + 1) % buffer_size;
  }
}
