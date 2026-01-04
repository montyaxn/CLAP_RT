// Utility function implementations
#include "utils.h"

static float lowpass_state = 0.0f;

float lowpass(float input, float cutoff) {
  float alpha = cutoff;  // simplified, should be calculated from sample rate
  lowpass_state = lerp(lowpass_state, input, alpha);
  return lowpass_state;
}
