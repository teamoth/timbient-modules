// Timbient native: more rhythm modules (bursts, walker, trig tools, tracker).
#include "params.h"

namespace tb {

struct ClockIn2 {   // learns the pulse period from what arrives at a CLOCK input
  double last = -1; float cp = 0, ppb = 1;
  void tick(double t, const Event& ev) {
    if (ev.period > 0) cp = ev.period; else if (last >= 0) { const float p = (float)(t - last); if (p > 0.01f && p < 8) cp = cp > 0 ? cp * 0.6f + p * 0.4f : p; }
    last = t; ppb = ev.ppb > 0 ? ev.ppb : (ppb > 0 ? ppb : 1);
  }
};

// ---------------- BURSTS: a little clump of notes, then a rest ----------------
struct Bursts : Module {
  enum { VEL, CHANCE, HITS, CLUMP, LEN, REST, RANGE, OCT };
  double q[9]; int nq = 0, qi = 0, run = 0, npat = 0; float pat[9]; double restUntil = 1, at = -100; ClockIn2 ck;
  void clumped(int n, float c) {   // n points across 0..1, pulled together into a few clumps
    const int k = std::max(1, (int)std::lround(n / rng.range(2, 4))); float centres[9];
    for (int i = 0; i < k; i++) centres[i] = (i + 0.5f) / k + rng.range(-0.07f, 0.07f) / k;
    for (int i = 0; i < n; i++) { const float e0 = (float)i / n; float best = centres[0]; for (int j = 0; j < k; j++) if (std::fabs(centres[j] - e0) < std::fabs(best - e0)) best = centres[j]; pat[i] = clampf(e0 + (best - e0) * c, 0, 0.999f); }
    std::sort(pat, pat + n);
  }
  void begin(double now) { npat = std::min(9, (int)e[HITS]); clumped(npat, e[CLUMP]); at = now; for (int i = 0; i < npat; i++) q[i] = now + pat[i] * e[LEN]; nq = npat; qi = 0; run = 0; restUntil = now + e[LEN] + e[REST] * rng.range(0.6f, 1.4f); }
  void blockStart(int) override { clockedIn.store(patched(0)); }
  void trig(int port, const Event& ev) override { if (port != 0) return; const double now = t + eng->pos / sr; ck.tick(now, ev); if (qi >= nq && now >= restUntil - 0.02) begin(now); }
  void render(int a, int b) override {
    for (int i = a; i < b; i++) { const double now = t + i / sr; eng->pos = i;
      if (!clockedIn.load() && qi >= nq && now > restUntil) begin(now);
      while (qi < nq && q[qi] <= now) { qi++; Event ev; ev.note = note((int)e[OCT], run++ % std::max(1, (int)e[RANGE])); ev.vel = e[VEL]; emit(0, ev); } }
    disp[0].store((float)npat); for (int i = 0; i < 9; i++) disp[1 + i].store(pat[i]); disp[10].store((float)((t + b / sr - at) / e[LEN]));
  }
};
static Reg rBursts(new ModuleDef{"bursts", "BURSTS", Cat::Trig, {{"CLOCK", PType::Trig}}, {{"TRIG", PType::Trig}},
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1), ST("hits", "HITS", 2, 9, 5), pr("clump", "CLUMP", 0, 0.9f, 0.5f), T("len", "LENGTH", 0.4f, 6, 1.8f), T("rest", "REST", 0.2f, 12, 3), ST("range", "RANGE", 1, 8, 4), OCTP("oct", 1, 7, 6)},
  7, 58, false, [] { return new Bursts(); }});

// ---------------- WALKER: a head that gets pushed along the scale and slides to a stop ----------------
struct Walker : Module {
  enum { VEL, CHANCE, PUSH, GLIDE, RANGE, OCT };
  float head = 0.5f, vx = 0, next = 1, dir = 1; ClockIn2 ck;
  void push() { if (rng.uni() < 0.3f) dir = -dir; if (head < 0.05f) dir = 1; if (head > 0.95f) dir = -1;
    vx += dir * rng.range(0.12f, 0.22f); Event ev; ev.note = note((int)e[OCT], (int)(head * 0.999f * e[RANGE])); ev.vel = e[VEL]; emit(0, ev); }
  void blockStart(int) override { clockedIn.store(patched(0)); }
  void trig(int port, const Event& ev) override { if (port != 0) return; ck.tick(t + eng->pos / sr, ev); if (rng.uni() < 0.15f + e[PUSH] * 0.85f) push(); }
  void render(int a, int b) override {
    const float dt = 1.f / sr;
    if (!clockedIn.load()) for (int i = a; i < b; i++) { next -= dt * (0.15f + e[PUSH] * 1.6f); if (next <= 0) { next = rng.range(0.6f, 2.2f); eng->pos = i; push(); } }
    const float D = (b - a) * dt; vx *= std::exp(-D * (0.3f + (1 - e[GLIDE]) * 2.5f)); head = clampf(head + vx * D, 0, 1);
    disp[0].store(head);
  }
};
static Reg rWalker(new ModuleDef{"walker", "WALKER", Cat::Trig, {{"CLOCK", PType::Trig}}, {{"TRIG", PType::Trig}},
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1), pct("push", "PUSH", 0.4f), pct("glide", "GLIDE", 0.5f), ST("range", "RANGE", 1, 16, 10), OCTP("oct", 1, 8, 6)},
  6, 58, false, [] { return new Walker(); }});

// ---------------- TRIG TOOLS: thin, repeat and transpose whatever passes through ----------------
struct Tools : Module {
  enum { CHANCE, DIV, REP, GAP, FALL, STEPS, OCT, QUANT };
  long n = 0; struct Rep { double t; Event ev; }; Rep q[64]; int nq = 0; unsigned hist = 0; int nh = 0;
  void trig(int port, const Event& in) override {
    if (port != 0) return; n++; const double now = t + eng->pos / sr;
    const bool pass = (n - 1) % std::max(1, (int)e[DIV]) == 0 && rng.uni() < e[CHANCE];
    hist = (hist << 1) | (pass ? 1u : 0u); nh = std::min(14, nh + 1);
    if (!pass) return;
    Event ev = in; const int st = (int)e[STEPS];
    if (e[QUANT] > 0.5f || st != 0) ev.note = midiOf(nearDeg(ev.note, root(), scale()) + st, root(), scale());
    ev.note += e[OCT] * 12; if (ev.period > 0) ev.period *= e[DIV];
    emit(0, ev);
    for (int i = 1; i <= (int)e[REP]; i++) { if (nq >= 64) { for (int k = 1; k < 64; k++) q[k - 1] = q[k]; nq--; } Event r = ev; r.vel = ev.vel * std::pow(1 - e[FALL], (float)i); q[nq++] = {now + i * e[GAP], r}; }
  }
  void render(int a, int b) override {
    if (nq) for (int i = a; i < b; i++) { const double now = t + i / sr; for (int k = 0; k < nq; ) { if (q[k].t <= now) { eng->pos = i; emit(0, q[k].ev); q[k] = q[--nq]; } else k++; } }
    disp[0].store((float)nh); disp[1].store((float)hist);
  }
};
static Reg rTools(new ModuleDef{"tools", "TRIG TOOLS", Cat::Trig, {{"IN", PType::Trig}}, {{"OUT", PType::Trig}},
  {pct("chance", "CHANCE", 1), ST("div", "DIVIDE", 1, 16, 1), ST("rep", "REPEATS", 0, 8, 0), T("gap", "REP GAP", 0.03f, 1, 0.12f), pct("fall", "REP FALL", 0.3f),
   ST("steps", "STEPS", -7, 7, 0), ST("oct", "OCTAVE", -3, 3, 0), OP("quant", "QUANTISE", {"OFF", "ON"}, 0)}, 4, 58, false, [] { return new Tools(); }});

// ---------------- TRACKER: notes typed in step by step ----------------
static const int TRK_DIV[3] = {1, 2, 4};
struct Tracker : Module {
  enum { RUN, SYNC, BPM, DIV, LEN, ESTEP, REC, VEL, CHANCE, FOLLOW, REST, DEL, CLEAR };
  int pos = -1; double next = -1; ClockIn2 ck;
  Tracker() { allocSeq(); }
  int len() const { return std::max(1, std::min(SEQ_MAX, (int)e[LEN])); }
  void step() {
    const bool clk = clockedIn.load(); const float per = clk && ck.cp > 0 ? ck.cp : 60.f / (e[SYNC] > 0.5f ? eng->tempo.bpm.load(std::memory_order_relaxed) : e[BPM]) / TRK_DIV[(int)e[DIV]];
    const float ppb = clk ? ck.ppb : (float)TRK_DIV[(int)e[DIV]];
    pos = (pos + 1) % len();
    Event p; p.note = note(4, 0); p.vel = e[VEL]; p.period = per; p.ppb = ppb; emit(1, p);
    const float nt = seq[pos * 2].load(std::memory_order_relaxed);
    if (nt >= 0) { Event ev; ev.note = nt; ev.vel = seq[pos * 2 + 1].load(std::memory_order_relaxed) * e[VEL]; ev.period = per; ev.ppb = ppb; emit(0, ev); }
  }
  void blockStart(int) override { clockedIn.store(patched(0)); int c = cursor.load(); if (c >= len()) cursor.store(len() - 1); }
  void trig(int port, const Event& ev) override {
    if (port == 0) { ck.tick(t + eng->pos / sr, ev); if (e[RUN] > 0.5f) step(); return; }
    if (e[REC] < 0.5f) return;   // ENTER (when RECORD is on): write at the cursor, then move on by ADVANCE
    const int c = std::min(cursor.load(), len() - 1);
    seq[c * 2].store(ev.note); seq[c * 2 + 1].store(std::round(ev.vel * 100) / 100);
    disp[2].store((float)c); disp[3].store(ev.note); disp[4].store(disp[4].load() + 1);
    cursor.store((c + (int)e[ESTEP]) % len()); seqVer.fetch_add(1);
  }
  void action(const std::string& k) override {
    const int c = std::min(cursor.load(), len() - 1);
    if (k == "rest" || k == "del") { seq[c * 2].store(-1); if (k == "rest") cursor.store((c + std::max(1, (int)e[ESTEP])) % len()); seqVer.fetch_add(1); }
    else if (k == "clear") { for (int i = 0; i < SEQ_MAX; i++) seq[i * 2].store(-1); cursor.store(0); seqVer.fetch_add(1); }
  }
  void render(int a, int b) override {
    if (e[RUN] < 0.5f) { pos = -1; next = -1; }
    else if (!clockedIn.load()) {
      const double per = 60.0 / (e[SYNC] > 0.5f ? eng->tempo.bpm.load(std::memory_order_relaxed) : e[BPM]) / TRK_DIV[(int)e[DIV]];
      for (int i = a; i < b; i++) { const double now = t + i / sr; if (next < 0 || now - next > 1) next = now; while (now >= next) { eng->pos = i; step(); next += per; } }
    }
    disp[0].store((float)pos); disp[1].store((float)TRK_DIV[(int)e[DIV]]);
  }
};
static Reg rTracker(new ModuleDef{"tracker", "TRACKER", Cat::Trig, {{"CLOCK", PType::Trig}, {"ENTER", PType::Trig}}, {{"TRIG", PType::Trig}, {"PULSE", PType::Trig}},
  {playBtn(OP("run", "RUN", {"STOP", "RUN"}, 1)), only(tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), "free"), only(fmt(ST("bpm", "BPM", 20, 240, 90), [](float v) { return std::to_string((int)v); }), "own"),
   only(OP("div", "STEP", {"1/4", "1/8", "1/16"}, 1), "free"), ST("len", "LENGTH", 1, 128, 16), ST("estep", "ADVANCE", 0, 8, 1), OP("rec", "RECORD", {"OFF", "ON"}, 1), pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1),
   OP("follow", "FOLLOW", {"OFF", "ON"}, 1), ACT("rest", "REST"), ACT("del", "CLEAR STEP"), ACT("clear", "CLEAR ALL")},
  85, 58, false, [] { return new Tracker(); }});

}  // namespace tb
