// Timbient native: effects. Each one passes the incoming sound through untouched on its dry side.
#include "params.h"

namespace tb {

// ---------------- FILTER ----------------
struct Filter : Module {
  Biquad f; int cnt = 0; Smooth dry, wet, cutS, resS;   // CUTOFF and RES glide, the filter re-tuned every 4 samples (once a block before: a zipper when swept)
  enum { TYPE, CUT, RES, MIX };
  void prepare() override { dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX]); cutS.setTime(0.006f, sr); cutS.snap(std::log(e[CUT])); resS.setTime(0.006f, sr); resS.snap(e[RES]); }
  void render(int a, int b) override {
    const int ty = (int)e[TYPE]; const Biquad::Type T2 = ty == 0 ? Biquad::LP : ty == 1 ? Biquad::HP : Biquad::BP;
    cutS.t = std::log(std::max(10.f, e[CUT])); resS.t = e[RES];
    dry.t = 1 - e[MIX]; wet.t = e[MIX];
    for (int i = a; i < b; i++) { const float d = dry.tick(), w = wet.tick(), lc = cutS.tick(), rs = resS.tick();
      if ((cnt++ & 3) == 0) f.set(T2, std::exp(lc), ty == 2 ? 0.6f + rs * 14 : std::max(0.5f, rs * 16), sr);
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i]; out[0][ch][i] = x * d + f.run(x, ch) * w; } }
  }
};
static Reg rFilter(new ModuleDef{"filter", "FILTER", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {OP("type", "TYPE", {"LOWPASS", "HIGHPASS", "BANDPASS"}, 0), prHz("cut", "CUTOFF", 40, 20000, 1800), pct("res", "RES", 0.2f), pct("mix", "MIX", 1)}, 0, 68, false, [] { return new Filter(); }});


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
