// Timbient native: LEVEL (the smallest: a level and a meter, and a place to merge a few cables) and CROSSFADER (A to B, by
// hand, or moving by itself in the LFO's shapes, to a clock or its own tempo).
#include "params.h"

namespace tb {

// ---------------- LEVEL: in, out, how loud, and a meter ----------------
struct Level : Module {
  Smooth g; float peak[2] = {0, 0};
  void prepare() override { g.setTime(0.012f, sr); g.snap(e[0]); }
  void render(int a, int b) override {
    g.t = e[0];
    for (int i = a; i < b; i++) { const float k = g.tick(); for (int ch = 0; ch < 2; ch++) { const float y = sane(in[0][ch][i] * k); out[0][ch][i] = y; peak[ch] = std::max(peak[ch] * 0.9999f, std::fabs(y)); } }
    disp[0].store(peak[0]); disp[1].store(peak[1]);
  }
};
// ---------------- NOTES: a box to type into (no sound; the screen keeps the text and its size, saved with the session) ----------------
struct Notes : Module { void render(int, int) override {} };
static Reg rNotes(new ModuleDef{"notes", "NOTES", Cat::Misc, {}, {}, {}, 40, 98, false, [] { return new Notes(); }});
static Reg rLevel(new ModuleDef{"level", "LEVEL", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}}, {pctX("level", "LEVEL", 2, 1)}, 13, 58, false, [] { return new Level(); }});

// ---------------- CROSSFADER: A to B ----------------
// FADE: where it sits (all A on the left, all B on the right), by hand or with a modulation cable. MOVE sets it going by itself in
// one of the LFO's shapes: SINE, TRI, SQUARE, RANDOM (a jump to a new place each cycle) or DRIFT (a slow wander); CYCLE is how
// long one round takes, in beats: of a clock in CLOCK, or of its own TEMPO (OWN: its BPM, which you can tap; SHARED: the
// session's). RANGE: how far it travels either side of FADE. The fade keeps the loudness even all the way across.
namespace {
struct XClock { double last = -1; float cp = 0, ppb = 1; long pulses = 0;
  void tick(double t, const Event& ev) { if (ev.period > 0) cp = ev.period; else if (last >= 0) { const float p = (float)(t - last); if (p > 0.01f && p < 8) cp = cp > 0 ? cp * 0.6f + p * 0.4f : p; }
    last = t; ppb = ev.ppb > 0 ? ev.ppb : (ppb > 0 ? ppb : 1); pulses++; } };
const float CYCLES[8] = {0.25f, 0.5f, 1, 2, 4, 8, 16, 32};
}
struct Crossfader : Module {
  enum { FADE, MOVE, CYCLE, RANGE, SYNC, BPM };
  float ph = 0, a0 = 0, b0 = 0, pos = 0.5f, gA = 0.7071f, gB = 0.7071f; XClock ck;
  void prepare() override { ph = 0; a0 = rng.bi(); b0 = rng.bi(); pos = e[FADE]; gA = std::cos(pos * PI / 2); gB = std::sin(pos * PI / 2); }
  void blockStart(int) override { clockedIn.store(patched(2)); }
  float beats() const { return (ck.ppb > 0 ? ck.ppb : 1) * CYCLES[std::max(0, std::min(7, (int)e[CYCLE]))]; }
  void trig(int port, const Event& ev) override { if (port != 2) return; ck.tick(t + eng->pos / sr, ev); const float per = beats();
    if (per >= 1) { const int P = (int)std::round(per); const float target = (float)(ck.pulses % P) / P; if (target < ph - 0.5f) { a0 = b0; b0 = rng.bi(); } ph = target; } }   // in step with the clock
  void render(int from, int to) override {
    const int mv = std::max(0, std::min(5, (int)e[MOVE]));
    if (mv > 0) { const float bpm = e[SYNC] > 0.5f ? eng->tempo.bpm.load(std::memory_order_relaxed) : e[BPM];
      const float per = clockedIn.load() && ck.cp > 0 ? ck.cp * beats() : 60.f / std::max(20.f, bpm) * CYCLES[std::max(0, std::min(7, (int)e[CYCLE]))];
      ph += (to - from) / sr / std::max(0.01f, per); while (ph >= 1) { ph -= 1; a0 = b0; b0 = rng.bi(); } }
    const float p = ph; const int s = mv - 1;
    const float sh = mv == 0 ? 0.f : s == 0 ? std::sin(p * TAU) : s == 1 ? 1 - 4 * std::fabs(p - 0.5f) : s == 2 ? (p < 0.5f ? 1.f : -1.f) : s == 3 ? b0 : a0 + (b0 - a0) * (0.5f - 0.5f * std::cos(PI * p));
    const float target = clampf(e[FADE] + sh * e[RANGE] * 0.5f, 0, 1), k = 1 - std::exp(-1.f / (0.004f * sr));   // (a few milliseconds' glide: a jump is a hard change, without a click)
    for (int i = from; i < to; i++) { pos += (target - pos) * k; const float ga = std::cos(pos * PI / 2), gb = std::sin(pos * PI / 2);
      for (int ch = 0; ch < 2; ch++) out[0][ch][i] = sane(in[0][ch][i] * ga + in[1][ch][i] * gb); }
    disp[0].store(pos);
  }
};
static Reg rCrossfader(new ModuleDef{"xfade", "CROSSFADER", Cat::Proc, {{"A", PType::Audio}, {"B", PType::Audio}, {"CLOCK", PType::Trig}}, {{"OUT", PType::Audio}},
  {pct("fade", "FADE", 0.5f), noMod(OP("move", "MOVE", {"OFF", "SINE", "TRI", "SQUARE", "RANDOM", "DRIFT"}, 0)),
   only(noMod(OP("cycle", "CYCLE", {"1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "4 BEATS", "8 BEATS", "16 BEATS", "32 BEATS"}, 4)), "v:1,2,3,4,5"), only(pct("range", "RANGE", 1), "v:1,2,3,4,5"),
   only(tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), "free"), only(fmt([] { auto q = ST("bpm", "BPM", 20, 240, 90); q.step = 0.5f; return q; }(), [](float x) { return bpmText(x); }), "own")},
  12, 68, false, [] { return new Crossfader(); }});

}  // namespace tb
