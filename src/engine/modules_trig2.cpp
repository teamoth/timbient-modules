// Timbient native: more rhythm modules (bursts, walker, trig tools, tracker).
#include "params.h"
#include <cstring>

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
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1), ST("hits", "HITS", 2, 9, 5), pr("clump", "CLUMP", 0, 0.9f, 0.5f), T("len", "LENGTH", 0.4f, 6, 1.8f), T("rest", "REST", 0.2f, 12, 3), ST("range", "RANGE", 1, 8, 4), OCTP("oct", 1, 8, 4)},
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
  {pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1), pct("push", "PUSH", 0.4f), pct("glide", "SLIDE", 0.5f), ST("range", "RANGE", 1, 16, 10), OCTP("oct", 1, 8, 4)},
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
  static constexpr int COLS = 4;   // always four columns
  int cols() const { return COLS; }
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
      if (nt >= 0 && rng.uni() < e[CHANCE]) { Event ev; ev.note = nt; ev.vel = e[VEL]; /* every step at VELOCITY: patch into its socket to move it */ ev.period = per; ev.ppb = ppb;
        emit(0, ev); emit(2 + c, ev); } }   // TRIG has every column; outputs 1 to 4 one each (CHANCE rolled once, for both)
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
static Reg rTracker(new ModuleDef{"tracker", "TRACKER", Cat::Trig, {{"CLOCK", PType::Trig}, {"STEP ENTER", PType::Trig}},
  {{"TRIG", PType::Trig}, {"PULSE", PType::Trig}, {"CHN 1", PType::Trig}, {"CHN 2", PType::Trig}, {"CHN 3", PType::Trig}, {"CHN 4", PType::Trig}},
  {only(tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), "free"), only(fmt(ST("bpm", "BPM", 20, 240, 90), [](float v) { return std::to_string((int)v); }), "own"),
   only(OP("div", "STEP", {"1/4", "1/8", "1/16"}, 1), "free"), ST("len", "LENGTH", 1, 128, 16), noMod(ST("estep", "STEP JUMP", 0, 8, 1)), OP("rec", "STEP RECORD", {"OFF", "ON"}, 1), pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1),
   ACT("del", "CLEAR STEP"), ACT("clear", "CLEAR ALL")},
  85, 92, false, [] { return new Tracker(); }});
}  // namespace tb

namespace tb {

// ---------------- TOMBOLA: a slowly turning drum with note-balls bouncing inside; a ball plays its note when it hits a wall ----------------
struct Tombola : Module {
  // notes come in only through ADD (another sequencer, say a NOTE GRID); each becomes a ball in the turning drum
  enum { VEL, CHANCE, SPIN, SPEED, BOUNCE, DISSOLVE, MAXB, SIDES, WALLS, FREEZE, CLEAR };
  static constexpr int MAXN = 12; static constexpr float RB = 0.07f, G = 3.2f;
  struct Ball { float x, y, vx, vy, age, cool, flash; float midi; };
  Ball b[MAXN]; int n = 0; float ang = 0; int seen = 0;
  Tombola() { allocEv(); }
  // its balls' notes are kept in ev (saved as the session's "notes"), so a session opens with the same notes in the drum
  void keepBalls() {
    const int v = seqVer.load(); if (v & 1) return;   // the screen is writing them in
    if (v != seen) { seen = v; const int m = std::min(MAXN, evN.load()); n = 0; for (int i = 0; i < m; i++) { const float midi = ev[i * 4].load(); if (midi >= 0 && midi < 128) addBall(midi); } return; }
    for (int i = 0; i < n; i++) if (ev[i * 4].load(std::memory_order_relaxed) != b[i].midi) { ev[i * 4].store(b[i].midi); ev[i * 4 + 1].store(0); ev[i * 4 + 2].store(0); ev[i * 4 + 3].store(0); }
    if (evN.load(std::memory_order_relaxed) != n) evN.store(n);
  }
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
    keepBalls();
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
// Notes are kept in beats: of a clock patched into CLOCK, or of the session's tempo (TEMPO SHARED), or of its own BPM.
// So patching in a clock or changing the BPM changes how fast it plays, and the notes keep their places in the bar:
// record at 90, play at 45 for half speed; record more at 45, play at 22 for half again.
// The first take sets the loop (when RECORD goes off); after that, recording overdubs a new layer. START and LENGTH
// choose which part of what's recorded plays round.
struct NoteLooper : Module {
  enum { REC, THRU, START, LEN, DIR, SYNC, BPM, TRANS, QUANT, VEL, CHANCE, UNDO, RRHY, RORD, URAND, CLEAR, DRIFT };
  static constexpr int SNAPS = 8;   // RANDOM RHYTHM / RANDOM ORDER: what it was, for UNDO RANDOM (the last 8)
  std::vector<float> snap[SNAPS]; int snapN[SNAPS] = {}; int snaps = 0, snapTop = 0;
  struct Late { float note, vel; double left; }; Late late[64]; int nLate = 0;   // DRIFT: notes held back a moment
  double pos = 0, recPos = 0; bool firstTake = false, wasRec = false, fwd = true; int layer = 0; double last = -1; float cp = 0, ppb = 1;
  NoteLooper() { allocEv(); for (auto& v : snap) v.assign((size_t)EV_MAX * 4, 0.f); }
  bool clocked() const { return clockedIn.load(std::memory_order_relaxed) && cp > 0; }
  double beatSecs() const { return clocked() ? cp * ppb : 60.0 / std::max(10.f, e[SYNC] > 0.5f ? eng->tempo.bpm.load(std::memory_order_relaxed) : e[BPM]); }
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
    else if (k == "rrhythm" || k == "rorder") shuffle(k == "rrhythm", S, L);
    else if (k == "urand") { if (snaps == 0) return; snapTop = (snapTop + SNAPS - 1) % SNAPS; snaps--; const int m = snapN[snapTop];
      for (int i = 0; i < m * 4; i++) ev[i].store(snap[snapTop][i]); evN.store(m); }
  }
  // RANDOM RHYTHM: the notes in the loop (chords together, in the same order) on new places on the QUANTISE grid (1/16 when
  // it's off), the first still on the downbeat. RANDOM ORDER: the same places, the notes (or chords) shuffled among them.
  void shuffle(bool rhythm, double S, double L) {
    const int n = count(); if (n == 0) return;
    for (int i = 0; i < n * 4; i++) snap[snapTop][i] = ev[i].load(); snapN[snapTop] = n; snapTop = (snapTop + 1) % SNAPS; snaps = std::min(SNAPS, snaps + 1);
    struct Note { int i; double t; }; std::vector<Note> in; for (int i = 0; i < n; i++) { const double tt = ev[i * 4].load(); if (tt >= S && tt < S + L) in.push_back({i, tt}); }
    if (in.empty()) return;
    std::sort(in.begin(), in.end(), [](const Note& a, const Note& b) { return a.t < b.t; });
    std::vector<std::vector<int>> groups; std::vector<double> times; double gt = -1e9;   // chords: notes within a fiftieth of a beat
    for (auto& nt : in) { if (nt.t - gt > 0.02) { groups.push_back({}); times.push_back(nt.t); gt = nt.t; } groups.back().push_back(nt.i); }
    const int G = (int)groups.size(); std::vector<double> to(G);
    if (rhythm) { static const double GR[4] = {0.25, 0.25, 0.5, 1}; double g = GR[(int)e[QUANT] & 3];
      while (g > 1.0 / 32 && std::floor(L / g + 1e-6) < G * 1.5) g /= 2;   // lots of notes for the loop: a finer grid, so there's room to move them
      const int slots = std::max(1, (int)std::floor(L / g + 1e-6));
      for (int tries = 0; tries < 8; tries++) {   // a rhythm that's different from the one there now
        std::vector<int> pick; if (G >= slots) { for (int k = 0; k < G; k++) pick.push_back(k * slots / G); }   // still more notes than places: spread them evenly
        else { std::vector<int> rest; for (int k = 1; k < slots; k++) rest.push_back(k);
          for (int k = (int)rest.size() - 1; k > 0; k--) std::swap(rest[k], rest[(int)(rng.uni() * (k + 1)) % (k + 1)]);
          pick.push_back(0); for (int k = 0; k < G - 1; k++) pick.push_back(rest[k]); std::sort(pick.begin(), pick.end()); }
        bool same = true; for (int k = 0; k < G; k++) { to[k] = S + pick[k] * g; if (std::fabs(to[k] - times[k]) > 1e-4) same = false; }
        if (!same || G >= slots) break; } }
    else { std::vector<int> ord(G); for (int k = 0; k < G; k++) ord[k] = k; for (int k = G - 1; k > 0; k--) std::swap(ord[k], ord[(int)(rng.uni() * (k + 1)) % (k + 1)]);
      for (int k = 0; k < G; k++) to[ord[k]] = times[k]; }
    for (int k = 0; k < G; k++) for (int i : groups[k]) ev[i * 4].store((float)to[k]);
  }
  void play(double a0, double a1) {   // the notes between two positions (either way round)
    const double lo = std::min(a0, a1), hi = std::max(a0, a1); const int n = count();
    for (int i = 0; i < n; i++) { const double tt = ev[i * 4].load(std::memory_order_relaxed); if (tt <= lo || tt > hi) continue;
      Event o; o.note = ev[i * 4 + 1].load() + e[TRANS]; o.vel = ev[i * 4 + 2].load() * e[VEL]; if (o.note < 0 || o.note >= 128) continue;
      if (rng.uni() >= e[CHANCE]) continue;   // CHANCE: each note, each time round (notes played in through THRU always pass)
      if (e[DRIFT] > 0.001f && nLate < 64) {   // DRIFT: each note a little late, by a different amount each time round (up to a third of a beat at 100%, two thirds at 200%), and a touch softer or louder
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
    const double step = (b - a) / (sr * beatSecs());   // in beats
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
   OP("dir", "PLAYS", {"FORWARDS", "BACKWARDS", "PING-PONG"}, 0),
   only(tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), "free"), only(fmt(ST("bpm", "BPM", 20, 240, 90), [](float v) { return std::to_string((int)v); }), "own"),
   ST("trans", "TRANSPOSE", -24, 24, 0),
   OP("quant", "QUANTISE", {"OFF", "1/16", "1/8", "1/4"}, 0), pct("vel", "VELOCITY", 1), pct("chance", "CHANCE", 1),
   ACT("undo", "UNDO LAYER"), ACT("rrhythm", "RANDOM RHYTHM"), ACT("rorder", "RANDOM ORDER"), ACT("urand", "UNDO RANDOM"), ACT("clear", "CLEAR"),
   pctX("drift", "DRIFT", 2, 0)}, 30, 68, false, [] { return new NoteLooper(); }});


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
  {playBtn(OP("run", "RUN", {"OFF", "ON"}, 0)), [] { auto q = noMod(fmt(pr("len", "LENGTH", 10, 1800, 300), mmss)); q.step = 5; return q; }(),
   OP("end", "AT THE END", {"STOP", "LOOP"}, 1), OP("snap", "SNAP", {"OFF", "1 S", "5 S", "10 S", "30 S"}, 1), ACT("clear", "CLEAR EVENTS")}, 58, 150, false, [] { return new Timeline(); }});


// ---------------- MUTE SEQUENCER: four lanes of steps that switch modules on and off ----------------
// Each lane is an output: patch it into a module's SWITCH socket. A lit step is ON (playing), a dark one OFF; the lane only
// sends when it changes, and says exactly ON or OFF, so a module can't end up the wrong way round. Each lane has its own
// LENGTH, so they can run in different time signatures against each other. HOLD TO PLAY: hold the button for ON and let go
// for OFF while it plays; one time round the lane is written from that, onto its steps.
struct MuteSeq : Module {
  enum { SYNC, BPM, DIV, L1, L2, L3, L4, CHANCE, SWING, RLANE, CLEAR };
  static constexpr int LANES = 4;
  long n = -1; double next = -1, lastStep = -1, per = 0.5; int last[LANES] = {-1, -1, -1, -1}; ClockIn2 ck;
  bool held = false; int recLeft = 0, recLane = 0;
  MuteSeq() { allocSeq(); for (int l = 0; l < LANES; l++) for (int k = 0; k < SEQ_MAX; k++) seq[si(k, l)].store(l == 0 && k % 8 >= 5 ? -1.f : 1.f); }   // lanes start all ON (lane 1: five on, three off, to show how)
  int len(int l) const { return std::max(1, std::min(32, (int)e[L1 + l])); }
  int laneStep(int l) const { return n < 0 ? 0 : (int)(n % len(l)); }
  void step() {
    n++;
    if (recLeft > 0) { seq[si(laneStep(recLane), recLane)].store(held ? 1.f : -1.f); recLeft--; seqVer.fetch_add(1); }   // HOLD TO PLAY: this step, as you're holding it
    lastStep = t + eng->pos / sr;
    for (int l = 0; l < LANES; l++) { const bool on = seq[si(laneStep(l), l)].load(std::memory_order_relaxed) >= 0 && (rng.uni() < e[CHANCE]);
      if ((int)on != last[l]) { last[l] = on; Event ev; ev.sw = on ? 1 : 0; emit(l, ev); } }
  }
  void blockStart(int) override { clockedIn.store(patched(0)); }
  void trig(int port, const Event& ev) override { if (port == 0) { ck.tick(t + eng->pos / sr, ev); if (!mute.load(std::memory_order_relaxed)) { if (ck.cp > 0) per = ck.cp; step(); } } }
  void action(const std::string& k) override {
    const int rl = std::max(0, std::min(LANES - 1, (int)e[RLANE])); const double now = t, into = lastStep < 0 ? 1 : (now - lastStep) / std::max(0.01, per);
    if (k == "hold:1") { held = true; if (recLeft == 0) { recLane = rl; recLeft = len(rl); } if (into < 0.5) seq[si(laneStep(recLane), recLane)].store(1); }   // pressed a little late: this step counts
    else if (k == "hold:0") { held = false; if (recLeft > 0 && into < 0.5) seq[si(laneStep(recLane), recLane)].store(-1); }
    else if (k == "clear") { for (int i = 0; i < SEQ_MAX * SEQ_COLS; i++) seq[i * 2].store(-1); seqVer.fetch_add(1); }
  }
  void render(int a, int b) override {
    if (mute.load(std::memory_order_relaxed)) { n = -1; next = -1; for (auto& l : last) l = -1; recLeft = 0; }   // the footer's ON/OFF starts and stops it
    else if (!clockedIn.load()) {
      const double base = 60.0 / (e[SYNC] > 0.5f ? eng->tempo.bpm.load(std::memory_order_relaxed) : e[BPM]) / TRK_DIV[(int)e[DIV]];
      for (int i = a; i < b; i++) { const double now = t + i / sr; if (next < 0 || now - next > 1) next = now;
        while (now >= next) { eng->pos = i; step(); per = base * ((n % 2 == 0) ? 1 + e[SWING] * 0.5 : 1 - e[SWING] * 0.5); next += per; } }   // SWING: long, short, long, short
    }
    for (int l = 0; l < LANES; l++) { disp[l].store((float)laneStep(l)); disp[4 + l].store((float)last[l]); }
    disp[8].store((float)recLeft); disp[11].store((float)recLane); disp[9].store(held ? 1.f : 0.f); disp[10].store(n < 0 ? 0.f : 1.f);
  }
};
static Reg rMuteSeq(new ModuleDef{"mutes", "MUTE SEQUENCER", Cat::Trig, {{"CLOCK", PType::Trig}},
  {{"LANE 1", PType::Trig}, {"LANE 2", PType::Trig}, {"LANE 3", PType::Trig}, {"LANE 4", PType::Trig}},
  {only(tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), "free"), only(fmt(ST("bpm", "BPM", 20, 240, 90), [](float v) { return std::to_string((int)v); }), "own"),
   only(OP("div", "STEP", {"1/4", "1/8", "1/16"}, 1), "free"),
   stepper(noMod(ST("len1", "LENGTH 1", 1, 32, 16))), stepper(noMod(ST("len2", "LENGTH 2", 1, 32, 16))), stepper(noMod(ST("len3", "LENGTH 3", 1, 32, 16))), stepper(noMod(ST("len4", "LENGTH 4", 1, 32, 16))),
   pct("chance", "CHANCE", 1), only(pct("swing", "SWING", 0), "free"), OP("rlane", "HOLD LANE", {"1", "2", "3", "4"}, 0), ACT("clear", "CLEAR ALL")}, 40, 130, false, [] { return new MuteSeq(); }});

// ---------------- KEY JOURNEY: moves through keys and scales musically, on the bar ----------------
// PATH: FIFTHS (round the circle), MODES (brighter or darker: Lydian, major, Mixolydian, Dorian, minor, Phrygian, one
// note changing each time), RELATIVE (major and its relative minor: the same notes), PARALLEL (major and minor on the
// same root), THIRDS (chromatic mediants: a third away, same kind of scale) or WANDER (any of these, chosen at random,
// with HOME PULL drawing it back towards where it began). It moves every few bars (EVERY, with CHANCE), or on the bar
// after a NEXT trigger. Its KEY output goes into sequencers' KEY sockets; MOVES SESSION KEY moves the whole session.
// SOFT CHANGE: for a bar after each move, the sequencers keep to the notes the old and new keys share.
static int pcMask(int r, int sc) { int m = 0; for (int st : scales()[sc].steps) m |= 1 << ((r + st) % 12); return m; }
static int scaleNamed(const char* n) { for (size_t i = 0; i < scales().size(); i++) if (!std::strcmp(scales()[i].name, n)) return (int)i; return 0; }
static int bitsOf(int x) { int n = 0; for (; x; x &= x - 1) n++; return n; }
struct KeyJourney : Module {
  enum { PATH, DIR, EVERY, CHANCE, PULL, HKEY, HSCALE, SOFT, MOVES, RESET };
  int cr = 0, cs = 0, nr = 0, ns = 0, pr0 = -1, ps0 = -1, hr = 0, hs = 0, modeDir = 1, barsLeft = 4; bool started = false, pending = false;
  double barPos = 0, softUntil = -1; long pulses = 0; ClockIn2 ck;
  static int every(float v) { static const int E[6] = {0, 1, 2, 4, 8, 16}; return E[std::max(0, std::min(5, (int)v))]; }
  int dirSign() { const int d = (int)e[DIR]; return d == 0 ? 1 : d == 1 ? -1 : (rng.uni() < 0.5f ? 1 : -1); }
  bool session() const { return e[MOVES] > 0.5f; }
  void home() { const int k = (int)e[HKEY], q = (int)e[HSCALE]; hr = k >= 1 ? k - 1 : eng->key.root.load(std::memory_order_relaxed); hs = q >= 1 && q <= (int)scales().size() ? q - 1 : eng->key.scale.load(std::memory_order_relaxed); }
  // the moves, each from (r, s) to a new key
  void fifth(int r, int s, int d, int& r2, int& s2) { r2 = (r + (d > 0 ? 7 : 5)) % 12; s2 = s; }
  void third(int r, int s, int d, int& r2, int& s2) { const int up[2] = {4, 3}, dn[2] = {8, 9}; const int k = rng.uni() < 0.6f ? 0 : 1; r2 = (r + (d > 0 ? up[k] : dn[k])) % 12; s2 = s; }
  bool relative(int r, int s, int& r2, int& s2) {
    static const char* PAIRS[][2] = {{"MAJOR", "NAT MINOR"}, {"MAJ PENTA", "MIN PENTA"}}; const std::string n = scales()[s].name;
    for (auto& p : PAIRS) { if (n == p[0]) { r2 = (r + 9) % 12; s2 = scaleNamed(p[1]); return true; } if (n == p[1]) { r2 = (r + 3) % 12; s2 = scaleNamed(p[0]); return true; } }
    const int m = pcMask(r, s); for (size_t q = 0; q < scales().size(); q++) for (int k = 1; k < 12; k++) if (pcMask((r + k) % 12, (int)q) == m) { r2 = (r + k) % 12; s2 = (int)q; return true; }   // the same notes from another root
    return false; }
  void parallel(int r, int s, int& r2, int& s2) {
    const auto& st = scales()[s].steps; const bool maj3 = std::find(st.begin(), st.end(), 4) != st.end(); const std::string n = scales()[s].name; r2 = r;
    if (st.size() == 5) s2 = scaleNamed(maj3 ? "MIN PENTA" : "MAJ PENTA"); else s2 = scaleNamed(n == "NAT MINOR" ? "MAJOR" : maj3 && n != "MAJOR" ? "MAJOR" : maj3 ? "NAT MINOR" : n == "MAJOR" ? "NAT MINOR" : "NAT MINOR");
    if (!maj3) s2 = scaleNamed(st.size() == 5 ? "MAJ PENTA" : "MAJOR"); }
  void mode(int r, int s, int d, int& r2, int& s2) {   // one step brighter (d > 0) or darker, on the same root; at either end it turns back
    static const char* M[6] = {"LYDIAN", "MAJOR", "MIXOLYDIAN", "DORIAN", "NAT MINOR", "PHRYGIAN"}; int i = -1; for (int k = 0; k < 6; k++) if (scales()[s].name == std::string(M[k])) i = k;
    if (i < 0) { const auto& st = scales()[s].steps; i = std::find(st.begin(), st.end(), 4) != st.end() ? 1 : 4; }
    int j = i - d; if (j < 0 || j > 5) j = i + d; r2 = r; s2 = scaleNamed(M[std::max(0, std::min(5, j))]); }
  int dist(int r, int s) const { const int m = pcMask(r, s), h = pcMask(hr, hs); return bitsOf(m ^ h) + (r != hr ? 1 : 0) + (s != hs ? 1 : 0); }
  void plan() {   // choose the next key (shown before it comes)
    const int path = (int)e[PATH]; int r2 = cr, s2 = cs;
    if (path == 0) fifth(cr, cs, dirSign(), r2, s2);
    else if (path == 1) { const int d0 = (int)e[DIR]; int d = d0 == 2 ? dirSign() : modeDir * (d0 == 0 ? 1 : -1); mode(cr, cs, d, r2, s2);
      if (s2 == cs) { modeDir = -modeDir; mode(cr, cs, -d, r2, s2); } else if (d0 != 2) { int t1, t2; mode(r2, s2, d, t1, t2); if (t2 == s2 || (t1 == cr && t2 == cs)) modeDir = -modeDir; } }   // ping-pong at the ends
    else if (path == 2) { if (!relative(cr, cs, r2, s2)) fifth(cr, cs, 1, r2, s2); }
    else if (path == 3) parallel(cr, cs, r2, s2);
    else if (path == 4) third(cr, cs, dirSign(), r2, s2);
    else {   // WANDER: every kind of move, weighted; HOME PULL favours moves back towards home
      struct C { int r, s; float w; } c[8]; int n = 0; const int d0 = (int)e[DIR]; const float up = d0 == 0 ? 2.f : d0 == 1 ? 0.5f : 1.f;
      auto add = [&](int r, int s, float w) { if ((r == cr && s == cs) || n >= 8) return; c[n++] = {r, s, w}; };
      int a, b; fifth(cr, cs, 1, a, b); add(a, b, 4 * up); fifth(cr, cs, -1, a, b); add(a, b, 4 / up);
      if (relative(cr, cs, a, b)) add(a, b, 3); parallel(cr, cs, a, b); add(a, b, 2);
      mode(cr, cs, 1, a, b); add(a, b, 1.5f * up); mode(cr, cs, -1, a, b); add(a, b, 1.5f / up); third(cr, cs, rng.uni() < 0.5f ? 1 : -1, a, b); add(a, b, 1);
      const float pull = 1 + 4 * e[PULL]; const int d = dist(cr, cs); float tot = 0;
      for (int i = 0; i < n; i++) { c[i].w *= std::pow(pull, (float)(d - dist(c[i].r, c[i].s)) / 2); if (c[i].r == pr0 && c[i].s == ps0) c[i].w *= 0.4f; tot += c[i].w; }
      float x = rng.uni() * tot; for (int i = 0; i < n; i++) { x -= c[i].w; if (x <= 0 || i == n - 1) { r2 = c[i].r; s2 = c[i].s; break; } } }
    nr = r2; ns = s2; }
  void show() { keyOut = cr; scaleOut = cs; if (session()) { eng->key.root.store(cr, std::memory_order_relaxed); eng->key.scale.store(cs, std::memory_order_relaxed); } }
  void move() {
    if (rng.uni() >= e[CHANCE]) { plan(); return; }   // not this time (a fresh next key, so WANDER varies)
    const int was = pcMask(cr, cs); pr0 = cr; ps0 = cs; cr = nr; cs = ns; const int now = pcMask(cr, cs), common = was & now;
    softUntil = e[SOFT] > 0.5f && bitsOf(common) >= 3 && common != now ? t + eng->pos / sr + barSecs() : -1; softOut = softUntil > 0 ? common : 0;
    show(); plan();
    Event ev; ev.note = 60 + cr; ev.vel = 1; emit(0, ev); emit(1, ev); }
  double barSecs() const { return ck.cp > 0 && clockedIn.load(std::memory_order_relaxed) ? ck.cp * ck.ppb * 4 : 240.0 / std::max(10.f, eng->tempo.bpm.load(std::memory_order_relaxed)); }
  void bar() {   // a bar begins: move if it's time
    if (mute.load(std::memory_order_relaxed)) return;
    if (pending) { pending = false; barsLeft = every(e[EVERY]); move(); return; }
    const int ev = every(e[EVERY]); if (!ev) return; if (--barsLeft <= 0) { barsLeft = ev; move(); } }
  void start() { home(); cr = hr; cs = hs; started = true; barsLeft = every(e[EVERY]); show(); plan(); }
  void blockStart(int n) override {
    clockedIn.store(patched(1));
    if (!started) start();
    if (session() && (eng->key.root.load(std::memory_order_relaxed) != cr || eng->key.scale.load(std::memory_order_relaxed) != cs)) { cr = eng->key.root.load(); cs = eng->key.scale.load(); plan(); }   // the session's key was changed by hand: go on from there
    keyOut = cr; scaleOut = cs;
    if (softUntil > 0 && t >= softUntil) { softUntil = -1; softOut = 0; }
    if (session()) eng->key.soft = softOut;
    if (!clockedIn.load(std::memory_order_relaxed)) { barPos += n / sr / barSecs(); if (barPos >= 1) { barPos -= std::floor(barPos); bar(); } }
    publish();
  }
  void trig(int port, const Event& ev) override {
    if (port == 0) { pending = true; return; }   // NEXT: on the next bar
    if (port == 1) { ck.tick(t + eng->pos / sr, ev); pulses++; const long per = std::max(1L, std::lround(ck.ppb * 4)); if (pulses % per == 0) bar(); }
  }
  void action(const std::string& k) override { if (k == "reset") { home(); pr0 = cr; ps0 = cs; cr = hr; cs = hs; softUntil = -1; softOut = 0; barsLeft = every(e[EVERY]); show(); plan(); Event ev; ev.note = 60 + cr; emit(1, ev); } }
  void publish() { const float v[8] = {(float)cr, (float)cs, (float)nr, (float)ns, every(e[EVERY]) ? (float)barsLeft : -1.f, pending ? 1.f : 0.f, softUntil > 0 ? 1.f : 0.f, clockedIn.load() ? -1.f : (float)barPos}; for (int i = 0; i < 8; i++) disp[i].store(v[i], std::memory_order_relaxed); }
  ~KeyJourney() override { if (session()) eng->key.soft = 0; }
};
static std::vector<std::string> keyNamesH() { std::vector<std::string> v = {"SESSION"}; for (int i = 0; i < 12; i++) v.push_back(NOTE_NAMES[i]); return v; }
static std::vector<std::string> scaleNamesH() { std::vector<std::string> v = {"SESSION"}; for (auto& x : scales()) v.push_back(x.name); return v; }
static Reg rJourney(new ModuleDef{"journey", "KEY JOURNEY", Cat::Trig, {{"NEXT", PType::Trig}, {"CLOCK", PType::Trig}}, {{"KEY", PType::Trig}, {"CHANGED", PType::Trig}},
  {OP("path", "PATH", {"FIFTHS", "MODES", "RELATIVE", "PARALLEL", "THIRDS", "WANDER"}, 5), OP("dir", "DIRECTION", {"UP", "DOWN", "BOTH"}, 2),
   OP("every", "EVERY", {"NEXT ONLY", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS"}, 3), pct("chance", "CHANCE", 1), only(pct("pull", "HOME PULL", 0.5f), "wander"),
   OP("hkey", "HOME KEY", keyNamesH(), 0), OP("hscale", "HOME SCALE", scaleNamesH(), 0), OP("soft", "SOFT CHANGE", {"OFF", "ON"}, 1),
   OP("moves", "MOVES", {"PATCHED", "SESSION KEY"}, 0), ACT("reset", "GO HOME")}, 25, 70, false, [] { return new KeyJourney(); }});

}  // namespace tb
