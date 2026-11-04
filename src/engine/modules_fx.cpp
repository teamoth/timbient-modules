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
  {OP("type", "TYPE", {"LOWPASS", "HIGHPASS", "BANDPASS"}, 0), prHz("cut", "CUTOFF", 40, 16000, 1800), pct("res", "RES", 0.2f), pct("mix", "MIX", 1)}, 0, 58, false, [] { return new Filter(); }});

// ---------------- ECHO: a ping-pong pair with a darkening feedback loop ----------------
struct Echo : Module {
  DelayLine dl, dr; Biquad lp; Smooth tl, trr, fb, dry, wet; float lastR = 0;
  enum { TIME, SPREAD, FB, TONE, MIX };
  void prepare() override { dl.init(8, sr); dr.init(8, sr); for (Smooth* s : {&tl, &trr}) s->setTime(0.3f, sr); for (Smooth* s : {&fb, &dry, &wet}) s->setTime(0.012f, sr);
    tl.snap(e[TIME] * sr); trr.snap(e[TIME] * (1 + e[SPREAD] * 0.6f) * sr); fb.snap(e[FB]); dry.snap(1 - e[MIX]); wet.snap(e[MIX]); }
  void render(int a, int b) override {
    const bool frz = eng->freeze.load(std::memory_order_relaxed);
    tl.t = e[TIME] * sr; trr.t = e[TIME] * (1 + e[SPREAD] * 0.6f) * sr; fb.t = frz ? 0.995f : e[FB]; dry.t = 1 - e[MIX]; wet.t = e[MIX];
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
static Reg rEcho(new ModuleDef{"echo", "ECHO", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {T("time", "TIME", 0.05f, 4, 0.71f), pct("spread", "SPREAD", 0.6f), pr("fb", "FEEDBACK", 0, 0.95f, 0.5f), prHz("tone", "TONE", 300, 8000, 1600), pct("mix", "MIX", 0.4f)}, 0, 58, false, [] { return new Echo(); }});

// ---------------- REVERB: eight damped feedback combs per side and four diffusers, any length for the same cost ----------------
struct Reverb : Module {
  static constexpr int NC = 8, NA = 4;
  struct Comb { DelayLine d; float len = 1000, lp = 0, g = 0.8f; };
  struct AP { DelayLine d; float len = 300; };
  Comb c[2][NC]; AP ap[2][NA]; Biquad pre[2]; Smooth dry, wet; float damp = 0.4f, size = 5;
  enum { SIZE, DAMP, MIX };
  void prepare() override {
    const int cl[NC] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116}, al[NA] = {225, 556, 441, 341}; const float sc = sr / 44100.f;
    for (int s = 0; s < 2; s++) { for (int k = 0; k < NC; k++) { c[s][k].len = (cl[k] + s * 23) * sc * 1.6f; c[s][k].d.init(c[s][k].len / sr + 0.01f, sr); }
      for (int k = 0; k < NA; k++) { ap[s][k].len = (al[k] + s * 23) * sc; ap[s][k].d.init(ap[s][k].len / sr + 0.01f, sr); } pre[s].set(Biquad::HP, 120, 0.7f, sr); }
    dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX] * 0.6f); wet.snap(e[MIX] * 1.2f);
  }
  void render(int a, int b) override {
    size = e[SIZE]; damp = e[DAMP];
    const float dk = 0.05f + damp * 0.6f;   // how much each pass darkens
    for (int s = 0; s < 2; s++) for (int k = 0; k < NC; k++) c[s][k].g = std::min(0.985f, std::pow(10.f, -3.f * (c[s][k].len / sr) / std::max(0.2f, size)));
    dry.t = 1 - e[MIX] * 0.6f; wet.t = e[MIX] * 1.2f;
    for (int i = a; i < b; i++) {
      const float d = dry.tick(), w = wet.tick();
      for (int s = 0; s < 2; s++) {
        const float x = pre[s].run(in[0][s][i], 0) * 0.15f; float acc = 0;
        for (int k = 0; k < NC; k++) { Comb& cb = c[s][k]; const float y = cb.d.read(cb.len); cb.lp = y + (cb.lp - y) * dk; cb.d.write(sane(x + cb.lp * cb.g)); acc += y; }
        for (int k = 0; k < NA; k++) { AP& q = ap[s][k]; const float bd = q.d.read(q.len), v = acc + bd * 0.5f; q.d.write(sane(v)); acc = bd - v * 0.5f; }
        out[0][s][i] = in[0][s][i] * d + acc * w;
      }
    }
  }
};
static Reg rReverb(new ModuleDef{"reverb", "REVERB", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(prExp("size", "SIZE", 1, 30, 5), [](float v) { char b[16]; std::snprintf(b, 16, "%.1fS", v); return std::string(b); }), pct("damp", "DAMP", 0.4f), pct("mix", "MIX", 0.5f)}, 0, 58, false, [] { return new Reverb(); }});

// ---------------- DUCKER: dips on each KEY trigger, then breathes back (CURVE), optionally timed in beats ----------------
struct Ducker : Module {
  float g = 1, low = 1, relT = 1, tSince = 1e9f, atkT = 0.01f; bool rising = false; Smooth dry, wet;
  enum { DEPTH, ATK, REL, RBEATS, CURVE, MIX };
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
      const float d = dry.tick(), w = wet.tick();
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i]; out[0][ch][i] = x * d + x * g * w; }
    }
    disp[0].store(1 - g);
  }
};
static Reg rDucker(new ModuleDef{"ducker", "DUCKER", Cat::Proc, {{"IN", PType::Audio}, {"KEY", PType::Trig}}, {{"OUT", PType::Audio}},
  {pct("depth", "DEPTH", 0.6f), T("atk", "ATTACK", 0.002f, 2, 0.01f), T("rel", "RELEASE", 0.05f, 12, 0.9f), OP("rbeats", "BEAT REL", {"OFF", "1/2", "1", "2", "3", "4", "8"}, 0),
   pct("curve", "CURVE", 0), pct("mix", "MIX", 1)}, 0, 58, false, [] { return new Ducker(); }});

// ---------------- AMP: up to +36 dB, with a soft limiter ----------------
struct Amp : Module {
  Smooth g; float peak = 0;
  void prepare() override { g.setTime(0.012f, sr); g.snap(dbToGain(e[0])); }
  void render(int a, int b) override {
    g.t = dbToGain(e[0]); const bool lim = e[1] > 0.5f;
    for (int i = a; i < b; i++) { const float k = g.tick();
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i] * k; peak = std::max(peak * 0.9999f, std::fabs(x)); out[0][ch][i] = lim ? std::tanh(x * 0.25f * 4) * 0.9f : x; } }
    disp[0].store(peak);
  }
};
static Reg rAmp(new ModuleDef{"amp", "AMP", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(pr("gain", "GAIN", -12, 36, 12), [](float v) { return (v > 0 ? "+" : "") + std::to_string((int)std::round(v)) + "DB"; }), OP("limit", "LIMIT", {"OFF", "ON"}, 1)}, 5, 58, false, [] { return new Amp(); }});

// ---------------- MIXER: four channels, each with level, pan and a send ----------------
struct Mixer : Module {
  Smooth lv[4], pl[4], pr_[4], sd[4]; float pk[4] = {0, 0, 0, 0};
  void prepare() override { for (int k = 0; k < 4; k++) { for (Smooth* s : {&lv[k], &pl[k], &pr_[k], &sd[k]}) s->setTime(0.012f, sr); } }
  void render(int a, int b) override {
    for (int k = 0; k < 4; k++) { float l, r; panGains(e[k * 3 + 1], l, r); lv[k].t = e[k * 3]; pl[k].t = l * 1.414f; pr_[k].t = r * 1.414f; sd[k].t = e[k * 3 + 2]; }
    for (int i = a; i < b; i++) {
      float L = 0, R = 0, SL = 0, SR = 0;
      for (int k = 0; k < 4; k++) { const float g = lv[k].tick(), gl = pl[k].tick(), gr = pr_[k].tick(), s = sd[k].tick(), xl = in[k][0][i], xr = in[k][1][i];
        L += xl * g * gl; R += xr * g * gr; SL += xl * s; SR += xr * s; pk[k] = std::max(pk[k] * 0.9999f, std::max(std::fabs(xl), std::fabs(xr)) * g); }
      out[0][0][i] = L; out[0][1][i] = R; out[1][0][i] = SL; out[1][1][i] = SR;
    }
    for (int k = 0; k < 4; k++) disp[k].store(pk[k]);
  }
};
static std::vector<ParamDef> mixerParams() { std::vector<ParamDef> p; for (int k = 1; k <= 4; k++) { std::string n = std::to_string(k);
    ParamDef l = pct("", "", 0.8f); l.key = "l" + n; l.label = "LEVEL " + n; p.push_back(l);
    ParamDef q = pr("", "", -1, 1, 0); q.key = "p" + n; q.label = "PAN " + n; q.fmt = [](float v) { if (std::fabs(v) < 0.02f) return std::string("C"); return std::string(v < 0 ? "L" : "R") + std::to_string((int)std::round(std::fabs(v) * 100)); }; p.push_back(q);
    ParamDef s = pct("", "", 0); s.key = "s" + n; s.label = "SEND " + n; p.push_back(s); } return p; }
static Reg rMixer(new ModuleDef{"mixer", "MIXER", Cat::Proc, {{"1", PType::Audio}, {"2", PType::Audio}, {"3", PType::Audio}, {"4", PType::Audio}}, {{"OUT", PType::Audio}, {"SEND", PType::Audio}},
  mixerParams(), 8, 58, false, [] { return new Mixer(); }});

}  // namespace tb
