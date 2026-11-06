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
  7, 68, false, [] { return new Bursts(); }});

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
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1), pct("push", "PUSH", 0.4f), pct("glide", "SLIDE", 0.5f), ST("range", "RANGE", 1, 16, 10), OCTP("oct", 1, 8, 6)},
  6, 68, false, [] { return new Walker(); }});

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
   ST("steps", "SHIFT", -7, 7, 0), ST("oct", "OCTAVE", -3, 3, 0), OP("quant", "QUANTISE", {"OFF", "ON"}, 0)}, 4, 68, false, [] { return new Tools(); }});

// ---------------- TRACKER: notes typed in step by step ----------------
static const int TRK_DIV[3] = {1, 2, 4};
struct Tracker : Module {
  enum { SYNC, BPM, DIV, LEN, ESTEP, REC, VEL, CHANCE, DEL, CLEAR };
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
    if (port == 0) { ck.tick(t + eng->pos / sr, ev); if (!mute.load(std::memory_order_relaxed)) step(); return; }
    if (e[REC] < 0.5f) return;   // ENTER (when RECORD is on): write at the cursor, then move on by ADVANCE
    const int c = std::min(cursor.load(), len() - 1);
    seq[c * 2].store(ev.note); seq[c * 2 + 1].store(std::round(ev.vel * 100) / 100);
    disp[2].store((float)c); disp[3].store(ev.note); disp[4].store(disp[4].load() + 1);
    cursor.store((c + (int)e[ESTEP]) % len()); seqVer.fetch_add(1);
  }
  void action(const std::string& k) override {
    const int c = std::min(cursor.load(), len() - 1);
    if (k == "del") { seq[c * 2].store(-1); seqVer.fetch_add(1); }
    else if (k == "clear") { for (int i = 0; i < SEQ_MAX; i++) seq[i * 2].store(-1); cursor.store(0); seqVer.fetch_add(1); }
  }
  void render(int a, int b) override {
    if (mute.load(std::memory_order_relaxed)) { pos = -1; next = -1; }   // the footer's ON/OFF starts and stops it
    else if (!clockedIn.load()) {
      const double per = 60.0 / (e[SYNC] > 0.5f ? eng->tempo.bpm.load(std::memory_order_relaxed) : e[BPM]) / TRK_DIV[(int)e[DIV]];
      for (int i = a; i < b; i++) { const double now = t + i / sr; if (next < 0 || now - next > 1) next = now; while (now >= next) { eng->pos = i; step(); next += per; } }
    }
    disp[0].store((float)pos); disp[1].store((float)TRK_DIV[(int)e[DIV]]);
  }
};
static Reg rTracker(new ModuleDef{"tracker", "TRACKER", Cat::Trig, {{"CLOCK", PType::Trig}, {"ENTER", PType::Trig}}, {{"TRIG", PType::Trig}, {"PULSE", PType::Trig}},
  {only(tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), "free"), only(fmt(ST("bpm", "BPM", 20, 240, 90), [](float v) { return std::to_string((int)v); }), "own"),
   only(OP("div", "STEP", {"1/4", "1/8", "1/16"}, 1), "free"), ST("len", "LENGTH", 1, 128, 16), ST("estep", "ADVANCE", 0, 8, 1), OP("rec", "RECORD", {"OFF", "ON"}, 1), pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1),
   ACT("del", "CLEAR STEP"), ACT("clear", "CLEAR ALL")},
  85, 68, false, [] { return new Tracker(); }});

}  // namespace tb

namespace tb {

// ---------------- TOMBOLA: a slowly turning drum with note-balls bouncing inside; a ball plays its note when it hits a wall ----------------
struct Tombola : Module {
  enum { VEL, CHANCE, SPIN, BOUNCE, ADD, EVERY, DISSOLVE, MAXB, SIDES, RANGE, OCT, SHAKE };
  static constexpr int MAXN = 12; static constexpr float RB = 0.07f, G = 3.2f;
  struct Ball { float x, y, vx, vy, age, cool, flash; int deg; float midi; };   // deg < 0: a note that came in through ADD
  Ball b[MAXN]; int n = 0; float ang = 0, addT = 0;
  void addBall(int deg, float midi) {
    if (n >= std::min(MAXN, (int)e[MAXB])) return;
    Ball& q = b[n++]; q.x = rng.range(-0.3f, 0.3f); q.y = rng.range(-0.5f, -0.1f); q.vx = rng.range(-0.6f, 0.6f); q.vy = 0; q.age = 0; q.cool = 0.3f; q.flash = 0; q.deg = deg; q.midi = midi;
  }
  void prepare() override { for (int i = 0; i < 3; i++) addBall((int)(rng.uni() * e[RANGE]), 0); }
  void shake() { for (int i = 0; i < n; i++) { b[i].vx += rng.range(-2.5f, 2.5f); b[i].vy += rng.range(-3.5f, -1); } }
  void trig(int port, const Event& ev) override { if (port == 0) addBall(-1, ev.note); else if (port == 1) shake(); }   // ADD: another sequencer drops balls in, with its notes. SHAKE: each pulse (a slow clock, say) shakes the drum
  void action(const std::string& k) override { if (k == "shake") shake(); }
  void render(int a, int bEnd) override {
    const float dt = (bEnd - a) / sr; eng->pos = a;
    const int S = (int)e[SIDES]; const float w = e[SPIN] * TAU, ap = std::cos(PI / S), life = e[DISSOLVE], rest = 0.35f + e[BOUNCE] * 0.6f;
    ang += w * dt; if (ang > TAU) ang -= TAU; if (ang < 0) ang += TAU;
    if (e[ADD] > 0.5f) { addT += dt; if (addT >= e[EVERY]) { addT = 0; addBall((int)(rng.uni() * e[RANGE]), 0); } }
    for (int i = 0; i < n; i++) {
      Ball& q = b[i]; q.vy += G * dt; q.x += q.vx * dt; q.y += q.vy * dt; q.age += dt; q.cool -= dt; q.flash = std::max(0.f, q.flash - dt);
      for (int s = 0; s < S; s++) {   // each wall: push back inside, bounce off it, and get carried along a little by its turning
        const float na = ang + (s + 0.5f) * TAU / S, nx = std::cos(na), ny = std::sin(na), d = q.x * nx + q.y * ny;
        if (d <= ap - RB) continue;
        q.x -= (d - (ap - RB)) * nx; q.y -= (d - (ap - RB)) * ny;
        const float wx = -w * q.y, wy = w * q.x, rvx = q.vx - wx, rvy = q.vy - wy, vn = rvx * nx + rvy * ny;
        if (vn <= 0) continue;
        const float tx = rvx - vn * nx, ty = rvy - vn * ny;
        q.vx = wx + tx * 0.8f - rest * vn * nx; q.vy = wy + ty * 0.8f - rest * vn * ny;
        if (vn > 0.35f && q.cool <= 0) {   // a real knock, not just rolling: play its note
          q.cool = 0.12f; q.flash = 0.15f;
          const float fade = life > 0 ? clampf(1 - (q.age / life - 0.7f) / 0.3f, 0, 1) : 1;   // dissolving balls get quieter
          Event ev; ev.note = q.deg >= 0 ? note((int)e[OCT], q.deg) : q.midi; ev.vel = clampf(vn / 2.5f, 0.15f, 1) * e[VEL] * fade; if (ev.vel > 0.02f) emit(0, ev);
        }
      }
    }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {   // balls knock each other about (silently)
      const float dx = b[j].x - b[i].x, dy = b[j].y - b[i].y, d2 = dx * dx + dy * dy; if (d2 >= 4 * RB * RB || d2 < 1e-8f) continue;
      const float d = std::sqrt(d2), nx = dx / d, ny = dy / d, over = 2 * RB - d; b[i].x -= nx * over / 2; b[i].y -= ny * over / 2; b[j].x += nx * over / 2; b[j].y += ny * over / 2;
      const float vn = (b[j].vx - b[i].vx) * nx + (b[j].vy - b[i].vy) * ny; if (vn < 0) { b[i].vx += vn * nx; b[i].vy += vn * ny; b[j].vx -= vn * nx; b[j].vy -= vn * ny; } }
    if (life > 0) for (int i = 0; i < n; ) { if (b[i].age > life) b[i] = b[--n]; else i++; }   // DISSOLVE: old balls fade away
    while (n > std::max(1, (int)e[MAXB])) n--;
    disp[0].store(ang); disp[1].store((float)S); disp[2].store((float)n);
    for (int i = 0; i < n; i++) { disp[3 + i * 4].store(b[i].x); disp[4 + i * 4].store(b[i].y); disp[5 + i * 4].store(life > 0 ? clampf(1 - (b[i].age / life - 0.7f) / 0.3f, 0, 1) : 1); disp[6 + i * 4].store(b[i].flash); }
  }
};
static Reg rTombola(new ModuleDef{"tombola", "TOMBOLA", Cat::Trig, {{"ADD", PType::Trig}, {"SHAKE", PType::Trig}}, {{"TRIG", PType::Trig}},
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1),
   fmt(pr("spin", "SPIN", -0.5f, 0.5f, 0.08f), [](float v) { if (std::fabs(v) < 0.005f) return std::string("STOPPED"); char t[16]; std::snprintf(t, 16, "%s%.2f REV/S", v < 0 ? "<" : "", std::fabs(v)); return std::string(t); }),
   pct("bounce", "BOUNCE", 0.6f), OP("add", "ADD", {"OFF", "ON"}, 1), T("every", "EVERY", 0.5f, 60, 6),
   fmt(pr("dissolve", "DISSOLVE", 0, 240, 60), [](float v) { return v < 1 ? std::string("OFF") : fmtSecs(v); }), ST("maxb", "BALLS", 1, 12, 6), ST("sides", "SIDES", 3, 8, 6),
   ST("range", "RANGE", 1, 14, 7), OCTP("oct", 1, 7, 5), ACT("shake", "SHAKE")}, 40, 68, false, [] { return new Tombola(); }});

}  // namespace tb
