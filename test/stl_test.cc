// Test STL support
#include <cmath>

extern "C" int abs_value(int val) {
  return std::abs(val);
}
