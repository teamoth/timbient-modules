// A plain radix-2 FFT (complex, in place), shared by STRETCH and the SPECTRAL reverb
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "dsp.h"
namespace tb {
struct FFT {
  int N; std::vector<int> rev; std::vector<float> cs, sn;
  explicit FFT(int n) : N(n), rev(n), cs(n / 2), sn(n / 2) {
    int lv = 0; while ((1 << lv) < N) lv++;
    for (int i = 0; i < N; i++) { int r = 0; for (int b = 0; b < lv; b++) r |= ((i >> b) & 1) << (lv - 1 - b); rev[i] = r; }
    for (int i = 0; i < N / 2; i++) { cs[i] = std::cos(TAU * i / N); sn[i] = std::sin(TAU * i / N); }
  }
  void run(float* re, float* im, bool inv) const {
    for (int i = 0; i < N; i++) { const int j = rev[i]; if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
    const float sg = inv ? 1.f : -1.f;
    for (int size = 2; size <= N; size <<= 1) { const int half = size >> 1, step = N / size;
      for (int i = 0; i < N; i += size) for (int j = 0, k = 0; j < half; j++, k += step) {
        const float wr = cs[k], wi = sg * sn[k]; const int l = i + j + half, m = i + j; const float tr = re[l] * wr - im[l] * wi, ti = re[l] * wi + im[l] * wr;
        re[l] = re[m] - tr; im[l] = im[m] - ti; re[m] += tr; im[m] += ti; } }
  }
};
}  // namespace tb
