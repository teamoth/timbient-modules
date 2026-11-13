// Timbient native: voices. Each module plays up to 12 notes at once; when it runs out, the oldest note gives way.
#include "voice.h"

namespace tb {

// ---------------- PLUCK STRING: a Karplus-Strong string ----------------
struct PluckV : VoiceBase { float buf[8192]; int w = 0; float D = 100, gp = 0.99f, dc = 0; };
struct Pluck : Poly<PluckV> {
  Pluck() { gainScale = 0.4f; }
  void start(PluckV& x) override {
    const float bright = e[1], t60 = e[2];
    x.D = std::max(2.f, sr / x.f - 0.5f); x.gp = std::exp(std::log(0.001f) / (x.f * t60));
    const int P = std::min(8191, (int)x.D + 2); float lp = 0, mean = 0;
    for (int i = 0; i < 8192; i++) x.buf[i] = 0;
    // excite with lowpassed noise plus one cycle of the fundamental (keeps the pitch clear)
    for (int i = 0; i < P; i++) { lp += bright * (rng.bi() - lp); x.buf[i] = lp + 0.35f * std::sin(TAU * i / x.D); mean += x.buf[i]; }
    mean /= P; for (int i = 0; i < P; i++) x.buf[i] -= mean;
    x.w = P;
  }
  void voice(PluckV& x, float& l, float& r) override {
    auto rd = [&](float d) { float rp = x.w - d; while (rp < 0) rp += 8192; int i = (int)rp; float f = rp - i; return x.buf[i & 8191] + (x.buf[(i + 1) & 8191] - x.buf[i & 8191]) * f; };
    if (x.glLen > 0) x.D = std::max(2.f, std::min(8190.f, sr / x.f - 0.5f));   // GLIDE: the string's length follows the pitch
    const float y = x.gp * 0.5f * (rd(x.D) + rd(x.D + 1));
    x.buf[x.w & 8191] = y; x.w = (x.w + 1) & 8191;
    l = r = y;
  }
};
static std::vector<ParamDef> pluckParams() { std::vector<ParamDef> p = {PITCH(), prExp("tone", "TONE", 0.01f, 0.95f, 0.5f), T("decay", "RING", 0.2f, 5, 1.6f)}; for (auto& q : envP(0.003f, 0.3f, 1, 4, 2)) p.push_back(q); p.push_back(pct("vsens", "VEL SENS", 1)); p.push_back(pct("width", "WIDTH", 0.4f)); p.push_back(LEVEL()); return p; }
static Reg rPluck(new ModuleDef{"pluck", "PLUCK STRING", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, pluckParams(), 0, 68, false, [] { return new Pluck(); }});

// ---------------- TONE: a clean sine or triangle, with glide, vibrato and an optional detuned twin ----------------
struct ToneV : VoiceBase { Osc o1, o2; float from = 220, gt = 0, vt = 0, vph = 0; };
struct Tone : Poly<ToneV> {
  float lastF = 0, detR = 1;
  void voicePrep() override { detR = std::pow(2.f, e[5] / 1200.f); }
  enum { PITCHK, WAVE, GLIDE, VIB, VRATE, DET };
  Tone() { gainScale = 0.2f; }
  void start(ToneV& x) override { x.from = lastF > 0 ? lastF : x.f; lastF = x.f; x.gt = 0; x.vt = 0; x.vph = 0; x.o1.ph = x.o2.ph = 0; }
  void voice(ToneV& x, float& l, float& r) override {
    const float dt = 1.f / sr; x.gt += dt; x.vt += dt;
    const float g = e[GLIDE] > 0.005f ? std::min(1.f, x.gt / e[GLIDE]) : 1.f, f0 = x.from * std::pow(x.f / x.from, g);
    x.vph += e[VRATE] * dt; if (x.vph >= 1) x.vph -= 1;
    const float f = f0 + std::sin(TAU * x.vph) * f0 * 0.012f * e[VIB] * std::min(1.f, x.vt / 0.5f);
    const Osc::Wave w = e[WAVE] > 0.5f ? Osc::TRI : Osc::SINE;
    float y = x.o1.tick(w, f, sr);
    if (e[DET] > 0.5f) y = 0.5f * (y + x.o2.tick(w, f * detR, sr));
    l = r = y;
  }
};
static std::vector<ParamDef> toneParams() { std::vector<ParamDef> p = {PITCH(), OP("wave", "WAVE", {"SINE", "TRI"}, 0), T("glide", "GLIDE", 0.001f, 4, 0.001f), pct("vib", "VIBRATO", 0.15f),
  fmt(prExp("vrate", "VIB RATE", 0.1f, 9, 4.5f), [](float v) { char b[16]; std::snprintf(b, 16, "%.1fHZ", v); return std::string(b); }),
  fmt(pr("det", "DETUNE", 0, 30, 0), [](float v) { return std::to_string((int)std::round(v)) + "C"; })};
  for (auto& q : envP(0.02f, 0.4f, 0.8f, 1.5f, 2.5f)) p.push_back(q); p.push_back(pct("vsens", "VEL SENS", 1)); p.push_back(pct("width", "WIDTH", 0.3f)); p.push_back(LEVEL()); return p; }
static Reg rTone(new ModuleDef{"tone", "TONE", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, toneParams(), 0, 68, false, [] { return new Tone(); }});

// ---------------- PAD: detuned saws across the stereo field, a slowly moving filter, a breath of air ----------------
struct PadV : VoiceBase { Osc o[7]; float pan[7], ratio[7], gl[7], gr[7]; Biquad lp, bp; float lph = 0, lrate = 0.1f, ft = 0; int n = 5; int cnt = 0; };
struct Pad : Poly<PadV> {
  enum { PITCHK, VOICES, DETUNE, CUT, RES, FENV, FDEC, MOTION, AIR };
  Pad() { gainScale = 0.18f; selfPan = true; }
  void start(PadV& x) override {
    x.n = (int)e[VOICES]; for (int k = 0; k < x.n; k++) { x.o[k].ph = rng.uni(); x.pan[k] = x.n > 1 ? (float)k / (x.n - 1) * 2 - 1 : 0; x.ratio[k] = std::pow(2.f, x.pan[k] * e[DETUNE] * 30 / 1200.f); panGains(x.pan[k] * e[iWidth], x.gl[k], x.gr[k]); }
    x.lph = rng.uni(); x.lrate = rng.range(0.07f, 0.2f); x.lp.reset(); x.bp.reset(); x.bp.set(Biquad::BP, std::min(12000.f, x.f * 6), 1.5f, sr); x.cnt = 0; x.ft = 0;
  }
  void voice(PadV& x, float& l, float& r) override {
    if ((x.cnt++ & 31) == 0) { x.lph += 32 * x.lrate / sr; x.ft += 32 / sr; const float fe = 1 + e[FENV] * 12 * x.vel * std::exp(-x.ft * 3 / std::max(0.01f, e[FDEC]));   // FILT ENV opens the filter at each note, FILT DEC closes it again
      const float c = std::min(16000.f, (e[CUT] + std::sin(TAU * x.lph) * e[CUT] * e[MOTION] * 0.7f) * fe); x.lp.set(Biquad::LP, std::max(40.f, c), 0.7f + e[RES] * e[RES] * 9, sr); }   // RES: none at 0 (as before), a singing peak at full
    float L = 0, R = 0; const float sc = 1.f / std::sqrt((float)x.n);
    for (int k = 0; k < x.n; k++) { const float s = x.o[k].tick(Osc::SAW, x.f * x.ratio[k], sr) * sc; L += s * x.gl[k]; R += s * x.gr[k]; }
    L = x.lp.run(L, 0); R = x.lp.run(R, 1);
    if (e[AIR] > 0) { const float nz = x.bp.run(rng.bi(), 0) * e[AIR] * 0.83f; L += nz; R += nz; }
    l = L; r = R;
  }
};
static std::vector<ParamDef> padParams() { std::vector<ParamDef> p = {PITCH(), ST("voices", "VOICES", 2, 7, 5), pct("detune", "DETUNE", 0.35f), prHz("cut", "CUTOFF", 40, 20000, 1400), pct("res", "RES", 0), pct("fenv", "FILT ENV", 0), T("fdec", "FILT DEC", 0.05f, 10, 1.5f), pct("motion", "MOTION", 0.4f), pct("air", "AIR", 0.2f)};
  for (auto& q : envP(1.6f, 1, 0.8f, 3, 3.5f)) p.push_back(q); p.push_back(pct("vsens", "VEL SENS", 1)); p.push_back(pct("width", "WIDTH", 0.8f)); p.push_back(LEVEL()); return p; }
static Reg rPad(new ModuleDef{"pad", "PAD", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, padParams(), 0, 68, false, [] { return new Pad(); }});



}  // namespace tb
