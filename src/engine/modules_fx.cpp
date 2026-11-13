// Timbient native: effects. Each one passes the incoming sound through untouched on its dry side.
#include "params.h"

namespace tb {

// ---------------- FILTER ----------------
struct Filter : Module {
  Biquad f; int cnt = 0; Smooth dry, wet;
  enum { TYPE, CUT, RES, MIX };
  void prepare() override { dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX]); }
  void render(int a, int b) override {
    const int ty = (int)e[TYPE]; const Biquad::Type T2 = ty == 0 ? Biquad::LP : ty == 1 ? Biquad::HP : Biquad::BP;
    f.set(T2, e[CUT], ty == 2 ? 0.6f + e[RES] * 14 : std::max(0.5f, e[RES] * 16), sr);
    dry.t = 1 - e[MIX]; wet.t = e[MIX];
    for (int i = a; i < b; i++) { const float d = dry.tick(), w = wet.tick();
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i]; out[0][ch][i] = x * d + f.run(x, ch) * w; } }
  }
};
static Reg rFilter(new ModuleDef{"filter", "FILTER", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {OP("type", "TYPE", {"LOWPASS", "HIGHPASS", "BANDPASS"}, 0), prHz("cut", "CUTOFF", 40, 20000, 1800), pct("res", "RES", 0.2f), pct("mix", "MIX", 1)}, 0, 68, false, [] { return new Filter(); }});

// ---------------- ECHO: a ping-pong pair with a darkening feedback loop ----------------
struct Echo : Module {
  DelayLine dl, dr; Biquad lp; Smooth tl, trr, fb, dry, wet; float lastR = 0;
  double last = -1; float cp = 0, ppb = 1;   // the clock patched into CLOCK: how far apart its pulses are
  enum { TIME, DIV, SPREAD, FB, TONE, MIX };
  // free: TIME. With a clock patched in: a note length of that clock's beat
  float secs() const { static const float B[8] = {0.25f, 1 / 3.f, 0.5f, 0.75f, 1, 1.5f, 2, 4};
    return clockedIn.load(std::memory_order_relaxed) && cp > 0 ? std::min(7.5f, B[(int)e[DIV]] * cp * ppb) : e[TIME]; }
  void blockStart(int) override { clockedIn.store(patched(1)); }
  void trig(int port, const Event& ev) override { if (port != 1) return; const double now = t + eng->pos / sr;
    if (ev.period > 0) cp = ev.period; else if (last >= 0) { const float p = (float)(now - last); if (p > 0.01f && p < 8) cp = cp > 0 ? cp * 0.6f + p * 0.4f : p; }
    last = now; ppb = ev.ppb > 0 ? ev.ppb : (ppb > 0 ? ppb : 1); }
  void prepare() override { dl.init(8, sr); dr.init(8, sr); for (Smooth* s : {&tl, &trr}) s->setTime(0.3f, sr); for (Smooth* s : {&fb, &dry, &wet}) s->setTime(0.012f, sr);
    tl.snap(secs() * sr); trr.snap(std::min(7.9f, secs() * (1 + e[SPREAD] * 0.6f)) * sr); fb.snap(e[FB]); dry.snap(1 - e[MIX]); wet.snap(e[MIX]); }
  void render(int a, int b) override {
    const bool frz = eng->freeze.load(std::memory_order_relaxed);
    const float tm = secs(); tl.t = tm * sr; trr.t = std::min(7.9f, tm * (1 + e[SPREAD] * 0.6f)) * sr; fb.t = frz ? 0.995f : e[FB]; dry.t = 1 - e[MIX]; wet.t = e[MIX];
    lp.set(Biquad::LP, e[TONE], 0.707f, sr);
    for (int i = a; i < b; i++) {
      const float x = 0.5f * (in[0][0][i] + in[0][1][i]), f = fb.tick();
      const float yl = dl.read(tl.tick()), yr = dr.read(trr.tick());
      dl.write(sane((frz ? 0.f : x) + yr * f));          // left feeds right through the tone filter; right comes back into left
      dr.write(sane(lp.run(yl)));
      const float d = dry.tick(), w = wet.tick();
      out[0][0][i] = in[0][0][i] * d + yl * w; out[0][1][i] = in[0][1][i] * d + yr * w;
    }
  }
};
static Reg rEcho(new ModuleDef{"echo", "ECHO", Cat::Proc, {{"IN", PType::Audio}, {"CLOCK", PType::Trig}}, {{"OUT", PType::Audio}},
  {only(T("time", "TIME", 0.05f, 4, 0.71f), "free"), only(OP("div", "NOTE", {"1/16", "1/8T", "1/8", "1/8 DOT", "1/4", "1/4 DOT", "1/2", "1 BAR"}, 4), "clock"),
   pct("spread", "WIDTH", 0.6f), pr("fb", "FEEDBACK", 0, 0.95f, 0.5f), prHz("tone", "TONE", 300, 8000, 1600), pct("mix", "MIX", 0.4f)}, 0, 68, false, [] { return new Echo(); }});

// ---------------- REVERB: eight damped feedback combs per side and four diffusers, any length for the same cost ----------------
struct Reverb : Module {
  static constexpr int NC = 8, NA = 4;
  struct Comb { DelayLine d; float len = 1000, lp = 0, g = 0.8f; };
  struct AP { DelayLine d; float len = 300; };
  Comb c[2][NC]; AP ap[2][NA]; Biquad pre[2]; Smooth dry, wet, pd; DelayLine pdl[2]; float damp = 0.4f, size = 5;
  enum { SIZE, PRE, DAMP, MIX };
  void prepare() override {
    const int cl[NC] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116}, al[NA] = {225, 556, 441, 341}; const float sc = sr / 44100.f;
    for (int s = 0; s < 2; s++) { for (int k = 0; k < NC; k++) { c[s][k].len = (cl[k] + s * 23) * sc * 1.6f; c[s][k].d.init(c[s][k].len / sr + 0.01f, sr); }
      for (int k = 0; k < NA; k++) { ap[s][k].len = (al[k] + s * 23) * sc; ap[s][k].d.init(ap[s][k].len / sr + 0.01f, sr); } pre[s].set(Biquad::HP, 120, 0.7f, sr); }
    dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX] * 0.6f); wet.snap(e[MIX] * 1.2f);
    for (auto& d : pdl) d.init(0.26f, sr); pd.setTime(0.2f, sr); pd.snap(e[PRE] * sr);
  }
  void render(int a, int b) override {
    size = e[SIZE]; damp = e[DAMP];
    const float dk = 0.05f + damp * 0.6f;   // how much each pass darkens
    for (int s = 0; s < 2; s++) for (int k = 0; k < NC; k++) c[s][k].g = std::min(0.985f, std::pow(10.f, -3.f * (c[s][k].len / sr) / std::max(0.2f, size)));
    dry.t = 1 - e[MIX] * 0.6f; wet.t = e[MIX] * 1.2f; pd.t = e[PRE] * sr;
    for (int i = a; i < b; i++) {
      const float d = dry.tick(), w = wet.tick(), pdt = pd.tick();
      for (int s = 0; s < 2; s++) {
        pdl[s].write(in[0][s][i]); const float x = pre[s].run(pdt >= 1 ? pdl[s].read(pdt) : in[0][s][i], 0) * 0.15f; float acc = 0;   // PRE-DELAY: a gap before the room answers
        for (int k = 0; k < NC; k++) { Comb& cb = c[s][k]; const float y = cb.d.read(cb.len); cb.lp = y + (cb.lp - y) * dk; cb.d.write(sane(x + cb.lp * cb.g)); acc += y; }
        for (int k = 0; k < NA; k++) { AP& q = ap[s][k]; const float bd = q.d.read(q.len), v = acc + bd * 0.5f; q.d.write(sane(v)); acc = bd - v * 0.5f; }
        out[0][s][i] = in[0][s][i] * d + acc * w;
      }
    }
  }
};
static Reg rReverb(new ModuleDef{"reverb", "REVERB", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(prExp("size", "SIZE", 1, 30, 5), [](float v) { char b[16]; std::snprintf(b, 16, "%.1fS", v); return std::string(b); }),
   fmt(pr("pre", "PRE-DELAY", 0, 0.25f, 0), [](float v) { return std::to_string((int)std::lround(v * 1000)) + "MS"; }), pct("damp", "DAMP", 0.4f), pct("mix", "MIX", 0.5f)}, 0, 68, false, [] { return new Reverb(); }});

// ---------------- INFINITE HALL: a figure-of-eight reverb tank (after Jon Dattorro's 1997 plate) whose delays
// wander slowly (MOTION), so the tail drifts and detunes as it hangs. DECAY runs all the way to INFINITE; FREEZE holds it.
struct Hall : Module {
  enum { SIZE, DECAY, MOTION, DAMP, PRE, FREEZE, MIX };
  struct AP { DelayLine d; float n = 1, g = 0.5f; float run(float x, float extra = 0) { const float z = d.read(std::max(1.f, n + extra) - 1); const float w = x + g * z; d.write(w); return z - g * w; } };
  DelayLine pre; AP inAp[4]; AP mAp[2], ap2[2]; DelayLine dl1[2], dl2[2]; float n1[2] = {}, n2[2] = {}, lp[2] = {}, bw = 0, fbk[2] = {};
  double ph[4] = {0, 0.3, 0.55, 0.8}; Smooth size, dry, wet; float drift[4] = {}, driftT[4] = {};
  float sc() const { return (float)sr / 29761.f; }
  void prepare() override {
    pre.init(0.6f, sr); const float k = sc() * 2.2f;
    const float inN[4] = {142, 107, 379, 277}, inG[4] = {0.75f, 0.75f, 0.625f, 0.625f};
    for (int i = 0; i < 4; i++) { inAp[i].d.init(inN[i] * sc() / sr + 0.01f, sr); inAp[i].n = inN[i] * sc(); inAp[i].g = inG[i]; }
    for (int h = 0; h < 2; h++) { mAp[h].d.init((h ? 908 : 672) * k / sr + 0.01f, sr); mAp[h].g = -0.7f; ap2[h].d.init((h ? 2656 : 1800) * k / sr + 0.01f, sr); ap2[h].g = 0.5f;
      dl1[h].init((h ? 4217 : 4453) * k / sr + 0.01f, sr); dl2[h].init((h ? 3163 : 3720) * k / sr + 0.01f, sr); }
    size.setTime(1.5f, sr); size.snap(e[SIZE]); dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX]);
  }
  bool frozen() const { return e[FREEZE] > 0.5f || eng->freeze.load(std::memory_order_relaxed); }
  void trig(int port, const Event&) override { if (port == 1) p[FREEZE].store(p[FREEZE].load() > 0.5f ? 0.f : 1.f); }   // FREEZE in: each trigger flips it
  void render(int a, int b) override {
    const bool fz = frozen(); size.t = e[SIZE]; dry.t = 1 - e[MIX]; wet.t = e[MIX];
    const float s0 = sc();
    // how much each trip round the tank keeps, from DECAY (the time to fall by 60 dB); the top of the range never falls
    const float loopT = (4453 + 3720) * s0 * e[SIZE] / sr; const bool inf = e[DECAY] >= 99.f || fz;
    const float g = inf ? 1.f : std::pow(10.f, -3.f * loopT / e[DECAY]);
    const float damp = fz ? 0.f : e[DAMP] * 0.85f, mot = e[MOTION];
    for (int k = 0; k < 4; k++) { driftT[k] -= (float)(b - a) / sr; if (driftT[k] <= 0) { driftT[k] = 2 + rng.uni() * 5; } drift[k] += ((rng.uni() * 2 - 1) - drift[k]) * 0.0004f * (b - a); }
    for (int i = a; i < b; i++) {
      const float S = size.tick(), k2 = s0 * S;
      const float x = fz ? 0.f : 0.5f * (in[0][0][i] + in[0][1][i]);
      pre.write(x); float v = pre.read(std::max(1.f, e[PRE] * sr));
      bw += 0.7f * (v - bw); v = bw;
      for (auto& ap : inAp) v = ap.run(v);
      for (int k = 0; k < 4; k++) { ph[k] += (0.11 + k * 0.067) / sr; if (ph[k] >= 1) ph[k] -= 1; }
      float outL = 0, outR = 0;
      for (int h = 0; h < 2; h++) {
        const float wob = mot * (std::sin((float)(ph[h] * TAU)) * 14.f + drift[h] * 10.f) * s0;   // the slow wander: allpasses and delays drift by a few ms
        float t = v + fbk[1 - h];
        mAp[h].n = (h ? 908 : 672) * k2; t = mAp[h].run(t, wob);
        dl1[h].write(t); n1[h] = (h ? 4217 : 4453) * k2; t = dl1[h].read(std::max(1.f, n1[h] + mot * std::sin((float)(ph[h + 2] * TAU)) * 6 * s0) - 1);
        lp[h] += (1 - damp) * (t - lp[h]); t = lp[h] * g;
        ap2[h].n = (h ? 2656 : 1800) * k2; t = ap2[h].run(t);
        dl2[h].write(t); n2[h] = (h ? 3163 : 3720) * k2; t = dl2[h].read(n2[h] - 1) * g;
        fbk[h] = inf ? std::tanh(t * 0.9f) / 0.9f : t;   // forever: a soft ceiling stops it growing
        // listen at a few places in the tank: one side from each half, crossed, for a wide sound
        const float tap = dl1[h].read(n1[h] * 0.37f) + dl1[h].read(n1[h] * 0.81f) - dl2[h].read(n2[h] * 0.55f);
        (h ? outL : outR) += tap; (h ? outR : outL) -= 0.4f * dl2[h].read(n2[h] * 0.21f);
      }
      const float d = dry.tick(), w = wet.tick() * 0.5f;
      out[0][0][i] = in[0][0][i] * d + sane(outL) * w; out[0][1][i] = in[0][1][i] * d + sane(outR) * w;
    }
  }
};
static Reg rHall(new ModuleDef{"hall", "INFINITE HALL", Cat::Proc, {{"IN", PType::Audio}, {"FREEZE", PType::Trig}}, {{"OUT", PType::Audio}},
  {fmt(pr("size", "SIZE", 0.4f, 2.2f, 1.2f), [](float v) { return std::to_string((int)std::lround(v * 100)) + "%"; }),
   fmt(prExp("decay", "DECAY", 1, 100, 12), [](float v) { if (v >= 99.f) return std::string("INFINITE"); char b[16]; std::snprintf(b, 16, v < 10 ? "%.1fS" : "%.0fS", v); return std::string(b); }),
   pct("motion", "MOTION", 0.5f), pct("damp", "DAMP", 0.35f), fmt(pr("pre", "PRE-DELAY", 0, 0.5f, 0.02f), [](float v) { return std::to_string((int)std::lround(v * 1000)) + "MS"; }),
   OP("freeze", "FREEZE", {"OFF", "ON"}, 0), pct("mix", "MIX", 0.5f)}, 0, 68, false, [] { return new Hall(); }});

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
struct Amp : Module {
  Smooth g; float peak[2] = {0, 0};
  void prepare() override { g.setTime(0.012f, sr); g.snap(dbToGain(e[0])); }
  void render(int a, int b) override {
    g.t = dbToGain(e[0]); const bool lim = e[1] > 0.5f;
    for (int i = a; i < b; i++) { const float k = g.tick();
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i] * k; const float ax = std::fabs(x); out[0][ch][i] = lim && ax > 0.8f ? std::copysign(0.8f + 0.2f * std::tanh((ax - 0.8f) / 0.2f), x) : x; /* LIMIT: untouched below 0.8, rounded off above (so at 0 dB it changes nothing) */ peak[ch] = std::max(peak[ch] * 0.9999f, std::fabs(out[0][ch][i])); } }
    disp[0].store(peak[0]); disp[1].store(peak[1]);   // L, R
  }
};
static Reg rAmp(new ModuleDef{"amp", "AMP", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(pr("gain", "GAIN", -12, 36, 0), [](float v) { return (v > 0 ? "+" : "") + std::to_string((int)std::round(v)) + "DB"; }), OP("limit", "LIMIT", {"OFF", "ON"}, 1)}, 13, 68, false, [] { return new Amp(); }});

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
