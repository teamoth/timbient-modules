// Timbient native: NO-INPUT MIXER. A mixer fed back into itself, as players of real mixing desks do: each channel's
// output goes back into the channels (ROUTING), through its GAIN, an EQ boost at its FREQ (where it sings), the shared
// LOW and HIGH shelves and some DRIVE. Near where a channel's gain just tips into feedback it chirps, squeals and hums;
// above it, it sings steadily. IN (optional) nudges it: patch a sound in and turn INPUT up.
// All of it runs sample by sample inside the module (patching the MIXER into itself would add the engine's
// block of delay and force every pitch onto one grid), and a ceiling at the end keeps it safe for ears and speakers.
#include "params.h"
#include "dsp2.h"

namespace tb {

struct NoInput : Module {
  enum { ROUTE, G1, F1, G2, F2, G3, F3, G4, F4, LOW, HIGH, DRIVE, INPUT, LEVEL };
  static constexpr int C = 4;
  Biquad pk[C], ls[C], hs[C], bl[C], hpf[C]; float y[C] = {}, dc[C] = {}; Smooth gS[C], fS[C], lowS, highS, drS, inS, lvS; int cnt = 0;
  void prepare() override {
    for (int c = 0; c < C; c++) { gS[c].setTime(0.02f, sr); gS[c].snap(e[G1 + 2 * c]); fS[c].setTime(0.02f, sr); fS[c].snap(std::log(e[F1 + 2 * c])); bl[c].set(Biquad::LP, 12000, 0.6f, sr); hpf[c].set(Biquad::HP, 45, 0.6f, sr); }
    for (Smooth* s : {&lowS, &highS, &drS, &inS, &lvS}) s->setTime(0.02f, sr);
    lowS.snap(e[LOW]); highS.snap(e[HIGH]); drS.snap(e[DRIVE]); inS.snap(e[INPUT]); lvS.snap(e[LEVEL]);
    retune();
  }
  void retune() { for (int c = 0; c < C; c++) { pk[c].set(Biquad::PEAK, std::exp(fS[c].v), 5.f, sr, 10.f); ls[c].setShelf(false, 200, lowS.v, sr); hs[c].setShelf(true, 3000, highS.v, sr); } }
  void render(int a, int b) override {
    for (int c = 0; c < C; c++) { gS[c].t = e[G1 + 2 * c]; fS[c].t = std::log(std::max(30.f, e[F1 + 2 * c])); }
    lowS.t = e[LOW]; highS.t = e[HIGH]; drS.t = e[DRIVE]; inS.t = e[INPUT]; lvS.t = e[LEVEL];
    // ROUTING: where each channel takes its feedback from
    float R[C][C] = {};
    switch ((int)std::lround(e[ROUTE])) {
      default: case 0: for (int c = 0; c < C; c++) R[c][c] = 1; break;                          // SELF: each channel into itself
      case 1: for (int c = 0; c < C; c++) R[c][(c + C - 1) % C] = 1; break;                     // RING: 1 into 2 into 3 into 4 into 1
      case 2: R[0][1] = R[1][0] = R[2][3] = R[3][2] = 1; break;                                // PAIRS: 1 and 2 into each other, 3 and 4 too
      case 3: for (int c = 0; c < C; c++) for (int j = 0; j < C; j++) R[c][j] = c == j ? 0.4f : 0.35f; break;   // ALL: everything into everything
    }
    for (int i = a; i < b; i++) {
      const float x = 0.5f * (in[0][0][i] + in[0][1][i]) * inS.tick() * 2, dr = 1 + drS.tick() * 5, lv = lvS.tick(); lowS.tick(); highS.tick();
      float g[C]; for (int c = 0; c < C; c++) { const float q = gS[c].tick(); g[c] = 0.05f + q * q * 2.5f; fS[c].tick(); }   // GAIN: about a third of the way up it tips into feedback on its own; higher, enough for rings and pairs too
      if ((cnt++ & 7) == 0) retune();   // FREQ, LOW and HIGH glide (the EQs re-tuned every 8 samples)
      float ny[C];
      for (int c = 0; c < C; c++) {
        float v = x + (rng.uni() * 2 - 1) * 1e-5f;   // a desk's own faint hiss: what the feedback grows from
        for (int j = 0; j < C; j++) if (R[c][j] != 0) v += R[c][j] * y[j];
        v *= g[c];
        v = (float)pk[c].run(v, 0); v = (float)ls[c].run(v, 0); v = (float)hs[c].run(v, 0); v = (float)bl[c].run(v, 0);
        v = (float)hpf[c].run(v, 0);   // nothing below about 45 Hz goes round (it would rumble, or creep off-centre)
        ny[c] = softLimit(v * dr) / dr;    // DRIVE: how hard it saturates (and what holds it in bounds); quiet signals pass unchanged, so GAIN alone decides when it tips over
      }
      for (int c = 0; c < C; c++) y[c] = sane(ny[c]);
      const float mk = std::sqrt(dr) * 1.8f;   // louder again after the drive, then the ceiling
      const float L = (y[0] + 0.35f * y[1] + y[2] * 0.75f + 0.25f * y[3]) * 0.4f, Rr = (0.35f * y[0] + y[1] + 0.25f * y[2] + 0.75f * y[3]) * 0.4f;
      out[0][0][i] = softLimit(L * lv * mk) * 0.7f; out[0][1][i] = softLimit(Rr * lv * mk) * 0.7f;   // the ceiling: never louder than this
    }
  }
};

static Reg rNoInput(new ModuleDef{"noinput", "NO-INPUT MIXER", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {noMod(OP("route", "ROUTING", {"SELF", "RING", "PAIRS", "ALL"}, 0)),
   pct("g1", "GAIN 1", 0.5f), prHz("f1", "FREQ 1", 40, 8000, 220),
   pct("g2", "GAIN 2", 0.f), prHz("f2", "FREQ 2", 40, 8000, 660),
   pct("g3", "GAIN 3", 0.f), prHz("f3", "FREQ 3", 40, 8000, 1500),
   pct("g4", "GAIN 4", 0.f), prHz("f4", "FREQ 4", 40, 8000, 3300),
   fmt(pr("low", "LOW", -15, 15, 0), [](float v) { char b[16]; std::snprintf(b, 16, "%+.0f DB", v); return std::string(b); }),
   fmt(pr("high", "HIGH", -15, 15, 0), [](float v) { char b[16]; std::snprintf(b, 16, "%+.0f DB", v); return std::string(b); }),
   pct("drive", "DRIVE", 0.3f), pct("input", "INPUT", 0.f), pct("level", "LEVEL", 0.5f)}, 0, 72, false, [] { return new NoInput(); }});

}  // namespace tb
