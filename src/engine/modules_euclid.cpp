// Timbient native: EUCLID DRUMS. Four drum sounds (KICK, SNARE, HAT, PERC), each playing a Euclidean rhythm: HITS spread as
// evenly as they'll go over STEPS, turned round by ROTATE. Lanes of different lengths drift against each other.
// Each lane's sound: TUNE, DECAY and CHARACTER (punchier or softer, brighter or darker, more or less ring, as fits the drum).
// Shared: the tempo (or CLOCK), STEP, SWING, ACCENT (some hits louder, in a rhythm of their own), CHANCE (hits sometimes drop
// out) and MUTATE (now and then a lane's HITS or ROTATE moves by one for a while, then drifts back).
// Each lane also has a trigger output, so its rhythm can play other voices too.
#include "params.h"
#include "dsp2.h"

namespace tb {
namespace {
struct EClock {   // learns the pulse period from what arrives at CLOCK
  double last = -1; float cp = 0;
  void tick(double t, const Event& ev) { if (ev.period > 0) cp = ev.period; else if (last >= 0) { const float p = (float)(t - last); if (p > 0.01f && p < 8) cp = cp > 0 ? cp * 0.6f + p * 0.4f : p; } last = t; }
};
}  // namespace

bool euclidHit(int i, int steps, int hits, int rot) {   // is step i a hit, with HITS spread evenly over STEPS, turned by ROT?
  if (steps < 1 || hits < 1) return false; if (hits >= steps) return true;
  const int k = ((i - rot) % steps + steps) % steps; return (k * hits) % steps < hits;
}

struct Euclid : Module {
  enum { SYNC, BPM, DIV, SWING, ACCENT, CHANCE, MUTATE, LEVEL, LANE, P0 };   // then each lane's STEPS, HITS, ROTATE, TUNE, DECAY, CHARACTER, LEVEL
  static constexpr int LANES = 4, PER = 7;
  int pi(int l, int k) const { return P0 + l * PER + k; }
  long n = -1; double next = -1, per = 0.125; EClock ck;
  int mutH[LANES] = {}, mutR[LANES] = {}, mutLeft[LANES] = {};   // MUTATE: a lane's temporary change, and how many cycles it lasts
  struct Drum { float t = 1e9, vel = 0, ph = 0, ph2 = 0, env = 0, nz = 0; float hp = 0, bp1 = 0, bp2 = 0; float metal[6] = {}; };
  Drum dr[LANES]; float pendVel[LANES] = {}; bool pend[LANES] = {};
  Biquad snareBp, hatBp, hatHp;
  void prepare() override { snareBp.set(Biquad::BP, 3500, 0.8f, sr); hatBp.set(Biquad::BP, 9000, 1.2f, sr); hatHp.set(Biquad::HP, 6500, 0.7f, sr); }
  int steps(int l) const { return std::max(1, std::min(32, (int)std::lround(e[pi(l, 0)]))); }
  int hits(int l) const { return std::max(0, std::min(steps(l), (int)std::lround(e[pi(l, 1)]) + mutH[l])); }
  int rot(int l) const { return (int)std::lround(e[pi(l, 2)]) + mutR[l]; }
  void step(int at) {
    n++;
    for (int l = 0; l < LANES; l++) {
      const int st = steps(l), k = (int)(n % st);
      if (k == 0 && n > 0) {   // a lane's cycle starts again: MUTATE may nudge it for a while, or let it drift back
        if (mutLeft[l] > 0 && --mutLeft[l] == 0) { mutH[l] = mutR[l] = 0; }
        else if (mutLeft[l] == 0 && rng.uni() < e[MUTATE] * 0.35f) { if (rng.uni() < 0.5f) mutH[l] = rng.uni() < 0.5f ? 1 : -1; else mutR[l] = rng.uni() < 0.5f ? 1 : -1; mutLeft[l] = 1 + (int)(rng.uni() * 3); } }
      if (!euclidHit(k, st, hits(l), rot(l)) || rng.uni() > e[CHANCE]) continue;
      const bool acc = euclidHit((int)(n % 16), 16, 5, 0);   // ACCENT: its own rhythm, five in sixteen
      const float vel = std::min(1.f, (acc ? 1.f : 1.f - e[ACCENT] * 0.55f) * e[pi(l, 6)]);
      Drum& d = dr[l]; d.t = 0; d.vel = vel; d.ph = 0; d.env = 1;
      Event ev; ev.note = 36 + l * 2; ev.vel = vel; ev.period = per; emit(1 + l, ev); (void)at;
    }
    disp[8].store((float)n);
  }
  void blockStart(int) override { clockedIn.store(patched(0)); }
  void trig(int port, const Event& ev) override { if (port == 0) { ck.tick(t + eng->pos / sr, ev); if (!mute.load(std::memory_order_relaxed)) { if (ck.cp > 0) per = ck.cp; step(eng->pos); } } }
  // one sample of a drum
  float play(int l, Drum& d) {
    if (d.t > 8 * sr) return 0;
    const float tt = d.t / sr; d.t += 1;
    const float tune = e[pi(l, 3)], dec = e[pi(l, 4)], ch = e[pi(l, 5)];
    float y = 0;
    switch (l) {
      case 0: {   // KICK: a sine that swoops down to TUNE, with a click (CHARACTER: punch and drive)
        const float f = tune * (1 + (2 + ch * 6) * std::exp(-tt / (0.012f + 0.02f * (1 - ch)))); d.ph += f / sr; if (d.ph >= 1) d.ph -= 1;
        y = std::sin(d.ph * TAU) * std::exp(-tt / (dec * 0.35f)); y += (tt < 0.004f ? (1 - tt / 0.004f) * ch * 0.6f : 0); y = softLimit(y * (1 + ch * 2)) / (1 + ch * 0.6f); break; }
      case 1: {   // SNARE: a short tuned body and a burst of noise (CHARACTER: how much noise, how bright)
        d.ph += tune / sr; if (d.ph >= 1) d.ph -= 1; d.ph2 += tune * 1.48f / sr; if (d.ph2 >= 1) d.ph2 -= 1;
        const float body = (std::sin(d.ph * TAU) + 0.5f * std::sin(d.ph2 * TAU)) * std::exp(-tt / 0.045f) * (1 - ch * 0.6f);
        const float nz = (float)snareBp.run(rng.bi(), 0) * std::exp(-tt / (dec * 0.25f)) * (0.6f + ch * 1.2f);
        y = body * 0.7f + nz; break; }
      case 2: {   // HAT: six detuned square waves (the metal) and noise, high-passed (CHARACTER: from noisy to metallic)
        static const float R[6] = {1.f, 1.483f, 1.8f, 2.546f, 2.63f, 3.897f}; float mt = 0;
        for (int k = 0; k < 6; k++) { d.metal[k] += tune * R[k] / sr; if (d.metal[k] >= 1) d.metal[k] -= 1; mt += d.metal[k] < 0.5f ? 1 : -1; }
        const float src = mt * ch * 0.25f + rng.bi() * (1 - ch * 0.7f);
        y = (float)hatHp.run(hatBp.run(src, 0), 0) * std::exp(-tt / (dec * 0.12f)) * 1.4f; break; }
      default: {  // PERC: a tuned ping, on a note of the key near TUNE (CHARACTER: from a pure ping to a clangy bell)
        const int rt = root(), sc = scale(); const float note = midiOf(nearDeg(69 + 12 * std::log2(tune / 440.f), rt, sc), rt, sc), f = 440 * std::pow(2.f, (note - 69) / 12);
        d.ph2 += f * (1.4f + ch * 2.1f) / sr; if (d.ph2 >= 1) d.ph2 -= 1; const float mod = std::sin(d.ph2 * TAU) * ch * 3 * std::exp(-tt / (dec * 0.2f));
        d.ph += f / sr; if (d.ph >= 1) d.ph -= 1; y = std::sin(d.ph * TAU + mod) * std::exp(-tt / (dec * 0.5f)); break; }
    }
    return y * d.vel;
  }
  void render(int a, int b) override {
    if (mute.load(std::memory_order_relaxed)) { n = -1; next = -1; }
    const bool clocked = clockedIn.load();
    static const double DIVS[3] = {2, 4, 8};   // steps a beat: 1/8, 1/16, 1/32
    const double base = 60.0 / (e[SYNC] > 0.5f ? eng->tempo.bpm.load(std::memory_order_relaxed) : e[BPM]) / DIVS[std::max(0, std::min(2, (int)e[DIV]))];
    const float lv = e[LEVEL];
    for (int i = a; i < b; i++) {
      if (!clocked && !mute.load(std::memory_order_relaxed)) { const double now = t + i / sr; if (next < 0 || now - next > 1) next = now;
        while (now >= next) { eng->pos = i; step(i); per = base * ((n % 2 == 0) ? 1 + e[SWING] * 0.5 : 1 - e[SWING] * 0.5); next += per; } }   // SWING: long, short
      float L = 0, R = 0; static const float PAN[LANES] = {0.5f, 0.45f, 0.62f, 0.38f};
      for (int l = 0; l < LANES; l++) { const float y = play(l, dr[l]); L += y * (1 - PAN[l]) * 1.4f; R += y * PAN[l] * 1.4f; }
      out[0][0][i] = softLimit(L * lv); out[0][1][i] = softLimit(R * lv);
    }
    for (int l = 0; l < LANES; l++) { disp[l].store(n < 0 ? -1.f : (float)(n % steps(l))); disp[4 + l].store((float)(hits(l) * 64 + ((rot(l) % 32) + 32))); }
    disp[9].store(dr[0].t < sr * 0.08f ? 1.f : 0.f); disp[10].store(dr[1].t < sr * 0.08f ? 1.f : 0.f); disp[11].store(dr[2].t < sr * 0.08f ? 1.f : 0.f); disp[12].store(dr[3].t < sr * 0.08f ? 1.f : 0.f);
  }
};

static std::vector<ParamDef> euclidParams() {
  std::vector<ParamDef> v = {
    only(tog(OP("sync", "TEMPO", {"OWN", "SHARED"}, 1)), "free"), only(fmt(ST("bpm", "BPM", 20, 240, 100), [](float x) { return std::to_string((int)x); }), "own"),
    only(OP("div", "STEP", {"1/8", "1/16", "1/32"}, 1), "free"), only(pct("swing", "SWING", 0), "free"),
    pct("accent", "ACCENT", 0.5f), pct("chance", "CHANCE", 1), pct("mutate", "MUTATE", 0), pct("level", "LEVEL", 0.6f),
    noMod(OP("lane", "LANE", {"KICK", "SNARE", "HAT", "PERC"}, 0))};
  static const char* NAMES[4] = {"KICK", "SNARE", "HAT", "PERC"};
  static const int ST0[4] = {16, 16, 16, 16}, HI0[4] = {4, 2, 7, 3}, RO0[4] = {0, 4, 0, 2};
  static const float TU0[4] = {50, 190, 420, 520}, TUmin[4] = {30, 100, 200, 110}, TUmax[4] = {120, 400, 1000, 1760}, DE0[4] = {0.6f, 0.6f, 0.4f, 0.5f}, CH0[4] = {0.4f, 0.5f, 0.5f, 0.3f};
  for (int l = 0; l < 4; l++) {
    const std::string tag = "l:" + std::to_string(l), k = std::to_string(l + 1);
    v.push_back(only(stepper(noMod(ST(("st" + k).c_str(), "STEPS", 1, 32, ST0[l]))), tag.c_str()));
    v.push_back(only(stepper(ST(("hi" + k).c_str(), "HITS", 0, 32, HI0[l])), tag.c_str()));
    v.push_back(only(stepper(ST(("ro" + k).c_str(), "ROTATE", 0, 31, RO0[l])), tag.c_str()));
    v.push_back(only(fmt(prHz(("tu" + k).c_str(), "TUNE", TUmin[l], TUmax[l], TU0[l]), [](float x) { char b[16]; std::snprintf(b, 16, "%dHZ", (int)std::lround(x)); return std::string(b); }), tag.c_str()));
    v.push_back(only(fmt(prExp(("de" + k).c_str(), "DECAY", 0.05f, 2.f, DE0[l]), [](float x) { char b[16]; std::snprintf(b, 16, "%.2fS", x); return std::string(b); }), tag.c_str()));
    v.push_back(only(pct(("ch" + k).c_str(), "CHARACTER", CH0[l]), tag.c_str()));
    v.push_back(only(pct(("lv" + k).c_str(), (std::string(NAMES[l]) + " LEVEL").c_str(), 0.85f), tag.c_str()));
  }
  return v;
}

static Reg rEuclid(new ModuleDef{"euclid", "EUCLID DRUMS", Cat::Voice, {{"CLOCK", PType::Trig}},
  {{"OUT", PType::Audio}, {"KICK", PType::Trig}, {"SNARE", PType::Trig}, {"HAT", PType::Trig}, {"PERC", PType::Trig}},
  euclidParams(), 36, 96, false, [] { return new Euclid(); }});

}  // namespace tb
