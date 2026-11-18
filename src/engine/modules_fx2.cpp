// Timbient native: more effects (envelope VCA, resonator, phaser, tape, bitcrush) and the mod sources
// that follow triggers or sound (trig envelope, random, follower).
#include "params.h"
#include "dsp2.h"
#include "../engine_mi/rings_core.h"

namespace tb {

// dry and wet levels that glide rather than jump
struct DryWet {
  Smooth d, w;
  void init(float sr, float dry, float wet) { d.setTime(0.012f, sr); w.setTime(0.012f, sr); d.snap(dry); w.snap(wet); }
  void target(float dry, float wet) { d.t = dry; w.t = wet; }
};

// ---------------- ENVELOPE VCA: each trigger opens it, then it closes again ----------------
struct Vca : Module {
  enum { ATK, DEC, SUS, HOLD, REL, MIX };
  float g = 0, from = 0, tt = 1e9f; DryWet dw;
  void prepare() override { dw.init(sr, 1 - e[MIX], e[MIX]); }
  void trig(int port, const Event&) override { if (port == 1) { from = g; tt = 0; } }
  void render(int a, int b) override {
    dw.target(1 - e[MIX], e[MIX]); const float dt = 1.f / sr, atk = std::max(0.002f, e[ATK]), endT = std::max(atk, e[HOLD]);
    const float kd = 1 - std::exp(-dt * 3 / std::max(0.01f, e[DEC])), kr = 1 - std::exp(-dt * 3 / std::max(0.01f, e[REL]));
    for (int i = a; i < b; i++) {
      tt += dt;
      if (tt < atk) g = from + (1 - from) * tt / atk; else if (tt < endT) g += (e[SUS] - g) * kd; else g -= g * kr;
      const float d = dw.d.tick(), w = dw.w.tick();
      for (int ch = 0; ch < 2; ch++) { const float x = in[0][ch][i]; out[0][ch][i] = x * d + x * g * w; }
    }
  }
};
static Reg rVca(new ModuleDef{"vca", "ENVELOPE VCA", Cat::Proc, {{"IN", PType::Audio}, {"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  {T("atk", "ATTACK", 0.002f, 4, 0.02f), T("dec", "DECAY", 0.01f, 4, 0.3f), pct("sus", "SUSTAIN", 0.6f), T("hold", "HOLD", 0.01f, 8, 0.6f), T("rel", "RELEASE", 0.01f, 8, 1.5f), pct("mix", "MIX", 1)},
  0, 68, false, [] { return new Vca(); }});

// ---------------- RESONATOR: things that ring when the incoming sound excites them, tuned to notes of the chord on the key.
// TYPE: STRINGS (as it always was): sympathetic strings, feedback delays like Karplus-Strong, like the open strings of a sitar or
// a harp. MODAL: struck bars and bells: a few separate ringing partials per note, INHARM moves them from in tune (a vibraphone)
// to clangorous (a bell), DAMP makes the high ones die sooner. TUBE: pipes closed at one end, only the odd harmonics, hollow
// and woody like a clarinet's bore or a bottle. Heavy: RINGS, Mutable Instruments' Rings with the incoming sound exciting it.
struct Resonator : Module {
  enum { NOTES, OCT, DECAY, DAMP, INHARM, SPREAD, MIX, TYPE, RMODEL, RSTRUCT, RPOS };
  enum { STRINGS, MODAL, TUBE, RINGST };
  static constexpr int NS = 6, NM = 6;
  struct Str { DelayLine d; float len = 100, lp = 0, ap = 0, g = 0.99f, apc = 0; };
  struct Mode { float c1 = 0, c2 = 0, gin = 0, y1 = 0, y2 = 0; };
  Str st[NS][2]; Mode md[NS][NM][2]; DryWet dw; float sig[9] = {-1}; int lastRoot = -1, lastScale = -1, cur = -1; Biquad hp[2]; RingsCore rings; float rin[2][64], rout[2][64];
  void prepare() override { for (auto& sc : st) for (auto& s : sc) s.d.init(0.06f, sr); dw.init(sr, 1 - e[MIX], e[MIX]); for (auto& h : hp) h.set(Biquad::HP, 30, 0.707f, sr); rings.init(sr); cur = -1; }
  int type() const { return std::max(0, std::min(3, (int)std::lround(e[TYPE]))); }
  void tune() {
    const float sg[9] = {e[NOTES], e[OCT], e[DECAY], e[DAMP], e[INHARM], e[SPREAD], e[TYPE], 0, 0}; bool same = root() == lastRoot && scale() == lastScale;
    for (int k = 0; k < 7; k++) if (sg[k] != sig[k]) same = false; if (same) return; for (int k = 0; k < 7; k++) sig[k] = sg[k]; lastRoot = root(); lastScale = scale();
    static const int DEG[NS] = {0, 4, 2, 7, 9, 11};   // root, fifth, third, octave, octave+third, octave+fifth (as steps along the scale)
    const int ty = type();
    for (int k = 0; k < NS; k++) { const float f = mtof(note((int)e[OCT], DEG[k])); const float sp = 1 + e[SPREAD] * 0.004f;
      for (int c = 0; c < 2; c++) { const float fc = f * (c ? sp : 1 / sp);
        if (ty == MODAL) {   // each partial a two-pole resonator: in tune at INHARM 0, a free bar's partials (1, 2.76, 5.40, 8.93...) at 1
          static const float BAR[NM] = {1, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f};
          for (int q = 0; q < NM; q++) { Mode& m = md[k][q][c]; const float ratio = (q + 1) + (BAR[q] - (q + 1)) * e[INHARM], fq = fc * ratio;
            if (fq > 0.45f * sr) { m.gin = 0; m.c1 = m.c2 = 0; continue; }
            const float t60 = e[DECAY] / (1 + q * e[DAMP] * 3), r = std::pow(10.f, -3.f / (t60 * sr)), w = TAU * fq / sr;
            m.c1 = 2 * r * std::cos(w); m.c2 = -r * r; m.gin = std::sin(w) * 0.03f / std::pow(q + 1.f, 0.6f + e[DAMP]); }   /* a strike rings at the same size whatever the DECAY */
          continue; }
        Str& s = st[k][c];
        if (ty == TUBE) {   // closed at one end: the wave goes round twice, turned over at each trip, so the loop is half as long
          s.len = std::max(2.f, std::min(0.055f * sr, sr / (2 * fc) - 0.5f)); s.g = -std::pow(10.f, -3.f / (e[DECAY] * 2 * fc)); s.apc = e[INHARM] * 0.4f; continue; }
        s.len = std::max(2.f, std::min(0.055f * sr, sr / fc - 0.5f));   // (the loop's filters add about half a sample)
        s.g = std::pow(10.f, -3.f / (e[DECAY] * fc));   // DECAY: the time to fall by 60 dB
        s.apc = e[INHARM] * 0.6f; } }   // INHARM: an allpass in the loop bends the partials out of line, string to bell
  }
  void render(int a, int b) override {
    const int ty = type(); if (ty != cur) { cur = ty; for (auto& sc : st) for (auto& s : sc) { s.d.clear(); s.lp = s.ap = 0; } for (auto& a1 : md) for (auto& a2 : a1) for (auto& m : a2) m.y1 = m.y2 = 0; if (ty == RINGST) rings.clear(); sig[0] = -1; }
    tune(); dw.target(1 - e[MIX], e[MIX]); const int n = std::max(1, std::min(NS, (int)e[NOTES])); const float damp = 0.05f + e[DAMP] * 0.9f, gain = 0.6f / std::sqrt((float)n);
    if (ty == RINGST) {   // RINGS: the input excites it; it rings at the key's root at OCTAVE (CHORDS: STRUCTURE picks the chord)
      rings.note(note((int)e[OCT], 0)); rings.set((int)std::lround(e[RMODEL]), std::min(4, n), e[RSTRUCT], 1 - e[DAMP] * 0.9f, std::log(e[DECAY] / 0.2f) / std::log(60.f), e[RPOS]);
      for (int i = a; i < b; i++) { rin[0][i] = hp[0].run(in[0][0][i], 0); rin[1][i] = hp[1].run(in[0][1][i], 0); }
      rings.process(rin[0] + a, rin[1] + a, rout[0] + a, rout[1] + a, b - a, false);
      for (int i = a; i < b; i++) { const float d = dw.d.tick(), w = dw.w.tick(); for (int c = 0; c < 2; c++) out[0][c][i] = in[0][c][i] * d + sane(rout[c][i]) * w * 0.3f; }
      return; }
    for (int i = a; i < b; i++) { const float d = dw.d.tick(), w = dw.w.tick();
      for (int c = 0; c < 2; c++) { const float x = hp[c].run(in[0][c][i], 0) * 0.25f; float acc = 0;
        if (ty == MODAL) { for (int k = 0; k < n; k++) for (int q = 0; q < NM; q++) { Mode& m = md[k][q][c]; const float y = m.c1 * m.y1 + m.c2 * m.y2 + m.gin * x; m.y2 = m.y1; m.y1 = sane(y); acc += y; } acc *= 0.5f; }
        else for (int k = 0; k < n; k++) { Str& s = st[k][c]; float y = s.d.read(s.len - 1);
          s.lp += (1 - damp) * (y - s.lp); y = s.lp;   // DAMP: darker strings lose their brightness quicker
          const float ap = s.apc * y + s.ap; s.ap = y - s.apc * ap; y = ap;
          s.d.write(sane(x + y * s.g)); acc += y; }
        out[0][c][i] = in[0][c][i] * d + std::tanh(acc * gain) * w; } }
  }
};
static Reg rResonator(new ModuleDef{"resonator", "RESONATOR", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {ST("notes", "STRINGS", 1, 6, 4), OCTP("oct", 1, 5, 3), fmt(prExp("decay", "DECAY", 0.2f, 12, 3), [](float v) { char b[16]; std::snprintf(b, 16, "%.1fS", v); return std::string(b); }),
   pct("damp", "DAMP", 0.4f), only(pct("inharm", "INHARM", 0), "v:0,1,2"), pct("spread", "WIDTH", 0.4f), pct("mix", "MIX", 0.5f),
   noMod(OP("type", "TYPE", {"STRINGS", "MODAL", "TUBE", "RINGS"}, 0)), only(noMod(OP("rmodel", "MODEL", {"MODAL", "SYMPATHY", "STRING", "FM", "CHORDS", "STR+VERB"}, 1)), "v:3"),
   only(pct("rstruct", "STRUCTURE", 0.4f), "v:3"), only(pct("rpos", "POSITION", 0.4f), "v:3")}, 0, 68, false, [] { return new Resonator(); }});

// ---------------- PHASER: two chains of allpass filters swept by one LFO; STEREO sweeps the right side the opposite way ----------------
// MOVEMENT (it was PHASER; its key is still "phaser"). TYPE: PHASER as it always was. Light: CHORUS (a slightly late,
// wavering copy: wider and softer), FLANGER (a very short wavering copy fed back: the jet sweep; TIME sets where it sits),
// VIBRATO (only the wavering copy: the pitch wobbles). Heavy: ENSEMBLE (three wavering copies each side, each moved by a slow
// and a quick wobble together: the lush string-machine shimmer).
struct Phaser : Module {
  enum { RATE, DEPTH, CENTRE, STAGES, FB, STEREO, MIX, TYPE, TIME };
  enum { PHASE, CHORUS, FLANGE, VIB, ENSEMBLE };
  Biquad ap[2][8]; DelayLine fbd[2], md[2]; float tail[2] = {0, 0}, ph = 0, ph2 = 0, fl[2] = {}; int cnt = 0, cur = -1; DryWet dw; Smooth fbS, dpS, tmS;
  void prepare() override { for (auto& d : fbd) d.init(0.01f, sr); for (auto& d : md) d.init(0.06f, sr); dw.init(sr, 1 - e[MIX], e[MIX]); ph = rng.uni(); ph2 = rng.uni(); fbS.setTime(0.01f, sr); fbS.snap(e[FB] * 0.85f);
    dpS.setTime(0.02f, sr); dpS.snap(e[DEPTH]); tmS.setTime(0.05f, sr); tmS.snap(e[TIME]); }
  void render(int a, int b) override {
    const int ty = std::max(0, std::min(4, (int)std::lround(e[TYPE]))); if (ty != cur) { cur = ty; for (int c = 0; c < 2; c++) { md[c].clear(); fl[c] = 0; } }
    if (ty != PHASE) { renderMod(a, b, ty); return; }
    dw.target(1 - e[MIX], e[MIX]); const int n = ((int)e[STAGES] + 1) * 2; const float sw = e[CENTRE] * e[DEPTH] * 0.85f, dly = 0.003f * sr; fbS.t = e[FB] * 0.85f;
    for (int i = a; i < b; i++) {
      ph += e[RATE] / sr; if (ph >= 1) ph -= 1;
      if ((cnt++ & 15) == 0) { const float l = std::sin(TAU * ph);
        for (int s = 0; s < 2; s++) { const float f = std::max(20.f, e[CENTRE] + l * sw * (s == 1 && e[STEREO] > 0.5f ? -1 : 1)); ap[s][0].set(Biquad::AP, f, 0.6f, sr); for (int k = 1; k < n; k++) { ap[s][k].b0 = ap[s][0].b0; ap[s][k].b1 = ap[s][0].b1; ap[s][k].b2 = ap[s][0].b2; ap[s][k].a1 = ap[s][0].a1; ap[s][k].a2 = ap[s][0].a2; } } }
      const float d = dw.d.tick(), w = dw.w.tick(), fb = fbS.tick();
      for (int s = 0; s < 2; s++) {
        const float x = in[0][s][i]; float y = x + fbd[s].read(dly);
        for (int k = 0; k < n; k++) y = ap[s][k].run(y, 0);
        fbd[s].write(sane(y * fb)); out[0][s][i] = x * d + y * w;
      }
    }
    disp[0].store(ph);
  }
  void renderMod(int a, int b, int ty) {
    const float mix = ty == VIB ? 1.f : e[MIX]; dw.target(ty == VIB ? 0.f : 1 - mix * 0.5f, ty == VIB ? 1.f : mix * (ty == CHORUS ? 0.8f : 0.7f));
    fbS.t = ty == FLANGE ? e[FB] * 0.95f : 0; dpS.t = e[DEPTH]; tmS.t = e[TIME]; const bool st = e[STEREO] > 0.5f;
    for (int i = a; i < b; i++) {
      ph += e[RATE] / sr; if (ph >= 1) ph -= 1; ph2 += (e[RATE] * 7.3f + 3.1f) / sr; if (ph2 >= 1) ph2 -= 1;
      const float d = dw.d.tick(), w = dw.w.tick(), fb = fbS.tick(), dp = dpS.tick(), tm = tmS.tick();
      for (int s = 0; s < 2; s++) {
        const float off = st && s == 1 ? 0.25f : 0.f, l = std::sin(TAU * (ph + off)); const float x = in[0][s][i]; float y;
        switch (ty) {
          case CHORUS: { const float dl = (0.012f + 0.006f * dp * (1 + l) * 0.5f) * sr; y = readCubic(md[s], dl); md[s].write(x); break; }
          case VIB: { const float dl = (0.002f + 0.004f * dp * (1 + l) * 0.5f) * sr; y = readCubic(md[s], dl); md[s].write(x); break; }
          case FLANGE: { const float base = 0.0003f + 0.006f * tm * tm, dl = std::max(2.f, (base + base * 0.95f * dp * l) * sr + 1); y = readCubic(md[s], dl); md[s].write(sane(x + softLimit(y * fb))); break; }
          default: {   // ENSEMBLE: three taps 120 degrees apart, each moved by the slow wobble and a quick shimmer
            y = 0; for (int v = 0; v < 3; v++) { const float pv = ph + v / 3.f + off * 0.5f, qv = ph2 + v / 3.f + off; const float dl = (0.007f + 0.0035f * dp * (1 + std::sin(TAU * pv)) + 0.0004f * dp * std::sin(TAU * qv)) * sr; y += readCubic(md[s], dl); }
            y *= 0.45f; md[s].write(x); break; }
        }
        out[0][s][i] = x * d + y * w;
      }
    }
    disp[0].store(ph);
  }
};
static Reg rPhaser(new ModuleDef{"phaser", "MOVEMENT", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(prExp("rate", "RATE", 0.02f, 8, 0.25f), [](float v) { char b[16]; std::snprintf(b, 16, v < 1 ? "%.2fHZ" : "%.1fHZ", v); return std::string(b); }), pct("depth", "DEPTH", 0.7f),
   only(prHz("centre", "CENTRE", 150, 4000, 700), "v:0"), only(OP("stages", "STAGES", {"2", "4", "6", "8"}, 1), "v:0"), only(pr("fb", "FEEDBACK", 0, 0.9f, 0.45f), "v:0,2"), OP("stereo", "STEREO", {"OFF", "ON"}, 1), only(pct("mix", "MIX", 0.5f), "v:0,1,2,4"),
   noMod(OP("type", "TYPE", {"PHASER", "CHORUS", "FLANGER", "VIBRATO", "ENSEMBLE"}, 0)), only(pct("time", "TIME", 0.4f), "v:2")},
  5, 68, false, [] { return new Phaser(); }});

// ---------------- TAPE: wow, flutter, saturation, a darker tone and some hiss ----------------
// TYPE: CLASSIC is the tape it always was. CASSETTE: a quicker, scratchier flutter, more hiss, highs gone above 12 kHz and a
// warm bump in the bass. REEL: a studio reel-to-reel: steady and full, its saturation run at twice the rate with the tape's own
// memory (it magnetises a little differently going up than coming down), which thickens and gently squashes. DICTAPHONE: a
// pocket recorder, only the middle of the sound (300 Hz to 3.4 kHz), wobbly and crunchy. AGE: worn tape: the sound
// dips and dulls for a moment now and then.
struct Tape : Module {
  enum { WOW, FLUTTER, SAT, TONE, HISS, MIX, TYPE, AGE };
  enum { CLASSIC, CASSETTE, REEL, DICTA };
  DelayLine dry[2], wet[2]; Biquad lp, hp, bump, band, band2; Smooth base, toneS, satS, dropG; float p1 = 0, p2 = 0, p3 = 0, ph = 0, drT = 0, satK = -1, satN = 1, dropT = 1, dropLp[2] = {}, mag[2] = {}, xPrev[2] = {}, scrape = 0; int cnt = 0, cur = -1; DryWet dw;
  void prepare() override { for (int c = 0; c < 2; c++) { dry[c].init(0.1f, sr); wet[c].init(0.1f, sr); } base.setTime(0.4f, sr); base.snap(0.012f); hp.set(Biquad::HP, 2500, 0.707f, sr); dw.init(sr, 1 - e[MIX], e[MIX]);
    toneS.setTime(0.015f, sr); toneS.snap(e[TONE]); satS.setTime(0.015f, sr); satS.snap(e[SAT]); lp.set(Biquad::LP, e[TONE], 0.75f, sr); dropG.setTime(0.02f, sr); dropG.snap(1); cur = -1; }
  void setType(int ty) { cur = ty; bump.reset(); band.reset(); band2.reset();
    if (ty == CASSETTE) { bump.set(Biquad::PEAK, 90, 1.f, sr, 3); band.set(Biquad::HP, 30, 0.707f, sr); }
    else if (ty == DICTA) { bump.set(Biquad::PEAK, 1500, 0.8f, sr, 4); band.set(Biquad::HP, 300, 0.707f, sr); band2.set(Biquad::HP, 300, 0.707f, sr); }
    else if (ty == REEL) { bump.set(Biquad::PEAK, 55, 1.2f, sr, 2.5f); band.set(Biquad::HP, 20, 0.707f, sr); } }
  void render(int a, int b) override {
    const int ty = std::max(0, std::min(3, (int)std::lround(e[TYPE]))); if (ty != cur) setType(ty);
    // how each kind moves: wow and flutter (how much, how fast), hiss, how far the highs reach
    const float wowK = ty == CASSETTE ? 1.3f : ty == DICTA ? 2.f : ty == REEL ? 0.4f : 1, flK = ty == CASSETTE ? 1.6f : ty == DICTA ? 3.f : ty == REEL ? 0.3f : 1, flHz = ty == CASSETTE ? 9.f : ty == DICTA ? 12.f : ty == REEL ? 5.f : 6.3f,
      wowHz = ty == CASSETTE ? 0.8f : ty == DICTA ? 1.4f : 0.55f, hissK = ty == CASSETTE ? 1.8f : ty == DICTA ? 2.5f : ty == REEL ? 0.4f : 1, topHz = ty == CASSETTE ? 12000.f : ty == DICTA ? 3400.f : 20000.f, satX = ty == DICTA ? 1.5f : 1;
    dw.target(1 - e[MIX], e[MIX]); toneS.t = std::min(e[TONE], topHz); satS.t = e[SAT] * satX;
    const float hiss = e[HISS] * e[HISS] * 0.03f * hissK, dt = 1.f / sr, age = ty == CLASSIC ? 0.f : e[AGE];
    drT -= (b - a) * dt; if (drT <= 0) { drT = rng.range(0.4f, 1.5f); base.t = 0.012f + rng.bi() * e[WOW] * wowK * 0.0015f; }   // a slow random drift on top of the wow
    if (age > 0) { dropT -= (b - a) * dt; if (dropT <= 0) { const bool on = dropG.t > 0.99f; dropG.t = on ? 1 - age * rng.range(0.3f, 0.8f) : 1; dropT = on ? rng.range(0.02f, 0.12f) : rng.range(0.3f, 4.f) / (0.2f + age * 2); } }   // AGE: a dip now and then
    else dropG.t = 1;
    for (int i = a; i < b; i++) {
      p1 += wowHz * dt; if (p1 >= 1) p1 -= 1; p2 += flHz * dt; if (p2 >= 1) p2 -= 1;
      if (ty == CASSETTE || ty == DICTA) scrape += 0.05f * (rng.bi() - scrape);   // scrape flutter: fast and irregular
      const float dl = (base.tick() + std::sin(TAU * p1) * e[WOW] * wowK * 0.0035f + (std::sin(TAU * p2) + scrape * 2) * e[FLUTTER] * flK * 0.00035f) * sr;
      const float h = hp.run(rng.bi(), 0) * hiss, d = dw.d.tick(), w = dw.w.tick(), dg = dropG.tick();
      const float tn = toneS.tick(), k = 1 + satS.tick() * 7, sN = 1.f / std::pow(k, 0.7f); if ((cnt++ & 3) == 0) lp.set(Biquad::LP, tn, 0.75f, sr);   // TONE and SATURATE glide (they jumped once a block)
      for (int c = 0; c < 2; c++) { const float x = in[0][c][i]; dry[c].write(x); float sx;
        if (ty == REEL) { float o = 0;   // twice the rate: the tape's memory (it follows rising and falling a little differently)
          for (int os = 0; os < 2; os++) { const float xi = os == 0 ? 0.5f * (xPrev[c] + x) : x, up = std::tanh((xi - xPrev[c]) * 150);   /* rising or falling, eased (a hard switch would alias) */ const float tgt = std::tanh(k * xi + 0.08f * up * std::min(1.f, k * 0.3f));
            mag[c] += (tgt - mag[c]) * 0.85f; o += mag[c]; } xPrev[c] = x; sx = o * 0.5f * sN; }
        else if (ty == CLASSIC) sx = std::tanh(x * k) * sN;
        else sx = (std::tanh(x * k + 0.05f * k) - std::tanh(0.05f * k)) * sN;   // a little uneven, like cheap tape and heads
        if (ty != CLASSIC) { sx = bump.run(band.run(sx, c), c); if (ty == DICTA) sx = band2.run(sx, c) * 1.5f; }
        wet[c].write(sx);
        float y = lp.run(wet[c].read(std::max(1.f, dl)), c);
        if (age > 0) { dropLp[c] += (0.15f + 0.85f * dg * dg) * (y - dropLp[c]); y = dropLp[c] * dg; }   // a dip is quieter and duller
        out[0][c][i] = dry[c].read(0.012f * sr) * d + (y + h) * w; }
    }
    ph += (b - a) * dt * 0.55f * (1 + std::cos(TAU * p1) * e[WOW] * 0.6f); ph -= std::floor(ph); disp[0].store(ph);   // the reels: wow makes them surge and lag
    disp[1].store(e[SAT]); disp[2].store(std::sin(TAU * p2) * e[FLUTTER]);
  }
};
static Reg rTape(new ModuleDef{"tape", "TAPE", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {pct("wow", "WOW", 0.3f), pct("flutter", "FLUTTER", 0.2f), pct("sat", "SATURATE", 0.3f), prHz("tone", "TONE", 800, 16000, 6000), pct("hiss", "HISS", 0.1f), pct("mix", "MIX", 1),
   noMod(OP("type", "TYPE", {"CLASSIC", "CASSETTE", "REEL", "DICTAPHONE"}, 0)), only(pct("age", "AGE", 0.2f), "v:1,2,3")},
  36, 68, false, [] { return new Tape(); }});


// ---------------- TRIG ENV: each trigger draws a rise and a fall ----------------
struct TrigEnv : Module {
  enum { ATK, DEC, CURVE, VSENS, DEPTH, DIR };
  float lv = 0, peak = 1; int stage = 0;
  void trig(int, const Event& ev) override { stage = 1; peak = 1 - e[VSENS] * (1 - ev.vel); }
  void render(int a, int b) override {
    const float dt = (b - a) / sr;
    if (stage == 1) { lv += dt / std::max(0.002f, e[ATK]); if (lv >= peak) { lv = peak; stage = 2; } }
    else if (stage == 2) { const float lin = peak * dt / e[DEC], ex = lv * (1 - std::exp(-dt * 5 / e[DEC])); lv = std::max(0.f, lv - (lin * (1 - e[CURVE]) + ex * e[CURVE])); if (lv < 0.0005f) { lv = 0; stage = 0; } }
    modv = lv * 2 - 1; disp[0].store(modv);
  }
};
static Reg rEnv(new ModuleDef{"env", "TRIG ENV", Cat::Mod, {{"TRIG", PType::Trig}}, {{"MOD", PType::Mod}},
  {T("atk", "ATTACK", 0.002f, 8, 0.05f), T("dec", "DECAY", 0.02f, 16, 1.5f), pct("curve", "CURVE", 0.6f), pct("vsens", "VEL SENS", 1), pct("depth", "DEPTH", 0.5f), OP("dir", "DIRECTION", {"BOTH", "UP", "DOWN"}, 1)},
  7, 68, false, [] { return new TrigEnv(); }});

// ---------------- RANDOM: a new value every so often (or on each trigger), with a glide between ----------------
struct RandomMod : Module {
  enum { RATE, GLIDE, CHANCE, DEPTH, DIR };
  float v = 0, target = 0, next = 0;
  void pick() { if (rng.uni() < e[CHANCE]) target = rng.bi(); }
  void blockStart(int) override { clockedIn.store(patched(0)); }
  void trig(int, const Event&) override { pick(); }
  void render(int a, int b) override {
    const float dt = (b - a) / sr;
    if (!clockedIn.load()) { next -= dt; if (next <= 0) { next = e[RATE]; pick(); } }
    v += (target - v) * (1 - std::exp(-dt * 3 / std::max(0.001f, e[GLIDE]))); modv = v; disp[0].store(v);
  }
};
static Reg rRand(new ModuleDef{"rand", "RANDOM", Cat::Mod, {{"TRIG", PType::Trig}}, {{"MOD", PType::Mod}},
  {fmt(prExp("rate", "EVERY", 0.05f, 60, 2), [](float v) { return fmtSecs(v); }), T("glide", "GLIDE", 0.001f, 8, 0.05f), pct("chance", "CHANCE", 1), pct("depth", "DEPTH", 0.5f), OP("dir", "DIRECTION", {"BOTH", "UP", "DOWN"}, 0)},
  7, 68, false, [] { return new RandomMod(); }});

// ---------------- FOLLOWER: how loud the incoming sound is, as a modulation ----------------
struct Follower : Module {
  enum { GAIN, ATK, REL, DEPTH, DIR, THRESH, HOLDOFF };
  float ms = 0, env = 0, pk = 0, slow = 0, since = 1;
  void render(int a, int b) override {
    const float kw = 1 - std::exp(-1.f / (0.021f * sr));   // about the same window as the web version's 1024 samples
    const float kp = 1.f / (0.03f * sr), ks = 1 - std::exp(-1.f / (0.25f * sr)), thr = dbToGain(e[THRESH]);
    for (int i = a; i < b; i++) { const float x = 0.5f * (in[0][0][i] + in[0][1][i]); ms += (x * x - ms) * kw;
      // CLOCK: a trigger on each hit: the level jumps past THRESHOLD and well above what it's been, at most once each HOLD-OFF
      const float ax = std::fabs(x) * dbToGain(e[GAIN] - 12); pk = ax > pk ? ax : std::max(0.f, pk - kp * pk * 3); slow += (pk - slow) * ks; since += 1.f / sr;
      if (pk > thr && pk > slow * 1.8f + 1e-4f && since > e[HOLDOFF]) { since = 0; eng->pos = i; Event ev; ev.vel = clampf(pk / std::max(thr, 0.05f), 0.2f, 1); emit(1, ev); } }
    const float target = clampf(std::sqrt(std::max(0.f, ms)) * 1.4f * dbToGain(e[GAIN]), 0, 1), tc = target > env ? e[ATK] : e[REL], dt = (b - a) / sr;
    env += (target - env) * (1 - std::exp(-dt * 3 / std::max(0.002f, tc))); modv = env * 2 - 1; disp[0].store(modv);
  }
};
static Reg rFollow(new ModuleDef{"follow", "FOLLOWER", Cat::Mod, {{"IN", PType::Audio}}, {{"MOD", PType::Mod}, {"CLOCK", PType::Trig}},
  {fmt(pr("gain", "GAIN", -12, 36, 12), [](float v) { return (v > 0 ? "+" : "") + std::to_string((int)std::lround(v)) + "DB"; }), T("atk", "ATTACK", 0.002f, 2, 0.02f), T("rel", "RELEASE", 0.02f, 8, 0.4f), pct("depth", "DEPTH", 0.5f), OP("dir", "DIRECTION", {"BOTH", "UP", "DOWN"}, 1),
   fmt(pr("thresh", "THRESHOLD", -48, 0, -30), [](float v) { return std::to_string((int)std::lround(v)) + "DB"; }), T("hold", "HOLD-OFF", 0.03f, 1, 0.1f)},
  7, 68, false, [] { return new Follower(); }});

}  // namespace tb

namespace tb {
// ---------------- dynamics: COMPRESSOR, LIMITER, MASTER ----------------
static inline float toDb(float g) { return 20.f * std::log10(std::max(g, 1e-6f)); }
static std::string dbText(float v) { char b[16]; std::snprintf(b, 16, "%+.1fDB", v); return std::string(b); }
static std::string msText(float v) { char b[16]; if (v < 0.01f) std::snprintf(b, 16, "%.1fMS", v * 1000); else if (v < 1) std::snprintf(b, 16, "%.0fMS", v * 1000); else std::snprintf(b, 16, "%.2fS", v); return std::string(b); }

// how much to turn down, in dB, for a level in dB: above THRESHOLD by RATIO, eased in over KNEE
static inline float compGain(float lev, float thr, float ratio, float knee) {
  const float over = lev - thr, k = std::max(0.01f, knee);
  if (over <= -k / 2) return 0; if (over >= k / 2) return over * (1 / ratio - 1);
  const float x = over + k / 2; return (1 / ratio - 1) * x * x / (2 * k);
}

// COMPRESSOR: turns loud parts down. Listens to IN, or to KEY when something is plugged in there (sidechain)
// TYPE: CLASSIC is the clean compressor it always was. VCA: listens to the average loudness (not the peaks): punchy and tight,
// good on drums and the mix. OPTO: smooth and musical, like the old light-and-photocell kind: a gentle attack, and the longer
// and harder it works the slower it lets go (RELEASE sets how slow at most). FET: fast and aggressive, listening to its own
// output, with COLOUR adding its grit. Heavy: MULTIBAND splits the sound at 200 Hz and 2.5 kHz and squashes lows, mids and highs
// each on their own, so a loud bass doesn't pump the rest.
struct CompModule : Module {
  enum { THR, RATIO, ATK, REL, KNEE, MAKEUP, MIX, TYPE, COLOUR };
  enum { CLASSIC, VCA, OPTO, FET, MULTI };
  float env = -120, gr = 0, peakDisp = -120, peakR = -120, ms = 0, charge = 0, fbL = 0, fbR = 0, bEnv[3] = {-120, -120, -120}; Smooth mk, dry, th, ra, col;   // THRESHOLD and RATIO glide too (they jumped once a block when modulated: a faint zipper)
  Biquad xo[3][2][2][2]; int cur = -1;   // MULTIBAND's crossovers: [which][low/high][two in a row][]
  void prepare() override { mk.setTime(0.02f, sr); mk.snap(dbToGain(e[MAKEUP])); dry.setTime(0.02f, sr); dry.snap(1 - e[MIX]); th.setTime(0.01f, sr); th.snap(e[THR]); ra.setTime(0.01f, sr); ra.snap(e[RATIO]); col.setTime(0.02f, sr); col.snap(e[COLOUR]); cur = -1; }
  void setType(int ty) { cur = ty; env = -120; gr = 0; ms = 0; charge = 0; fbL = fbR = 0; for (float& v : bEnv) v = -120;
    if (ty == MULTI) { const float F[3] = {200, 2500, 2500}; for (int x = 0; x < 3; x++) for (int s2 = 0; s2 < 2; s2++) { xo[x][0][s2][0].set(Biquad::LP, F[x], 0.7071f, sr); xo[x][1][s2][0].set(Biquad::HP, F[x], 0.7071f, sr); xo[x][0][s2][0].reset(); xo[x][1][s2][0].reset(); } } }
  // a Linkwitz-Riley split (two Butterworths in a row each way): the two halves add back up flat
  inline void split(int x, float in, int ch, float& lo, float& hi) { lo = xo[x][0][1][0].run(xo[x][0][0][0].run(in, ch), ch); hi = xo[x][1][1][0].run(xo[x][1][0][0].run(in, ch), ch); }
  void render(int a, int b) override {
    const int ty = std::max(0, std::min(4, (int)std::lround(e[TYPE]))); if (ty != cur) setType(ty);
    const bool key = patched(1); const float aC = std::exp(-1.f / (std::max(0.0001f, e[ATK]) * sr)), rC = std::exp(-1.f / (std::max(0.005f, e[REL]) * sr));
    mk.t = dbToGain(e[MAKEUP]); dry.t = 1 - e[MIX]; th.t = e[THR]; ra.t = e[RATIO]; col.t = e[COLOUR]; float pk = 0, pkR = 0, worst = 0;
    const float msC = std::exp(-1.f / (0.008f * sr)), optoA = std::exp(-1.f / (0.012f * sr));
    for (int i = a; i < b; i++) {
      const float l = in[0][0][i], r = in[0][1][i], dl = key ? in[1][0][i] : l, dr = key ? in[1][1][i] : r;
      const float thr = th.tick(), rat = ra.tick(), d = dry.tick(), w = 1 - d, m = mk.tick(), cl = col.tick(); float ol, orr;
      if (ty == MULTI) {   // three bands, each with its own squash (the lows a little slower, the highs a little quicker)
        float lo[2], mid[2], hi[2]; const float xs[2] = {l, r};
        for (int c = 0; c < 2; c++) { float L1, H1; split(0, xs[c], c, L1, H1); float M1, H2; split(1, H1, c, M1, H2); float La, Lb; split(2, L1, c, La, Lb); lo[c] = La + Lb; mid[c] = M1; hi[c] = H2; }   // (the lows go through the upper split too, so the phases line up)
        float* bands[3] = {lo, mid, hi}; ol = orr = 0;
        for (int k = 0; k < 3; k++) { const float lev = toDb(key ? std::max(std::fabs(dl), std::fabs(dr)) : std::max(std::fabs(bands[k][0]), std::fabs(bands[k][1]))), sl = k == 0 ? 2.f : k == 2 ? 0.5f : 1.f;
          const float ak = std::pow(aC, 1 / sl), rk = std::pow(rC, 1 / sl);
          bEnv[k] = lev > bEnv[k] ? lev + ak * (bEnv[k] - lev) : lev + rk * (bEnv[k] - lev); const float g2 = compGain(bEnv[k], thr, rat, e[KNEE]); worst = std::min(worst, g2);
          const float gg = dbToGain(g2); ol += bands[k][0] * gg; orr += bands[k][1] * gg; }
        ol *= m; orr *= m;
      } else {
        float lev;
        if (ty == VCA) { ms = std::max(dl * dl, dr * dr) + msC * (ms - std::max(dl * dl, dr * dr)); lev = 10 * std::log10(std::max(ms * 2, 1e-12f)); }   // the average (RMS), lifted to a sine's peak
        else if (ty == FET) lev = toDb(std::max(std::fabs(fbL), std::fabs(fbR)));   // it listens to what comes out of it
        else lev = toDb(std::max(std::fabs(dl), std::fabs(dr)));
        if (ty == OPTO) { const float rel = std::exp(-1.f / ((0.06f + charge * e[REL] * 2.5f) * sr));   // the cell remembers: the more it's worked, the slower it lets go
          env = lev > env ? lev + optoA * (env - lev) : lev + rel * (env - lev); charge += ((gr < -3 ? 1.f : 0.f) - charge) * (gr < -3 ? 0.3f : 0.05f) / sr; }
        else env = lev > env ? lev + aC * (env - lev) : lev + rC * (env - lev);   // the level it follows: quick up (ATTACK), slower down (RELEASE)
        gr = ty == FET ? std::max(-40.f, compGain(env, thr, rat, e[KNEE]) * rat) : ty == OPTO ? compGain(env, thr, rat, std::max(12.f, e[KNEE])) : compGain(env, thr, rat, e[KNEE]); worst = std::min(worst, gr);
        const float g = dbToGain(gr) * m; ol = l * g; orr = r * g;
        if (ty == FET) { fbL = l * dbToGain(gr); fbR = r * dbToGain(gr); if (cl > 0.001f) { const float k = 1 + cl * 3; auto sat = [&](float x) { return (std::tanh(k * x + 0.1f * cl) - std::tanh(0.1f * cl)) / k * (1 + cl * 0.5f); }; ol = sat(ol); orr = sat(orr); } }
      }
      out[0][0][i] = l * d + ol * w; out[0][1][i] = r * d + orr * w; pk = std::max(pk, std::fabs(out[0][0][i])); pkR = std::max(pkR, std::fabs(out[0][1][i]));
    }
    disp[0].store(worst, std::memory_order_relaxed); peakDisp = std::max(toDb(pk), peakDisp - 0.5f); disp[1].store(peakDisp, std::memory_order_relaxed); this->peakR = std::max(toDb(pkR), this->peakR - 0.5f); disp[3].store(this->peakR, std::memory_order_relaxed);
  }
};
static Reg rComp(new ModuleDef{"comp", "COMPRESSOR", Cat::Proc, {{"IN", PType::Audio}, {"KEY", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(pr("thr", "THRESHOLD", -48, 0, -18), dbText), noMod(fmt(prExp("ratio", "RATIO", 1, 20, 4), [](float v) { char b[16]; std::snprintf(b, 16, "%.1f:1", v); return std::string(b); })),
   only(fmt(prExp("atk", "ATTACK", 0.0001f, 0.2f, 0.01f), msText), "v:0,1,3,4"), fmt(prExp("rel", "RELEASE", 0.01f, 2, 0.2f), msText),
   noMod(fmt(pr("knee", "KNEE", 0, 18, 6), [](float v) { char b[16]; std::snprintf(b, 16, "%.0fDB", v); return std::string(b); })), noMod(fmt(pr("makeup", "MAKEUP", 0, 24, 0), dbText)), pct("mix", "MIX", 1),
   noMod(OP("type", "TYPE", {"CLASSIC", "VCA", "OPTO", "FET", "MULTIBAND"}, 0)), only(pct("colour", "COLOUR", 0.3f), "v:3")},
  20, 68, false, [] { return new CompModule(); }});

// a look-ahead peak limiter: the sound is held back a moment so the gain can come down before a peak arrives
struct PeakLimiter {
  static constexpr int LA = 96; float bufL[LA] = {}, bufR[LA] = {}, want[LA] = {}; int w = 0; float g = 1;
  inline void run(float& l, float& r, float ceil, float relC) {
    const float pk = std::max(std::fabs(l), std::fabs(r)), tgt = pk > ceil ? ceil / pk : 1.f;
    bufL[w] = l; bufR[w] = r; want[w] = tgt; w = (w + 1) % LA;
    float mn = 1; for (int k = 0; k < LA; k += 4) mn = std::min(mn, want[k]);   // the lowest gain needed in the next 2 ms (every 4th is plenty)
    for (int k = 0; k < 4; k++) mn = std::min(mn, want[(w + LA - 1 - k) % LA]);
    g = mn < g ? g + (mn - g) * 0.25f : mn + relC * (g - mn);
    l = clampf(bufL[w] * g, -ceil, ceil); r = clampf(bufR[w] * g, -ceil, ceil);
  }
};
// LIMITER: nothing gets past CEILING. DRIVE pushes it harder into it, for a louder, denser sound
struct Limiter : Module {
  enum { DRIVE, CEIL, REL };
  PeakLimiter lim; float peakDisp = -120, peakR = -120; Smooth dvS; bool fresh = true;   // DRIVE glides
  void render(int a, int b) override {
    if (fresh) { dvS.setTime(0.01f, sr); dvS.snap(dbToGain(e[DRIVE])); fresh = false; } dvS.t = dbToGain(e[DRIVE]);
    const float ce = dbToGain(e[CEIL]), rC = std::exp(-1.f / (std::max(0.005f, e[REL]) * sr)); float worst = 1, pk = 0, pkR = 0;
    for (int i = a; i < b; i++) { const float dv = dvS.tick(); float l = in[0][0][i] * dv, r = in[0][1][i] * dv; lim.run(l, r, ce, rC); worst = std::min(worst, lim.g); out[0][0][i] = l; out[0][1][i] = r; pk = std::max(pk, std::fabs(l)); pkR = std::max(pkR, std::fabs(r)); }
    disp[0].store(toDb(worst), std::memory_order_relaxed); peakDisp = std::max(toDb(pk), peakDisp - 0.5f); disp[1].store(peakDisp, std::memory_order_relaxed); this->peakR = std::max(toDb(pkR), this->peakR - 0.5f); disp[3].store(this->peakR, std::memory_order_relaxed);
  }
};
static Reg rLimiter(new ModuleDef{"limit", "LIMITER", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {fmt(pr("drive", "DRIVE", 0, 24, 0), dbText), noMod(fmt(pr("ceil", "CEILING", -12, 0, -0.3f), dbText)), fmt(prExp("rel", "RELEASE", 0.01f, 1, 0.1f), msText)},
  20, 68, false, [] { return new Limiter(); }});

// MASTER: a light finishing chain for just before OUTPUT: rumble out, a gentle tone tilt, a little warmth,
// soft glue compression, stereo width, and a limiter so nothing clips
struct Master : Module {
  enum { LOWCUT, TILT, WARMTH, GLUE, WIDTH, CEIL };
  Biquad hp, ls, hs; PeakLimiter lim; float env = -120, peakDisp = -120, peakR = -120; float lastTilt = 99, lastCut = -1; Smooth tiltS, warmS, glueS; bool fresh = true; int cnt = 0;
  void render(int a, int b) override {
    if (e[LOWCUT] != lastCut) { lastCut = e[LOWCUT]; hp.set(Biquad::HP, e[LOWCUT], 0.707f, sr); }
    if (fresh) { for (Smooth* q : {&tiltS, &warmS, &glueS}) q->setTime(0.015f, sr); tiltS.snap(e[TILT]); warmS.snap(e[WARMTH]); glueS.snap(e[GLUE]); fresh = false; }
    tiltS.t = e[TILT]; warmS.t = e[WARMTH]; glueS.t = e[GLUE];   // TILT, WARMTH and GLUE glide (they jumped once a block when modulated)
    const float aC = std::exp(-1.f / (0.03f * sr)), rC = std::exp(-1.f / (0.3f * sr));
    const float ce = dbToGain(e[CEIL]), lrC = std::exp(-1.f / (0.08f * sr)), wd = e[WIDTH];
    float worstL = 1, worstG = 0, pk = 0, pkR = 0;
    for (int i = a; i < b; i++) {
      const float tl = tiltS.tick(), wet = warmS.tick(), glue = glueS.tick();
      if ((cnt++ & 15) == 0 && std::fabs(tl - lastTilt) > 1e-4f) { lastTilt = tl; ls.setShelf(false, 700, -tl * 4, sr); hs.setShelf(true, 1400, tl * 4, sr); }   // TILT: darker to the left, brighter to the right
      const float drv = 1 + wet * 3, norm = 1 / std::tanh(drv), thr = -4 - 16 * glue, ratio = 1 + 2 * glue;
      float l = in[0][0][i], r = in[0][1][i];
      l = hs.run(ls.run(hp.run(l, 0), 0), 0); r = hs.run(ls.run(hp.run(r, 1), 1), 1);
      if (wet > 0) { l = l * (1 - wet) + std::tanh(l * drv) * norm * wet; r = r * (1 - wet) + std::tanh(r * drv) * norm * wet; }   // WARMTH: soft, tape-like rounding
      if (glue > 0.0005f) { const float lev = toDb(std::max(std::fabs(l), std::fabs(r))); env = lev > env ? lev + aC * (env - lev) : lev + rC * (env - lev);
        const float gr = compGain(env, thr, ratio, 8); worstG = std::min(worstG, gr); const float g = dbToGain(gr - gr * 0.35f * glue); l *= g; r *= g; }   // GLUE, with a little of it made back up
      const float m = (l + r) * 0.5f, sd = (l - r) * 0.5f * wd; l = m + sd; r = m - sd;   // WIDTH: 0 mono, 1 as it was, 2 wider
      lim.run(l, r, ce, lrC); worstL = std::min(worstL, lim.g);
      out[0][0][i] = l; out[0][1][i] = r; pk = std::max(pk, std::fabs(l)); pkR = std::max(pkR, std::fabs(r));
    }
    disp[0].store(toDb(worstL), std::memory_order_relaxed); disp[2].store(worstG, std::memory_order_relaxed);
    peakDisp = std::max(toDb(pk), peakDisp - 0.5f); disp[1].store(peakDisp, std::memory_order_relaxed); this->peakR = std::max(toDb(pkR), this->peakR - 0.5f); disp[3].store(this->peakR, std::memory_order_relaxed);
  }
};
static Reg rMaster(new ModuleDef{"master", "MASTER", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {noMod(fmt(prExp("lowcut", "LOW CUT", 15, 200, 30), [](float v) { return std::to_string((int)std::lround(v)) + "HZ"; })),
   fmt(pr("tilt", "TILT", -1, 1, 0), [](float v) { if (std::fabs(v) < 0.02f) return std::string("FLAT"); char b[16]; std::snprintf(b, 16, "%s %.0f%%", v < 0 ? "DARKER" : "BRIGHTER", std::fabs(v) * 100); return std::string(b); }),
   pct("warmth", "WARMTH", 0.2f), pct("glue", "GLUE", 0.3f),
   fmt(pr("width", "WIDTH", 0, 2, 1), [](float v) { return std::to_string((int)std::lround(v * 100)) + "%"; }), noMod(fmt(pr("ceil", "CEILING", -6, 0, -0.5f), dbText))},
  20, 68, false, [] { return new Master(); }});
}  // namespace tb
