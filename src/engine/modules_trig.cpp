// Timbient native: rhythm modules, the Note Grid, the LFO and the Output.
// All of their timing runs on the audio thread, accurate to the sample.
#include "params.h"

namespace tb {

// shared by anything with a CLOCK input: learns the pulse period from what arrives
struct ClockIn {
  double last = -1; float cp = 0, ppb = 1; long pulses = 0;
  void tick(double t, const Event& ev) {
    if (ev.period > 0) cp = ev.period; else if (last >= 0) { const float p = (float)(t - last); if (p > 0.01f && p < 8) cp = cp > 0 ? cp * 0.6f + p * 0.4f : p; }
    last = t; ppb = ev.ppb > 0 ? ev.ppb : (ppb > 0 ? ppb : 1); pulses++;
  }
};
static const int PPB_TABLE[4] = {1, 2, 4, 3};
static const int ORBIT_TABLE[8] = {32, 16, 12, 8, 6, 4, 3, 2};
static const float SYNC_TABLE[8] = {0.25f, 0.5f, 1, 2, 4, 8, 16, 32};
static float bpmOf(Module* m, int syncK, int bpmK) { return m->e[syncK] > 0.5f ? m->eng->tempo.bpm.load(std::memory_order_relaxed) : m->e[bpmK]; }

// ---------------- CLOCK ----------------
struct Clock : Module {
  double next = -1; long n = 0; double now = 0;
  void render(int a, int b) override {
    const float run = mute.load(std::memory_order_relaxed) ? 0.f : 1.f;   // the footer's ON/OFF starts and stops it
    const int div = (int)e[2]; const float ppb = (float)PPB_TABLE[div], per = 60.f / bpmOf(this, 0, 1) / ppb;
    for (int i = a; i < b; i++) {
      now = t + (i) / sr;
      if (run < 0.5f) { next = -1; continue; }
      if (next < 0 || now - next > 1) next = now;
      while (now >= next) {
        eng->pos = i;
        Event ev; ev.note = note(4, 0); ev.vel = e[4]; ev.period = per; ev.ppb = ppb; emit(0, ev);
        if (n % (long)(ppb * 4) == 0) { Event bar; bar.note = note(3, 0); bar.vel = e[4]; bar.period = per * ppb * 4; bar.ppb = 0.25f; emit(1, bar); }
        next += per * (1 + (n % 2 == 0 ? 1 : -1) * e[3] * 0.5f); n++;
      }
    }
    disp[0].store((float)((n - 1) / (long)ppb % 4)); disp[1].store(run);
  }
};
static Reg rClock(new ModuleDef{"clock", "CLOCK", Cat::Trig, {}, {{"PULSE", PType::Trig}, {"BAR", PType::Trig}},
  {tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), fmt(ST("bpm", "BPM", 20, 240, 72), [](float v) { return std::to_string((int)v); }),   // on SHARED, this is the session's tempo (the top bar's)
   OP("div", "STEP", {"1/4", "1/8", "1/16", "1/8T"}, 1), pct("swing", "SWING", 0), pct("vel", "VELOCITY", 1)}, 5, 68, false, [] { return new Clock(); }});

// ---------------- CLUMP SEQ ----------------
struct Clump : Module {
  struct Step { float r, r2, f, fl, pos; int o; };
  Step st[16]; int nsteps = 0, total = 1, unit = 0; float ph = 0; ClockIn ck;
  enum { VEL, CHANCE, STEPS, CLUMP, SPEED, DRIFT, MUTATE, RANGE, OCT };
  void prepare() override { for (auto& s : st) { s.r = rng.uni(); s.r2 = rng.uni(); s.f = rng.uni(); s.fl = 0; } }
  void layout() {
    nsteps = (int)e[STEPS]; int o = 0;
    for (int i = 0; i < nsteps; i++) { st[i].o = o; o += st[i].r < e[CLUMP] ? 1 : 2 + (int)(st[i].r2 * 4); }
    total = std::max(1, o); for (int i = 0; i < nsteps; i++) st[i].pos = (float)st[i].o / total;
  }
  void fire(Step& s) { s.fl = 0.15f; Event ev; ev.note = note((int)e[OCT], (int)(s.f * e[RANGE])); ev.vel = e[VEL]; emit(0, ev); }
  void mutate() { for (float k = e[MUTATE]; k > 0; k -= 1) if (rng.uni() < std::min(1.f, k)) { Step& s = st[rng.next() % std::max(1, nsteps)];
      s.f = clampf(s.f + (rng.uni() < 0.5f ? -1 : 1) * (1 + (rng.uni() < k - 1 ? 1 : 0)) / e[RANGE], 0, 0.999f); } }
  void blockStart(int n) override { layout(); for (int i = 0; i < nsteps; i++) st[i].fl = std::max(0.f, st[i].fl - n / sr); clockedIn.store(patched(0)); }
  void trig(int port, const Event& ev) override {
    if (port != 0) return; ck.tick(t + eng->pos / sr, ev);
    unit = (unit + 1) % total; if (unit == 0) mutate(); ph = (float)unit / total;
    for (int i = 0; i < nsteps; i++) if (st[i].o == unit) fire(st[i]);
  }
  void render(int a, int b) override {
    if (!clockedIn.load()) for (int i = a; i < b; i++) {
      const double tt = t + i / sr; const float rub = 1 + e[DRIFT] * 0.6f * std::sin((float)tt * 0.37f) * std::sin((float)tt * 0.11f + 1);
      const float prev = ph; ph += (1.f / sr) * rub * e[SPEED] / total; bool wrap = false; if (ph >= 1) { ph -= 1; wrap = true; mutate(); }
      eng->pos = i;
      for (int k = 0; k < nsteps; k++) { const float p = st[k].pos; if (wrap ? (p > prev || p <= ph) : (p > prev && p <= ph)) fire(st[k]); }
    }
    disp[0].store(ph); disp[1].store((float)nsteps);
    for (int k = 0; k < nsteps && k < 16; k++) { disp[2 + k].store(st[k].pos); disp[18 + k].store(st[k].f); disp[34 + k].store(st[k].fl); }
  }
};
static Reg rClump(new ModuleDef{"clump", "CLUMP SEQ", Cat::Trig, {{"CLOCK", PType::Trig}}, {{"TRIG", PType::Trig}},
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1), ST("steps", "STEPS", 2, 16, 8), pct("clump", "CLUMP", 0.6f),
   only(fmt([] { auto q = prExp("speed", "BPM", 0.2f, 8, 1.6f); q.step = 1 / 60.f; return q; }(), [](float v) { return std::to_string((int)std::lround(v * 60)) + " BPM"; }), "free"),   // stored as steps a second; shown as whole BPM
   only(pctX("drift", "DRIFT", 1.6f, 0.25f), "free"), pctX("mutate", "MUTATE", 3, 0.2f), ST("range", "RANGE", 1, 14, 7), OCTP("oct", 1, 8, 4)}, 14, 68, false, [] { return new Clump(); }});

// ---------------- ORBITS ----------------
struct Orbits : Module {
  double ang[8]; float fl[8]; int n = 0; ClockIn ck;   // angles in double, kept within one turn: a float angle that keeps growing loses its small steps and the rings stall
  enum { VEL, CHANCE, RINGS, SPEED, PHASE, HITS, RANGE, OCT };
  void prepare() override { for (int i = 0; i < 8; i++) { ang[i] = rng.uni() * 6.283185307179586; fl[i] = 0; } }
  void blockStart(int nn) override { clockedIn.store(patched(0)); for (int i = 0; i < 8; i++) fl[i] = std::max(0.f, fl[i] - nn / sr); }
  void trig(int port, const Event& ev) override { if (port == 0) ck.tick(t + eng->pos / sr, ev); }
  void render(int a, int b) override {
    n = (int)e[RINGS];
    const float base = clockedIn.load() && ck.cp > 0 ? TAU / (ck.cp * (float)ORBIT_TABLE[std::min(7, (int)(e[SPEED] * 8))]) : 0.5f + e[SPEED] * 4;
    const double step = 6.283185307179586 / e[HITS];
    for (int i = a; i < b; i++) for (int r = 0; r < n; r++) {
      const double w = base * (1 + e[PHASE] * r * 0.13f) / (1 + r * 0.12f) * (r & 1 ? -1 : 1), pv = ang[r]; ang[r] += w / sr;
      if (std::floor(pv / step) != std::floor(ang[r] / step)) { fl[r] = 0.15f; eng->pos = i; Event ev; ev.note = note((int)e[OCT], n > 1 ? (int)std::lround((n - 1 - r) * (e[RANGE] - 1) / (n - 1)) : 0); /* the rings share out RANGE scale notes */ ev.vel = e[VEL]; emit(0, ev); }
      if (ang[r] >= 6.283185307179586) ang[r] -= 6.283185307179586; else if (ang[r] < 0) ang[r] += 6.283185307179586;   // a whole turn is a whole number of HITS steps, so crossings are unchanged
    }
    disp[0].store((float)n); for (int r = 0; r < 8; r++) { disp[1 + r].store((float)ang[r]); disp[9 + r].store(fl[r]); }
  }
};
static Reg rOrbits(new ModuleDef{"orbits", "ORBITS", Cat::Trig, {{"CLOCK", PType::Trig}}, {{"TRIG", PType::Trig}},
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1), ST("rings", "RINGS", 1, 8, 5), pct("speed", "SPEED", 0.3f), pct("phase", "PHASING", 0.4f), ST("hits", "HITS", 1, 5, 2), ST("range", "RANGE", 1, 14, 5), OCTP("oct", 1, 8, 4)},
  26, 68, false, [] { return new Orbits(); }});

// ---------------- RANDOM HOPS ----------------
struct Hops : Module {
  double next = 0.5; double q[16]; int nq = 0; ClockIn ck;
  enum { VEL, CHANCE, RATE, CLUSTER, SPREAD, OCT };
  int fired = 0;
  void fire() { const int st = (int)(rng.uni() * e[SPREAD]); Event ev; ev.note = note((int)e[OCT], st); ev.vel = rng.range(0.5f, 1) * e[VEL]; emit(0, ev);
    disp[1].store(e[SPREAD] > 1 ? st / (e[SPREAD] - 1) : 0.5f); disp[2].store(ev.vel); disp[0].store((float)++fired); }   // for the display: where the hop landed
  void hop(double now) { fire(); if (rng.uni() < e[CLUSTER]) { const int k = 2 + (int)(rng.uni() * 3); for (int i = 0; i < k && nq < 16; i++) q[nq++] = now + rng.range(0.02f, 0.25f); } }
  void blockStart(int) override { clockedIn.store(patched(0)); }
  void trig(int port, const Event& ev) override { if (port != 0) return; ck.tick(t + eng->pos / sr, ev); if (rng.uni() < clampf(e[RATE] * (ck.cp > 0 ? ck.cp : 0.5f), 0, 1)) hop(t + eng->pos / sr); }
  void render(int a, int b) override {
    for (int i = a; i < b; i++) {
      const double now = t + i / sr; eng->pos = i;
      if (!clockedIn.load()) { next -= 1.0 / sr; if (next <= 0) { next = -std::log(1 - rng.uni() * 0.9999) / e[RATE]; hop(now); } }
      for (int k = 0; k < nq; ) { if (q[k] <= now) { fire(); q[k] = q[--nq]; } else k++; }
    }
  }
};
static Reg rHops(new ModuleDef{"hops", "RANDOM HOPS", Cat::Trig, {{"CLOCK", PType::Trig}}, {{"TRIG", PType::Trig}},
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1), fmt([] { auto q = prExp("rate", "BPM", 0.1f, 20, 2); q.step = 1 / 60.f; return q; }(), [](float v) { return std::to_string((int)std::lround(v * 60)) + " BPM"; }),   // whole BPM
   pct("cluster", "CLUMP", 0.3f), ST("spread", "RANGE", 1, 14, 8), OCTP("oct", 1, 8, 4)}, 16, 68, false, [] { return new Hops(); }});

// ---------------- NOTE GRID: pads pressed on screen arrive here and go out as notes ----------------
struct Keys : Module {
  void trig(int port, const Event& ev) override { if (port == -1) emit(0, ev); }
};
static Reg rKeys(new ModuleDef{"keys", "NOTE GRID", Cat::Trig, {}, {{"NOTE", PType::Trig}},
  {noMod(pct("vel", "VELOCITY", 0.8f)), noMod(OCTP("oct", 1, 8, 4)), OP("size", "SIZE", {"SMALL", "MEDIUM", "LARGE"}, 1)}, 36, 118, false, [] { return new Keys(); }});

// ---------------- QWERTY MIDI: the computer keyboard plays notes, three octaves of them (C-0 to B-2), on keys you choose ----------------
// The screen sends each key press in (port -1) as a note; OCTAVE says where C-0 sits
struct Qwerty : Module { void trig(int port, const Event& ev) override { if (port == -1) emit(0, ev); } };
static Reg rQwerty(new ModuleDef{"qwerty", "QWERTY MIDI", Cat::Trig, {}, {{"NOTE", PType::Trig}},
  {noMod(pct("vel", "VELOCITY", 0.8f)), noMod(fmt(ST("oct", "OCTAVE", 1, 8, 4), [](float v) { return "C-0 = C" + std::to_string((int)std::lround(v)); })), ACT("reset", "DEFAULT KEYS")}, 32, 118, false, [] { return new Qwerty(); }});

// ---------------- LFO ----------------
struct Lfo : Module {
  float ph = 0, a = 0, bv = 0; ClockIn ck;
  enum { SHAPE, RATE, SYNC, DEPTH, DIR };
  void prepare() override { ph = rng.uni(); bv = rng.bi(); }
  float beats() const { return (ck.ppb > 0 ? ck.ppb : 1) * SYNC_TABLE[(int)e[SYNC]]; }
  void blockStart(int) override { clockedIn.store(patched(0)); }
  void trig(int port, const Event& ev) override { if (port != 0) return; ck.tick(t + eng->pos / sr, ev); const float per = beats();
    if (per >= 1) { const int P = (int)std::round(per); const float target = (float)(ck.pulses % P) / P; if (target < ph - 0.5f) { a = bv; bv = rng.bi(); } ph = target; } }
  void render(int from, int to) override {
    const float per = clockedIn.load() && ck.cp > 0 ? ck.cp * beats() : e[RATE];
    ph += (to - from) / sr / per; while (ph >= 1) { ph -= 1; a = bv; bv = rng.bi(); }
    const int s = (int)e[SHAPE]; const float p = ph;
    modv = mute.load() ? 0 : s == 0 ? std::sin(p * TAU) : s == 1 ? 1 - 4 * std::fabs(p - 0.5f) : s == 2 ? (p < 0.5f ? 1.f : -1.f) : s == 3 ? bv : a + (bv - a) * (0.5f - 0.5f * std::cos(PI * p));
    disp[0].store(modv);
  }
};
static Reg rLfo(new ModuleDef{"lfo", "LFO", Cat::Mod, {{"CLOCK", PType::Trig}}, {{"MOD", PType::Mod}},
  {OP("shape", "SHAPE", {"SINE", "TRI", "SQUARE", "RANDOM", "DRIFT"}, 4),
   only(rev(fmt(prExp("rate", "RATE", 0.05f, 300, 20), [](float v) { char b[16]; if (v < 1) std::snprintf(b, 16, "%.1fHZ", 1 / v); else if (v < 10) std::snprintf(b, 16, "1 PER %.1fS", v); else if (v < 120) std::snprintf(b, 16, "1 PER %dS", (int)v); else std::snprintf(b, 16, "1 PER %.1fM", v / 60); return std::string(b); })), "free"),
   OP("sync", "SYNC", {"1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "4 BEATS", "8 BEATS", "16 BEATS", "32 BEATS"}, 4),   // drawn like the other settings (it is used once a clock is patched in)
   pct("depth", "DEPTH", 0.5f), OP("dir", "DIRECTION", {"BOTH", "UP", "DOWN"}, 0)}, 7, 68, false, [] { return new Lfo(); }});

// ---------------- OUTPUT: crossfade A/B, LOW / MID / HIGH EQ, level, to the master ----------------
struct Out : Module {
  Biquad lo, mid, hi; float gA = 1, gB = 0, lev = 0.85f, peak = 0, sig[3] = {99, 99, 99};
  enum { REC, AGAIN, XF, LEVEL_, LOW, MID, HIGH };
  void render(int a, int b) override {
    const float x = clampf(e[XF], 0, 1), ta = std::cos(x * PI / 2), tb = std::sin(x * PI / 2), k = 1 - std::exp(-1.f / (0.012f * sr));
    if (e[LOW] != sig[0] || e[MID] != sig[1] || e[HIGH] != sig[2]) { sig[0] = e[LOW]; sig[1] = e[MID]; sig[2] = e[HIGH];   // two gentle shelves and a broad middle
      lo.setShelf(false, 200, e[LOW], sr); mid.set(Biquad::PEAK, 1000, 0.6f, sr, e[MID]); hi.setShelf(true, 4000, e[HIGH], sr); }
    const bool flat = std::fabs(e[LOW]) < 0.05f && std::fabs(e[MID]) < 0.05f && std::fabs(e[HIGH]) < 0.05f;
    for (int i = a; i < b; i++) {
      gA += (ta - gA) * k; gB += (tb - gB) * k; lev += (e[LEVEL_] - lev) * k;
      for (int ch = 0; ch < 2; ch++) {
        float v = in[0][ch][i] * gA + in[1][ch][i] * gB;
        if (!flat) v = hi.run(mid.run(lo.run(v, ch), ch), ch);
        v = sane(v) * lev; (ch ? eng->masterR : eng->masterL)[i] += v; float& pk = ch ? peakR : peak; pk = std::max(pk * 0.99995f, std::fabs(v));
      }
    }
    disp[0].store(peak); disp[3].store(peakR);   // left, right
  }
  float peakR = 0;
};
static ParamDef eqP(const char* k, const char* l) { auto p = pr(k, l, -12, 12, 0); p.fmt = [](float v) { char b[16]; std::snprintf(b, 16, "%+.1fDB", v); return std::string(std::fabs(v) < 0.05f ? "0DB" : b); }; return p; }
static Reg rOut(new ModuleDef{"out", "OUTPUT", Cat::Out, {{"A", PType::Audio}, {"B", PType::Audio}}, {},
  {ACT("rec", "RECORD"), ACT("again", "RECORDINGS"), pct("xf", "X-FADE", 0), pctX("level", "LEVEL", 1.5f, 0.85f), noMod(eqP("low", "LOW")), noMod(eqP("mid", "MID")), noMod(eqP("high", "HIGH"))}, 30, 68, true, [] { return new Out(); }});

}  // namespace tb
