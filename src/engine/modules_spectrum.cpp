// Timbient native: SPECTRUM. The sound passes straight through (drop it onto any audio cable); what's in it is shown from
// 10 Hz to 24 kHz, low on the left, as 64 bars a few times a second. The screen shades what's outside hearing, the edges of it,
// and (GUIDE ALL) where sound often piles up; and points at narrow peaks standing well above their neighbours.
#include "params.h"
#include "fft.h"

namespace tb {
constexpr int SPEC_BANDS = 64;
inline float specBandHz(float k) { return 10.f * std::pow(2400.f, k / SPEC_BANDS); }   // the edge of band k (k = 0..64): 10 Hz to 24 kHz, evenly on a log scale

struct Spectrum : Module {
  static constexpr int N = 8192, HOP = 2048;
  std::vector<float> ring[2], win, re, im, pw; int w = 0, since = 0; FFT fft{N};   // each side analysed on its own (a mix of the two can cancel: wide stereo, opposite sides)
  void prepare() override { ring[0].assign(N, 0); ring[1].assign(N, 0); pw.assign(N / 2, 0); win.resize(N); re.resize(N); im.resize(N); for (int i = 0; i < N; i++) win[i] = 0.5f - 0.5f * std::cos(TAU * i / N); w = since = 0;
    for (int k = 0; k < SPEC_BANDS; k++) disp[k].store(-120); }
  void analyse() {
    std::fill(pw.begin(), pw.end(), 0.f);
    for (int ch = 0; ch < 2; ch++) { for (int i = 0; i < N; i++) { re[i] = ring[ch][(w + i) & (N - 1)] * win[i]; im[i] = 0; }
      fft.run(re.data(), im.data(), false); for (int b = 0; b < N / 2; b++) pw[b] += 0.5f * (re[b] * re[b] + im[b] * im[b]); }
    const float binHz = sr / N, ref = N / 4.f;   // a full-scale sine reads 0 dB
    for (int k = 0; k < SPEC_BANDS; k++) {
      const float lo = specBandHz((float)k), hi = specBandHz((float)k + 1); float best = 0;
      if (lo >= sr / 2) { disp[k].store(-120); continue; }
      int b0 = (int)std::ceil(lo / binHz), b1 = (int)std::floor(std::min(hi, sr / 2) / binHz);
      if (b1 < b0) b0 = b1 = std::max(1, (int)std::lround(0.5f * (lo + hi) / binHz));   // narrower than one step of the analysis: the nearest
      for (int b = std::max(1, b0); b <= std::min(N / 2 - 1, b1); b++) best = std::max(best, pw[b]);
      disp[k].store(10 * std::log10(best / (ref * ref) + 1e-14f)); }
  }
  void render(int a, int b) override {
    for (int i = a; i < b; i++) { for (int ch = 0; ch < 2; ch++) out[0][ch][i] = in[0][ch][i]; ring[0][w] = in[0][0][i]; ring[1][w] = in[0][1][i]; w = (w + 1) & (N - 1); }
    since += b - a; if (since >= HOP) { since = 0; analyse(); }
    disp[SPEC_BANDS].store(sr);
  }
};
static Reg rSpectrum(new ModuleDef{"spectrum", "SPECTRUM", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {noMod(OP("guide", "GUIDE", {"OFF", "EDGES", "ALL"}, 1))}, 46, 120, false, [] { return new Spectrum(); }});
}  // namespace tb
