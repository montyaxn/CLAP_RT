// Test DSP with init/destroy lifecycle
#include <cstdint>

static bool g_initialized = false;
static double g_sample_rate = 0;

extern "C" bool init(double sample_rate, uint32_t min_frames, uint32_t max_frames) {
  (void)min_frames;
  (void)max_frames;
  g_sample_rate = sample_rate;
  g_initialized = true;
  return true;
}

extern "C" void destroy() {
  g_initialized = false;
  g_sample_rate = 0;
}

extern "C" bool is_initialized() {
  return g_initialized;
}

extern "C" double get_sample_rate() {
  return g_sample_rate;
}
