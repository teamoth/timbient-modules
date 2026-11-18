// Timbient native: the rest of the voices (glass, noise click, grain cloud, analog, formant, breath, additive, wavefold).
// Each one follows the web version's recipe and levels, so sessions sound the same.
#include "voice.h"

namespace tb {

static std::vector<ParamDef> vp(std::vector<ParamDef> p, std::vector<ParamDef> env, float width) {
  for (auto& q : env) p.push_back(q); p.push_back(pct("vsens", "VEL SENS", 1)); p.push_back(pct("width", "WIDTH", width)); p.push_back(LEVEL()); return p;
}
static std::vector<ParamDef> ENVD() { return envP(0.003f, 0.3f, 1, 4, 2); }

// ---------------- NOISE CLICK: a burst of noise through a tuned band ----------------
struct ClickV : VoiceBase { Biquad bp; float t = 0, d = 0.08f, kd = 0; int gc = 0; };
struct Click : Poly<ClickV> {
  enum { PITCHK, Q, DECAY, TUNE };
  Click() { gainScale = 2.2f; }
  void start(ClickV& x) override { x.t = 0; x.d = e[DECAY]; x.bp.reset(); x.bp.set(Biquad::BP, std::min(15000.f, x.f * e[TUNE]), e[Q], sr); x.kd = std::log(0.0001f) / std::max(0.005f, x.d - 0.002f); }
  void voice(ClickV& x, float& l, float& r) override {
    x.t += 1.f / sr; if (x.t > x.d + 0.02f) { l = r = 0; return; }
    if (x.glLen > 0 && (++x.gc & 31) == 0) x.bp.set(Biquad::BP, std::min(15000.f, x.f * e[TUNE]), e[Q], sr);   // GLIDE: the band follows
    const float a = x.t < 0.002f ? x.t / 0.002f : std::exp(x.kd * (x.t - 0.002f));
    l = r = x.bp.run(rng.bi()) * a;
  }
};
static Reg rClick(new ModuleDef{"click", "NOISE CLICK", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  vp({PITCH(), pr("q", "FOCUS", 1, 30, 12), T("decay", "RING", 0.01f, 0.5f, 0.08f), pr("tune", "BAND", 0.5f, 4, 2)}, ENVD(), 0.4f), 0, 68, false, [] { return new Click(); }});

// ---------------- ANALOG: an oscillator and a sub through a resonant lowpass that snaps open on each note ----------------
// FILTER: CLASSIC is the clean lowpass it always was. SEM: a smooth two-pole state-variable filter; SHAPE moves it from lowpass
// through a notch to highpass, like the old Oberheim modules. Heavy (each note's filter runs at twice the rate): LADDER, the
// warm four-pole that sings with RES up, and DIODE, squelchier and more rubbery. DRIVE pushes into the heavy ones.
static inline float atanh2(float x) { x = std::max(-3.f, std::min(3.f, x)); const float x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }   // a cheap tanh
struct AnalogV : VoiceBase { Osc o, sub; Biquad lp; float t = 0, top = 1000, c = 1000, st[4] = {}, s1 = 0, s2 = 0, xp = 0; int cnt = 0; };
struct Analog : Poly<AnalogV> {
  enum { PITCHK, WAVE, SUB, CUT, RES, FENV, FDEC };
  enum { CLASSIC, SEM, LADDER, DIODE };
  float q = 1, kf = 0.01f; int iType = -1, iShape = -1, iDrive = -1, ty = 0;
  Analog() { gainScale = 0.15f; }
  void prepare() override { Poly<AnalogV>::prepare(); iType = pi("type"); iShape = pi("shape"); iDrive = pi("drive"); }
  void voicePrep() override { q = std::pow(10.f, (0.5f + e[RES] * 14) / 20.f); kf = 3.f / std::max(0.01f, e[FDEC]); ty = iType >= 0 ? std::max(0, std::min(3, (int)std::lround(e[iType]))) : 0; }
  void start(AnalogV& x) override { voicePrep(); x.t = 0; x.cnt = 0; x.o.ph = rng.uni(); x.sub.ph = x.o.ph; x.lp.reset(); x.top = std::min(16000.f, e[CUT] * (1 + e[FENV] * 12 * x.vel)); for (float& v : x.st) v = 0; x.s1 = x.s2 = x.xp = 0; }
  void voice(AnalogV& x, float& l, float& r) override {
    if ((x.cnt++ & 15) == 0) { x.c = x.t < 0.003f ? x.top : e[CUT] + (x.top - e[CUT]) * std::exp(-(x.t - 0.003f) * kf); if (ty == CLASSIC) x.lp.set(Biquad::LP, x.c, q, sr); }
    x.t += 1.f / sr;
    const int w = (int)e[WAVE]; const Osc::Wave wv = w == 0 ? Osc::SAW : w == 1 ? Osc::SQUARE : Osc::TRI;
    const float y = x.o.tick(wv, x.f, sr) + x.sub.tick(Osc::SQUARE, x.f * 0.5f, sr) * e[SUB] * 0.6f;
    if (ty == CLASSIC) { l = r = x.lp.run(y); return; }
    const float fc = std::min(x.c, 0.45f * sr), rs = e[RES];
    if (ty == SEM) {   // state-variable: lowpass, bandpass and highpass at once; SHAPE goes lowpass -> notch (lowpass + highpass) -> highpass
      const float g = std::tan(PI * fc / sr), k = 2 - 1.9f * rs, hp = (y - (k + g) * x.s1 - x.s2) / (1 + k * g + g * g), bp = g * hp + x.s1, lp = g * bp + x.s2;
      x.s1 = sane(g * hp + bp); x.s2 = sane(g * bp + lp); const float sh = iShape >= 0 ? e[iShape] : 0;
      l = r = (sh < 0.5f ? lp + hp * (sh * 2) : hp + lp * (2 - sh * 2)) * (1 + rs * 0.5f) * 0.9f; return; }
    const float a1 = 1 - std::exp(-TAU * fc / (2 * sr)), dv = 1 + (iDrive >= 0 ? e[iDrive] : 0.2f) * 5, kq = rs * (ty == LADDER ? 4.3f : 4.6f); float o = 0;
    for (int os = 0; os < 2; os++) { const float xi = (os == 0 ? 0.5f * (x.xp + y) : y) * 0.6f;   // twice the rate inside
      float u = atanh2(xi * dv - kq * x.st[3]);
      for (int s2 = 0; s2 < 4; s2++) { const float tgt = ty == DIODE && s2 < 3 ? 0.6f * u + 0.4f * x.st[s2 + 1] : u; x.st[s2] += a1 * (atanh2(tgt) - atanh2(x.st[s2])); u = x.st[s2]; }
      o = x.st[3]; }
    x.xp = y; l = r = sane(o * (1 + rs * (ty == LADDER ? 1.6f : 1.3f)) / std::sqrt(dv) * (ty == LADDER ? 1.9f : 2.9f));
  }
};
static Reg rAnalog(new ModuleDef{"analog", "ANALOG", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  [] { auto p = vp({PITCH(), OP("wave", "WAVE", {"SAW", "SQUARE", "TRI"}, 0), pct("sub", "SUB", 0.3f), prHz("cut", "CUTOFF", 40, 20000, 700), pct("res", "RES", 0.3f), pct("fenv", "FILT ENV", 0.5f), T("fdec", "FILT DEC", 0.01f, 6, 0.4f)},
     envP(0.005f, 0.4f, 0.6f, 0.6f, 0.8f), 0.3f);
   p.push_back(noMod(OP("type", "FILTER", {"CLASSIC", "SEM", "LADDER", "DIODE"}, 0))); p.push_back(only(pct("shape", "SHAPE", 0.f), "v:1")); p.push_back(only(pct("drive", "DRIVE", 0.2f), "v:2,3")); return p; }(), 0, 68, false, [] { return new Analog(); }});

// ---------------- FORMANT: a sung vowel, which can glide to another vowel ----------------
static const float FORMANTS[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};   // A E I O U
static float fmAt(float v, int k) { const float x = clampf(v, 0, 1) * 4; const int i = std::min(3, (int)x); const float fr = x - i; return FORMANTS[i][k] * (1 - fr) + FORMANTS[i + 1][k] * fr; }
// TYPE: CLASSIC is the sung vowel it always was (a saw through three resonances). SINGER is a fuller voice: the source is a
// pulse shaped like the vocal folds opening and snapping shut, wavering very slightly from one cycle to the next as a real voice
// does, with breath that comes and goes with each opening; five resonances per vowel, including the "singer's ring" round 3 kHz.
// SIZE moves the voice from a bass (left) to a soprano (right).
static const float SING[2][5][15] = {   // [bass, soprano][A E I O U]: five resonances (Hz), their levels (dB), their widths (Hz)
  {{600, 1040, 2250, 2450, 2750, 0, -7, -9, -9, -20, 60, 70, 110, 120, 130}, {400, 1620, 2400, 2800, 3100, 0, -12, -9, -12, -18, 40, 80, 100, 120, 120},
   {250, 1750, 2600, 3050, 3340, 0, -30, -16, -22, -28, 60, 90, 100, 120, 120}, {400, 750, 2400, 2600, 2900, 0, -11, -21, -20, -40, 40, 80, 100, 120, 120},
   {350, 600, 2400, 2675, 2950, 0, -20, -32, -28, -36, 40, 80, 100, 120, 120}},
  {{800, 1150, 2900, 3900, 4950, 0, -6, -32, -20, -50, 80, 90, 120, 130, 140}, {350, 2000, 2800, 3600, 4950, 0, -20, -15, -40, -56, 60, 100, 120, 150, 200},
   {270, 2140, 2950, 3900, 4950, 0, -12, -26, -26, -44, 60, 90, 100, 120, 120}, {450, 800, 2830, 3800, 4950, 0, -11, -22, -22, -50, 40, 80, 100, 120, 120},
   {325, 700, 2700, 3800, 4950, 0, -16, -35, -40, -60, 50, 60, 170, 180, 200}}};
struct FormantV : VoiceBase { Osc src; Biquad bp[3], b5[5]; float a5[5] = {}, t = 0, v0 = 0, v1 = 0, vr = 5, vph = 0, gph = 0, jit = 1, shm = 1, drift = 0; int cnt = 0; };
struct Formant : Poly<FormantV> {
  enum { PITCHK, VOWEL, GLIDE, GTIME, VIB, BREATH };
  int iType = -1, iSize = -1, ty = 0;
  Formant() { gainScale = 0.72f; }
  void prepare() override { Poly<FormantV>::prepare(); iType = pi("type"); iSize = pi("size"); }
  void voicePrep() override { ty = iType >= 0 ? std::max(0, std::min(1, (int)std::lround(e[iType]))) : 0; }
  void start(FormantV& x) override { voicePrep(); x.t = 0; x.cnt = 0; x.vph = 0; x.src.ph = 0; x.v0 = e[VOWEL]; x.v1 = clampf(e[VOWEL] + e[GLIDE], 0, 1); x.vr = rng.range(4.6f, 5.6f); for (auto& b : x.bp) b.reset(); for (auto& b : x.b5) b.reset(); x.gph = 0; x.jit = x.shm = 1; x.drift = 0; }
  void singerAt(FormantV& x, float v) {   // the five resonances for this vowel (sliding between neighbours) and SIZE (bass to soprano)
    const float p = clampf(v, 0, 1) * 4; const int i = std::min(3, (int)p); const float fr = p - i, sz = iSize >= 0 ? e[iSize] : 0.5f;
    for (int k = 0; k < 5; k++) { float F[2], A[2], W[2];
      for (int g = 0; g < 2; g++) { const float* a0 = SING[g][i]; const float* a1 = SING[g][i + 1]; F[g] = a0[k] + (a1[k] - a0[k]) * fr; A[g] = a0[5 + k] + (a1[5 + k] - a0[5 + k]) * fr; W[g] = a0[10 + k] + (a1[10 + k] - a0[10 + k]) * fr; }
      const float f = std::min(0.45f * sr, F[0] * std::pow(F[1] / F[0], sz)), w = W[0] + (W[1] - W[0]) * sz; x.b5[k].set(Biquad::BP, f, f / w, sr); x.a5[k] = dbToGain(A[0] + (A[1] - A[0]) * sz); } }
  void voice(FormantV& x, float& l, float& r) override {
    static const float Q[3] = {7, 10, 12}, A[3] = {1, 0.45f, 0.2f};
    const float dt = 1.f / sr;
    if ((x.cnt++ & 31) == 0 && (x.cnt == 1 || x.t <= e[GTIME] + 0.05f)) { const float u = std::min(1.f, x.t / std::max(0.05f, e[GTIME])), v = x.v0 + (x.v1 - x.v0) * u;
      if (ty == 1) singerAt(x, v); else for (int k = 0; k < 3; k++) x.bp[k].set(Biquad::BP, fmAt(v, k), Q[k], sr); }
    x.t += dt; x.vph += x.vr * dt; if (x.vph >= 1) x.vph -= 1;
    const float f = x.f + std::sin(TAU * x.vph) * x.f * 0.012f * e[VIB] * std::min(1.f, x.t / 0.6f);
    if (ty == 1) {   // the vocal folds: open (rising), closing (falling faster), then shut; what comes out is how fast the air changes
      const float ff = f * x.jit * (1 + x.drift), d = std::min(0.45f, ff * dt); x.gph += d;
      if (x.gph >= 1) { x.gph -= 1; x.jit = 1 + rng.bi() * 0.003f; x.shm = 1 + rng.bi() * 0.05f; x.drift += 0.002f * (rng.bi() * 0.003f - x.drift * 0.5f); }
      const float Tp = 0.4f, Tn = 0.16f, p = x.gph; float g, flow;
      if (p < Tp) { g = 0.5f * PI / Tp * std::sin(PI * p / Tp); flow = 0.5f * (1 - std::cos(PI * p / Tp)); }
      else if (p < Tp + Tn) { g = -PI / (2 * Tn) * std::sin(PI * (p - Tp) / (2 * Tn)); flow = std::cos(PI * (p - Tp) / (2 * Tn)); }
      else { g = 0; flow = 0; }
      { float t2 = p - (Tp + Tn); if (t2 < 0) t2 += 1; const float J = PI / (2 * Tn);   // the snap shut is a jump of J: smoothed over a sample either side (polyBLEP), so it doesn't alias
        float c = 0; if (t2 < d) { const float u = t2 / d; c = u + u - u * u - 1; } else if (t2 > 1 - d) { const float u = (t2 - 1) / d; c = u * u + u + u + 1; } g += 0.5f * J * c; }
      const float src = g / (PI / (2 * Tn)) * x.shm + rng.bi() * e[BREATH] * (0.15f + 0.6f * flow);
      float y = 0; for (int k = 0; k < 5; k++) y += x.b5[k].run(src) * x.a5[k];
      l = r = y * 1.3f; return; }
    const float s = x.src.tick(Osc::SAW, f, sr) + rng.bi() * e[BREATH] * 0.5f;
    float y = 0; for (int k = 0; k < 3; k++) y += x.bp[k].run(s) * A[k];
    l = r = y;
  }
};
static Reg rFormant(new ModuleDef{"formant", "FORMANT", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  [] { auto p = vp({PITCH(), fmt(pct("vowel", "VOWEL", 0.2f), [](float v) { return std::string(1, "AEIOU"[std::min(4, (int)std::lround(v * 4))]) + " " + std::to_string((int)std::lround(v * 100)); }),
      fmt(pr("glide", "MOVE TO", -1, 1, 0), [](float v) { return (v > 0 ? "+" : "") + std::to_string((int)std::lround(v * 100)); }),
      T("gtime", "MOVE TIME", 0.05f, 8, 1.5f), pct("vib", "VIBRATO", 0.3f), pct("breath", "BREATH", 0.15f)}, envP(0.08f, 0.5f, 0.8f, 1.5f, 1.5f), 0.3f);
   p.push_back(noMod(OP("type", "TYPE", {"CLASSIC", "SINGER"}, 0))); p.push_back(only(pct("size", "SIZE", 0.5f), "v:1")); return p; }(), 0, 68, false, [] { return new Formant(); }});

// ---------------- BREATH: noise breathing through a narrow resonance at the note, a little pure tone under it ----------------
// TYPE: AIR is the breathy resonance it always was. FLUTE and REED are physical models: a tube of air (a delay as long as the
// note) kept sounding by a jet of breath across an edge (FLUTE; JET is how far the jet travels: short is breathy and high, long
// overblows lower) or by a vibrating reed (REED, closed at one end like a clarinet: hollow, odd harmonics; STIFF is the reed).
// BLOW is how hard you blow; NOISE the breath in the tone; FLUTTER a gentle vibrato.
struct BreathV : VoiceBase { Biquad b1, b2; float fph = 0, fr = 5, sph = 0, g1 = 0, g2 = 0; bool two = false;
  float bore[4096], jet[2048]; int bw = 0, jw = 0; float D = 100, JD = 30, lp = 0, dcx = 0, dcy = 0, ramp = 0, z1 = 0; };
struct Breath : Poly<BreathV> {
  enum { PITCHK, NOISE, Q, TONE, HM, FLUTTER };
  enum { AIR, FLUTE, REED };
  int iType = -1, iBlow = -1, iJet = -1, iStiff = -1, ty = 0;
  Breath() { gainScale = 2.4f; }
  void prepare() override { Poly<BreathV>::prepare(); iType = pi("type"); iBlow = pi("blow"); iJet = pi("jet"); iStiff = pi("stiff"); }
  void voicePrep() override { ty = iType >= 0 ? std::max(0, std::min(2, (int)std::lround(e[iType]))) : 0; }
  void tuneModel(BreathV& x) {
    if (ty == FLUTE) { const float jt = iJet >= 0 ? e[iJet] : 0.5f; x.D = std::max(4.f, std::min(4000.f, (sr / (x.f * 0.6557f) - 1.2f) * std::pow(2.f, 0.058f * (1 - 2 * jt))));   /* (the jet pulls the pitch: kept in tune) */   /* it sounds a fifth above its bore (it overblows) */ x.JD = std::max(2.f, std::min(2000.f, x.D * (0.25f + 0.14f * (iJet >= 0 ? e[iJet] : 0.5f)))); }
    else x.D = std::max(4.f, std::min(4000.f, sr / x.f * 0.5f - 0.5f)); }
  void start(BreathV& x) override { voicePrep();   // (a note can come before the first block)
    x.b1.reset(); x.b2.reset(); x.fph = rng.uni(); x.fr = rng.range(3, 7); x.sph = 0;
    if (ty != AIR) { for (float& v : x.bore) v = 0; for (float& v : x.jet) v = 0; x.bw = x.jw = 0; x.lp = x.dcx = x.dcy = x.ramp = x.z1 = 0; tuneModel(x); x.fr = rng.range(4.5f, 5.5f); return; }
    const float q = e[Q], hm = std::round(e[HM]); x.b1.set(Biquad::BP, x.f, q, sr); x.g1 = e[NOISE] * 0.22f * std::sqrt(q);
    x.two = hm > 0 && x.f * (hm + 1) <= 15000; if (x.two) { x.b2.set(Biquad::BP, x.f * (hm + 1), q, sr); x.g2 = e[NOISE] * 0.4f * 0.22f * std::sqrt(q); }
    if (x.f > 15000) x.g1 = 0;
  }
  static inline float rdl(const float* b, int mask, int w, float d) { float rp = (float)w - d; while (rp < 0) rp += mask + 1; const int i = (int)rp; const float f = rp - i; return b[i & mask] + (b[(i + 1) & mask] - b[i & mask]) * f; }
  void voice(BreathV& x, float& l, float& r) override {
    const float dt = 1.f / sr; x.fph += x.fr * dt; if (x.fph >= 1) x.fph -= 1; x.sph += x.f * dt; if (x.sph >= 1) x.sph -= 1;
    if (ty != AIR) {
      if (x.glLen > 0) tuneModel(x);
      x.ramp = std::min(1.f, x.ramp + dt / 0.03f); const float vib = std::sin(TAU * x.fph) * e[FLUTTER] * 0.04f, blow = iBlow >= 0 ? e[iBlow] : 0.6f, nz = rng.bi() * e[NOISE];
      float y;
      if (ty == FLUTE) {   // after the classic flute model: the jet's delay, its curve, and the bore
        const float bp = (1.0f + blow * 0.35f) * x.ramp * (1 + vib), noise = nz * 0.18f * bp;
        float t = rdl(x.bore, 4095, x.bw, x.D); const float pole = 0.7f - 0.1f * 22050.f / sr; x.lp = -(1 - pole) * t + pole * x.lp; t = x.lp;
        const float dc = t - x.dcx + 0.995f * x.dcy; x.dcx = t; x.dcy = dc; t = dc;
        x.jet[x.jw & 2047] = sane(bp + noise - 0.5f * t); x.jw++;
        float pd = rdl(x.jet, 2047, x.jw, x.JD); pd = std::max(-1.f, std::min(1.f, pd * (pd * pd - 1))) + 0.5f * t;
        x.bore[x.bw & 4095] = sane(pd); x.bw++; y = t * 0.3f * 0.45f;
      } else {   // the reed: how far it opens follows the pressure across it
        const float bp = (0.56f + blow * 0.32f) * x.ramp * (1 + vib + nz * 0.2f), st = iStiff >= 0 ? e[iStiff] : 0.5f;
        const float last = rdl(x.bore, 4095, x.bw, x.D); const float f1 = 0.5f * (last + x.z1); x.z1 = last;
        float pd = -0.95f * f1 - bp; const float reed = std::max(-1.f, std::min(1.f, (0.8f - 0.2f * st) + (-0.2f - 0.3f * st) * pd));
        x.bore[x.bw & 4095] = sane(bp + pd * reed); x.bw++; y = last * 0.28f; }
      l = r = y * 0.25f; return; }
    const float n = rng.bi() * (1 + e[FLUTTER] * 0.5f * std::sin(TAU * x.fph));
    float y = x.b1.run(n) * x.g1; if (x.two) y += x.b2.run(n) * x.g2;
    y += std::sin(TAU * x.sph) * e[TONE] * 0.12f;
    l = r = y;
  }
};
static Reg rBreath(new ModuleDef{"breath", "BREATH", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  [] { auto p = vp({PITCH(), pct("noise", "NOISE", 0.6f), only(fmt(prExp("q", "FOCUS", 5, 200, 40), [](float v) { return std::to_string((int)std::lround(v)); }), "v:0"), only(pct("tone", "SINE", 0.3f), "v:0"), only(OP("hm", "OVERTONE", {"OFF", "2ND", "3RD"}, 1), "v:0"), pct("flutter", "FLUTTER", 0.2f)},
     envP(0.25f, 0.6f, 0.7f, 1.2f, 1.8f), 0.4f);
   p.push_back(noMod(OP("type", "TYPE", {"AIR", "FLUTE", "REED"}, 0))); p.push_back(only(pct("blow", "BLOW", 0.6f), "v:1,2")); p.push_back(only(pct("jet", "JET", 0.5f), "v:1")); p.push_back(only(pct("stiff", "STIFF", 0.5f), "v:2")); return p; }(), 0, 68, false, [] { return new Breath(); }});

// ---------------- ADDITIVE: a stack of sine partials; the upper ones fade sooner (TILT DEC) ----------------
struct AddV : VoiceBase { static constexpr int P = 16; float c[P], s[P], cw[P], sw[P], a[P], lv[P], kd[P], gl[P], gr[P], mul[P]; int n = 0, cnt = 0; float t = 0; };
struct Additive : Poly<AddV> {
  enum { PITCHK, PARTS, BRIGHT, EVEN, STRETCH, DECAY, SHIMMER };
  Additive() { gainScale = 0.22f; selfPan = true; }
  void start(AddV& x) override {
    const int parts = (int)e[PARTS]; float amps[AddV::P], tot = 0;
    for (int k = 1; k <= parts; k++) { const float a = std::pow((float)k, -(0.6f + (1 - e[BRIGHT]) * 2)) * (k % 2 == 0 ? e[EVEN] : 1); amps[k - 1] = a; tot += a; }
    const float len = noteLen(x.env); x.n = 0; x.t = 0; x.cnt = 0;
    for (int k = 1; k <= parts; k++) {
      const float fr = x.f * k * (1 + e[STRETCH] * 0.015f * (k - 1)) * std::pow(2.f, rng.bi() * e[SHIMMER] * 12 / 1200.f); if (fr > 16000 || fr > sr * 0.45f) break;
      const int i = x.n++; const float w = TAU * fr / sr; x.mul[i] = fr / x.f; x.cw[i] = std::cos(w); x.sw[i] = std::sin(w); x.c[i] = 1; x.s[i] = 0;
      x.a[i] = amps[k - 1] / tot; x.lv[i] = 1; const float dk = std::max(0.05f, len / (1 + e[DECAY] * (k - 1) * 1.5f)); x.kd[i] = std::exp(-3.f / (dk * sr));
      panGains(rng.bi() * e[iWidth] * std::min(1.f, k / 4.f), x.gl[i], x.gr[i]);
    }
  }
  void voice(AddV& x, float& l, float& r) override {
    x.t += 1.f / sr; const float atk = std::min(1.f, x.t / 0.005f); float L = 0, R = 0;
    if (x.glLen > 0 && (x.cnt & 15) == 0) for (int i = 0; i < x.n; i++) { const float w = TAU * std::min(sr * 0.45f, x.mul[i] * x.f) / sr; x.cw[i] = std::cos(w); x.sw[i] = std::sin(w); }   // GLIDE: the partials follow
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
     envP(0.01f, 0.5f, 0.7f, 1.5f, 2), 0.4f), 0, 68, false, [] { return new Additive(); }});

// ---------------- WAVEFOLD: a sine pushed into a folding curve; FOLD DEC lets it relax back towards a sine ----------------
// TYPE: CLASSIC is the folder it always was (a little grit up high is part of it). Heavy: CLEAN folds at four times the rate,
// works each step out from the curve's integral (so the sharp corners don't fold back as false tones), and filters on the way
// back down: high notes and deep folds stay clean.
struct FoldV : VoiceBase { float ph = 0, t = 0, drive = 0.12f, pk = 0.5f; Biquad lp, hp; float prev = 0, xo = 0, h[64] = {}; int hw = 0; };
struct Fold : Poly<FoldV> {
  enum { PITCHK, FOLD, FDEC, FSUS, SYM };
  float kd = 0.01f, fir[64]; int iType = -1, ty = 0;
  Fold() { gainScale = 0.13f;   // the filter that brings CLEAN back down: a windowed sinc, cut a little below the top of the hearing range
    float sum = 0; for (int k = 0; k < 64; k++) { const float n = k - 31.5f, x = 2 * 0.105f * n; const float sn = std::fabs(x) < 1e-6f ? 1 : std::sin(PI * x) / (PI * x);
      const float w = 0.42f - 0.5f * std::cos(TAU * k / 63) + 0.08f * std::cos(2 * TAU * k / 63); fir[k] = sn * w; sum += fir[k]; } for (float& c : fir) c /= sum; }
  void prepare() override { Poly<FoldV>::prepare(); iType = pi("type"); }
  void voicePrep() override { kd = 1 - std::exp(-3.f / (std::max(0.01f, e[FDEC]) * sr)); ty = iType >= 0 ? std::max(0, std::min(1, (int)std::lround(e[iType]))) : 0; }
  void start(FoldV& x) override { voicePrep(); x.ph = 0; x.t = 0; x.drive = 0.12f; x.pk = 0.12f + e[FOLD] * 0.88f * (0.5f + 0.5f * x.vel); x.hp.reset(); x.hp.set(Biquad::HP, 25, 0.707f, sr); x.prev = 0; x.xo = e[SYM] * 0.12f; for (float& v : x.h) v = 0; x.hw = 0; }
  static inline float shape(float v) { v = clampf(v, -1, 1); return std::sin(v * 4 * PI); }
  static inline float shapeInt(float v) { v = clampf(v, -1, 1); return (1 - std::cos(v * 4 * PI)) / (4 * PI); }   // its integral (flat beyond the ends, where the curve is 0)
  void voice(FoldV& x, float& l, float& r) override {
    const float dt = 1.f / sr; x.t += dt;
    if (x.t < 0.004f) x.drive = 0.12f + (x.pk - 0.12f) * x.t / 0.004f; else x.drive += (0.12f + (x.pk - 0.12f) * e[FSUS] - x.drive) * kd;
    const float ph0 = x.ph; x.ph += x.f * dt; if (x.ph >= 1) x.ph -= 1;
    if (ty == 1) { float acc = 0;
      for (int k = 1; k <= 4; k++) { const float in = std::sin(TAU * (ph0 + x.f * dt * k * 0.25f)) * x.drive + e[SYM] * 0.12f, dx = in - x.xo;
        const float y = std::fabs(dx) > 1e-5f ? (shapeInt(in) - shapeInt(x.xo)) / dx : shape(0.5f * (in + x.xo)); x.xo = in; x.h[x.hw & 63] = y; x.hw++; }
      for (int k = 0; k < 64; k++) acc += fir[k] * x.h[(x.hw - 1 - k) & 63];
      l = r = x.hp.run(acc); return; }
    const float in = std::sin(TAU * x.ph) * x.drive + e[SYM] * 0.12f;
    const float y = 0.5f * (shape(0.5f * (in + x.prev)) + shape(in)); x.prev = in;   // two points per sample: tames the folding's harshest edge
    l = r = x.hp.run(y);   // CUTOFF and RES (the engine's, after) soften it
  }
};
static Reg rFold(new ModuleDef{"fold", "WAVEFOLD", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  [] { auto p = vp({PITCH(), pct("fold", "FOLD", 0.5f), T("fdec", "FOLD DEC", 0.01f, 6, 0.5f), pct("fsus", "FOLD SUS", 0.2f), pct("sym", "SYMMETRY", 0)},
     envP(0.003f, 0.4f, 0.6f, 0.8f, 1.2f), 0.3f); p.push_back(noMod(OP("type", "TYPE", {"CLASSIC", "CLEAN"}, 0))); return p; }(), 0, 68, false, [] { return new Fold(); }});

}  // namespace tb
