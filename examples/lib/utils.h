// Utility functions for DSP
#pragma once

// Soft clip using tanh approximation
inline float soft_clip(float x) {
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Linear interpolation
inline float lerp(float a, float b, float t) {
  return a + t * (b - a);
}

// Clamp value to range
inline float clamp(float x, float min, float max) {
  return x < min ? min : (x > max ? max : x);
}

// Simple one-pole lowpass filter
float lowpass(float input, float cutoff);
