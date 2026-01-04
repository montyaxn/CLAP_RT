extern "C" int square(int x);

extern "C" int sum_of_squares(int a, int b) {
  return square(a) + square(b);
}
