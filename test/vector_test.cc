// Test std::vector support
#include <vector>

extern "C" int vector_sum(int n) {
  std::vector<int> v;
  for (int i = 1; i <= n; ++i) {
    v.push_back(i);
  }
  int sum = 0;
  for (int x : v) {
    sum += x;
  }
  return sum;
}
