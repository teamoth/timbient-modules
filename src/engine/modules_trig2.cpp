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
  enum { SYNC, BPM, DIV, LEN, ESTEP, REC, VEL, CHANCE, DEL, CLEAR, COLS };
  int cols() const { return std::max(1, std::min(SEQ_COLS, (int)e[COLS])); }
  int pos = -1; double next = -1; ClockIn2 ck;
  Tracker() { allocSeq(); }
  int len() const { return std::max(1, std::min(SEQ_MAX, (int)e[LEN])); }
  void step() {
    const bool clk = clockedIn.load(); const float per = clk && ck.cp > 0 ? ck.cp : 60.f / (e[SYNC] > 0.5f ? eng->tempo.bpm.load(std::memory_order_relaxed) : e[BPM]) / TRK_DIV[(int)e[DIV]];
    const float ppb = clk ? ck.ppb : (float)TRK_DIV[(int)e[DIV]];
    pos = (pos + 1) % len();
    Event p; p.note = note(4, 0); p.vel = e[VEL]; p.period = per; p.ppb = ppb; emit(1, p);
    for (int c = 0; c < cols(); c++) {   // every column's note on this step, together
      const float nt = seq[si(pos, c)].load(std::memory_order_relaxed);
      if (nt >= 0) { Event ev; ev.note = nt; ev.vel = seq[si(pos, c) + 1].load(std::memory_order_relaxed) * e[VEL]; ev.period = per; ev.ppb = ppb; emit(0, ev); } }
  }
  void blockStart(int) override { clockedIn.store(patched(0)); int c = cursor.load(); if (c >= len()) cursor.store(len() - 1); if (cursorCol.load() >= cols()) cursorCol.store(cols() - 1); }
  void trig(int port, const Event& ev) override {
    if (port == 0) { ck.tick(t + eng->pos / sr, ev); if (!mute.load(std::memory_order_relaxed)) step(); return; }
    if (e[REC] < 0.5f) return;   // STEP ENTER (when STEP RECORD is on): write at the cursor, then move on by ADVANCE
    const int c = std::min(cursor.load(), len() - 1), cc = std::min(cursorCol.load(), cols() - 1);
    seq[si(c, cc)].store(ev.note); seq[si(c, cc) + 1].store(std::round(ev.vel * 100) / 100);
    disp[2].store((float)c); disp[3].store(ev.note); disp[4].store(disp[4].load() + 1);
    cursor.store((c + (int)e[ESTEP]) % len()); seqVer.fetch_add(1);
  }
  void action(const std::string& k) override {
    const int c = std::min(cursor.load(), len() - 1);
    if (k == "del") { seq[si(c, std::min(cursorCol.load(), cols() - 1))].store(-1); seqVer.fetch_add(1); }
    else if (k == "clear") { for (int i = 0; i < SEQ_MAX * SEQ_COLS; i++) seq[i * 2].store(-1); cursor.store(0); cursorCol.store(0); seqVer.fetch_add(1); }
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
static Reg rTracker(new ModuleDef{"tracker", "TRACKER", Cat::Trig, {{"CLOCK", PType::Trig}, {"STEP ENTER", PType::Trig}}, {{"TRIG", PType::Trig}, {"PULSE", PType::Trig}},
  {only(tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), "free"), only(fmt(ST("bpm", "BPM", 20, 240, 90), [](float v) { return std::to_string((int)v); }), "own"),
   only(OP("div", "STEP", {"1/4", "1/8", "1/16"}, 1), "free"), ST("len", "LENGTH", 1, 128, 16), ST("estep", "ADVANCE", 0, 8, 1), OP("rec", "STEP RECORD", {"OFF", "ON"}, 1), pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1),
   ACT("del", "CLEAR STEP"), ACT("clear", "CLEAR ALL"), ST("cols", "COLUMNS", 1, 8, 1)},
  85, 84, false, [] { return new Tracker(); }});

}  // namespace tb

namespace tb {

// ---------------- TOMBOLA: a slowly turning drum with note-balls bouncing inside; a ball plays its note when it hits a wall ----------------
struct Tombola : Module {
  // notes come in only through ADD (another sequencer, say a NOTE GRID); each becomes a ball in the turning drum
  enum { VEL, CHANCE, SPIN, SPEED, BOUNCE, DISSOLVE, MAXB, SIDES, WALLS, FREEZE, CLEAR };
  static constexpr int MAXN = 12; static constexpr float RB = 0.07f, G = 3.2f;
  struct Ball { float x, y, vx, vy, age, cool, flash; float midi; };
  Ball b[MAXN]; int n = 0; float ang = 0;
  void addBall(float midi) {
    if (n >= std::min(MAXN, (int)e[MAXB])) { for (int i = 1; i < n; i++) b[i - 1] = b[i]; n--; }   // full: the oldest makes room
    Ball& q = b[n++]; q.x = rng.range(-0.3f, 0.3f); q.y = rng.range(-0.5f, -0.1f); q.vx = rng.range(-0.6f, 0.6f); q.vy = 0; q.age = 0; q.cool = 0.3f; q.flash = 0; q.midi = midi;
  }
  // DISSOLVE: off at the left; further right, balls last less time (down to about a second and a half), and get quieter as they go
  float life() const { const float d = e[DISSOLVE]; return d < 0.01f ? 0.f : 1.5f + 150 * std::pow(1 - d, 3.f); }
  float fadeOf(const Ball& q) const { const float L = life(); return L > 0 ? clampf(1 - q.age / L, 0, 1) : 1; }
  void shake() { for (int i = 0; i < n; i++) { b[i].vx += rng.range(-2.5f, 2.5f); b[i].vy += rng.range(-3.5f, -1); } }
  void trig(int port, const Event& ev) override { if (port == 0) addBall(ev.note); else if (port == 1) shake(); }   // ADD: a note comes in. SHAKE: each pulse shakes the drum
  void action(const std::string& k) override { if (k == "shake") shake(); else if (k == "clear") n = 0; }
  void play(Ball& q, float vn) { if (q.cool > 0) return; q.cool = 0.12f; q.flash = 0.15f;   // (CHANCE is applied as every sequencer's is)
    Event ev; ev.note = q.midi; ev.vel = clampf(vn / 2.5f, 0.15f, 1) * e[VEL] * fadeOf(q); if (ev.vel > 0.02f) emit(0, ev); }
  void render(int a, int bEnd) override {
    const float dt = (bEnd - a) / sr, bt = dt * e[SPEED]; eng->pos = a;   // SPEED: the balls in slow (or quick) motion
    const int S = (int)e[SIDES], wm = (int)e[WALLS]; const float w = (e[FREEZE] > 0.5f ? 0.f : e[SPIN]) * TAU, ap = std::cos(PI / S), rest = 0.35f + e[BOUNCE] * 0.6f, L = life();
    ang += w * dt; ang = std::fmod(ang, TAU); if (ang < 0) ang += TAU;
    const float wb = w * dt / std::max(1e-6f, bt);   // the walls' speed as the balls feel it
    for (int i = 0; i < n; i++) {
      Ball& q = b[i]; q.vy += G * bt; q.x += q.vx * bt; q.y += q.vy * bt; q.age += dt; q.cool -= dt; q.flash = std::max(0.f, q.flash - dt);
      for (int s = 0; s < S; s++) {   // each wall: push back inside, bounce off it, and get carried along a little by its turning
        const float na = ang + (s + 0.5f) * TAU / S, nx = std::cos(na), ny = std::sin(na), d = q.x * nx + q.y * ny;
        if (d <= ap - RB) continue;
        q.x -= (d - (ap - RB)) * nx; q.y -= (d - (ap - RB)) * ny;
        const float wx = -wb * q.y, wy = wb * q.x, rvx = q.vx - wx, rvy = q.vy - wy, vn = rvx * nx + rvy * ny;
        if (vn <= 0) continue;
        const float tx = rvx - vn * nx, ty = rvy - vn * ny;
        q.vx = wx + tx * 0.8f - rest * vn * nx; q.vy = wy + ty * 0.8f - rest * vn * ny;
        const bool sounds = wm == 0 || (wm == 1 && s % 2 == 0) || (wm == 2 && s == 0);   // WALLS: which walls play
        if (vn > 0.35f && sounds) play(q, vn);
      }
    }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {   // balls knock each other, and both sound when they knock hard
      const float dx = b[j].x - b[i].x, dy = b[j].y - b[i].y, d2 = dx * dx + dy * dy; if (d2 >= 4 * RB * RB || d2 < 1e-8f) continue;
      const float d = std::sqrt(d2), nx = dx / d, ny = dy / d, over = 2 * RB - d; b[i].x -= nx * over / 2; b[i].y -= ny * over / 2; b[j].x += nx * over / 2; b[j].y += ny * over / 2;
      const float vn = (b[j].vx - b[i].vx) * nx + (b[j].vy - b[i].vy) * ny;
      if (vn < 0) { b[i].vx += vn * nx; b[i].vy += vn * ny; b[j].vx -= vn * nx; b[j].vy -= vn * ny; if (-vn > 0.6f && wm == 0) { play(b[i], -vn); play(b[j], -vn); } } }
    if (L > 0) for (int i = 0; i < n; ) { if (b[i].age > L) { for (int k = i + 1; k < n; k++) b[k - 1] = b[k]; n--; } else i++; }   // DISSOLVE: gone
    while (n > std::max(1, (int)e[MAXB])) { for (int k = 1; k < n; k++) b[k - 1] = b[k]; n--; }
    disp[0].store(ang); disp[1].store((float)S); disp[2].store((float)n); disp[51].store((float)wm);
    for (int i = 0; i < n; i++) { disp[3 + i * 4].store(b[i].x); disp[4 + i * 4].store(b[i].y); disp[5 + i * 4].store(fadeOf(b[i]) * e[VEL]); disp[6 + i * 4].store(b[i].flash); }
  }
};
static Reg rTombola(new ModuleDef{"tombola", "TOMBOLA", Cat::Trig, {{"ADD", PType::Trig}, {"SHAKE", PType::Trig}}, {{"TRIG", PType::Trig}},
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1),
   fmt(pr("spin", "SPIN", -1.5f, 1.5f, 0.08f), [](float v) { if (std::fabs(v) < 0.005f) return std::string("STOPPED"); char t[16]; std::snprintf(t, 16, "%s%.2f REV/S", v < 0 ? "<" : "", std::fabs(v)); return std::string(t); }),
   fmt(prExp("speed", "SPEED", 0.1f, 2, 1), [](float v) { char t[16]; std::snprintf(t, 16, "%.2fX", v); return std::string(t); }),
   pct("bounce", "BOUNCE", 0.6f), fmt(pct("dslv", "DISSOLVE", 0), [](float v) { return v < 0.01f ? std::string("OFF") : std::to_string((int)std::lround(v * 100)) + "%"; }),
   ST("maxb", "BALLS", 1, 12, 8), ST("sides", "SIDES", 3, 8, 6), OP("walls", "WALLS", {"ALL PLAY", "EVERY 2ND", "ONE WALL"}, 0), OP("freeze", "FREEZE", {"OFF", "ON"}, 0), ACT("clear", "CLEAR NOTES")}, 40, 68, false, [] { return new Tombola(); }});

// ---------------- NOTE LOOPER: records the notes that come in (from MIDI IN or any sequencer) and plays them round ----------------
// Notes are kept in beats: of a clock patched into CLOCK, or of the session's tempo. So patching in a clock (or changing
// the tempo) changes how fast it plays, and the notes keep their places in the bar.
// The first take sets the loop (when RECORD goes off); after that, recording overdubs a new layer. START and LENGTH
// choose which part of what's recorded plays round.
struct NoteLooper : Module {
  enum { REC, THRU, START, LEN, DIR, SPEED, TRANS, QUANT, VEL, CHANCE, UNDO, HALVE, DOUBLE, CLEAR, DRIFT };
  struct Late { float note, vel; double left; }; Late late[64]; int nLate = 0;   // DRIFT: notes held back a moment
  double pos = 0, recPos = 0; bool firstTake = false, wasRec = false, fwd = true; int layer = 0; double last = -1; float cp = 0, ppb = 1;
  NoteLooper() { allocEv(); }
  bool clocked() const { return clockedIn.load(std::memory_order_relaxed) && cp > 0; }
  double beatSecs() const { return clocked() ? cp * ppb : 60.0 / std::max(10.f, eng->tempo.bpm.load(std::memory_order_relaxed)); }
  int count() const { return evN.load(std::memory_order_relaxed); }
  void add(double t, float note, float vel) { const int n = count(); if (n >= EV_MAX) return;
    ev[n * 4].store((float)t); ev[n * 4 + 1].store(note); ev[n * 4 + 2].store(vel); ev[n * 4 + 3].store((float)layer); evN.store(n + 1); }
  void blockStart(int) override { clockedIn.store(patched(1)); }
  void trig(int port, const Event& in) override {
    const double now = t + eng->pos / sr;
    if (port == 1) { if (in.period > 0) cp = in.period; else if (last >= 0) { const float p0 = (float)(now - last); if (p0 > 0.01f && p0 < 8) cp = cp > 0 ? cp * 0.6f + p0 * 0.4f : p0; }
      last = now; ppb = in.ppb > 0 ? in.ppb : (ppb > 0 ? ppb : 1); return; }
    if (port == 2) { p[REC].store(p[REC].load() > 0.5f ? 0.f : 1.f); return; }   // REC in: each trigger starts or stops recording
    if (port != 0) return;
    if (e[THRU] > 0.5f) emit(0, in);   // THRU: you hear what comes in
    if (e[REC] < 0.5f) return;
    double tt = firstTake ? recPos : pos;
    if (e[QUANT] > 0.5f) { static const double G[4] = {0, 0.25, 0.5, 1}; const double g = G[(int)e[QUANT] & 3]; tt = std::round(tt / g) * g;
      if (!firstTake && tt >= e[START] + e[LEN]) tt -= e[LEN]; }
    add(tt, in.note, in.vel);
  }
  void action(const std::string& k) override {
    const int n = count(); const double S = e[START], L = e[LEN];
    if (k == "clear") { evN.store(0); layer = 0; firstTake = false; pos = 0; p[START].store(0); }
    else if (k == "undo") { int top = -1; for (int i = 0; i < n; i++) top = std::max(top, (int)ev[i * 4 + 3].load()); int w = 0;   // the last layer goes
      for (int i = 0; i < n; i++) if ((int)ev[i * 4 + 3].load() != top) { for (int c = 0; c < 4; c++) ev[w * 4 + c].store(ev[i * 4 + c].load()); w++; } evN.store(w); layer = std::max(0, top); }
    else if (k == "halve") p[LEN].store((float)std::max(0.25, L / 2));   // the notes past the end stay, so DOUBLE or LENGTH can bring them back
    else if (k == "double") { int m = n; for (int i = 0; i < n && m < EV_MAX; i++) { const double tt = ev[i * 4].load(); if (tt < S || tt >= S + L) continue;
        ev[m * 4].store((float)(tt + L)); for (int c = 1; c < 4; c++) ev[m * 4 + c].store(ev[i * 4 + c].load()); m++; } evN.store(m); p[LEN].store((float)std::min(128.0, L * 2)); }
  }
  void play(double a0, double a1) {   // the notes between two positions (either way round)
    const double lo = std::min(a0, a1), hi = std::max(a0, a1); const int n = count();
    for (int i = 0; i < n; i++) { const double tt = ev[i * 4].load(std::memory_order_relaxed); if (tt <= lo || tt > hi) continue;
      Event o; o.note = ev[i * 4 + 1].load() + e[TRANS]; o.vel = ev[i * 4 + 2].load() * e[VEL]; if (o.note < 0 || o.note >= 128) continue;
      if (e[DRIFT] > 0.001f && nLate < 64) {   // DRIFT: each note a little late, by a different amount each time round (up to a third of a beat), and a touch softer or louder
        const double d = rng.uni() * rng.uni() * e[DRIFT] * 0.33 * beatSecs() * sr; o.vel = clampf(o.vel * (1 + (rng.uni() - 0.5f) * 0.3f * e[DRIFT]), 0.02f, 1);
        late[nLate++] = {o.note, o.vel, d}; continue; }
      emit(0, o); }
  }
  void render(int a, int b) override {
    for (int k = 0; k < nLate; ) { late[k].left -= (b - a); if (late[k].left <= 0) { eng->pos = std::max(a, b + (int)late[k].left); Event o; o.note = late[k].note; o.vel = late[k].vel; emit(0, o); late[k] = late[--nLate]; } else k++; }
    const bool rec = e[REC] > 0.5f; eng->pos = a;
    if (rec && !wasRec) { if (count() == 0) { firstTake = true; recPos = 0; layer = 0; } else layer++; }   // RECORD on: the first take, or a new layer
    if (!rec && wasRec && firstTake) { firstTake = false; const double L = clocked() ? std::max(1.0, std::round(recPos)) : std::max(0.25, recPos);   // the first take ends: that's the loop
      p[START].store(0); e[START] = 0; p[LEN].store((float)L); e[LEN] = (float)L; pos = 0; fwd = true; }
    wasRec = rec;
    const double step = (b - a) / (sr * beatSecs()) * (e[SPEED] < 0.5f ? 0.5 : e[SPEED] < 1.5f ? 1.0 : 2.0);   // in beats
    if (firstTake) { recPos += step; disp[0].store(0); disp[1].store(1); disp[3].store((float)recPos); }
    else if (count() > 0) {
      const double S = e[START], L = std::max(0.01f, e[LEN]), E = S + L; const int dir = (int)e[DIR];
      if (pos < S || pos > E) pos = fwd ? S : E;   // START or LENGTH moved past the playhead
      if (dir == 0) fwd = true; else if (dir == 1) fwd = false;
      double np = pos + (fwd ? step : -step);
      if (np >= E) { if (dir == 2) { play(pos, E); fwd = false; np = E - (np - E); play(np, E); } else { play(pos, E + 1e-9); np -= L; play(S - 1e-9, np); } }
      else if (np < S) { if (dir == 2) { play(np, pos); fwd = true; np = S + (S - np); play(S, np); } else { play(S - 1e-9, pos); np += L; play(np, E + 1e-9); } }
      else play(pos, np);
      pos = std::max(S, std::min(E, np));
      disp[0].store((float)pos); disp[1].store(rec ? 2.f : 0.f);
    } else { disp[0].store(0); disp[1].store(0); }
    disp[2].store(e[LEN]); disp[4].store(clocked() ? 1.f : 0.f); disp[5].store((float)layer); disp[6].store(e[START]); disp[7].store((float)beatSecs());
  }
};
static std::string beatsText(float v) { char b[24]; std::snprintf(b, 24, v < 10 && std::fabs(v - std::round(v)) > 0.01f ? "%.2f BEATS" : "%.0f BEATS", v); return b; }
static Reg rNoteLooper(new ModuleDef{"nloop", "NOTE LOOPER", Cat::Trig, {{"NOTES", PType::Trig}, {"CLOCK", PType::Trig}, {"REC", PType::Trig}}, {{"NOTES", PType::Trig}},
  {OP("rec", "RECORD", {"OFF", "ON"}, 0), OP("thru", "THRU", {"OFF", "ON"}, 1),
   fine(fmt(pr("start", "START", 0, 128, 0), beatsText)), fmt(prExp("lenb", "LENGTH", 0.25f, 128, 16), beatsText),
   OP("dir", "PLAYS", {"FORWARDS", "BACKWARDS", "PING-PONG"}, 0), OP("speed", "SPEED", {"HALF", "NORMAL", "DOUBLE"}, 1), ST("trans", "TRANSPOSE", -24, 24, 0),
   OP("quant", "QUANTISE", {"OFF", "1/16", "1/8", "1/4"}, 0), pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1),
   ACT("undo", "UNDO LAYER"), ACT("halve", "HALVE"), ACT("double", "DOUBLE"), ACT("clear", "CLEAR"), pct("drift", "DRIFT", 0)}, 30, 68, false, [] { return new NoteLooper(); }});


// ---------------- TIMELINE: a stretch of time (5 minutes, say) with events placed along it ----------------
// Each of its six lanes is an output: patch one into the SWITCH socket of any module (beside its ON or M button) and the
// lane's events turn that module on, off or over at their times. Patched into an ordinary trigger input, an event simply fires it.
// Events are kept as (seconds, lane, what): what is 0 OFF, 1 ON, 2 OVER. The screen adds, moves and removes them.
struct Timeline : Module {
  enum { RUN, LEN, END, SNAP, CLEAR };
  double pos = 0; bool wasRun = false;
  Timeline() { allocEv(); }
  void trig(int port, const Event&) override {
    if (port == 0) { pos = 0; p[RUN].store(1); wasRun = true; }   // START: from the beginning
    else if (port == 1) p[RUN].store(0);                          // STOP
  }
  void action(const std::string& k) override {
    if (k == "clear") evN.store(0);
    else if (k == "rewind") pos = 0;
    else if (k.rfind("seek:", 0) == 0) pos = std::max(0.0, std::min((double)e[LEN], std::atof(k.c_str() + 5)));
  }
  void fire(double lo, double hi, int a, double rate) {   // the events from lo (included) up to hi
    const int n = evN.load(std::memory_order_relaxed);
    for (int i = 0; i < n; i++) { const double tt = ev[i * 4].load(std::memory_order_relaxed); if (tt < lo || tt >= hi) continue;
      const int lane = (int)ev[i * 4 + 1].load(std::memory_order_relaxed); if (lane < 0 || lane >= 6) continue;
      eng->pos = a + (int)((tt - lo) * rate); Event o; o.sw = (int)ev[i * 4 + 2].load(std::memory_order_relaxed); emit(lane, o); }
  }
  void render(int a, int b) override {
    const bool run = e[RUN] > 0.5f; const double L = std::max(1.f, e[LEN]);
    if (run && !wasRun && pos >= L - 1e-6) pos = 0;   // RUN again after it stopped at the end: from the beginning
    wasRun = run;
    if (pos > L) pos = L;
    if (run) { const double dt = (b - a) / sr, np = pos + dt;
      if (np < L) { fire(pos, np, a, sr); pos = np; }
      else { fire(pos, L, a, sr);
        if (e[END] > 0.5f) { const double r = np - L; fire(0, r, a + (int)((L - pos) * sr), sr); pos = r; }   // LOOP: round again
        else { pos = L; p[RUN].store(0); wasRun = false; } } }   // STOP at the end
    eng->pos = a;
    disp[0].store((float)pos); disp[1].store(run ? 1.f : 0.f); disp[2].store((float)L);
  }
};
static std::string mmss(float v) { const int s = (int)std::lround(v); char b[16]; std::snprintf(b, 16, "%d:%02d", s / 60, s % 60); return b; }
static Reg rTimeline(new ModuleDef{"timeline", "TIMELINE", Cat::Trig, {{"START", PType::Trig}, {"STOP", PType::Trig}},
  {{"1", PType::Trig}, {"2", PType::Trig}, {"3", PType::Trig}, {"4", PType::Trig}, {"5", PType::Trig}, {"6", PType::Trig}},
  {playBtn(OP("run", "RUN", {"OFF", "ON"}, 0)), [] { auto q = fmt(pr("len", "LENGTH", 10, 1800, 300), mmss); q.step = 5; return q; }(),
   OP("end", "AT THE END", {"STOP", "LOOP"}, 1), OP("snap", "SNAP", {"OFF", "1 S", "5 S", "10 S", "30 S"}, 1), ACT("clear", "CLEAR EVENTS")}, 58, 150, false, [] { return new Timeline(); }});

}  // namespace tb
