// Shared by REVERB and ECHO: a gentle ceiling, a smooth read between samples, a one-pole coefficient, and a pitch shifter
#pragma once
#include "dsp.h"
#include <cmath>
#include <algorithm>
namespace tb {
inline float softLimit(float x) { x = std::max(-3.f, std::min(3.f, x)); return x * (27 + x * x) / (27 + 9 * x * x); }   // tanh, near enough, and cheap
// a smoother read between samples than a straight line: a straight line dulls the highs a little every trip round,
// which in a tail that goes round hundreds of times is the difference between INFINITE and slowly fading
inline float readCubic(const DelayLine& d, float delay) {
  float rp = (float)d.w - delay - 1.f; while (rp < 0) rp += (float)(d.mask + 1);
  const int i = (int)rp; const float f = rp - i;
  const float xm = d.buf[(i - 1) & d.mask], x0 = d.buf[i & d.mask], x1 = d.buf[(i + 1) & d.mask], x2 = d.buf[(i + 2) & d.mask];
  const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2 * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
  return ((c3 * f + c2) * f + c1) * f + x0;
}
inline float onePoleA(float hz, float sr) { return 1 - std::exp(-TAU * std::min(hz, 0.45f * sr) / sr); }
// ---- a smoother pitch shifter: four read heads instead of two, each starting from a slightly different place ----
struct Shifter4 {
  DelayLine d; float ph = 0, W = 5000, off[4] = {}; Rng r{77};
  void init(float sr) { W = 0.11f * sr; d.init(0.2f, sr); }
  void clear() { d.clear(); }
  inline float run(float x, float ratio) {
    d.write(x); const float prev = ph; ph += (1 - ratio) / W; ph -= std::floor(ph);
    float o = 0;
    for (int k = 0; k < 4; k++) {
      float q = ph + 0.25f * k; q -= std::floor(q); float qp = prev + 0.25f * k; qp -= std::floor(qp);
      if (std::fabs(q - qp) > 0.5f) off[k] = r.uni() * 0.25f * W;   // this head wrapped (where it's silent): a new place to start from
      const float s = std::sin(PI * q); o += readCubic(d, q * W + 2 + off[k]) * s * s; }
    return o * 0.5f;
  }
};
}  // namespace tb
