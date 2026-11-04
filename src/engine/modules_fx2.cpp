// Timbient native: more effects (envelope VCA, resonator, phaser, tape, bitcrush) and the mod sources
// that follow triggers or sound (trig envelope, random, follower).
#include "params.h"

namespace tb {

// dry and wet levels that glide rather than jump
struct DryWet {
  Smooth d, w;
  void init(float sr, float dry, float wet) { d.setTime(0.012f, sr); w.setTime(0.012f, sr); d.snap(dry); w.snap(wet); }
  void target(float dry, float wet) { d.t = dry; w.t = wet; }
};

// ---------------- ENVELOPE VCA: each trigger opens it, then it closes again ----------------
struct Vca : Module {
  enum { ATK, DEC, SUS, HOLD, REL, MIX };
  float g = 0, from = 0, tt = 1e9f; DryWet dw;
  void prepare() override { dw.init(sr, 1 - e[MIX], e[MIX]); }
  void trig(int port, const Event&) override { if (port == 1) { from = g; tt = 0; } }
  void render(int a, int b) override {
    dw.target(1 - e[MIX], e[MIX]); const float dt = 1.f / sr, atk = std::max(0.002f, e[ATK]), endT = std::max(atk, e[HOLD]);
    const float kd = 1 - std::exp(-dt * 3 / std::max(0.01f, e[DEC])), kr = 1 - std::exp(-dt * 3 / std::max(0.01f, e[REL]));
    for (int i = a; i < b; i++) {
      tt += dt;
      if (tt < atk) g = from + (1 - from) * tt / atk; else if (tt < endT) g += (e[SUS] - g) * kd; else g -= g * kr;
      const float d = dw.d.tick(), w = dw.w.tick();
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i]; out[0][ch][i] = x * d + x * g * w; }
    }
  }
};
static Reg rVca(new ModuleDef{"vca", "ENVELOPE VCA", Cat::Proc, {{"IN", PType::Audio}, {"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  {T("atk", "ATTACK", 0.002f, 4, 0.02f), T("dec", "DECAY", 0.01f, 4, 0.3f), pct("sus", "SUSTAIN", 0.6f), T("hold", "HOLD", 0.01f, 8, 0.6f), T("rel", "RELEASE", 0.01f, 8, 1.5f), pct("mix", "MIX", 1)},
  0, 58, false, [] { return new Vca(); }});

// ---------------- RESONATOR: eight tuned bands, like striking a string or plate with the incoming sound ----------------
struct Resonator : Module {
  enum { STRUCT, BRIGHT, DAMP, POS, OCT, MIX };
  Biquad br, rf[8]; float rg[8]; DryWet dw; float sig[6] = {-1, -1, -1, -1, -1, -1}; int lastRoot = -1, lastScale = -1;
  void prepare() override { dw.init(sr, 1 - e[MIX] * 0.8f, e[MIX] * 1.4f); }
  void tune() {
    const float s[6] = {e[STRUCT], e[BRIGHT], e[DAMP], e[POS], e[OCT], 0}; bool same = root() == lastRoot && scale() == lastScale;
    for (int k = 0; k < 5; k++) if (s[k] != sig[k]) same = false;
    if (same) return; for (int k = 0; k < 5; k++) sig[k] = s[k]; lastRoot = root(); lastScale = scale();
    const float base = mtof(note((int)e[OCT], 0)), Q = 6 + std::pow(1 - e[DAMP], 2.f) * 420;
    br.set(Biquad::LP, 250 * std::pow(48.f, e[BRIGHT]), 0.707f, sr);
    for (int k = 0; k < 8; k++) { const int n = k + 1; rf[k].set(Biquad::BP, std::min(16000.f, base * n * (1 + e[STRUCT] * 0.22f * (n - 1))), Q, sr);
      rg[k] = (0.15f + std::fabs(std::sin(PI * n * (0.05f + e[POS] * 0.9f)))) * std::pow(0.45f + e[BRIGHT] * 0.5f, (float)k) * std::sqrt(Q) * 0.22f; }
  }
  void render(int a, int b) override {
    tune(); dw.target(1 - e[MIX] * 0.8f, e[MIX] * 1.4f);
    for (int i = a; i < b; i++) { const float d = dw.d.tick(), w = dw.w.tick();
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i], y = br.run(x, ch); float acc = 0; for (int k = 0; k < 8; k++) acc += rf[k].run(y, ch) * rg[k];
        out[0][ch][i] = x * d + sane(acc) * w; } }
  }
};
static Reg rResonator(new ModuleDef{"resonator", "RESONATOR", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {pct("struct", "STRUCT", 0.25f), pct("bright", "BRIGHT", 0.5f), pct("damp", "DAMP", 0.5f), pct("pos", "POSITION", 0.3f), OCTP("oct", 1, 5, 3), pct("mix", "MIX", 0.7f)}, 0, 58, false, [] { return new Resonator(); }});

// ---------------- PHASER: two chains of allpass filters swept by one LFO; STEREO sweeps the right side the opposite way ----------------
struct Phaser : Module {
  enum { RATE, DEPTH, CENTRE, STAGES, FB, STEREO, MIX };
  Biquad ap[2][8]; DelayLine fbd[2]; float tail[2] = {0, 0}, ph = 0; int cnt = 0; DryWet dw;
  void prepare() override { for (auto& d : fbd) d.init(0.01f, sr); dw.init(sr, 1 - e[MIX], e[MIX]); ph = rng.uni(); }
  void render(int a, int b) override {
    dw.target(1 - e[MIX], e[MIX]); const int n = ((int)e[STAGES] + 1) * 2; const float sw = e[CENTRE] * e[DEPTH] * 0.85f, fb = e[FB] * 0.85f, dly = 0.003f * sr;
    for (int i = a; i < b; i++) {
      ph += e[RATE] / sr; if (ph >= 1) ph -= 1;
      if ((cnt++ & 15) == 0) { const float l = std::sin(TAU * ph);
        for (int s = 0; s < 2; s++) { const float f = std::max(20.f, e[CENTRE] + l * sw * (s == 1 && e[STEREO] > 0.5f ? -1 : 1)); ap[s][0].set(Biquad::AP, f, 0.6f, sr); for (int k = 1; k < n; k++) { ap[s][k].b0 = ap[s][0].b0; ap[s][k].b1 = ap[s][0].b1; ap[s][k].b2 = ap[s][0].b2; ap[s][k].a1 = ap[s][0].a1; ap[s][k].a2 = ap[s][0].a2; } } }
      const float d = dw.d.tick(), w = dw.w.tick();
      for (int s = 0; s < 2; s++) {
        const float x = in[0][s][i]; float y = x + fbd[s].read(dly);
        for (int k = 0; k < n; k++) y = ap[s][k].run(y, 0);
        fbd[s].write(sane(y * fb)); out[0][s][i] = x * d + y * w;
      }
    }
    disp[0].store(ph);
  }
};
static Reg rPhaser(new ModuleDef{"phaser", "PHASER", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(prExp("rate", "RATE", 0.02f, 8, 0.25f), [](float v) { char b[16]; std::snprintf(b, 16, v < 1 ? "%.2fHZ" : "%.1fHZ", v); return std::string(b); }), pct("depth", "DEPTH", 0.7f),
   prHz("centre", "CENTRE", 150, 4000, 700), OP("stages", "STAGES", {"2", "4", "6", "8"}, 1), pr("fb", "FEEDBACK", 0, 0.9f, 0.45f), OP("stereo", "STEREO", {"OFF", "ON"}, 1), pct("mix", "MIX", 0.5f)},
  5, 58, false, [] { return new Phaser(); }});

// ---------------- TAPE: wow, flutter, saturation, a darker tone and some hiss ----------------
struct Tape : Module {
  enum { WOW, FLUTTER, SAT, TONE, HISS, MIX };
  DelayLine dry[2], wet[2]; Biquad lp, hp; Smooth base; float p1 = 0, p2 = 0, ph = 0, drT = 0, satK = -1, satN = 1; DryWet dw;
  void prepare() override { for (int c = 0; c < 2; c++) { dry[c].init(0.1f, sr); wet[c].init(0.1f, sr); } base.setTime(0.4f, sr); base.snap(0.012f); hp.set(Biquad::HP, 2500, 0.707f, sr); dw.init(sr, 1 - e[MIX], e[MIX]); }
  void render(int a, int b) override {
    dw.target(1 - e[MIX], e[MIX]); lp.set(Biquad::LP, e[TONE], 0.75f, sr);
    const float k = 1 + e[SAT] * 7; satN = 1.f / std::pow(k, 0.7f); const float hiss = e[HISS] * e[HISS] * 0.03f, dt = 1.f / sr;
    drT -= (b - a) * dt; if (drT <= 0) { drT = rng.range(0.4f, 1.5f); base.t = 0.012f + rng.bi() * e[WOW] * 0.0015f; }   // a slow random drift on top of the wow
    for (int i = a; i < b; i++) {
      p1 += 0.55f * dt; if (p1 >= 1) p1 -= 1; p2 += 6.3f * dt; if (p2 >= 1) p2 -= 1;
      const float dl = (base.tick() + std::sin(TAU * p1) * e[WOW] * 0.0035f + std::sin(TAU * p2) * e[FLUTTER] * 0.00035f) * sr;
      const float h = hp.run(rng.bi(), 0) * hiss, d = dw.d.tick(), w = dw.w.tick();
      for (int c = 0; c < 2; c++) { const float x = in[0][c][i]; dry[c].write(x); wet[c].write(std::tanh(x * k) * satN);
        out[0][c][i] = dry[c].read(0.012f * sr) * d + (lp.run(wet[c].read(std::max(1.f, dl)), c) + h) * w; }
    }
    ph += (b - a) * dt * 0.55f; ph -= std::floor(ph); disp[0].store(ph);
  }
};
static Reg rTape(new ModuleDef{"tape", "TAPE", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {pct("wow", "WOW", 0.3f), pct("flutter", "FLUTTER", 0.2f), pct("sat", "SATURATE", 0.3f), prHz("tone", "TONE", 800, 16000, 6000), pct("hiss", "HISS", 0.1f), pct("mix", "MIX", 1)},
  5, 58, false, [] { return new Tape(); }});

// ---------------- BITCRUSH: fewer bits, a lower sample rate ----------------
struct Crush : Module {
  enum { BITS, RATE, MIX };
  float ph = 0, hl = 0, hr = 0; DryWet dw;
  void prepare() override { dw.init(sr, 1 - e[MIX], e[MIX]); }
  void render(int a, int b) override {
    dw.target(1 - e[MIX], e[MIX]); const float q = std::pow(2.f, e[BITS] - 1), step = std::min(1.f, e[RATE] / sr);
    for (int i = a; i < b; i++) { ph += step; if (ph >= 1) { ph -= 1; hl = std::round(in[0][0][i] * q) / q; hr = std::round(in[0][1][i] * q) / q; }
      const float d = dw.d.tick(), w = dw.w.tick(); out[0][0][i] = in[0][0][i] * d + hl * w; out[0][1][i] = in[0][1][i] * d + hr * w; }
  }
};
static Reg rCrush(new ModuleDef{"crush", "BITCRUSH", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {ST("bits", "BITS", 1, 16, 8), prHz("rate", "RATE", 400, 44100, 11025), pct("mix", "MIX", 1)}, 0, 58, false, [] { return new Crush(); }});

// ---------------- TRIG ENV: each trigger draws a rise and a fall ----------------
struct TrigEnv : Module {
  enum { ATK, DEC, CURVE, VSENS, DEPTH, DIR };
  float lv = 0, peak = 1; int stage = 0;
  void trig(int, const Event& ev) override { stage = 1; peak = 1 - e[VSENS] * (1 - ev.vel); }
  void render(int a, int b) override {
    const float dt = (b - a) / sr;
    if (stage == 1) { lv += dt / std::max(0.002f, e[ATK]); if (lv >= peak) { lv = peak; stage = 2; } }
    else if (stage == 2) { const float lin = peak * dt / e[DEC], ex = lv * (1 - std::exp(-dt * 5 / e[DEC])); lv = std::max(0.f, lv - (lin * (1 - e[CURVE]) + ex * e[CURVE])); if (lv < 0.0005f) { lv = 0; stage = 0; } }
    modv = lv * 2 - 1; disp[0].store(modv);
  }
};
static Reg rEnv(new ModuleDef{"env", "TRIG ENV", Cat::Mod, {{"TRIG", PType::Trig}}, {{"MOD", PType::Mod}},
  {T("atk", "ATTACK", 0.002f, 8, 0.05f), T("dec", "DECAY", 0.02f, 16, 1.5f), pct("curve", "CURVE", 0.6f), pct("vsens", "VEL SENS", 0.5f), pct("depth", "DEPTH", 0.5f), OP("dir", "MOVES", {"BOTH", "UP", "DOWN"}, 1)},
  7, 58, false, [] { return new TrigEnv(); }});

// ---------------- RANDOM: a new value every so often (or on each trigger), with a glide between ----------------
struct RandomMod : Module {
  enum { RATE, GLIDE, CHANCE, DEPTH, DIR };
  float v = 0, target = 0, next = 0;
  void pick() { if (rng.uni() < e[CHANCE]) target = rng.bi(); }
  void blockStart(int) override { clockedIn.store(patched(0)); }
  void trig(int, const Event&) override { pick(); }
  void render(int a, int b) override {
    const float dt = (b - a) / sr;
    if (!clockedIn.load()) { next -= dt; if (next <= 0) { next = e[RATE]; pick(); } }
    v += (target - v) * (1 - std::exp(-dt * 3 / std::max(0.001f, e[GLIDE]))); modv = v; disp[0].store(v);
  }
};
static Reg rRand(new ModuleDef{"rand", "RANDOM", Cat::Mod, {{"TRIG", PType::Trig}}, {{"MOD", PType::Mod}},
  {fmt(prExp("rate", "EVERY", 0.05f, 60, 2), [](float v) { return fmtSecs(v); }), T("glide", "GLIDE", 0.001f, 8, 0.05f), pct("chance", "CHANGE", 1), pct("depth", "DEPTH", 0.5f), OP("dir", "MOVES", {"BOTH", "UP", "DOWN"}, 0)},
  7, 58, false, [] { return new RandomMod(); }});

// ---------------- FOLLOWER: how loud the incoming sound is, as a modulation ----------------
struct Follower : Module {
  enum { GAIN, ATK, REL, DEPTH, DIR };
  float ms = 0, env = 0;
  void render(int a, int b) override {
    const float kw = 1 - std::exp(-1.f / (0.021f * sr));   // about the same window as the web version's 1024 samples
    for (int i = a; i < b; i++) { const float x = 0.5f * (in[0][0][i] + in[0][1][i]); ms += (x * x - ms) * kw; }
    const float target = clampf(std::sqrt(std::max(0.f, ms)) * 1.4f * dbToGain(e[GAIN]), 0, 1), tc = target > env ? e[ATK] : e[REL], dt = (b - a) / sr;
    env += (target - env) * (1 - std::exp(-dt * 3 / std::max(0.002f, tc))); modv = env * 2 - 1; disp[0].store(modv);
  }
};
static Reg rFollow(new ModuleDef{"follow", "FOLLOWER", Cat::Mod, {{"IN", PType::Audio}}, {{"MOD", PType::Mod}},
  {fmt(pr("gain", "GAIN", -12, 36, 12), [](float v) { return (v > 0 ? "+" : "") + std::to_string((int)std::lround(v)) + "DB"; }), T("atk", "ATTACK", 0.002f, 2, 0.02f), T("rel", "RELEASE", 0.02f, 8, 0.4f), pct("depth", "DEPTH", 0.5f), OP("dir", "MOVES", {"BOTH", "UP", "DOWN"}, 1)},
  7, 58, false, [] { return new Follower(); }});

}  // namespace tb
