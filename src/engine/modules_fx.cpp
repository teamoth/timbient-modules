// Timbient native: effects. Each one passes the incoming sound through untouched on its dry side.
#include "params.h"
#include "dsp2.h"

namespace tb {

// ---------------- FILTER ----------------
// FILTER: TYPE picks the kind. Light: LOWPASS, HIGHPASS, BANDPASS (clean, as it always was), COMB (a pitched, metallic
// ringing at CUTOFF, RES its feedback), VOWEL (three resonances shaped like a voice's "ah", "eh", "ee", "oh", "oo": VOWEL moves
// through them, CUTOFF makes the voice bigger or smaller). Heavy (run at twice the rate inside, so pushing them stays clean):
// LADDER (warm and creamy, four stages, it can sing on its own with RES high), DIODE (squelchier), MORPH (one SHAPE knob from
// lowpass through bandpass to highpass). DRIVE pushes the heavy ones into their own saturation.
static inline float ftanh(float x) { x = std::max(-3.f, std::min(3.f, x)); const float x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }   // close to tanh, much cheaper
struct Filter : Module {
  Biquad f; int cnt = 0; Smooth dry, wet, cutS, resS, drS, shS, voS;   // CUTOFF and RES glide, the filter re-tuned every 4 samples (once a block before: a zipper when swept)
  enum { TYPE, CUT, RES, MIX, DRIVE, SHAPE, VOWEL };
  enum { LPF, HPF, BPF, COMB, VOWELT, LADDER, DIODE, MORPH };
  DelayLine comb[2]; float combLp[2] = {}; Biquad fm[3]; float st[2][4] = {}, sv[2][2] = {}, xPrev[2] = {}, F[3] = {800, 1150, 2900}, G[3] = {1, 0.5f, 0.25f}; int cur = -1;
  void prepare() override { dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX]); cutS.setTime(0.006f, sr); cutS.snap(std::log(e[CUT])); resS.setTime(0.006f, sr); resS.snap(e[RES]);
    for (Smooth* q : {&drS, &shS, &voS}) q->setTime(0.01f, sr); drS.snap(e[DRIVE]); shS.snap(e[SHAPE]); voS.snap(e[VOWEL]); for (auto& c : comb) c.init(0.06f, sr); cur = -1; }
  void clearAll() { for (auto& c : comb) c.clear(); combLp[0] = combLp[1] = 0; for (auto& a : st) for (auto& v : a) v = 0; for (auto& a : sv) for (auto& v : a) v = 0; xPrev[0] = xPrev[1] = 0; }
  // the vowels: three resonances each (Hz) and how loud each is
  static void vowelAt(float v, float* F, float* G) {
    static const float T[5][6] = {{800, 1150, 2900, 1, 0.5f, 0.25f}, {400, 1600, 2700, 1, 0.35f, 0.25f}, {350, 2300, 3000, 1, 0.2f, 0.2f}, {450, 800, 2830, 1, 0.45f, 0.15f}, {325, 700, 2530, 1, 0.3f, 0.1f}};   // AH EH EE OH OO
    const float p = std::max(0.f, std::min(3.999f, v * 4)); const int i = (int)p; const float fr = p - i;
    for (int k = 0; k < 3; k++) { F[k] = T[i][k] + (T[i + 1][k] - T[i][k]) * fr; G[k] = T[i][3 + k] + (T[i + 1][3 + k] - T[i][3 + k]) * fr; }
  }
  void render(int a, int b) override {
    const int ty = std::max(0, std::min(7, (int)std::lround(e[TYPE]))); if (ty != cur) { cur = ty; clearAll(); }
    const Biquad::Type T2 = ty == 0 ? Biquad::LP : ty == 1 ? Biquad::HP : Biquad::BP;
    cutS.t = std::log(std::max(10.f, e[CUT])); resS.t = e[RES]; drS.t = e[DRIVE]; shS.t = e[SHAPE]; voS.t = e[VOWEL];
    dry.t = 1 - e[MIX]; wet.t = e[MIX];
    for (int i = a; i < b; i++) { const float d = dry.tick(), w = wet.tick(), lc = cutS.tick(), rs = resS.tick(), dv = drS.tick(), sh = shS.tick(), vo = voS.tick(); const float fc = std::exp(lc);
      if (ty <= BPF) { if ((cnt++ & 3) == 0) f.set(T2, fc, ty == 2 ? 0.6f + rs * 14 : std::max(0.5f, rs * 16), sr);
        for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i]; out[0][ch][i] = x * d + f.run(x, ch) * w; } continue; }
      if (ty == VOWELT && (cnt++ & 7) == 0) { vowelAt(vo, F, G); const float scale = std::pow(fc / 1800.f, 0.35f); for (int k = 0; k < 3; k++) fm[k].set(Biquad::BP, std::min(0.45f * sr, F[k] * scale), 6 + rs * 14, sr); }
      for (int ch = 0; ch < 2; ch++) {
        const float x = in[0][ch][i]; float y = 0;
        switch (ty) {
          case COMB: {   // a short delay tuned to CUTOFF, fed back: it rings at that pitch
            const float L = std::max(2.f, sr / std::max(20.f, fc)); const float z = readCubic(comb[ch], L - 1); combLp[ch] += 0.6f * (z - combLp[ch]);
            comb[ch].write(sane(x + combLp[ch] * (0.5f + rs * 0.49f))); y = (x + z) * 0.6f; break; }
          case VOWELT: { y = 0; for (int k = 0; k < 3; k++) y += (float)fm[k].run(x, ch) * G[k] * 6.f; break; }
          default: {   // LADDER, DIODE and MORPH: twice the rate inside (the input in between two samples, then two steps)
            const float fcc = std::min(fc, 0.45f * sr), drive = 1 + dv * 6;
            const float g = std::tan(PI * fcc / (2 * sr)), a1 = 1 - std::exp(-TAU * fcc / (2 * sr));
            for (int os = 0; os < 2; os++) {
              const float xi = os == 0 ? 0.5f * (xPrev[ch] + x) : x;
              if (ty == MORPH) {   // a state-variable filter (lowpass, bandpass and highpass at once), SHAPE crossfades through them
                const float k = 2 - 1.9f * rs, xin = std::tanh(xi * drive * 0.7f) / 0.7f;
                const float hp = (xin - (k + g) * sv[ch][0] - sv[ch][1]) / (1 + k * g + g * g), bp = g * hp + sv[ch][0], lp = g * bp + sv[ch][1];
                sv[ch][0] = sane(g * hp + bp); sv[ch][1] = sane(g * bp + lp);
                const float t3 = sh * 2; y = t3 < 1 ? lp * (1 - t3) + bp * t3 * k : bp * k * (2 - t3) + hp * (t3 - 1); y /= std::sqrt(drive);
              } else {   // four one-pole stages with the last fed back round (RES); DIODE: each stage also leans on the next
                const float kq = rs * (ty == LADDER ? 4.3f : 4.6f);
                float u = ftanh(xi * drive - kq * st[ch][3]);
                for (int s2 = 0; s2 < 4; s2++) { const float tgt = ty == DIODE && s2 < 3 ? 0.6f * u + 0.4f * st[ch][s2 + 1] : u; st[ch][s2] += a1 * (ftanh(tgt) - ftanh(st[ch][s2])); u = st[ch][s2]; }
                y = st[ch][3] * (1 + rs * (ty == LADDER ? 1.6f : 1.3f)) / std::sqrt(drive);
              } }
            xPrev[ch] = x; break; }
        }
        out[0][ch][i] = x * d + sane(y) * w; }
    }
  }
};
static Reg rFilter(new ModuleDef{"filter", "FILTER", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {noMod(OP("type", "TYPE", {"LOWPASS", "HIGHPASS", "BANDPASS", "COMB", "VOWEL", "LADDER", "DIODE", "MORPH"}, 0)), prHz("cut", "CUTOFF", 40, 20000, 1800), pct("res", "RES", 0.2f), pct("mix", "MIX", 1),
   only(pct("drive", "DRIVE", 0.2f), "v:5,6,7"), only(pct("shape", "SHAPE", 0.f), "v:7"), only(pct("vowel", "VOWEL", 0.f), "v:4")}, 0, 68, false, [] { return new Filter(); }});


// ---------------- DUCKER: dips on each KEY trigger, then breathes back (CURVE), optionally timed in beats ----------------
struct Ducker : Module {
  float g = 1, low = 1, relT = 1, tSince = 1e9f, atkT = 0.01f, sEnv = 0, gs = 1; bool rising = false, side = false; Smooth dry, wet;
  enum { DEPTH, ATK, REL, RBEATS, CURVE, MIX, THRESH };
  void blockStart(int) override { side = patched(2); }
  void prepare() override { dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX]); }
  void trig(int port, const Event& ev) override {
    if (port != 1) return;
    low = 1 - e[DEPTH] * ev.vel; atkT = e[ATK];
    const float beat = ev.period > 0 && ev.ppb > 0 ? ev.period * ev.ppb : 60.f / eng->tempo.bpm.load();
    static const float beats[7] = {0, 0.5f, 1, 2, 3, 4, 8};
    relT = e[RBEATS] > 0.5f ? beats[(int)e[RBEATS]] * beat : e[REL]; tSince = 0; rising = false;
  }
  void render(int a, int b) override {
    dry.t = 1 - e[MIX]; wet.t = e[MIX]; const float dt = 1.f / sr, k = e[CURVE];
    for (int i = a; i < b; i++) {
      tSince += dt;
      if (tSince < atkT) g += (low - g) * (1 - std::exp(-dt * 3 / std::max(0.002f, atkT)));
      else if (tSince < atkT + relT) { const float u = (tSince - atkT) / std::max(0.02f, relT);
        const float f = (1 - k) * (1 - std::exp(-5 * u)) / (1 - std::exp(-5.f)) + k * (1 - std::cos(PI * u)) / 2; g = low + (1 - low) * f; }
      else g += (1 - g) * 0.001f;
      float gg = g;
      if (side) {   // SIDE: the louder that sound is past THRESHOLD (up to 12 dB past), the further IN goes down, up to DEPTH; ATTACK and RELEASE shape it
        const float lv = std::max(std::fabs(in[2][0][i]), std::fabs(in[2][1][i])); sEnv = lv > sEnv ? lv : sEnv * (1 - dt / 0.05f);   // a quick peak follower (catches short hits)
        const float over = clampf((20 * std::log10(std::max(1e-6f, sEnv)) - e[THRESH]) / 12, 0, 1), to = 1 - e[DEPTH] * over;
        gs += (to - gs) * (1 - std::exp(-dt * 3 / std::max(0.002f, to < gs ? e[ATK] : e[REL]))); gg = std::min(g, gs); }   // down at ATTACK, back up at RELEASE
      const float d = dry.tick(), w = wet.tick();
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i]; out[0][ch][i] = x * d + x * gg * w; }
    }
    disp[0].store(1 - (side ? std::min(g, gs) : g));
  }
};
static Reg rDucker(new ModuleDef{"ducker", "DUCKER", Cat::Proc, {{"IN", PType::Audio}, {"KEY", PType::Trig}, {"SIDE", PType::Audio}}, {{"OUT", PType::Audio}},
  {pct("depth", "DEPTH", 0.6f), T("atk", "ATTACK", 0.002f, 2, 0.01f), T("rel", "RELEASE", 0.05f, 12, 0.9f), OP("rbeats", "BEAT REL", {"OFF", "1/2", "1", "2", "3", "4", "8"}, 0),
   pct("curve", "CURVE", 0), pct("mix", "MIX", 1), only(fmt(pr("thresh", "THRESHOLD", -48, 0, -30), [](float v) { return std::to_string((int)std::lround(v)) + "DB"; }), "side")}, 10, 68, false, [] { return new Ducker(); }});

// ---------------- AMP: up to +36 dB, with a soft limiter ----------------
// DRIVE (it was AMP; its key is still "amp" so old sessions open as they were). TYPE: CLEAN is the old AMP, plain gain. Light:
// OVERDRIVE (warm, a little uneven, like a pedal), FUZZ (squared-off and buzzy), RECTIFY (folds the bottom half up: an octave up
// and a hollow edge), FOLD (the wave folds back on itself: brighter as DRIVE goes up), BITCRUSH (fewer bits, and RATE drops the
// sample rate). Heavy: TUBE (twice the rate inside, uneven, and it sags and blooms when hit hard). TONE darkens what comes out;
// the level is matched so turning DRIVE up changes the colour more than the loudness.
struct Amp : Module {
  enum { GAIN, LIMIT, TYPE, AMT, TONE, MIX, RATE };
  enum { CLEAN, OD, FUZZ, RECT, FOLDT, CRUSH, TUBE };
  Smooth g, amS, toS, mxS, rtS; float peak[2] = {0, 0}, uPrev[2] = {}, lp[2] = {}, dcX[2] = {}, dcY[2] = {}, hold[2] = {}, ph = 1, sag[2] = {}, xPrev[2] = {}, tlp[2] = {}; int cur = -1;
  void prepare() override { g.setTime(0.012f, sr); g.snap(dbToGain(e[GAIN])); for (Smooth* q : {&amS, &toS, &mxS, &rtS}) q->setTime(0.012f, sr); amS.snap(e[AMT]); toS.snap(e[TONE]); mxS.snap(e[MIX]); rtS.snap(e[RATE]); }
  static float shape(int ty, float x, float a, float& sg) {
    switch (ty) {
      case OD: { const float k = 1 + a * 24; return (std::tanh(k * x + 0.15f) - std::tanh(0.15f)) / std::tanh(k * 0.5f + 0.15f) * 0.5f; }
      case FUZZ: { const float k = 2 + a * 60, y = k * x; return (y / (std::fabs(y) + 0.15f)) * (0.35f + 0.1f * (1 - a)); }
      case RECT: { const float y = std::fabs(x) * a + x * (1 - a); return std::tanh(y * (1 + a * 3)) * 0.6f; }
      case FOLDT: { const float k = 1 + a * 7; return std::sin(x * k * 1.5707963f) * 0.5f; }
      case TUBE: { const float k = 1 + a * 14; sg += 0.0005f * (std::fabs(x * k) - sg); const float bias = 0.25f - 0.2f * std::min(1.f, sg);   // it sags: the bias falls as it's pushed
        const float y = k * x + bias; const float o = y > 0 ? std::tanh(y) : std::tanh(y * 0.6f) / 0.6f * 0.8f; return (o - std::tanh(bias)) / (1 + 0.6f * std::sqrt(k)) * 1.1f; }
      default: return x; } }
  void render(int a, int b) override {
    g.t = dbToGain(e[GAIN]); const bool lim = e[LIMIT] > 0.5f; const int ty = std::max(0, std::min(6, (int)std::lround(e[TYPE])));
    if (ty != cur) { cur = ty; for (int c = 0; c < 2; c++) { uPrev[c] = lp[c] = dcX[c] = dcY[c] = hold[c] = sag[c] = xPrev[c] = tlp[c] = 0; } }
    amS.t = e[AMT]; toS.t = e[TONE]; mxS.t = e[MIX]; rtS.t = e[RATE];
    const float dcR = 1 - TAU * 12 / sr;
    for (int i = a; i < b; i++) { const float k = g.tick(), am = amS.tick(), to = toS.tick(), mx = mxS.tick(), rt = rtS.tick();
      const float tc = 1 - std::exp(-TAU * (400 * std::pow(45.f, to)) / sr);   // TONE: 400 Hz to 18 kHz
      const float comp = ty == FUZZ ? 0.5f : ty == RECT ? 1.2f : ty == CRUSH ? 1.f : 1 / std::sqrt(1 + 4 * am);   // roughly the same loudness all the way up DRIVE
      bool step = true; if (ty == CRUSH) { ph += std::pow(2.f, -rt * 6); step = ph >= 1; if (step) ph -= std::floor(ph); }
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i] * k; float y = x;
        if (ty != CLEAN) {
          if (ty == CRUSH) { if (step) { const float lv = std::pow(2.f, 15 - am * 13); hold[ch] = std::round(std::max(-1.f, std::min(1.f, x)) * lv) / lv; } y = hold[ch]; }
          else if (ty == TUBE) { const float mid = shape(TUBE, 0.5f * (xPrev[ch] + x), am, sag[ch]); const float o2 = shape(TUBE, x, am, sag[ch]); tlp[ch] = 0.5f * (mid + o2); y = tlp[ch]; xPrev[ch] = x; }
          else if (ty == FUZZ) {   // the hardest clip: worked out from its integral between this sample and the last, so it doesn't alias
            auto F = [](float v) { const float a = std::fabs(v); return a - 0.15f * std::log1p(a / 0.15f); }; const float k2 = 2 + am * 50;
            auto ad = [&](float u) { const float du = u - uPrev[ch], m2 = 0.5f * (u + uPrev[ch]); const float r = std::fabs(du) > 1e-4f ? (F(u) - F(uPrev[ch])) / du : m2 / (std::fabs(m2) + 0.15f); uPrev[ch] = u; return r; };
            const float o1 = ad(k2 * 0.5f * (xPrev[ch] + x)), o2 = ad(k2 * x); xPrev[ch] = x; y = 0.5f * (o1 + o2) * (0.35f + 0.1f * (1 - am)); }   /* and at twice the rate */
          else y = shape(ty, x, am, sag[ch]);
          y *= comp; lp[ch] += tc * (y - lp[ch]); y = lp[ch];
          const float d = y - dcX[ch] + dcR * dcY[ch]; dcX[ch] = y; dcY[ch] = d; y = sane(d);   // the uneven kinds leave a slow offset: taken away
          y = x * (1 - mx) + y * mx; }
        const float ax = std::fabs(y); out[0][ch][i] = lim && ax > 0.8f ? std::copysign(0.8f + 0.2f * std::tanh((ax - 0.8f) / 0.2f), y) : y; /* LIMIT: untouched below 0.8, rounded off above (so at 0 dB it changes nothing) */ peak[ch] = std::max(peak[ch] * 0.9999f, std::fabs(out[0][ch][i])); }
    }
    disp[0].store(peak[0]); disp[1].store(peak[1]);   // L, R
  }
};
static Reg rAmp(new ModuleDef{"amp", "DRIVE", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(pr("gain", "GAIN", -12, 36, 0), [](float v) { return (v > 0 ? "+" : "") + std::to_string((int)std::round(v)) + "DB"; }), OP("limit", "LIMIT", {"OFF", "ON"}, 1),
   noMod(OP("type", "TYPE", {"CLEAN", "OVERDRIVE", "FUZZ", "RECTIFY", "FOLD", "BITCRUSH", "TUBE"}, 0)), only(pct("amt", "DRIVE", 0.4f), "v:1,2,3,4,5,6"), only(pct("tone", "TONE", 0.75f), "v:1,2,3,4,5,6"),
   only(pct("mix", "MIX", 1), "v:1,2,3,4,5,6"), only(pct("rate", "RATE", 0.f), "v:5")}, 13, 68, false, [] { return new Amp(); }});

// ---------------- MIXER: four channels, each with level, pan and a send ----------------
struct Mixer : Module {
  Smooth lv[4], pl[4], pr_[4], sd[4]; float pk[4] = {0, 0, 0, 0}, pkR[4] = {0, 0, 0, 0};
  void prepare() override { for (int k = 0; k < 4; k++) { for (Smooth* s : {&lv[k], &pl[k], &pr_[k], &sd[k]}) s->setTime(0.012f, sr); } }
  void render(int a, int b) override {
    for (int k = 0; k < 4; k++) { float l, r; panGains(e[k * 3 + 1], l, r); lv[k].t = e[k * 3]; pl[k].t = l * 1.414f; pr_[k].t = r * 1.414f; sd[k].t = e[k * 3 + 2]; }
    for (int i = a; i < b; i++) {
      float L = 0, R = 0, SL = 0, SR = 0;
      for (int k = 0; k < 4; k++) { const float g = lv[k].tick(), gl = pl[k].tick(), gr = pr_[k].tick(), s = sd[k].tick(), xl = in[k][0][i], xr = in[k][1][i];
        L += xl * g * gl; R += xr * g * gr; SL += xl * s; SR += xr * s; pk[k] = std::max(pk[k] * 0.9999f, std::fabs(xl * g * gl)); pkR[k] = std::max(pkR[k] * 0.9999f, std::fabs(xr * g * gr)); }   // after its pan
      out[0][0][i] = L; out[0][1][i] = R; out[1][0][i] = SL; out[1][1][i] = SR;
    }
    for (int k = 0; k < 4; k++) { disp[k].store(pk[k]); disp[4 + k].store(pkR[k]); }
  }
};
static std::vector<ParamDef> mixerParams() { std::vector<ParamDef> p; for (int k = 1; k <= 4; k++) { std::string n = std::to_string(k);
    ParamDef l = pct("", "", 0.8f); l.key = "l" + n; l.label = "LEVEL " + n; p.push_back(l);
    ParamDef q = pr("", "", -1, 1, 0); q.key = "p" + n; q.label = "PAN " + n; q.fmt = [](float v) { if (std::fabs(v) < 0.02f) return std::string("CENTRE"); return std::string(v < 0 ? "L " : "R ") + std::to_string((int)std::round(std::fabs(v) * 100)); };;   // as every PAN
    p.push_back(q);
    ParamDef s = pct("", "", 0); s.key = "s" + n; s.label = "SEND " + n; p.push_back(s); } return p; }
static Reg rMixer(new ModuleDef{"mixer", "MIXER", Cat::Proc, {{"1", PType::Audio}, {"2", PType::Audio}, {"3", PType::Audio}, {"4", PType::Audio}}, {{"OUT", PType::Audio}, {"SEND", PType::Audio}},
  mixerParams(), 8, 68, false, [] { return new Mixer(); }});

}  // namespace tb
