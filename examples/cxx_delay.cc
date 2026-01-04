// C++ delay with parameters
#include <cstdint>
#include <vector>

extern float g_params[];

static std::vector<float> *buffer_l = nullptr;
static std::vector<float> *buffer_r = nullptr;
static uint32_t buffer_size = 0;
static uint32_t write_pos = 0;

// Parameter definitions
int param_count() { return 2; }

const char *param_name(int i) {
  static const char *names[] = {"Feedback", "Mix"};
  return (i < 2) ? names[i] : "?";
}

float param_min(int) { return 0.0f; }
float param_max(int) { return 1.0f; }

float param_default(int i) {
  static float defaults[] = {0.5f, 0.4f};
  return (i < 2) ? defaults[i] : 0.5f;
}

bool init(double sample_rate, uint32_t min_frames, uint32_t max_frames) {
  (void)min_frames;
  (void)max_frames;

  buffer_size = static_cast<uint32_t>(sample_rate / 2);  // 0.5 second
  buffer_l = new std::vector<float>(buffer_size, 0.0f);
  buffer_r = new std::vector<float>(buffer_size, 0.0f);
  write_pos = 0;

  return true;
}

void destroy() {
  delete buffer_l;
  delete buffer_r;
  buffer_l = buffer_r = nullptr;
}

void process(const float *const *inputs, float *const *outputs,
             uint32_t num_channels, uint32_t num_frames) {
  if (!buffer_l || !buffer_r) return;

  const float feedback = g_params[0];
  const float wet_mix = g_params[1];
  float *buffers[2] = {buffer_l->data(), buffer_r->data()};

  for (uint32_t i = 0; i < num_frames; ++i) {
    for (uint32_t ch = 0; ch < num_channels && ch < 2; ++ch) {
      float dry = inputs[ch][i];
      uint32_t read_pos = (write_pos + 1) % buffer_size;
      float wet = buffers[ch][read_pos];

      buffers[ch][write_pos] = dry + wet * feedback;
      outputs[ch][i] = dry * (1.0f - wet_mix) + wet * wet_mix;
    }
    write_pos = (write_pos + 1) % buffer_size;
  }
}
