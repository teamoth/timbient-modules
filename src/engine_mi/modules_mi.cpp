// Timbient native: MI VOICES, the synthesis voices of Mutable Instruments' Plaits (by Emilie Gillet, MIT licence; see
// third_party/mutable/LICENSE). Each note gets its own Plaits voice; six can sound at once, and the oldest gives way.
// ENVELOPE NATURAL lets Plaits' own low-pass gate shape each note (LPG DEC, COLOUR); ADSR drives the gate from the usual envelope.
#define _USE_MATH_DEFINES   // M_PI on Windows
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TEST   // build Plaits for a computer rather than its own chip
#include "plaits/dsp/voice.h"
#undef TEST
#include "../engine/params.h"
#include "mi_labels.h"
#include <memory>

namespace tb {

struct MiVoices : Module {
  enum { VOICE, HARM, TIMB, MORPH, LPGDEC, COLOUR, ENV, PITCH_, ATK, DEC, SUS, HOLD, REL, VSENS, WIDTH, LEVEL_ };
  static constexpr int MAXV = 6, BS = (int)plaits::kBlockSize;
  struct V {
    plaits::Voice pv; char ram[16384]; plaits::Voice::Frame fr[BS]; float buf[BS]; int pos = BS;
    NoteEnv env; float note = 60, vel = 1, gl = 0.7f, gr = 0.7f, quiet = 0, life = 0; int trigBlocks = 0; bool on = false; long age = 0;
  };
  std::unique_ptr<V[]> v; long ages = 0; float comp = 0;
  void prepare() override {
    v.reset(new V[MAXV]);
    for (int i = 0; i < MAXV; i++) { stmlib::BufferAllocator a(v[i].ram, sizeof(v[i].ram)); v[i].pv.Init(&a); v[i].env.sr = sr; }
    comp = 12 * std::log2(47872.34f / sr);   // Plaits works at its own sample rate: keep notes in tune at ours
  }
  void trig(int, const Event& ev) override {
    V* x = nullptr; for (int i = 0; i < MAXV; i++) if (!v[i].on) { x = &v[i]; break; }
    if (!x) { x = &v[0]; for (int i = 1; i < MAXV; i++) if (v[i].age < x->age) x = &v[i]; }   // all busy: the oldest note gives way
    x->on = true; x->age = ++ages; x->note = ev.note + e[PITCH_]; x->vel = 1 - e[VSENS] + e[VSENS] * ev.vel; x->trigBlocks = 2; x->quiet = 0; x->life = 0;
    x->env.atk = e[ATK]; x->env.dec = e[DEC]; x->env.sus = e[SUS]; x->env.hold = e[HOLD]; x->env.rel = e[REL]; x->env.start(); panGains(rng.bi() * e[WIDTH], x->gl, x->gr);
  }
  void block(V& x) {   // the next few samples of one note, in Plaits' own block size (its timing depends on it)
    const MiEngine& me = miEngines()[std::max(0, std::min(MI_ENGINES - 1, (int)e[VOICE]))]; const bool adsr = e[ENV] > 0.5f;
    plaits::Patch p = {}; p.note = x.note + comp; p.harmonics = e[HARM]; p.timbre = e[TIMB]; p.morph = e[MORPH]; p.engine = me.plaits; p.decay = e[LPGDEC]; p.lpg_colour = e[COLOUR];
    plaits::Modulations m = {}; m.trigger_patched = true; m.trigger = x.trigBlocks > 0 ? 1.f : 0.f; if (x.trigBlocks > 0) x.trigBlocks--;
    float lev = 1;
    if (adsr) { for (int i = 0; i < BS; i++) lev = x.env.tick(); m.level_patched = true; m.level = lev; }
    x.pv.Render(p, m, x.fr, BS);
    float pk = 0; for (int i = 0; i < BS; i++) { x.buf[i] = x.fr[i].out / 32768.f * x.vel; pk = std::max(pk, std::fabs(x.buf[i])); }
    x.pos = 0; x.life += BS / sr;
    // a note is finished when its envelope is (ADSR), or when it has gone quiet for a while (NATURAL)
    if (adsr) { if (!x.env.active() && x.life > 0.05f) x.on = false; }
    else { x.quiet = pk < 0.0005f ? x.quiet + BS / sr : 0; if ((x.quiet > 0.25f && x.life > 0.2f) || x.life > 60) x.on = false; }
  }
  void render(int a, int b) override {
    const float lev = e[LEVEL_] * 0.6f;
    for (int i = a; i < b; i++) {
      float L = 0, R = 0;
      for (int k = 0; k < MAXV; k++) { V& x = v[k]; if (!x.on) continue; if (x.pos >= BS) { block(x); if (!x.on) continue; } const float s = x.buf[x.pos++]; L += s * x.gl; R += s * x.gr; }
      out[0][0][i] = L * lev; out[0][1][i] = R * lev;
    }
  }
};
static Reg rMi(new ModuleDef{"mi", "MI VOICES", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}},
  [] { std::vector<std::string> names; for (int i = 0; i < MI_ENGINES; i++) names.push_back(miEngines()[i].name);
    std::vector<ParamDef> p = {OP("voice", "MODEL", names, 0), pct("harm", "HARMONICS", 0.4f), pct("timb", "TIMBRE", 0.5f), pct("morph", "MORPH", 0.5f),
      pct("decay", "DECAY", 0.5f), pct("colour", "LPG COLOUR", 0.5f), OP("env", "ENVELOPE", {"NATURAL", "ADSR"}, 0), PITCH()};   // named as on Plaits
    p.back().label = "FREQUENCY";
    for (auto& q : envP(0.01f, 0.3f, 0.6f, 1, 1.5f)) { q.only = "adsr"; p.push_back(q); }
    p.push_back(pct("vsens", "VEL SENS", 1)); p.push_back(pct("width", "WIDTH", 0.4f)); p.push_back(LEVEL()); return p; }(),
  0, 86, false, [] { return new MiVoices(); }});

}  // namespace tb
