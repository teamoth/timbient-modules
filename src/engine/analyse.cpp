#include "analyse.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace tb {

static void fft(std::vector<std::complex<float>>& a) {   // in place, radix 2
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
  for (size_t len = 2; len <= n; len <<= 1) { const float ang = -2 * 3.14159265f / len; const std::complex<float> wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) { std::complex<float> w(1); for (size_t j = 0; j < len / 2; j++) { const auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } } }
}

SoundInfo analyseSound(const AudioBuf& b) {
  SoundInfo s; const auto& d = b.d; const long n = (long)d.size(); const float sr = b.sr; if (n < 64) return s;
  // the shape, and how loud
  s.peaks.assign(64, 0.f); double sq = 0; float pk = 0;
  for (long i = 0; i < n; i++) { const float a = std::fabs(d[i]); pk = std::max(pk, a); sq += (double)d[i] * d[i]; const int c = (int)(i * 64 / n); s.peaks[c] = std::max(s.peaks[c], a); }
  s.peakDb = 20 * std::log10(std::max(pk, 1e-6f)); s.rmsDb = 20 * std::log10(std::max((float)std::sqrt(sq / n), 1e-6f));
  if (pk > 0) for (float& v : s.peaks) v /= pk;
  if (pk < 1e-4f) return s;
  // key and scale: how much of each of the 12 notes, over the whole sound (up to 90 s of it)
  const int N = 8192; const long lim = std::min(n, (long)(90 * sr)); std::vector<std::complex<float>> buf(N); std::vector<float> win(N);
  for (int i = 0; i < N; i++) win[i] = 0.5f - 0.5f * std::cos(6.2831853f * i / N);
  float chroma[12] = {0}; int frames = 0;
  for (long at = 0; at + N <= lim || (at == 0 && lim < N); at += N / 2) {
    for (int i = 0; i < N; i++) { const long k = at + i; buf[i] = std::complex<float>(k < n ? d[k] * win[i] : 0.f, 0.f); }
    fft(buf); float now[12] = {0}, tot = 0;
    for (int k = 1; k < N / 2; k++) { const float f = k * sr / N; if (f < 55 || f > 4000) continue; const float mag = std::abs(buf[k]);
      const int pc = (((int)std::lround(69 + 12 * std::log2(f / 440))) % 12 + 12) % 12; now[pc] += mag; tot += mag; }
    if (tot > 1e-3f) { for (int i = 0; i < 12; i++) chroma[i] += now[i] / tot; frames++; }
    if (lim < N) break;
  }
  float cm = 0, cv2 = 0; for (float c : chroma) cm += c; cm /= 12; for (float c : chroma) cv2 += (c - cm) * (c - cm); const float spread = cm > 0 ? std::sqrt(cv2 / 12) / cm : 0;   // noise: every note about the same
  if (frames > 0 && spread > 0.3f) { const KeyMatch km = matchKey(chroma); if (km.conf > 0.5f) { s.keyKnown = true; s.root = km.root; s.scale = km.scale; s.keyConf = km.conf;
      s.altRoot = km.root2; s.altScale = km.scale2; s.altClose = km.conf2 > km.conf - 0.03f; } }
  // the note it plays: in the loudest stretch just after the attack, a strong, steady repeat means a pitched note
  { const int W = 2048; long loud = 0; for (long i = 0; i < std::min(n, (long)(sr * 2)); i++) if (std::fabs(d[i]) > std::fabs(d[loud])) loud = i;
    auto pitchAt = [&](long st, float& midi) -> bool { if (st < 0 || st + 2 * W > n) return false;
      float e0 = 0; for (int i = 0; i < W; i++) e0 += d[st + i] * d[st + i]; if (e0 < 1e-6f) return false;
      const int minL = (int)(sr / 1500), maxL = std::min(W - 1, (int)(sr / 40)); float bestC = 0; int bestL = 0;
      std::vector<float> cs(maxL + 2, 0.f);
      for (int L = minL; L <= maxL; L++) { float num = 0, e1 = 0; for (int i = 0; i < W; i++) { num += d[st + i] * d[st + i + L]; e1 += d[st + i + L] * d[st + i + L]; } cs[L] = num / std::sqrt(e0 * e1 + 1e-12f); }
      for (int L = minL + 1; L < maxL; L++) if (cs[L] > bestC && cs[L] >= cs[L - 1] && cs[L] >= cs[L + 1]) { bestC = cs[L]; bestL = L; }
      for (int L = minL + 1; L < maxL; L++) if (cs[L] > 0.92f * bestC && cs[L] >= cs[L - 1] && cs[L] >= cs[L + 1]) { bestL = L; break; }   // the shortest strong repeat (not an octave below)
      if (bestC < 0.85f || bestL == 0) return false;
      const float y0 = cs[bestL - 1], y1 = cs[bestL], y2 = cs[bestL + 1], den = y0 - 2 * y1 + y2, off = std::fabs(den) > 1e-9f ? 0.5f * (y0 - y2) / den : 0;
      midi = 69 + 12 * std::log2((sr / (bestL + off)) / 440); return true; };
    float found[3]; int ok = 0; for (int t = 0; t < 3; t++) if (pitchAt(loud + (long)(sr * (0.04f + 0.06f * t)), found[ok])) ok++; else break;
    bool single = ok == 3 && std::fabs(found[0] - found[2]) < 0.4f && std::fabs(found[1] - found[2]) < 0.4f;
    // and the same note all the way through (where it's still sounding), so a melody isn't taken for one note
    if (single) { float e0 = 0; for (int i = 0; i < W && loud + i < n; i++) e0 += d[loud + i] * d[loud + i];
      for (float at : {0.3f, 0.55f, 0.8f}) { const long st = (long)(at * (n - 2 * W)); if (st <= loud) continue; float e1 = 0; for (int i = 0; i < W; i++) e1 += d[st + i] * d[st + i];
        if (e1 < e0 * 0.02f) continue; float m2; if (!pitchAt(st, m2) || std::fabs(std::fmod(std::fabs(m2 - found[1]) + 6, 12.f) - 6) > 0.5f) { single = false; break; } } }
    if (single) { s.noteKnown = true; s.note = found[1]; } }
  if (s.noteKnown && n < sr * 8) s.keyKnown = false;   // a single note says which note, not which scale
  // tempo, for anything a few seconds long with a beat: how the onsets repeat
  if (n > sr * 3) { const int H = 512; const long frames2 = std::min(n, (long)(sr * 60)) / H; std::vector<float> env(frames2, 0.f), flux(frames2, 0.f);
    for (long f = 0; f < frames2; f++) { float e = 0; for (int i = 0; i < H; i++) { const float x = d[f * H + i]; e += x * x; } env[f] = std::log(1e-6f + e); }
    for (long f = 1; f < frames2; f++) flux[f] = std::max(0.f, env[f] - env[f - 1]);
    float m = 0; for (float v : flux) m += v; m /= std::max(1L, frames2); for (float& v : flux) v = std::max(0.f, v - m);
    const float fps = sr / H; float best = 0, mean2 = 0; int cnt = 0; float bestBpm = 0;
    for (float bpm = 60; bpm <= 180; bpm += 0.5f) { const float lag = fps * 60 / bpm; float acc = 0; int c = 0;
      for (long f = 0; f + (long)(lag * 2) < frames2; f++) { const long a1 = f + (long)std::lround(lag), a2 = f + (long)std::lround(lag * 2); acc += flux[f] * (flux[a1] + 0.5f * flux[a2]); c++; }
      if (c == 0) continue; acc /= c; mean2 += acc; cnt++; if (acc > best) { best = acc; bestBpm = bpm; } }
    if (cnt > 0 && best > 2.2f * (mean2 / cnt)) { s.tempoKnown = true; s.bpm = bestBpm; } }
  return s;
}

}  // namespace tb
