// Timbient native: small DSP building blocks. Everything here runs on the audio thread:
// no allocation, no locks, no system calls inside process functions.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

namespace tb {

constexpr float PI = 3.14159265358979f;
constexpr float TAU = 6.28318530717959f;
constexpr int BLOCK = 64;   // samples per processing block

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float mtof(float m) { return 440.f * std::pow(2.f, (m - 69.f) / 12.f); }
inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }
// a value that is not a number (or infinite) would poison everything after it: replace with silence
inline float sane(float v) { return std::isfinite(v) ? v : 0.f; }

// fast, decent random numbers (one per voice or module, never shared across threads)
struct Rng {
  uint32_t s = 0x9e3779b9u;
  explicit Rng(uint32_t seed = 0x9e3779b9u) : s(seed ? seed : 1u) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float uni() { return (next() >> 8) * (1.0f / 16777216.0f); }            // 0..1
  float bi() { return uni() * 2.f - 1.f; }                                  // -1..1
  float range(float a, float b) { return a + (b - a) * uni(); }
};

// a smoothed parameter: moves towards its target over a time constant (like setTargetAtTime)
struct Smooth {
  float v = 0, t = 0, k = 1;
  void setTime(float seconds, float sr) { k = seconds <= 0 ? 1.f : 1.f - std::exp(-1.f / (seconds * sr)); }
  void snap(float x) { v = t = x; }
  inline float tick() { v += (t - v) * k; return v; }
};

// RBJ biquad filter (double precision inside: narrow, low filters stay clean and stable)
struct Biquad {
  enum Type { LP, HP, BP, AP, PEAK, NOTCH };
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  double z1[2] = {0, 0}, z2[2] = {0, 0};
  void set(Type type, float f, float q, float sr, float gainDb = 0) {
    const double F = clampf(f, 10.f, sr * 0.49f), Q = std::max(q, 0.0001f);
    const double w = 6.283185307179586 * F / sr, cw = std::cos(w), sw = std::sin(w), al = sw / (2 * Q), A = std::pow(10.0, gainDb / 40.0);
    double B0, B1, B2, A0, A1, A2;
    switch (type) {
      case LP: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
      case HP: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
      case BP: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
      case AP: B0 = 1 - al; B1 = -2 * cw; B2 = 1 + al; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
      case NOTCH: B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
      default: B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; break;
    }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
  }
  void setShelf(bool high, float f, float gainDb, float sr) {   // RBJ shelving filters, slope 1
    const double A = std::pow(10.0, gainDb / 40.0), w = 6.283185307179586 * clampf(f, 10.f, sr * 0.49f) / sr, cw = std::cos(w), sw = std::sin(w), al = sw / 2 * std::sqrt(2.0), sA = 2 * std::sqrt(A) * al;
    double B0, B1, B2, A0, A1, A2;
    if (!high) { B0 = A * ((A + 1) - (A - 1) * cw + sA); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sA); A0 = (A + 1) + (A - 1) * cw + sA; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sA; }
    else { B0 = A * ((A + 1) + (A - 1) * cw + sA); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sA); A0 = (A + 1) - (A - 1) * cw + sA; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sA; }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
  }
  inline float run(float x, int ch = 0) {   // transposed direct form II
    const double y = b0 * x + z1[ch];
    z1[ch] = b1 * x - a1 * y + z2[ch];
    z2[ch] = b2 * x - a2 * y;
    if (!std::isfinite(y)) { reset(); return 0.f; }   // never let a blow-up jam the chain
    return (float)y;
  }
  void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

// a delay line with fractional (linear-interpolated) reads
struct DelayLine {
  std::vector<float> buf; int w = 0, mask = 0;
  void init(float maxSeconds, float sr) {
    int n = 1; while (n < (int)(maxSeconds * sr) + 4) n <<= 1;
    buf.assign(n, 0.f); mask = n - 1; w = 0;
  }
  inline void write(float x) { buf[w] = x; w = (w + 1) & mask; }
  inline float read(float delaySamples) const {
    float rp = (float)w - delaySamples - 1.f; while (rp < 0) rp += (float)(mask + 1);
    int i = (int)rp; float f = rp - i;
    return buf[i & mask] + (buf[(i + 1) & mask] - buf[i & mask]) * f;
  }
  void clear() { std::fill(buf.begin(), buf.end(), 0.f); }
};

// the shared note envelope: attack, decay to sustain, hold, release (the same shape as the web version)
struct NoteEnv {
  enum Stage { OFF, ATK, DEC, REL };
  Stage st = OFF; float v = 0, t = 0;
  float atk = 0.003f, dec = 0.3f, sus = 1, hold = 4, rel = 2, sr = 48000;
  float kd = 0, kr = 0, ka = 0, tEnd = 0;
  void start() { st = ATK; t = 0; const float dt = 1.f / sr; ka = dt / std::max(0.002f, atk); kd = 1.f - std::exp(-dt * 3.f / std::max(0.01f, dec)); kr = 1.f - std::exp(-dt * 3.f / std::max(0.01f, rel)); tEnd = std::max(atk, hold); }
  inline float tick() {
    t += 1.f / sr;
    switch (st) {
      case ATK: v += ka; if (v >= 1) { v = 1; st = DEC; } if (t >= tEnd) st = REL; break;
      case DEC: v += (sus - v) * kd; if (t >= tEnd) st = REL; break;
      case REL: v -= v * kr; if (v < 1e-4f) { v = 0; st = OFF; } break;
      default: break;
    }
    return v;
  }
  bool active() const { return st != OFF; }
  void fadeOut() { st = REL; kr = 1.f - std::exp(-3.f / (0.03f * sr)); }   // a stolen note fades quickly
};

// band-limited oscillators (polyBLEP)
inline float polyblep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
  if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
  return 0.f;
}
struct Osc {
  enum Wave { SINE, SAW, SQUARE, TRI };
  float ph = 0, tri = 0;
  inline float tick(Wave w, float freq, float sr) {
    const float dt = clampf(freq / sr, 0.f, 0.49f);
    float y;
    switch (w) {
      case SINE: y = std::sin(TAU * ph); break;
      case SAW: y = 2.f * ph - 1.f - polyblep(ph, dt); break;
      case SQUARE: { y = ph < 0.5f ? 1.f : -1.f; y += polyblep(ph, dt); float t2 = ph + 0.5f; if (t2 >= 1) t2 -= 1; y -= polyblep(t2, dt); break; }
      default: { float sq = ph < 0.5f ? 1.f : -1.f; sq += polyblep(ph, dt); float t2 = ph + 0.5f; if (t2 >= 1) t2 -= 1; sq -= polyblep(t2, dt);
        tri = dt * 4.f * sq + (1.f - dt * 4.f * 0.25f) * tri; y = tri; break; }
    }
    ph += dt; if (ph >= 1.f) ph -= 1.f;
    return y;
  }
};

// equal-power pan: p in -1..1
inline void panGains(float p, float& l, float& r) { const float a = (clampf(p, -1, 1) + 1.f) * PI / 4.f; l = std::cos(a); r = std::sin(a); }

// a gentle master compressor (like the web version's -18 dB / 3:1), with a soft safety limit after it
struct Compressor {
  float env = 0, sr = 48000, gainRed = 1;
  void process(float* L, float* R, int n) {
    const float thr = dbToGain(-18.f), ratio = 3.f, ka = 1.f - std::exp(-1.f / (0.02f * sr)), kr = 1.f - std::exp(-1.f / (0.4f * sr));
    for (int i = 0; i < n; i++) {
      const float a = std::max(std::fabs(L[i]), std::fabs(R[i]));
      env += (a - env) * (a > env ? ka : kr);
      float g = 1.f;
      if (env > thr) { const float over = 20.f * std::log10(env / thr); g = dbToGain(-over * (1.f - 1.f / ratio)); }
      gainRed = g;
      L[i] = std::tanh(sane(L[i]) * g * 0.95f) / 0.95f * 0.98f; R[i] = std::tanh(sane(R[i]) * g * 0.95f) / 0.95f * 0.98f;
    }
  }
};

}  // namespace tb
