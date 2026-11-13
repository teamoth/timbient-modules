// Timbient native: modules that play recorded sound: SAMPLE, GRAIN LOOPER, and STRETCH (a paulstretch of what comes in).
#include "params.h"

namespace tb {

static inline float readBuf(const std::vector<float>& d, double pos) {   // linear interpolation, silent outside
  if (pos < 0) return 0; const size_t i = (size_t)pos; if (i + 1 >= d.size()) return 0; const float f = (float)(pos - i); return d[i] + (d[i + 1] - d[i]) * f;
}

// ---------------- SAMPLE: plays recorded sound, in four ways ----------------
// ONE: one sound, a region of it. SLICES: one sound cut into equal pieces; each note (by scale step) picks a piece.
// KIT: up to eight sounds, each note (by scale step) picks one, always at its own pitch.
// MULTI: up to eight recordings of one instrument, each with the note it was recorded at (ROOT); a note uses the nearest and is shifted the rest of the way.
// It loops (crossfading each pass) or plays once per trigger; the PLAY style runs forwards, backwards, back and forth, or from a random place.
struct Sample : Module {
  enum { LOAD, KIND, SLICES, MODE, NOTES, START, LEN, PITCH, STYLE, ATK, REL, FADE, DRIFT, CUT, RES, VSENS, WIDTH, LEVEL_, EDIT_, EVERY };
  long clkN = 0;
  static constexpr int NS = 8;
  AudioBuf* buf[NS] = {nullptr};
  struct Seg { bool on = false, rev = false; const AudioBuf* b = nullptr; double pos = 0, inc = 1; long t = 0, d = 0, fi = 1, fo = 1; float gain = 1, gl = 1, gr = 1, fadeV = 1, fadeK = 0; };
  Seg vo[12]; double nextAt = -1, wob = 0; float noteIn = -1, loopVel = 1, dirty = 0; bool looping = false, bounce = false; int slicePos = 0; float sig[5] = {-9, -9, -9, -9, -9}; Biquad lp; float lpSig[2] = {-1, -1};
  Sample() { for (auto& r : roots) r.store(60); }
  ~Sample() override { for (auto* b : buf) delete b; }
  AudioBuf* swapBuf(AudioBuf* b, int slot) override { if (slot < 0 || slot >= NS) return b; AudioBuf* old = buf[slot]; buf[slot] = b; for (auto& v : vo) if (v.b == old) v.on = false; nextAt = -1; wob = 0; return old; }
  void fadeAll(float secs) { for (auto& v : vo) if (v.on && v.fadeK == 0) v.fadeK = 1.f / std::max(1.f, secs * sr); }
  int count() const { int n = 0; for (auto* b : buf) if (b) n++; return n; }
  const AudioBuf* nth(int k) const { int n = 0; for (auto* b : buf) if (b) { if (n == k) return b; n++; } return nullptr; }
  int stepOf(float nt) const { return nearDeg(nt, root(), scale()) - degOf(4, 0, scale()); }   // scale steps above the key's root in octave 4
  // which sound, which part of it, and how fast, for a note (nt < 0: no note, the free-running loop)
  bool pick(float nt, const AudioBuf*& b, double& s, double& len, float& rate) {
    const int kind = (int)e[KIND], n = count(); if (!n) return false;
    float shift = std::pow(2.f, e[PITCH] / 12.f);
    if (kind == 3 && nt >= 0) {   // MULTI: the nearest ROOT
      int best = -1; float bd = 1e9f; int k = 0; for (int i = 0; i < NS; i++) if (buf[i]) { const float d = std::fabs(nt - roots[i].load()); if (d < bd) { bd = d; best = i; } k++; }
      b = buf[best]; shift *= std::pow(2.f, (nt - roots[best].load()) / 12.f);
    } else if (kind == 2) { const int st = nt >= 0 ? stepOf(nt) : slicePos++; b = nth(((st % n) + n) % n); }   // KIT: one sound per scale step
    else { b = nth(0); if (e[NOTES] > 0.5f && nt >= 0) shift *= std::pow(2.f, (nt - note(4, 0)) / 12.f); }
    const double dur = b->dur();
    if (kind == 1) {   // SLICES
      const int N = std::max(1, (int)e[SLICES]), k = nt >= 0 ? stepOf(nt) : (e[STYLE] > 2.5f ? (int)(rng.uni() * N) : slicePos++);
      const int sl = ((k % N) + N) % N; s = dur * sl / N; len = std::max(0.02, dur / N * e[LEN]);
    } else {
      s = clampf((float)(e[START] * dur + wob), 0, (float)std::max(0.0, dur - 0.05)); len = std::max(0.05, e[LEN] * (dur - s));
      if (e[STYLE] > 2.5f) { const double room = std::max(0.0, dur - len); s = rng.uni() * room; }   // RANDOM: anywhere a pass of that length fits
    }
    rate = clampf(shift, 0.0625f, 16); return true;
  }
  void seg(float nt, float gain, double& d, double& f) {
    const AudioBuf* b; double s, len; float rate; d = 0.2; f = 0.01;
    if (!pick(nt, b, s, len, rate)) return;
    const bool once = e[MODE] > 0.5f; d = len / rate; f = once ? 0 : std::min((double)e[FADE], d / 2);
    Seg* v = nullptr; for (auto& x : vo) if (!x.on) { v = &x; break; }
    if (!v) { v = &vo[0]; for (auto& x : vo) if (x.t > v->t) v = &x; }   // twelve at once at most: the oldest goes
    const int style = (int)e[STYLE]; bool rv = style == 1; if (style == 2) { bounce = !bounce; rv = bounce; }   // BOUNCE: alternate directions
    v->on = true; v->b = b; v->rev = rv; v->inc = rate * b->sr / sr; v->pos = rv ? (s + len) * b->sr : s * b->sr;
    v->t = 0; v->d = (long)(d * sr);
    if (once) { v->fi = std::max(1L, (long)(std::min((double)e[ATK], d / 2) * sr)); v->fo = std::max(1L, (long)(std::min((double)e[REL], d / 2) * sr)); }
    else { v->fi = v->fo = std::max(1L, (long)(f * sr)); }
    v->gain = gain; v->fadeV = 1; v->fadeK = 0; panGains(rng.bi() * e[WIDTH], v->gl, v->gr);
    if (e[DRIFT] > 0 && (int)e[KIND] != 1) { const double dur = b->dur(); wob = clampf((float)(wob + rng.bi() * e[DRIFT] * std::min(len, 4.0) * 0.5), (float)(-e[START] * dur), (float)((1 - e[START]) * dur * 0.9)); } else wob = 0;
  }
  void blockStart(int) override { clockedIn.store(patched(1)); }
  void trig(int port, const Event& ev) override {
    if (port == 1) {   // CLOCK: RESTART EVERY so many beats (or bars of four) it starts again from the top, in time
      static const int BEATS[6] = {0, 1, 4, 8, 16, 32}; const int nb = BEATS[std::max(0, std::min(5, (int)e[EVERY]))]; if (!nb || !count()) return;
      const long every = std::max(1L, std::lround(nb * (ev.ppb > 0 ? ev.ppb : 1.f))); const bool now0 = clkN % every == 0; clkN++; if (!now0) return;
      const double now = t + eng->pos / sr;
      if (e[MODE] < 0.5f) { fadeAll(0.03f); wob = 0; nextAt = now; if (patched(0)) looping = true; }   // LOOP: the loop from its start
      else { double d, f; seg(patched(0) ? noteIn : -1.f, 1, d, f); }   // ONCE: plays it once
      return; }
    if (!count()) return;
    const double now = t + eng->pos / sr; noteIn = ev.note; const float vel = 1 - e[VSENS] + e[VSENS] * ev.vel;
    if (e[MODE] < 0.5f) { fadeAll(0.03f); looping = true; loopVel = vel; wob = 0; nextAt = now; return; }
    { const int ip = pi("poly"); if (ip >= 0 && e[ip] > 0.5f) fadeAll(0.015f); }   // MONO: the last one fades out
    double d, f; seg(ev.note, vel, d, f);
  }
  void render(int a, int b) override {
    for (int i = a; i < b; i++) { out[0][0][i] = 0; out[0][1][i] = 0; }
    if (!count()) { disp[0].store(0); return; }
    const bool trigged = patched(0);
    const float s5[5] = {base(START), base(LEN), base(PITCH), base(STYLE), base(KIND)}; bool ch = false; for (int k = 0; k < 5; k++) if (s5[k] != sig[k]) ch = true;
    if (ch) { if (sig[0] > -9) dirty = 0.3f; for (int k = 0; k < 5; k++) sig[k] = s5[k]; }   // moving the region crossfades to it once the hand settles
    if (dirty > 0) { dirty -= (b - a) / sr; if (dirty <= 0 && nextAt >= 0) { fadeAll(std::min(e[FADE], 1.f)); nextAt = t + a / sr + 0.02; } }
    if (!trigged) { noteIn = -1; loopVel = 1; }
    const bool run = trigged ? (e[MODE] < 0.5f && looping) : true;
    if (!run) { if (!trigged) nextAt = -1; else if (nextAt >= 0 && e[MODE] > 0.5f) { fadeAll(0.3f); nextAt = -1; } }
    const bool filt = e[CUT] < 19000 || e[RES] > 0.01f; if (filt && (e[CUT] != lpSig[0] || e[RES] != lpSig[1])) { lpSig[0] = e[CUT]; lpSig[1] = e[RES]; lp.set(Biquad::LP, e[CUT], 0.7f + e[RES] * e[RES] * 9, sr); }
    for (int i = a; i < b; i++) {
      if (run) { const double now = t + i / sr; if (nextAt < 0 || nextAt < now - 0.5) nextAt = now + 0.005;
        if (now >= nextAt) { double d, f; seg(trigged ? noteIn : -1.f, loopVel, d, f); nextAt += std::max(0.05, d - f); } }
      float L = 0, R = 0;
      for (auto& v : vo) { if (!v.on) continue;
        float g = v.gain; if (v.t < v.fi) g *= (float)v.t / v.fi; else if (v.t > v.d - v.fo) g *= std::max(0.f, (float)(v.d - v.t) / v.fo);
        if (v.fadeK > 0) { v.fadeV -= v.fadeK; if (v.fadeV <= 0) { v.on = false; continue; } g *= v.fadeV; }
        const float smp = readBuf(v.b->d, v.pos) * g; L += smp * v.gl; R += smp * v.gr;
        v.pos += v.rev ? -v.inc : v.inc; if (++v.t >= v.d) v.on = false; }
      if (filt) { L = lp.run(L, 0); R = lp.run(R, 1); }
      out[0][0][i] = L * e[LEVEL_]; out[0][1][i] = R * e[LEVEL_];
    }
    int n = 0; for (auto& v : vo) if (v.on && v.fadeK == 0 && n < 12) { int slot = 0; for (int k = 0; k < NS; k++) if (buf[k] == v.b) slot = k;
      disp[1 + n].store((float)(v.pos / v.b->sr / v.b->dur())); disp[20 + n].store((float)slot); n++; } disp[0].store((float)n);
  }
};
static Reg rSample(new ModuleDef{"sample", "SAMPLE", Cat::Voice, {{"TRIG", PType::Trig}, {"CLOCK", PType::Trig}}, {{"OUT", PType::Audio}},
  {ACT("load", "LOAD FILE"), OP("kind", "SOUNDS", {"ONE", "SLICES", "KIT", "MULTI"}, 0), only(stepper(noMod(ST("slices", "SLICES", 2, 32, 8))), "slices"),
   OP("mode", "TRIG", {"LOOP", "ONCE"}, 0), only(OP("notes", "NOTES", {"OFF", "ON"}, 0), "one"), only(fine(pct("start", "START", 0)), "region"), fmt(prExp("len", "LENGTH", 0.0005f, 1, 1), [](float v) { char b[16]; std::snprintf(b, 16, v < 0.1f ? "%.1f%%" : "%.0f%%", v * 100); return std::string(b); }),   // the screen shows these as times
   ST("pitch", "PITCH", -24, 24, 0), OP("rev", "PLAY", {"FWD", "BACK", "BOUNCE", "RANDOM"}, 0),
   only(T("atk", "ATTACK", 0.002f, 4, 0.01f), "once"), only(T("rel", "RELEASE", 0.005f, 8, 0.3f), "once"), only(T("fade", "FADE", 0.005f, 4, 0.4f), "loop"),
   only(pctX("drift", "DRIFT", 3, 0), "region"), prHz("cut", "CUTOFF", 80, 20000, 20000), pct("res", "RES", 0), pct("vsens", "VEL SENS", 1), pct("width", "WIDTH", 0), LEVEL(), ACT("edit", "EDIT SOUND"),
   only(OP("every", "RESTART", {"OFF", "EVERY BEAT", "EVERY BAR", "2 BARS", "4 BARS", "8 BARS"}, 2), "clock")},
  12, 68, false, [] { return new Sample(); }});

// ---------------- GRAIN LOOPER: catches the last seconds of its input (or a file) and plays it as a cloud of grains ----------------
struct Looper : Module {
  enum { CATCH, LOAD, LEN, POS, CRAWL, SIZE, DENS, PITCH, SCATTER, REV, TEX, MIX, LEVEL_ };
  AudioBuf* buf = nullptr;
  struct Grain { double pos, inc; long t, len; float gl, gr, ex, gain, x; float win[65]; };   // each grain's window shape, worked out once when it starts
  static constexpr int MAXG = 96; Grain g[MAXG]; int ng = 0; double acc = 0; float head = 0.5f, dir = 1, lastPos = -1; Smooth dry, wet;
  ~Looper() override { delete buf; }
  void prepare() override { ring.assign((size_t)(sr * 20), 0.f); dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX] * e[LEVEL_]); }
  AudioBuf* swapBuf(AudioBuf* b, int) override { AudioBuf* old = buf; buf = b; ng = 0; return old; }
  void spawn() {
    const double dur = buf->dur(), rate = std::pow(2.0, e[PITCH] / 12.0) * (1 + rng.bi() * 0.004);
    const double span = std::min(e[SIZE] * rate, dur * 0.95), center = head * dur + e[SCATTER] * rng.bi() * std::min(3.0, dur * 0.25);
    const double off = std::max(0.0, std::min(center - span / 2, std::max(0.0, dur - span))); const bool rv = rng.uni() < e[REV];
    const double inc = rate * buf->sr / sr, dens = 0.3 + e[DENS] * e[DENS] * 40; Grain& q = g[ng++];
    q.pos = rv ? (off + span) * buf->sr : off * buf->sr; q.inc = rv ? -inc : inc; q.len = std::max(16L, (long)(e[SIZE] * sr)); q.t = 0;
    panGains(rng.bi() * 0.4f, q.gl, q.gr); q.ex = 0.25f + e[TEX] * 3; q.gain = (float)(0.5 / std::sqrt(std::max(1.0, dens * e[SIZE]))); q.x = (float)((off + span / 2) / dur);
    for (int k = 0; k <= 64; k++) q.win[k] = (k == 0 || k == 64) ? 0.f : std::pow(std::max(0.f, std::sin(PI * k / 64.f)), q.ex) * q.gain;   // the ends are exactly silent (sin(pi) in floats is a hair below zero, and a power of that is not a number)
  }
  void render(int a, int b) override {
    // keep the last 20 seconds of the input, for CATCH
    long w = ringW.load(std::memory_order_relaxed); const long N = (long)ring.size();
    for (int i = a; i < b; i++, w++) ring[w % N] = 0.5f * (in[0][0][i] + in[0][1][i]);
    ringW.store(w, std::memory_order_release);
    if (e[POS] != lastPos) { head = e[POS]; lastPos = e[POS]; }   // POSITION places the playhead; CRAWL moves it on
    if (!eng->freeze.load(std::memory_order_relaxed)) head += dir * e[CRAWL] * 0.02f * (b - a) / sr;   // FREEZE holds it where it is
    if (head > 1) { head = 1; dir = -1; } if (head < 0) { head = 0; dir = 1; }
    dry.t = 1 - e[MIX]; wet.t = e[MIX] * e[LEVEL_];
    const bool run = buf && buf->d.size() > 16 && !mute.load(std::memory_order_relaxed); const double dens = 0.3 + e[DENS] * e[DENS] * 40;
    if (!run) ng = 0;
    for (int i = a; i < b; i++) {
      float L = 0, R = 0;
      if (run) { acc += dens / sr; if (acc >= 1) { acc -= 1; if (ng < MAXG) spawn(); }
        const float* bd = buf->d.data(); const double last = (double)buf->d.size() - 2;
        for (int k = 0; k < ng; ) { Grain& q = g[k];
          if (q.pos >= 0 && q.pos <= last) { const long ip = (long)q.pos; const float fr = (float)(q.pos - ip), u = 64.f * q.t / q.len; const int wi = std::min(63, (int)u);
            const float s = (bd[ip] + (bd[ip + 1] - bd[ip]) * fr) * (q.win[wi] + (q.win[wi + 1] - q.win[wi]) * (u - wi)); L += s * q.gl; R += s * q.gr; }
          q.pos += q.inc;
          if (++q.t >= q.len) { g[k] = g[--ng]; } else k++; } }
      const float d = dry.tick(), wv = wet.tick();
      out[0][0][i] = in[0][0][i] * d + sane(L) * wv; out[0][1][i] = in[0][1][i] * d + sane(R) * wv;
    }
    disp[0].store(head); const int show = std::min(ng, 30); disp[1].store((float)show); for (int k = 0; k < show; k++) disp[2 + k].store(g[ng - 1 - k].x);
  }
};
static Reg rLooper(new ModuleDef{"looper", "GRAIN LOOPER", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {ACT("catch", "CATCH"), ACT("load", "LOAD FILE"), ST("len", "LENGTH", 2, 16, 8), pct("pos", "POSITION", 0.5f), pct("crawl", "CRAWL", 0.3f),
   T("size", "SIZE", 0.03f, 2, 0.25f), pctX("dens", "DENSITY", 1.6f, 0.35f), ST("pitch", "GR PITCH", -24, 24, 0), pctX("scatter", "SCATTER", 2, 0.2f), pct("rev", "REVERSE", 0.2f), pctX("tex", "TEXTURE", 2, 0.4f), pct("mix", "MIX", 1), LEVEL(), ACT("edit", "EDIT SOUND")},
  12, 68, false, [] { return new Looper(); }});

// ---------------- STRETCH: paulstretch, a random-phase spectral smear of the last few seconds ----------------
struct FFT {
  int N; std::vector<int> rev; std::vector<float> cs, sn;
  explicit FFT(int n) : N(n), rev(n), cs(n / 2), sn(n / 2) {
    int lv = 0; while ((1 << lv) < N) lv++;
    for (int i = 0; i < N; i++) { int r = 0; for (int b = 0; b < lv; b++) r |= ((i >> b) & 1) << (lv - 1 - b); rev[i] = r; }
    for (int i = 0; i < N / 2; i++) { cs[i] = std::cos(TAU * i / N); sn[i] = std::sin(TAU * i / N); }
  }
  void run(float* re, float* im, bool inv) const {
    for (int i = 0; i < N; i++) { const int j = rev[i]; if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
    const float sg = inv ? 1.f : -1.f;
    for (int size = 2; size <= N; size <<= 1) { const int half = size >> 1, step = N / size;
      for (int i = 0; i < N; i += size) for (int j = 0, k = 0; j < half; j++, k += step) {
        const float wr = cs[k], wi = sg * sn[k]; const int l = i + j + half, m = i + j; const float tr = re[l] * wr - im[l] * wi, ti = re[l] * wi + im[l] * wr;
        re[l] = re[m] - tr; im[l] = im[m] - ti; re[m] += tr; im[m] += ti; } }
  }
};
// The PaulStretch idea (written here from the published method): take overlapping windows of the sound, keep how
// loud each frequency is, give every frequency a random phase, and lay the windows down further apart than they were
// read. Here it can stretch what comes in (LIVE) or a loaded file (FILE), shift the pitch and add an octave shimmer.
struct Stretch : Module {
  enum { SRC, LOAD, AMT, WIN, POS, PITCHK, SHIM, FREEZE, MIX };
  static constexpr int NMAX = 65536, R = 1 << 20;
  static constexpr int SIZES[6] = {2048, 4096, 8192, 16384, 32768, 65536};   // at 88.2K and up each WINDOW is one size bigger: the same length of time
  FFT* ffts[6] = {}; int N = 4096, HOP = 2048, cur = -1;
  std::vector<float> rg, win, re, im, mag, mag2, r2, i2, acc[2], q[2]; long wpos = 0; double rpos = -4096, trav = 0; int qpos = 1 << 30;
  Biquad hp; Smooth dry, wet; AudioBuf* buf = nullptr; bool lastFz = false;
  ~Stretch() override { for (auto* f : ffts) delete f; }
  AudioBuf* swapBuf(AudioBuf* b, int) override { AudioBuf* old = buf; buf = b; trav = 0; return old; }
  void prepare() override {
    for (int k = 0; k < 6; k++) if (!ffts[k]) ffts[k] = new FFT(SIZES[k]);
    rg.assign(R, 0.f); win.assign(NMAX, 0); re.assign(NMAX, 0); im.assign(NMAX, 0); mag.assign(NMAX / 2 + 1, 0); mag2.assign(NMAX / 2 + 1, 0); r2.assign(NMAX, 0); i2.assign(NMAX, 0);
    for (int c = 0; c < 2; c++) { acc[c].assign(NMAX, 0); q[c].assign(NMAX / 2, 0); }
    hp.set(Biquad::HP, 60, 0.707f, sr); dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX] * 0.9f); setSize();
  }
  void setSize() { const int k = std::max(0, std::min(4, (int)e[WIN])) + (sr > 70000 ? 1 : 0); if (k == cur) return; cur = k; N = SIZES[k]; HOP = N / 2;
    for (int i = 0; i < N; i++) win[i] = std::sin(PI * (i + 0.5f) / N); for (int c = 0; c < 2; c++) std::fill(acc[c].begin(), acc[c].end(), 0.f); qpos = HOP; }
  bool fileMode() const { return e[SRC] > 0.5f && buf && buf->d.size() > (size_t)32768; }
  bool frozen() const { return e[FREEZE] > 0.5f || eng->freeze.load(std::memory_order_relaxed); }
  void trig(int port, const Event&) override { if (port == 1) p[FREEZE].store(p[FREEZE].load() > 0.5f ? 0.f : 1.f); }   // FREEZE in: each trigger flips it
  void frame() {
    setSize(); const bool fz = frozen(), file = fileMode();
    double ratio = std::pow(2.0, e[PITCHK] / 12.0);
    if (file) {   // read through the file: from POSITION, moving on 1/STRETCH as fast as it plays, round and round
      const long len = (long)buf->d.size(); ratio *= buf->sr / sr;
      if (!fz) trav += (double)HOP / std::max(1.f, e[AMT]) * (buf->sr / sr);
      double at = e[POS] * len + trav; at = std::fmod(at, (double)len); if (at < 0) at += len;
      disp[0].store((float)(at / len), std::memory_order_relaxed);
      for (int i = 0; i < N; i++) { re[i] = buf->d[(size_t)(((long)at + i) % len)] * win[i]; im[i] = 0; }
    } else {
      if (!fz) { rpos += (double)HOP / std::max(1.f, e[AMT]); const double lag = wpos - rpos; if (lag > R - N - HOP) rpos = wpos - N * 3; if (lag < N) rpos = wpos - N; if (rpos < 0) rpos = 0; }
      const long base = (long)std::floor(rpos); disp[0].store(-1, std::memory_order_relaxed);
      for (int i = 0; i < N; i++) { re[i] = rg[(size_t)((base + i) & (R - 1))] * win[i]; im[i] = 0; }
    }
    FFT& fft = *ffts[cur]; fft.run(re.data(), im.data(), false);
    const int H = N / 2; for (int k = 0; k <= H; k++) mag[k] = std::hypot(re[k], im[k]);
    const float sh = e[SHIM];   // each output frequency takes its level from the frequency PITCH and the octave below it
    for (int k = 0; k <= H; k++) { const double s1 = k / ratio, s2 = k / (ratio * 2); float v = 0;
      if (s1 < H) { const int i0 = (int)s1; const float f = (float)(s1 - i0); v = mag[i0] * (1 - f) + mag[std::min(H, i0 + 1)] * f; }
      if (sh > 0 && s2 < H) { const int i0 = (int)s2; v += sh * 0.8f * mag[i0]; }
      mag2[k] = v; }
    for (int c = 0; c < 2; c++) {
      std::fill(r2.begin(), r2.begin() + N, 0.f); std::fill(i2.begin(), i2.begin() + N, 0.f);
      for (int k = 1; k < H; k++) { const float ph = rng.uni() * TAU; r2[k] = mag2[k] * std::cos(ph); i2[k] = mag2[k] * std::sin(ph); r2[N - k] = r2[k]; i2[N - k] = -i2[k]; }
      fft.run(r2.data(), i2.data(), true);
      float* a = acc[c].data(); for (int i = 0; i < N; i++) a[i] += (r2[i] / N) * win[i];
      std::copy(a, a + HOP, q[c].begin()); std::copy(a + HOP, a + N, a); std::fill(a + N - HOP, a + N, 0.f);
    }
    qpos = 0;
  }
  void render(int a, int b) override {
    dry.t = 1 - e[MIX]; wet.t = e[MIX] * 0.9f; const bool fz = frozen();
    for (int i = a; i < b; i++) {
      if (!fz) { rg[(size_t)(wpos & (R - 1))] = 0.5f * (in[0][0][i] + in[0][1][i]); wpos++; }
      if (qpos >= HOP) frame();
      const float d = dry.tick(), w = wet.tick();
      for (int c = 0; c < 2; c++) out[0][c][i] = in[0][c][i] * d + hp.run(sane(q[c][qpos]), c) * w;
      qpos++;
    }
  }
};
static Reg rStretch(new ModuleDef{"stretch", "STRETCH", Cat::Proc, {{"IN", PType::Audio}, {"FREEZE", PType::Trig}}, {{"OUT", PType::Audio}},
  {OP("src", "SOURCE", {"LIVE", "FILE"}, 0), only(ACT("load", "LOAD FILE"), "file"),
   fmt(prExp("amt", "STRETCH", 1, 1000, 10), [](float v) { char b[16]; std::snprintf(b, 16, v < 10 ? "%.1fX" : "%.0fX", v); return std::string(b); }),
   OP("win", "WINDOW", {"0.04S", "0.09S", "0.17S", "0.34S", "0.68S"}, 1), only(pct("pos", "POSITION", 0), "file"), ST("pitch", "PITCH", -24, 24, 0),
   pct("shim", "SHIMMER", 0), OP("freeze", "FREEZE", {"OFF", "ON"}, 0), pct("mix", "MIX", 1), only(ACT("edit", "EDIT SOUND"), "file")}, 12, 68, false, [] { return new Stretch(); }});

// ---------------- TAPE LOOP: one loop of tape, in the spirit of the oooooo script for norns ----------------
// What comes in is recorded onto a loop (LENGTH seconds, or BEATS of a clock patched into CLOCK). Each pass while
// recording keeps KEEP of what was there, so layers build up and fade. Pan and level WANDER on their own slow LFOs,
// and RANDOM sometimes, at the end of a pass, plays a smaller piece of the loop, or at another speed, for a while.
struct TapeLoop : Module {
  enum { REC, LEN, BEATS, KEEP, INLV, SPEED, WANDER, WSPD, RAND, CLEAR, LEVEL };
  static constexpr int SEGS = 32;
  std::vector<float> tape[2]; long cap = 0; double head = 0; long L = 1; float spd = 1, spdMul = 1; double winA = 0, winN = 0; int lastSeg = -1;
  double last = -1; float cp = 0, ppb = 1; double lph[2] = {0, 0.37}; float segPk[SEGS] = {}; Smooth lev, panS, recG;
  void prepare() override { cap = (long)(60.5 * sr); for (auto& t : tape) t.assign(cap, 0.f); lev.setTime(0.05f, sr); panS.setTime(0.05f, sr); recG.setTime(0.01f, sr); lev.snap(1); }
  void blockStart(int) override { clockedIn.store(patched(1)); }
  long clearAt = -1;   // CLEAR TAPE wipes a piece each block, so it never holds the sound up
  void action(const std::string& k) override { if (k == "clear") { clearAt = 0; for (float& v : segPk) v = 0; } }
  void trig(int port, const Event& ev) override {
    const double now = t + eng->pos / sr;
    if (port == 1) { if (ev.period > 0) cp = ev.period; else if (last >= 0) { const float p0 = (float)(now - last); if (p0 > 0.01f && p0 < 8) cp = cp > 0 ? cp * 0.6f + p0 * 0.4f : p0; }
      last = now; ppb = ev.ppb > 0 ? ev.ppb : (ppb > 0 ? ppb : 1); }
    if (port == 2) p[REC].store(p[REC].load() > 0.5f ? 0.f : 1.f);   // REC in: each trigger starts or stops recording
    if (port == 3) head = rng.uni() * L;                              // JUMP in: somewhere else on the tape
  }
  long loopLen() const { const bool ck = clockedIn.load(std::memory_order_relaxed) && cp > 0;
    const double secs = ck ? e[BEATS] * cp * ppb : e[LEN]; return std::max(64L, std::min(cap - 2, (long)(secs * sr))); }
  void render(int a, int b) override {
    if (clearAt >= 0) { const long e1 = std::min(cap, clearAt + 32768); for (auto& tp : tape) std::fill(tp.begin() + clearAt, tp.begin() + e1, 0.f); clearAt = e1 >= cap ? -1 : e1; }
    L = loopLen(); const bool rec = e[REC] > 0.5f; recG.t = rec ? 1.f : 0.f;
    const float keep = e[KEEP], inl = e[INLV], wan = e[WANDER];
    for (int k = 0; k < 2; k++) { lph[k] += e[WSPD] * (k ? 0.71 : 1.0) * (b - a) / sr; lph[k] -= std::floor(lph[k]); }
    panS.t = wan * 0.8f * std::sin((float)(lph[0] * TAU)); lev.t = 1 - wan * 0.45f * (1 + std::sin((float)(lph[1] * TAU)));
    if (winN <= 0 || winN > L) { winA = 0; winN = L; }
    for (int i = a; i < b; i++) {
      double h = head; if (h >= L) h = std::fmod(h, (double)L);
      const long i0 = (long)h; const float fr = (float)(h - i0); const long i1 = (i0 + 1) % L;
      const float yl = tape[0][i0] + (tape[0][i1] - tape[0][i0]) * fr, yr = tape[1][i0] + (tape[1][i1] - tape[1][i0]) * fr;
      const float rg = recG.tick();
      if (rg > 0.0001f) { const float k0 = 1 - rg * (1 - keep);   // recording: what was there fades to KEEP, what comes in goes on top
        tape[0][i0] = sane(tape[0][i0] * k0 + in[0][0][i] * inl * rg); tape[1][i0] = sane(tape[1][i0] * k0 + in[0][1][i] * inl * rg);
        const int sg = (int)(i0 * SEGS / L); if (sg != lastSeg) { segPk[sg] = 0; lastSeg = sg; }
        segPk[sg] = std::max(segPk[sg], std::max(std::fabs(tape[0][i0]), std::fabs(tape[1][i0]))); }
      const float pn = panS.tick(), lv = lev.tick() * e[LEVEL];
      out[0][0][i] = (yl * (1 - std::max(0.f, pn)) ) * lv; out[0][1][i] = (yr * (1 + std::min(0.f, pn))) * lv;
      // move along: at SPEED (backwards below zero), inside the piece of tape being played
      const double sp = e[SPEED] * spdMul; double nh = h + sp;
      const double ws = winA, we = winA + winN; bool wrapped = false;
      if (nh >= we) { nh -= winN; wrapped = true; } else if (nh < ws) { nh += winN; wrapped = true; }
      if (nh >= L) nh -= L; if (nh < 0) nh += L;
      head = nh;
      if (wrapped && rng.uni() < e[RAND]) {   // RANDOM: now and then a smaller piece, or another speed, for a pass
        const float r = rng.uni();
        if (r < 0.45f) { winN = L / (double)(2 << (int)(rng.uni() * 3)); winA = std::floor(rng.uni() * (L - winN)); head = winA; spdMul = 1; }
        else if (r < 0.8f) { static const float M[4] = {0.5f, 2, -1, -0.5f}; spdMul = M[(int)(rng.uni() * 4) & 3]; winA = 0; winN = L; }
        else { winA = 0; winN = L; spdMul = 1; } }
      else if (wrapped && rng.uni() < 0.5f) { winA = 0; winN = L; spdMul = 1; }
    }
    disp[0].store((float)(head / L), std::memory_order_relaxed); disp[1].store(rec ? 1.f : 0.f, std::memory_order_relaxed);
    disp[2].store((float)(winA / L), std::memory_order_relaxed); disp[3].store((float)(winN / L), std::memory_order_relaxed);
    for (int k = 0; k < SEGS; k++) disp[4 + k].store(segPk[k], std::memory_order_relaxed);
  }
};
static Reg rTapeLoop(new ModuleDef{"tloop", "TAPE LOOP", Cat::Proc, {{"IN", PType::Audio}, {"CLOCK", PType::Trig}, {"REC", PType::Trig}, {"JUMP", PType::Trig}}, {{"OUT", PType::Audio}},
  {OP("rec", "RECORD", {"OFF", "ON"}, 0), only(noMod(T("len", "LENGTH", 1, 60, 8)), "free"), only(stepper(noMod(ST("beats", "BEATS", 1, 64, 16))), "clock"),
   pct("keep", "KEEP", 0.8f), pct("inlv", "REC LEVEL", 0.8f),
   fmt(pr("speed", "SPEED", -2, 2, 1), [](float v) { char b[16]; std::snprintf(b, 16, "%.2fX", v); return std::string(b); }),
   pct("wander", "WANDER", 0.4f), fmt(prExp("wspd", "WANDER SPD", 0.005f, 0.5f, 0.05f), [](float v) { char b[16]; std::snprintf(b, 16, "1 PER %.0fS", 1 / v); return std::string(b); }),
   pct("rand", "RANDOM", 0.15f), ACT("clear", "CLEAR TAPE"), LEVEL()}, 12, 68, false, [] { return new TapeLoop(); }});

// ---------------- LISTEN: hears which key and scale a sound is in ----------------
// Every 1/10 s or so it takes the last 8192 samples, measures how strongly each of the 12 note names sounds
// (its "chroma"), lets that build up over TIME seconds, then compares it with every key and scale: a scale scores
// well when its notes are the ones heard and its other notes aren't, with the key's root and fifth counting a little extra.
struct Listen : Module {
  enum { TIME, USE };
  static constexpr int N = 8192, HOP = 4096;
  FFT fft{N}; std::vector<float> rg, win, re, im; long w = 0; int sinceFrame = 0; float chroma[12] = {0}, level = 0;
  void prepare() override { rg.assign(N, 0.f); win.resize(N); for (int i = 0; i < N; i++) win[i] = 0.5f - 0.5f * std::cos(TAU * i / N); re.assign(N, 0); im.assign(N, 0); }
  void frame() {
    for (int i = 0; i < N; i++) { re[i] = rg[(size_t)((w + i) % N)] * win[i]; im[i] = 0; }
    fft.run(re.data(), im.data(), false);
    float now[12] = {0}, tot = 0;
    for (int k = 1; k < N / 2; k++) { const float f = k * sr / N; if (f < 55 || f > 4000) continue;
      const float mag = std::hypot(re[k], im[k]); const float midi = 69 + 12 * std::log2(f / 440); const int pc = (((int)std::lround(midi)) % 12 + 12) % 12;
      now[pc] += mag; tot += mag; }
    const float rms = tot / N; level += (rms - level) * 0.3f;
    if (tot < 1e-3f) return;   // silence: keep what it heard before
    const float k = 1 - std::exp(-(HOP / sr) / std::max(0.5f, e[TIME]));
    for (int i = 0; i < 12; i++) chroma[i] += (now[i] / tot - chroma[i]) * k;
    // match against every key and scale
    KeyMatch km = matchKey(chroma); const int br = km.root, bs = km.scale, sr2 = km.root2, ss = km.scale2; const float best = km.conf, second = km.conf2;
    disp[0].store((float)br); disp[1].store((float)bs); disp[2].store(clampf(best, 0, 1)); disp[3].store((float)sr2); disp[4].store((float)ss); disp[5].store(clampf(second, 0, 1));
  }
  void render(int a, int b) override {
    for (int i = a; i < b; i++) { const float x = 0.5f * (in[0][0][i] + in[0][1][i]); rg[(size_t)(w % N)] = x; w++; out[0][0][i] = in[0][0][i]; out[0][1][i] = in[0][1][i];   // it passes the sound straight through
      if (++sinceFrame >= HOP) { sinceFrame = 0; frame(); } }
    float mx = 1e-6f; for (float c : chroma) mx = std::max(mx, c); for (int i = 0; i < 12; i++) disp[6 + i].store(chroma[i] / mx); disp[18].store(level);
  }
};
static Reg rListen(new ModuleDef{"listen", "LISTEN", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {noMod(T("time", "LISTEN", 1, 60, 8)), ACT("use", "USE THIS KEY")}, 26, 68, false, [] { return new Listen(); }});

}  // namespace tb
