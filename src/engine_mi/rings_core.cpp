// Timbient: the Rings wrapper (see rings_core.h)
#include "rings_core.h"
#define _USE_MATH_DEFINES   // M_PI on Windows
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TEST   // build Rings for a computer rather than its own chip
#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace tb {
struct RingsCore::Impl {
  static constexpr int BS = (int)rings::kMaxBlockSize;
  rings::Part part; rings::Patch patch{0.4f, 0.5f, 0.5f, 0.5f}; rings::PerformanceState ps{};
  uint16_t verb[32768]; float in[BS], outL[BS], outR[BS]; int pos = 0; float comp = 0, n0 = 60; bool strumNext = false; int model = 0, poly = 1;
};
RingsCore::RingsCore() : p(new Impl()) { init(48000); }
RingsCore::~RingsCore() { delete p; }
void RingsCore::init(float sr) {
  std::memset(p->verb, 0, sizeof(p->verb)); p->part.Init(p->verb); p->part.set_polyphony(1); p->part.set_model(rings::RESONATOR_MODEL_MODAL); p->model = 0; p->poly = 1;
  p->comp = 12 * std::log2(48000.f / sr); p->pos = 0; std::fill(p->outL, p->outL + Impl::BS, 0.f); std::fill(p->outR, p->outR + Impl::BS, 0.f);
  p->ps.strum = false; p->ps.internal_exciter = true; p->ps.internal_strum = false; p->ps.internal_note = false; p->ps.tonic = 0; p->ps.note = 60; p->ps.fm = 0; p->ps.chord = 0;
}
void RingsCore::clear() { std::memset(p->verb, 0, sizeof(p->verb)); p->part.Init(p->verb); p->part.set_polyphony(p->poly); p->part.set_model((rings::ResonatorModel)p->model); }
void RingsCore::set(int model, int poly, float structure, float brightness, float damping, float position) {
  model = std::max(0, std::min(5, model)); poly = poly >= 4 ? 4 : poly >= 2 ? 2 : 1;
  if (model != p->model) { p->model = model; p->part.set_model((rings::ResonatorModel)model); }
  if (poly != p->poly) { p->poly = poly; p->part.set_polyphony(poly); }
  auto c = [](float v) { return std::max(0.f, std::min(1.f, v)); };
  p->patch.structure = c(structure) * 0.9995f; p->patch.brightness = c(brightness); p->patch.damping = c(damping) * 0.9995f; p->patch.position = c(position) * 0.9995f;
  p->ps.chord = std::max(0, std::min(rings::kNumChords - 1, (int)(c(structure) * (rings::kNumChords - 1) + 0.5f)));
}
void RingsCore::strum(float n) { p->ps.note = n + p->comp; p->strumNext = true; }
void RingsCore::note(float n) { p->ps.note = n + p->comp; }
void RingsCore::process(const float* inL, const float* inR, float* oL, float* oR, int n, bool internal) {
  for (int i = 0; i < n; i++) {
    p->in[p->pos] = inL ? (inR ? 0.5f * (inL[i] + inR[i]) : inL[i]) : 0.f; oL[i] = p->outL[p->pos]; oR[i] = p->outR[p->pos];
    if (++p->pos == Impl::BS) { p->pos = 0; p->ps.internal_exciter = internal; p->ps.strum = p->strumNext; p->strumNext = false;
      float inb[Impl::BS]; std::copy(p->in, p->in + Impl::BS, inb);
      p->part.Process(p->ps, p->patch, inb, p->outL, p->outR, Impl::BS);
      for (int k = 0; k < Impl::BS; k++) { if (!std::isfinite(p->outL[k])) p->outL[k] = 0; if (!std::isfinite(p->outR[k])) p->outR[k] = 0; } }
  }
}
}  // namespace tb
