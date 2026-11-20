// Timbient native: sounds packed inside session files, and the outline of a sound for the screen (no sound-file
// libraries needed, so the web player uses this too). See audio_files.h
#include "audio_files.h"
#include <cmath>
#include <cstdint>

namespace tb {

std::vector<float> peaksOf(const AudioBuf& b, int n) {
  std::vector<float> out(n, 0.f); const double step = (double)b.d.size() / n; float mx = 0;
  for (int i = 0; i < n; i++) { float m = 0; for (size_t j = (size_t)(i * step); j < (size_t)((i + 1) * step) && j < b.d.size(); j += 8) m = std::max(m, std::max(std::fabs(b.d[j]), std::fabs(b.R()[j]))); out[i] = m; mx = std::max(mx, m); }
  for (auto& v : out) v /= mx > 0 ? mx : 1;
  return out;
}

AudioBuf* fromBase64Pcm16(const std::string& s, float sr, int ch) {
  static int T[256]; static bool init = false;
  if (!init) { for (int& v : T) v = -1; const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; i++) T[(unsigned char)A[i]] = i; init = true; }
  std::vector<unsigned char> bytes; bytes.reserve(s.size() * 3 / 4); int val = 0, bits = -8;
  for (unsigned char c : s) { if (T[c] < 0) continue; val = (val << 6) + T[c]; bits += 6; if (bits >= 0) { bytes.push_back((unsigned char)((val >> bits) & 0xFF)); bits -= 8; } }
  if (bytes.size() < 128 || sr < 1000) return nullptr;
  AudioBuf* b = new AudioBuf(); b->sr = sr; const size_t n = bytes.size() / 2 / (ch == 2 ? 2 : 1); b->d.resize(n); if (ch == 2) b->r.resize(n);
  auto at = [&](size_t k) { return (int16_t)(bytes[k * 2] | (bytes[k * 2 + 1] << 8)) / 32767.f; };
  for (size_t i = 0; i < n; i++) { if (ch == 2) { b->d[i] = at(i * 2); b->r[i] = at(i * 2 + 1); } else b->d[i] = at(i); }
  return b;
}

}  // namespace tb
