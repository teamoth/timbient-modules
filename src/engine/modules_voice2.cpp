// Timbient native: the rest of the voices (glass, noise click, grain cloud, analog, formant, breath, additive, wavefold).
// Each one follows the web version's recipe and levels, so sessions sound the same.
#include "voice.h"

namespace tb {

static std::vector<ParamDef> vp(std::vector<ParamDef> p, std::vector<ParamDef> env, float width) {
  for (auto& q : env) p.push_back(q); p.push_back(pct("width", "WIDTH", width)); p.push_back(LEVEL()); return p;
}
static std::vector<ParamDef> ENVD() { return envP(0.003f, 0.3f, 1, 4, 2); }

// ---------------- GLASS: three bright partials that bend down into pitch, then ring away ----------------
struct GlassV : VoiceBase { float ph[3], t = 0, d = 1, kd = 0; };
struct Glass : Poly<GlassV> {
  enum { PITCHK, BEND, DECAY };
  Glass() { gainScale = 0.08f; }
  void start(GlassV& x) override { x.t = 0; x.d = e[DECAY]; for (auto& p : x.ph) p = 0; x.kd = std::log(0.0001f / 1.f) / std::max(0.01f, x.d - 0.004f); }
  void voice(GlassV& x, float& l, float& r) override {
    const float dt = 1.f / sr; x.t += dt; if (x.t > x.d) { l = r = 0; return; }
    const float a = x.t < 0.004f ? x.t / 0.004f : std::exp(x.kd * (x.t - 0.004f));
    const float bend = std::pow(1 + e[BEND], 1 - std::min(1.f, x.t / 0.07f));
    static const float K[3] = {1, 2.01f, 3.98f}, A[3] = {1, 0.25f, 0.08f}; float y = 0;
    for (int k = 0; k < 3; k++) { const float fr = x.f * K[k] * bend; if (fr > sr * 0.45f) continue; x.ph[k] += fr * dt; x.ph[k] -= std::floor(x.ph[k]); y += std::sin(TAU * x.ph[k]) * A[k]; }
    l = r = y * a;
  }
};
static Reg rGlass(new ModuleDef{"glass", "GLASS", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), pr("glide", "BEND", 0, 0.1f, 0.025f), T("decay", "RING", 0.2f, 4, 0.9f)}, ENVD(), 0.4f), 0, 58, false, [] { return new Glass(); }});

// ---------------- NOISE CLICK: a burst of noise through a tuned band ----------------
struct ClickV : VoiceBase { Biquad bp; float t = 0, d = 0.08f, kd = 0; };
struct Click : Poly<ClickV> {
  enum { PITCHK, Q, DECAY, TUNE };
  Click() { gainScale = 2.2f; }
  void start(ClickV& x) override { x.t = 0; x.d = e[DECAY]; x.bp.reset(); x.bp.set(Biquad::BP, std::min(15000.f, x.f * e[TUNE]), e[Q], sr); x.kd = std::log(0.0001f) / std::max(0.005f, x.d - 0.002f); }
  void voice(ClickV& x, float& l, float& r) override {
    x.t += 1.f / sr; if (x.t > x.d + 0.02f) { l = r = 0; return; }
    const float a = x.t < 0.002f ? x.t / 0.002f : std::exp(x.kd * (x.t - 0.002f));
    l = r = x.bp.run(rng.bi()) * a;
  }
};
static Reg rClick(new ModuleDef{"click", "NOISE CLICK", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), pr("q", "Q", 1, 30, 12), T("decay", "RING", 0.01f, 0.5f, 0.08f), pr("tune", "TUNE", 0.5f, 4, 2)}, ENVD(), 0.4f), 0, 58, false, [] { return new Click(); }});

// ---------------- GRAIN CLOUD: a scatter of tiny sine grains around the note ----------------
struct GrainsV : VoiceBase { static constexpr int G = 12; float at[G], dur[G], fr[G], ph[G], gl[G], gr[G]; int n = 0; float t = 0; };
struct Grains : Poly<GrainsV> {
  enum { PITCHK, DENS, SPREAD, GRAIN };
  Grains() { gainScale = 0.05f; selfPan = true; }
  void start(GrainsV& x) override {
    static const float mult[4] = {1, 1.5f, 2, 3}; const float w = e[pi("width")];
    x.n = std::min(GrainsV::G, (int)e[DENS]); x.t = 0;
    for (int i = 0; i < x.n; i++) { x.at[i] = rng.uni() * e[SPREAD] + 0.005f; x.dur[i] = e[GRAIN] * rng.range(0.6f, 1.4f); x.fr[i] = x.f * mult[rng.next() % 4] * (1 + rng.range(-0.01f, 0.01f)); x.ph[i] = 0; panGains(rng.bi() * w, x.gl[i], x.gr[i]); }
  }
  void voice(GrainsV& x, float& l, float& r) override {
    const float dt = 1.f / sr; x.t += dt; float L = 0, R = 0;
    for (int i = 0; i < x.n; i++) { const float u = (x.t - x.at[i]) / x.dur[i]; if (u < 0 || u > 1) continue;
      x.ph[i] += x.fr[i] * dt; x.ph[i] -= std::floor(x.ph[i]); const float s = std::sin(TAU * x.ph[i]) * hann(u); L += s * x.gl[i]; R += s * x.gr[i]; }
    l = L; r = R;
  }
};
static Reg rGrains(new ModuleDef{"grains", "GRAIN CLOUD", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), ST("dens", "DENSITY", 1, 12, 7), T("spread", "SPREAD", 0.02f, 1, 0.18f), T("grain", "GRAIN", 0.005f, 0.12f, 0.03f)}, ENVD(), 0.5f), 0, 58, false, [] { return new Grains(); }});

// ---------------- ANALOG: an oscillator and a sub through a resonant lowpass that snaps open on each note ----------------
struct AnalogV : VoiceBase { Osc o, sub; Biquad lp; float t = 0, top = 1000; int cnt = 0; };
struct Analog : Poly<AnalogV> {
  enum { PITCHK, WAVE, SUB, CUT, RES, FENV, FDEC };
  float q = 1, kf = 0.01f;
  Analog() { gainScale = 0.15f; }
  void voicePrep() override { q = std::pow(10.f, (0.5f + e[RES] * 14) / 20.f); kf = 3.f / std::max(0.01f, e[FDEC]); }
  void start(AnalogV& x) override { x.t = 0; x.cnt = 0; x.o.ph = rng.uni(); x.sub.ph = x.o.ph; x.lp.reset(); x.top = std::min(16000.f, e[CUT] * (1 + e[FENV] * 12 * x.vel)); }
  void voice(AnalogV& x, float& l, float& r) override {
    if ((x.cnt++ & 15) == 0) { const float c = x.t < 0.003f ? x.top : e[CUT] + (x.top - e[CUT]) * std::exp(-(x.t - 0.003f) * kf); x.lp.set(Biquad::LP, c, q, sr); }
    x.t += 1.f / sr;
    const int w = (int)e[WAVE]; const Osc::Wave wv = w == 0 ? Osc::SAW : w == 1 ? Osc::SQUARE : Osc::TRI;
    const float y = x.o.tick(wv, x.f, sr) + x.sub.tick(Osc::SQUARE, x.f * 0.5f, sr) * e[SUB] * 0.6f;
    l = r = x.lp.run(y);
  }
};
static Reg rAnalog(new ModuleDef{"analog", "ANALOG", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), OP("wave", "WAVE", {"SAW", "SQUARE", "TRI"}, 0), pct("sub", "SUB", 0.3f), prHz("cut", "CUTOFF", 60, 12000, 700), pct("res", "RES", 0.3f), pct("fenv", "FILT ENV", 0.5f), T("fdec", "FILT DEC", 0.01f, 6, 0.4f)},
     envP(0.005f, 0.4f, 0.6f, 0.6f, 0.8f), 0.3f), 0, 58, false, [] { return new Analog(); }});

// ---------------- FORMANT: a sung vowel, which can glide to another vowel ----------------
static const float FORMANTS[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};   // A E I O U
static float fmAt(float v, int k) { const float x = clampf(v, 0, 1) * 4; const int i = std::min(3, (int)x); const float fr = x - i; return FORMANTS[i][k] * (1 - fr) + FORMANTS[i + 1][k] * fr; }
struct FormantV : VoiceBase { Osc src; Biquad bp[3]; float t = 0, v0 = 0, v1 = 0, vr = 5, vph = 0; int cnt = 0; };
struct Formant : Poly<FormantV> {
  enum { PITCHK, VOWEL, GLIDE, GTIME, VIB, BREATH };
  Formant() { gainScale = 0.72f; }
  void start(FormantV& x) override { x.t = 0; x.cnt = 0; x.vph = 0; x.src.ph = 0; x.v0 = e[VOWEL]; x.v1 = clampf(e[VOWEL] + e[GLIDE], 0, 1); x.vr = rng.range(4.6f, 5.6f); for (auto& b : x.bp) b.reset(); }
  void voice(FormantV& x, float& l, float& r) override {
    static const float Q[3] = {7, 10, 12}, A[3] = {1, 0.45f, 0.2f};
    const float dt = 1.f / sr;
    if ((x.cnt++ & 31) == 0 && (x.cnt == 1 || x.t <= e[GTIME] + 0.05f)) { const float u = std::min(1.f, x.t / std::max(0.05f, e[GTIME])), v = x.v0 + (x.v1 - x.v0) * u;
      for (int k = 0; k < 3; k++) x.bp[k].set(Biquad::BP, fmAt(v, k), Q[k], sr); }
    x.t += dt; x.vph += x.vr * dt; if (x.vph >= 1) x.vph -= 1;
    const float f = x.f + std::sin(TAU * x.vph) * x.f * 0.012f * e[VIB] * std::min(1.f, x.t / 0.6f);
    const float s = x.src.tick(Osc::SAW, f, sr) + rng.bi() * e[BREATH] * 0.5f;
    float y = 0; for (int k = 0; k < 3; k++) y += x.bp[k].run(s) * A[k];
    l = r = y;
  }
};
static Reg rFormant(new ModuleDef{"formant", "FORMANT", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), fmt(pct("vowel", "VOWEL", 0.2f), [](float v) { return std::string(1, "AEIOU"[std::min(4, (int)std::lround(v * 4))]) + " " + std::to_string((int)std::lround(v * 100)); }),
      fmt(pr("glide", "VOWEL GO", -1, 1, 0), [](float v) { return (v > 0 ? "+" : "") + std::to_string((int)std::lround(v * 100)); }),
      T("gtime", "GO TIME", 0.05f, 8, 1.5f), pct("vib", "VIBRATO", 0.3f), pct("breath", "BREATH", 0.15f)}, envP(0.08f, 0.5f, 0.8f, 1.5f, 1.5f), 0.3f), 0, 58, false, [] { return new Formant(); }});

// ---------------- BREATH: noise breathing through a narrow resonance at the note, a little pure tone under it ----------------
struct BreathV : VoiceBase { Biquad b1, b2; float fph = 0, fr = 5, sph = 0, g1 = 0, g2 = 0; bool two = false; };
struct Breath : Poly<BreathV> {
  enum { PITCHK, NOISE, Q, TONE, HM, FLUTTER };
  Breath() { gainScale = 2.4f; }
  void start(BreathV& x) override {
    x.b1.reset(); x.b2.reset(); x.fph = rng.uni(); x.fr = rng.range(3, 7); x.sph = 0;
    const float q = e[Q], hm = std::round(e[HM]); x.b1.set(Biquad::BP, x.f, q, sr); x.g1 = e[NOISE] * 0.22f * std::sqrt(q);
    x.two = hm > 0 && x.f * (hm + 1) <= 15000; if (x.two) { x.b2.set(Biquad::BP, x.f * (hm + 1), q, sr); x.g2 = e[NOISE] * 0.4f * 0.22f * std::sqrt(q); }
    if (x.f > 15000) x.g1 = 0;
  }
  void voice(BreathV& x, float& l, float& r) override {
    const float dt = 1.f / sr; x.fph += x.fr * dt; if (x.fph >= 1) x.fph -= 1; x.sph += x.f * dt; if (x.sph >= 1) x.sph -= 1;
    const float n = rng.bi() * (1 + e[FLUTTER] * 0.5f * std::sin(TAU * x.fph));
    float y = x.b1.run(n) * x.g1; if (x.two) y += x.b2.run(n) * x.g2;
    y += std::sin(TAU * x.sph) * e[TONE] * 0.12f;
    l = r = y;
  }
};
static Reg rBreath(new ModuleDef{"breath", "BREATH", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), pct("noise", "NOISE", 0.6f), fmt(prExp("q", "FOCUS", 5, 200, 40), [](float v) { return std::to_string((int)std::lround(v)); }), pct("tone", "TONE", 0.3f), OP("hm", "OVERTONE", {"OFF", "2ND", "3RD"}, 1), pct("flutter", "FLUTTER", 0.2f)},
     envP(0.25f, 0.6f, 0.7f, 1.2f, 1.8f), 0.4f), 0, 58, false, [] { return new Breath(); }});

// ---------------- ADDITIVE: a stack of sine partials; the upper ones fade sooner (TILT DEC) ----------------
struct AddV : VoiceBase { static constexpr int P = 16; float c[P], s[P], cw[P], sw[P], a[P], lv[P], kd[P], gl[P], gr[P]; int n = 0, cnt = 0; float t = 0; };
struct Additive : Poly<AddV> {
  enum { PITCHK, PARTS, BRIGHT, EVEN, STRETCH, DECAY, SHIMMER, ATK, DEC, SUS, HOLD, REL, WIDTH };
  Additive() { gainScale = 0.22f; selfPan = true; }
  void start(AddV& x) override {
    const int parts = (int)e[PARTS]; float amps[AddV::P], tot = 0;
    for (int k = 1; k <= parts; k++) { const float a = std::pow((float)k, -(0.6f + (1 - e[BRIGHT]) * 2)) * (k % 2 == 0 ? e[EVEN] : 1); amps[k - 1] = a; tot += a; }
    const float len = noteLen(x.env); x.n = 0; x.t = 0; x.cnt = 0;
    for (int k = 1; k <= parts; k++) {
      const float fr = x.f * k * (1 + e[STRETCH] * 0.015f * (k - 1)) * std::pow(2.f, rng.bi() * e[SHIMMER] * 12 / 1200.f); if (fr > 16000 || fr > sr * 0.45f) break;
      const int i = x.n++; const float w = TAU * fr / sr; x.cw[i] = std::cos(w); x.sw[i] = std::sin(w); x.c[i] = 1; x.s[i] = 0;
      x.a[i] = amps[k - 1] / tot; x.lv[i] = 1; const float dk = std::max(0.05f, len / (1 + e[DECAY] * (k - 1) * 1.5f)); x.kd[i] = std::exp(-3.f / (dk * sr));
      panGains(rng.bi() * e[WIDTH] * std::min(1.f, k / 4.f), x.gl[i], x.gr[i]);
    }
  }
  void voice(AddV& x, float& l, float& r) override {
    x.t += 1.f / sr; const float atk = std::min(1.f, x.t / 0.005f); float L = 0, R = 0;
    for (int i = 0; i < x.n; i++) {
      const float c = x.c[i] * x.cw[i] - x.s[i] * x.sw[i], s = x.s[i] * x.cw[i] + x.c[i] * x.sw[i]; x.c[i] = c; x.s[i] = s;   // a rotating phasor: no sin() per sample
      const float y = s * x.a[i] * x.lv[i]; if (x.t > 0.005f) x.lv[i] *= x.kd[i]; L += y * x.gl[i]; R += y * x.gr[i];
    }
    if ((++x.cnt & 255) == 0) for (int i = 0; i < x.n; i++) { const float m = 1.f / std::sqrt(x.c[i] * x.c[i] + x.s[i] * x.s[i]); x.c[i] *= m; x.s[i] *= m; }   // keep it on the circle
    l = L * atk; r = R * atk;
  }
};
static Reg rAdditive(new ModuleDef{"additive", "ADDITIVE", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), ST("parts", "PARTIALS", 2, 16, 10), pct("bright", "BRIGHT", 0.5f), pct("even", "EVEN", 0.7f), pct("stretch", "STRETCH", 0), pct("decay", "TILT DEC", 0.4f), pct("shimmer", "SHIMMER", 0.2f)},
     envP(0.01f, 0.5f, 0.7f, 1.5f, 2), 0.4f), 0, 58, false, [] { return new Additive(); }});

// ---------------- WAVEFOLD: a sine pushed into a folding curve; FOLD DEC lets it relax back towards a sine ----------------
struct FoldV : VoiceBase { float ph = 0, t = 0, drive = 0.12f, pk = 0.5f; Biquad lp, hp; float prev = 0; };
struct Fold : Poly<FoldV> {
  enum { PITCHK, FOLD, FDEC, FSUS, SYM, CUT };
  float kd = 0.01f;
  Fold() { gainScale = 0.13f; }
  void voicePrep() override { kd = 1 - std::exp(-3.f / (std::max(0.01f, e[FDEC]) * sr)); }
  void start(FoldV& x) override { x.ph = 0; x.t = 0; x.drive = 0.12f; x.pk = 0.12f + e[FOLD] * 0.88f * (0.5f + 0.5f * x.vel); x.lp.reset(); x.hp.reset(); x.lp.set(Biquad::LP, e[CUT], 0.707f, sr); x.hp.set(Biquad::HP, 25, 0.707f, sr); x.prev = 0; }
  static inline float shape(float v) { v = clampf(v, -1, 1); return std::sin(v * 4 * PI); }
  void voice(FoldV& x, float& l, float& r) override {
    const float dt = 1.f / sr; x.t += dt;
    if (x.t < 0.004f) x.drive = 0.12f + (x.pk - 0.12f) * x.t / 0.004f; else x.drive += (0.12f + (x.pk - 0.12f) * e[FSUS] - x.drive) * kd;
    x.ph += x.f * dt; if (x.ph >= 1) x.ph -= 1;
    const float in = std::sin(TAU * x.ph) * x.drive + e[SYM] * 0.12f;
    const float y = 0.5f * (shape(0.5f * (in + x.prev)) + shape(in)); x.prev = in;   // two points per sample: tames the folding's harshest edge
    l = r = x.hp.run(x.lp.run(y));
  }
};
static Reg rFold(new ModuleDef{"fold", "WAVEFOLD", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), pct("fold", "FOLD", 0.5f), T("fdec", "FOLD DEC", 0.01f, 6, 0.5f), pct("fsus", "FOLD SUS", 0.2f), pct("sym", "SYMMETRY", 0), prHz("cut", "SOFTEN", 500, 16000, 6000)},
     envP(0.003f, 0.4f, 0.6f, 0.8f, 1.2f), 0.3f), 0, 58, false, [] { return new Fold(); }});

}  // namespace tb
