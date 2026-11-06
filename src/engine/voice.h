// Timbient native: the shared voice machinery. Each voice module plays up to 12 notes at once; when it runs out, the oldest note gives way.
#pragma once
#include "params.h"

namespace tb {

struct VoiceBase { NoteEnv env; float pl = 0.7f, pr = 0.7f, vel = 1, f = 220; long age = 0; };

template <class V> struct Poly : Module {
  static constexpr int MAXV = 12;
  V v[MAXV]; long stamp = 0; float gainScale = 1; bool selfPan = false;
  int iPitch = -1, iLevel = -1, iWidth = -1, iAtk = -1, iVs = -1;
  virtual void start(V& x) = 0;                          // set up a new note (x.f, x.vel are filled in)
  virtual void voice(V& x, float& l, float& r) = 0;      // one sample of a note, before its envelope
  virtual void voicePrep() {}                            // once per block, before the notes render
  void prepare() override { iPitch = pi("pitch"); iLevel = pi("level"); iWidth = pi("width"); iAtk = pi("atk"); iVs = pi("vsens"); for (auto& x : v) x.env.sr = sr; }
  void trig(int, const Event& ev) override {
    V* best = nullptr; for (auto& x : v) if (!x.env.active()) { best = &x; break; }
    if (!best) { best = &v[0]; for (auto& x : v) if (x.age < best->age) best = &x; }
    V& x = *best; x.age = ++stamp;
    if (iAtk >= 0) { x.env.atk = e[iAtk]; x.env.dec = e[iAtk + 1]; x.env.sus = e[iAtk + 2]; x.env.hold = e[iAtk + 3]; x.env.rel = e[iAtk + 4]; }
    x.env.sr = sr; x.env.v = 0; x.env.start();
    if (!selfPan) panGains(rng.bi() * (iWidth >= 0 ? e[iWidth] : 0), x.pl, x.pr); else x.pl = x.pr = 1;
    x.vel = iVs >= 0 ? 1 - e[iVs] + e[iVs] * ev.vel : ev.vel; x.f = mtof(ev.note + (iPitch >= 0 ? e[iPitch] : 0));   // VEL SENS: how much harder notes are louder
    start(x);
  }
  void render(int a, int b) override {
    voicePrep();
    const float lev = iLevel >= 0 ? e[iLevel] : 1;
    for (int i = a; i < b; i++) {
      float L = 0, R = 0;
      for (auto& x : v) { if (!x.env.active()) continue; float l = 0, r = 0; voice(x, l, r); const float g = x.env.tick() * gainScale * x.vel; L += l * g * x.pl; R += r * g * x.pr; }
      out[0][0][i] = sane(L) * lev; out[0][1][i] = sane(R) * lev;
    }
  }
};

// the length of a note: the envelope's hold (or attack) plus its release tail, as the web version stops its sounds
inline float noteLen(const NoteEnv& e) { return std::max(e.atk, e.hold) + e.rel * 2 + 0.05f; }
inline float hann(float u) { return 0.5f - 0.5f * std::cos(TAU * u); }

}  // namespace tb
