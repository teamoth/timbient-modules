// Timbient native: the patch engine (see engine.h)
#include "engine.h"
#include <algorithm>
#include <chrono>

namespace tb {

const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const std::vector<ScaleDef>& scales() {
  static const std::vector<ScaleDef> s = {
    {"YO", {0, 2, 5, 7, 9}}, {"IN", {0, 1, 5, 7, 8}}, {"MIN PENTA", {0, 3, 5, 7, 10}}, {"MAJ PENTA", {0, 2, 4, 7, 9}},
    {"DORIAN", {0, 2, 3, 5, 7, 9, 10}}, {"LYDIAN", {0, 2, 4, 6, 7, 9, 11}}, {"HIRAJOSHI", {0, 2, 3, 7, 8}}, {"WHOLE TONE", {0, 2, 4, 6, 8, 10}}};
  return s;
}
float midiOf(int i, int root, int scale) {
  const auto& sc = scales()[scale].steps; const int n = (int)sc.size();
  const int o = (int)std::floor((float)i / n), d = ((i % n) + n) % n;
  return 36.f + root + 12.f * o + sc[d];
}

std::vector<const ModuleDef*>& allDefs() { static std::vector<const ModuleDef*> v; return v; }
void registerDef(ModuleDef* d) { allDefs().push_back(d); }
const ModuleDef* findDef(const std::string& type) { for (auto* d : allDefs()) if (d->type == type) return d; return nullptr; }

Module* Engine::create(const std::string& type, int id) {
  const ModuleDef* d = findDef(type); if (!d) return nullptr;
  Module* m = d->make(); m->def = d; m->id = id; m->eng = this; m->sr = sr; m->rng = Rng(0x1234567u + id * 7919u);
  const size_t np = d->params.size();
  m->p.reset(new std::atomic<float>[np]); for (size_t i = 0; i < np; i++) m->p[i].store(d->params[i].def);
  m->e.assign(np, 0.f); for (size_t i = 0; i < np; i++) m->e[i] = d->params[i].def;
  m->in.resize(d->ins.size()); m->out.resize(d->outs.size());
  for (auto& b : m->in) for (auto& c : b) c.fill(0.f);
  for (auto& b : m->out) for (auto& c : b) c.fill(0.f);
  m->prepare();
  return m;
}

void Engine::collectGarbage() { Module* m; while (garbage.pop(m)) delete m; }

bool Module::patched(int inPort) const { for (auto& c : eng->cables) if (c.to == id && c.toPort == inPort && c.type != PType::Mod) return true; return false; }
int Module::root() const { return eng->key.root.load(std::memory_order_relaxed); }
int Module::scale() const { return eng->key.scale.load(std::memory_order_relaxed); }
float Module::note(int oct, int step) const { return midiOf(degOf(oct, step, scale()), root(), scale()); }

void Module::emit(int port, Event ev) {
  if (mute.load(std::memory_order_relaxed)) return;
  // CHANCE: each note a sequencer plays has this chance of sounding (clock pulses always pass)
  if (def->cat == Cat::Trig && def->outs[port].name == "TRIG" && def->type != "tools") { const int ci = def->paramIndex("chance"); if (ci >= 0 && rng.uni() >= e[ci]) return; }
  flash.store(0.12f, std::memory_order_relaxed);
  ev.offset = eng->pos;
  for (auto& c : eng->cables) {
    if (c.type != PType::Trig || c.from != id || c.fromPort != port) continue;
    Module* t = eng->byId(c.to); if (!t) continue;
    Event e2 = ev; if (t->processed == eng->blockNo) e2.offset = 0;   // already done this block: it plays at the start of the next one
    t->events.push(c.toPort, e2);
  }
}

void Engine::applyCommands() {
  Command c;
  while (cmds.pop(c)) {
    switch (c.kind) {
      case Command::ADD: mods.push_back(c.mod); dirty = true; break;
      case Command::REMOVE: {
        for (size_t i = 0; i < mods.size(); i++) if (mods[i]->id == c.id) { garbage.push(mods[i]); mods.erase(mods.begin() + i); break; }
        cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const Cable& k) { return k.from == c.id || k.to == c.id; }), cables.end());
        dirty = true; break; }
      case Command::CONNECT: cables.push_back(c.cable); dirty = true; break;
      case Command::DISCONNECT:
        cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const Cable& k) { return k.from == c.cable.from && k.fromPort == c.cable.fromPort && k.to == c.cable.to && k.toPort == c.cable.toPort && k.toParam == c.cable.toParam && k.type == c.cable.type; }), cables.end());
        dirty = true; break;
      case Command::ACTION: if (Module* m = byId(c.id)) m->action(c.key); break;
      case Command::TRIGGER: if (Module* m = byId(c.id)) { Event e = c.ev; e.offset = 0; m->events.push(c.port, e); } break;
      case Command::CLEAR: for (auto* m : mods) garbage.push(m); mods.clear(); cables.clear(); dirty = true; break;
    }
  }
}

// processing order: anything feeding a module runs before it (loops are broken with a one-block delay)
void Engine::reorder() {
  order.clear(); std::vector<int> state(mods.size(), 0);
  std::function<void(int)> visit = [&](int i) {
    if (state[i]) return; state[i] = 1;
    for (auto& c : cables) { if (c.to != mods[i]->id || c.type == PType::Mod) continue;
      for (size_t j = 0; j < mods.size(); j++) if (mods[j]->id == c.from && !state[j]) visit((int)j); }
    order.push_back(mods[i]); state[i] = 2;
  };
  for (size_t i = 0; i < mods.size(); i++) visit((int)i);
  soloGates(); dirty = false;
}

// SOLO: a soloed module is heard through everything after it; whatever feeds it still feeds it; nothing else passes
void Engine::soloGates() {
  std::vector<int> solo; for (auto* m : mods) if (m->solo.load()) solo.push_back(m->id);
  anySolo = !solo.empty();
  if (!anySolo) { for (auto& c : cables) c.open = true; return; }
  std::vector<int> down = solo, up = solo; bool grew = true;
  auto has = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
  while (grew) { grew = false; for (auto& c : cables) { if (c.type != PType::Audio) continue;
      if (has(down, c.from) && !has(down, c.to)) { down.push_back(c.to); grew = true; }
      if (has(up, c.to) && !has(up, c.from)) { up.push_back(c.from); grew = true; } } }
  for (auto& c : cables) c.open = c.type != PType::Audio || has(down, c.from) || has(up, c.to);
}

void Engine::render(float* L, float* R, int n) {
  const auto t0 = std::chrono::steady_clock::now();
  int done = 0;
  while (done < n) { const int k = std::min(BLOCK, n - done); block(L + done, R + done, k); done += k; }
  const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), avail = n / (double)sr;
  cpuLoad.store(cpuLoad.load() * 0.95f + (float)(used / avail) * 0.05f, std::memory_order_relaxed);
}

void Engine::block(float* L, float* R, int n) {
  applyCommands();
  if (dirty) reorder();
  { int s = 0; for (auto* m : mods) if (m->solo.load(std::memory_order_relaxed)) s += m->id; if (s != soloSig) { soloSig = s; soloGates(); } }   // re-gate only when solo changes
  blockNo++;
  std::fill(masterL, masterL + n, 0.f); std::fill(masterR, masterR + n, 0.f);
  for (Module* m : order) {
    const ModuleDef* d = m->def;
    // effective settings: base value plus any modulation cables
    for (size_t k = 0; k < d->params.size(); k++) {
      const ParamDef& q = d->params[k]; const float b = m->p[k].load(std::memory_order_relaxed);
      if (!q.opts.empty() || q.act) { m->e[k] = b; continue; }
      float nn = q.toNorm(b); bool modded = false;
      for (auto& c : cables) if (c.type == PType::Mod && c.to == m->id && c.toParam == (int)k) {
        Module* s = byId(c.from); if (!s || s->mute.load(std::memory_order_relaxed)) continue;
        const int di = s->def->paramIndex("depth"), ri = s->def->paramIndex("dir");
        const float depth = di >= 0 ? s->e[di] : 0.5f, dir = ri >= 0 ? s->e[ri] : 0.f, v = s->modv;
        nn += (dir < 0.5f ? v * 0.5f : dir < 1.5f ? (v + 1.f) / 2.f : -(v + 1.f) / 2.f) * depth; modded = true;
      }
      m->e[k] = modded ? q.fromNorm(nn) : b;
    }
    // audio inputs: the sum of every open cable into each input
    for (size_t i = 0; i < d->ins.size(); i++) { if (d->ins[i].type != PType::Audio) continue; for (auto& ch : m->in[i]) std::fill(ch.begin(), ch.begin() + n, 0.f); }
    for (auto& c : cables) {
      if (c.type != PType::Audio || c.to != m->id || !c.open || c.toPort < 0) continue;
      Module* s = byId(c.from); if (!s || c.fromPort >= (int)s->out.size()) continue;
      for (int ch = 0; ch < 2; ch++) { float* dst = m->in[c.toPort][ch].data(); const float* src = s->out[c.fromPort][ch].data(); for (int i = 0; i < n; i++) dst[i] += src[i]; }
    }
    m->blockStart(n);
    // render, stopping at each trigger so notes start on the exact sample
    EventQueue q = m->events; m->events.n = 0;
    int idx[MAX_EVENTS]; for (int i = 0; i < q.n; i++) { int j = i; while (j > 0 && q.ev[idx[j - 1]].offset > q.ev[i].offset) { idx[j] = idx[j - 1]; j--; } idx[j] = i; }   // in time order
    int at = 0;
    for (int ii = 0; ii < q.n; ii++) { const int i = idx[ii]; const int off = std::min(std::max(q.ev[i].offset, 0), n - 1);
      if (off > at) { pos = at; m->render(at, off); at = off; }
      pos = at; m->trig(q.port[i], q.ev[i]); }
    if (at < n) { pos = at; m->render(at, n); }
    m->t += n / sr; m->processed = blockNo;
    if (m->mute.load(std::memory_order_relaxed) && d->cat != Cat::Out) for (auto& o : m->out) for (auto& ch : o) std::fill(ch.begin(), ch.begin() + n, 0.f);
    const float f = m->flash.load(std::memory_order_relaxed); if (f > 0) m->flash.store(std::max(0.f, f - n / sr), std::memory_order_relaxed);
  }
  // the master: a gentle compressor and a safety limit, then out to the sound card
  comp.sr = sr; comp.process(masterL, masterR, n);
  float pk = 0;
  for (int i = 0; i < n; i++) { L[i] = masterL[i]; R[i] = masterR[i]; pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i]))); }
  outPeak.store(std::max(pk, outPeak.load(std::memory_order_relaxed) * 0.995f), std::memory_order_relaxed);
}

}  // namespace tb
