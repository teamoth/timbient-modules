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
    const float y = x.gp * 0.5f * (rd(x.D) + rd(x.D + 1));
    x.buf[x.w & 8191] = y; x.w = (x.w + 1) & 8191;
    l = r = y;
  }
};
static std::vector<ParamDef> pluckParams() { std::vector<ParamDef> p = {PITCH(), prExp("tone", "TONE", 0.01f, 0.95f, 0.5f), T("decay", "RING", 0.2f, 5, 1.6f)}; for (auto& q : envP(0.003f, 0.3f, 1, 4, 2)) p.push_back(q); p.push_back(pct("width", "WIDTH", 0.4f)); p.push_back(LEVEL()); return p; }
static Reg rPluck(new ModuleDef{"pluck", "PLUCK STRING", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, pluckParams(), 0, 58, false, [] { return new Pluck(); }});

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
  for (auto& q : envP(0.02f, 0.4f, 0.8f, 1.5f, 2.5f)) p.push_back(q); p.push_back(pct("width", "WIDTH", 0.3f)); p.push_back(LEVEL()); return p; }
static Reg rTone(new ModuleDef{"tone", "TONE", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, toneParams(), 0, 58, false, [] { return new Tone(); }});

// ---------------- PAD: detuned saws across the stereo field, a slowly moving filter, a breath of air ----------------
struct PadV : VoiceBase { Osc o[7]; float pan[7], ratio[7], gl[7], gr[7]; Biquad lp, bp; float lph = 0, lrate = 0.1f; int n = 5; int cnt = 0; };
struct Pad : Poly<PadV> {
  enum { PITCHK, VOICES, DETUNE, CUT, MOTION, AIR, ATK, DEC, SUS, HOLD, REL, WIDTH, LEVELK };
  Pad() { gainScale = 0.18f; selfPan = true; }
  void start(PadV& x) override {
    x.n = (int)e[VOICES]; for (int k = 0; k < x.n; k++) { x.o[k].ph = rng.uni(); x.pan[k] = x.n > 1 ? (float)k / (x.n - 1) * 2 - 1 : 0; x.ratio[k] = std::pow(2.f, x.pan[k] * e[DETUNE] * 30 / 1200.f); panGains(x.pan[k] * e[WIDTH], x.gl[k], x.gr[k]); }
    x.lph = rng.uni(); x.lrate = rng.range(0.07f, 0.2f); x.lp.reset(); x.bp.reset(); x.bp.set(Biquad::BP, std::min(12000.f, x.f * 6), 1.5f, sr); x.cnt = 0;
  }
  void voice(PadV& x, float& l, float& r) override {
    if ((x.cnt++ & 31) == 0) { x.lph += 32 * x.lrate / sr; const float c = e[CUT] + std::sin(TAU * x.lph) * e[CUT] * e[MOTION] * 0.7f; x.lp.set(Biquad::LP, std::max(40.f, c), 0.7f, sr); }
    float L = 0, R = 0; const float sc = 1.f / std::sqrt((float)x.n);
    for (int k = 0; k < x.n; k++) { const float s = x.o[k].tick(Osc::SAW, x.f * x.ratio[k], sr) * sc; L += s * x.gl[k]; R += s * x.gr[k]; }
    L = x.lp.run(L, 0); R = x.lp.run(R, 1);
    if (e[AIR] > 0) { const float nz = x.bp.run(rng.bi(), 0) * e[AIR] * 0.83f; L += nz; R += nz; }
    l = L; r = R;
  }
};
static std::vector<ParamDef> padParams() { std::vector<ParamDef> p = {PITCH(), ST("voices", "VOICES", 2, 7, 5), pct("detune", "DETUNE", 0.35f), prHz("cut", "CUTOFF", 100, 10000, 1400), pct("motion", "MOTION", 0.4f), pct("air", "AIR", 0.2f)};
  for (auto& q : envP(1.6f, 1, 0.8f, 3, 3.5f)) p.push_back(q); p.push_back(pct("width", "WIDTH", 0.8f)); p.push_back(LEVEL()); return p; }
static Reg rPad(new ModuleDef{"pad", "PAD", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, padParams(), 0, 58, false, [] { return new Pad(); }});

// ---------------- FM 2-OP ----------------
struct FmV : VoiceBase { float cph = 0, mph = 0, depth = 0, peak = 0; };
struct Fm2 : Poly<FmV> {
  enum { PITCHK, RATIO, FINE, INDEX, IDEC, ISUS };
  float mr = 1, kd = 0.01f;
  void voicePrep() override { mr = e[RATIO] * std::pow(2.f, e[FINE] / 1200.f); kd = 1 - std::exp(-3.f / (std::max(0.01f, e[IDEC]) * sr)); }
  Fm2() { gainScale = 0.12f; }
  void start(FmV& x) override { const float mf = x.f * e[RATIO] * std::pow(2.f, e[FINE] / 1200.f); x.peak = e[INDEX] * mf; x.depth = x.peak; x.cph = x.mph = 0; }
  void voice(FmV& x, float& l, float& r) override {
    const float mf = x.f * mr;
    x.depth += (x.peak * e[ISUS] - x.depth) * kd;
    x.mph += mf / sr; if (x.mph >= 1) x.mph -= 1;
    const float fc = x.f + std::sin(TAU * x.mph) * x.depth; x.cph += fc / sr; x.cph -= std::floor(x.cph);
    l = r = std::sin(TAU * x.cph);
  }
};
static std::vector<ParamDef> fmParams() { std::vector<ParamDef> p = {PITCH(), fmt(pr("ratio", "RATIO", 0.5f, 8, 2), [](float v) { char b[8]; std::snprintf(b, 8, "%.1f", v); return std::string(b); }),
  fmt(pr("fine", "FINE", -50, 50, 0), [](float v) { return (v > 0 ? "+" : "") + std::to_string((int)std::round(v)) + "C"; }),
  fmt(pr("index", "INDEX", 0, 10, 3), [](float v) { char b[8]; std::snprintf(b, 8, "%.1f", v); return std::string(b); }), T("idec", "IDX DEC", 0.01f, 6, 0.8f), pct("isus", "IDX SUS", 0.2f)};
  p[1].step = 0.5f; for (auto& q : envP(0.003f, 0.3f, 1, 4, 2)) p.push_back(q); p.push_back(pct("width", "WIDTH", 0.3f)); p.push_back(LEVEL()); return p; }
static Reg rFm(new ModuleDef{"fm2", "FM 2-OP", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, fmParams(), 0, 58, false, [] { return new Fm2(); }});

// ---------------- MODAL BELL: four partials, each with its own ring ----------------
struct BellV : VoiceBase { float ph[4], fr[4], amp[4], kd[4], lv[4]; float t = 0; };
struct Bell : Poly<BellV> {
  enum { PITCHK, CRACK, DECAY };
  Bell() { gainScale = 0.09f; }
  void start(BellV& x) override {
    const float cr = e[CRACK]; const float ratios[4] = {1, 2.76f + cr * 0.55f, 5.4f - cr * 0.9f, 8.93f + cr * 1.6f}, amps[4] = {1, 0.45f + cr * 0.3f, 0.22f + cr * 0.3f, 0.1f + cr * 0.25f}, decs[4] = {2.2f, 1.1f, 0.5f, 0.25f};
    for (int i = 0; i < 4; i++) { x.ph[i] = 0; x.fr[i] = x.f * ratios[i]; x.amp[i] = amps[i]; x.lv[i] = 1; x.kd[i] = std::exp(std::log(0.0001f) / (decs[i] * e[DECAY] * sr)); }
    x.t = 0;
  }
  void voice(BellV& x, float& l, float& r) override {
    x.t += 1.f / sr; const float atk = std::min(1.f, x.t / 0.004f); float y = 0;
    for (int i = 0; i < 4; i++) { if (x.fr[i] > sr * 0.45f) continue; x.ph[i] += x.fr[i] / sr; if (x.ph[i] >= 1) x.ph[i] -= 1; y += std::sin(TAU * x.ph[i]) * x.amp[i] * x.lv[i]; x.lv[i] *= x.kd[i]; }
    l = r = y * atk;
  }
};
static std::vector<ParamDef> bellParams() { std::vector<ParamDef> p = {PITCH(), pct("crack", "CRACK", 0.15f), T("decay", "RING", 0.3f, 4, 1.2f)}; for (auto& q : envP(0.003f, 0.3f, 1, 4, 2)) p.push_back(q); p.push_back(pct("width", "WIDTH", 0.4f)); p.push_back(LEVEL()); return p; }
static Reg rBell(new ModuleDef{"bell", "MODAL BELL", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, bellParams(), 0, 58, false, [] { return new Bell(); }});

}  // namespace tb
