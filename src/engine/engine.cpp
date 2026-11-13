// Timbient native: the patch engine (see engine.h)
#include "engine.h"
#include "params.h"
#include <algorithm>
#include <chrono>

namespace tb {

const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const std::vector<ScaleDef>& scales() {
  static const std::vector<ScaleDef> s = {
    {"YO", {0, 2, 5, 7, 9}}, {"IN", {0, 1, 5, 7, 8}}, {"MIN PENTA", {0, 3, 5, 7, 10}}, {"MAJ PENTA", {0, 2, 4, 7, 9}},
    {"DORIAN", {0, 2, 3, 5, 7, 9, 10}}, {"LYDIAN", {0, 2, 4, 6, 7, 9, 11}}, {"HIRAJOSHI", {0, 2, 3, 7, 8}}, {"WHOLE TONE", {0, 2, 4, 6, 8, 10}},
    // added later: the first eight keep their places, so saved sessions open in the same scale
    {"MAJOR", {0, 2, 4, 5, 7, 9, 11}}, {"NAT MINOR", {0, 2, 3, 5, 7, 8, 10}}, {"PHRYGIAN", {0, 1, 3, 5, 7, 8, 10}}, {"MIXOLYDIAN", {0, 2, 4, 5, 7, 9, 10}},
    {"LOCRIAN", {0, 1, 3, 5, 6, 8, 10}}, {"HARM MINOR", {0, 2, 3, 5, 7, 8, 11}}, {"MEL MINOR", {0, 2, 3, 5, 7, 9, 11}},
    {"IWATO", {0, 1, 5, 6, 10}}, {"KUMOI", {0, 2, 3, 7, 9}}, {"INSEN", {0, 1, 5, 7, 10}}, {"EGYPTIAN", {0, 2, 5, 7, 10}}, {"BLUES", {0, 3, 5, 6, 7, 10}},
    {"PELOG", {0, 1, 3, 7, 8}}, {"DOUBLE HARM", {0, 1, 4, 5, 7, 8, 11}}, {"HUNGARIAN", {0, 2, 3, 6, 7, 8, 11}}, {"PROMETHEUS", {0, 2, 4, 6, 9, 10}},
    {"PERSIAN", {0, 1, 4, 5, 6, 8, 11}}, {"ENIGMATIC", {0, 1, 4, 6, 8, 10, 11}}, {"AUGMENTED", {0, 3, 4, 7, 8, 11}}, {"DIMINISHED", {0, 2, 3, 5, 6, 8, 9, 11}},
    {"CHROMATIC", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}}};
  return s;
}
float midiOf(int i, int root, int scale) {
  const auto& sc = scales()[scale].steps; const int n = (int)sc.size();
  const int o = (int)std::floor((float)i / n), d = ((i % n) + n) % n;
  return 36.f + root + 12.f * o + sc[d];
}

KeyMatch matchKey(const float chroma[12]) {
  KeyMatch k; float mean = 0; for (int i = 0; i < 12; i++) mean += chroma[i]; mean /= 12; float cv = 0; for (int i = 0; i < 12; i++) cv += (chroma[i] - mean) * (chroma[i] - mean);
  float best = -2, second = -2; int br = 0, bs = 0, sr2 = 0, ss = 0;
  if (cv > 1e-9f) for (int sc = 0; sc < (int)scales().size(); sc++) { const auto& st = scales()[sc].steps; if (st.size() >= 12) continue;
    for (int r = 0; r < 12; r++) { float t[12] = {0}; for (int d : st) t[(r + d) % 12] = 1; t[r] = 1.6f; t[(r + 7) % 12] *= (t[(r + 7) % 12] > 0 ? 1.15f : 1);
      float tm = 0; for (float x : t) tm += x; tm /= 12; float num = 0, tv = 0; for (int i = 0; i < 12; i++) { num += (chroma[i] - mean) * (t[i] - tm); tv += (t[i] - tm) * (t[i] - tm); }
      const float corr = num / std::sqrt(cv * tv) - 0.004f * st.size();   // a nudge towards the smaller scale when two explain it equally
      if (corr > best) { if (!(r == br && sc == bs)) { second = best; sr2 = br; ss = bs; } best = corr; br = r; bs = sc; }
      else if (corr > second && !(r == br && sc == bs)) { second = corr; sr2 = r; ss = sc; } } }
  k.root = br; k.scale = bs; k.root2 = sr2; k.scale2 = ss; k.conf = std::max(0.f, std::min(1.f, best)); k.conf2 = std::max(0.f, std::min(1.f, second)); return k;
}

std::vector<const ModuleDef*>& allDefs() { static std::vector<const ModuleDef*> v; return v; }
void registerDef(ModuleDef* d) {
  d->width += 8;   // room for the - and + beside every slider
  static const char* KEYED[] = {"clump", "orbits", "hops", "keys", "bursts", "walker", "tools"};   // sequencers that pick notes from a key
  for (const char* k : KEYED) if (d->type == k) {
    std::vector<std::string> keys = {"GLOBAL"}; for (int i = 0; i < 12; i++) keys.push_back(NOTE_NAMES[i]);
    std::vector<std::string> sc = {"GLOBAL"}; for (auto& x : scales()) sc.push_back(x.name);
    ParamDef a = OP("skey", "KEY", keys, 0); ParamDef b = OP("sscale", "SCALE", sc, 0);
    d->keyIdx = (int)d->params.size(); d->params.push_back(a); d->scaleIdx = (int)d->params.size(); d->params.push_back(b); }
  static const char* LOWPASSED[] = {"looper", "stretch", "tloop", "click", "fold"};
  for (const char* k : LOWPASSED) if (d->type == k) { d->lpCut = (int)d->params.size(); d->params.push_back(prHz("cut", "CUTOFF", 80, 20000, 20000)); d->lpRes = (int)d->params.size(); d->params.push_back(pct("res", "RES", 0)); }
  if (d->cat == Cat::Voice) {   // VOICES: POLY (up to 12 notes at once) or MONO (one: a new note fades the last out); GLIDE slides MONO from note to note
    d->params.push_back(OP("poly", "VOICES", {"POLY", "MONO"}, 0)); if (d->type != "sample") { ParamDef g = T("glide", "GLIDE", 0, 2, 0); g.only = "mono"; d->params.push_back(g); } }
  bool audio = false; for (auto& o : d->outs) if (o.type == PType::Audio) audio = true;
  if (audio && d->type != "mixer" && d->cat != Cat::Out && d->paramIndex("pan") < 0) {
    ParamDef q = pr("pan", "PAN", -1, 1, 0); q.fmt = [](float v) { if (std::fabs(v) < 0.02f) return std::string("CENTRE"); return std::string(v < 0 ? "L " : "R ") + std::to_string((int)std::round(std::fabs(v) * 100)); };
    d->panIdx = (int)d->params.size(); d->params.push_back(q); }
  for (auto& q : d->params) if (q.opts.empty() && !q.act && q.step >= 1 && std::fabs(q.step - std::round(q.step)) < 1e-4f && (q.max - q.min) / q.step + 1 <= 16.5f) q.stepper = true;   // up to 16 whole numbers: a stepper
  { int end = 39; for (auto& q : d->params) if (q.opts.empty() && !q.act) end = std::max(end, 3 + (int)std::min<size_t>(11, q.label.size()) * 4 - 1 + 2 - (q.noMod ? 7 : 0));
    d->labelEnd = end; d->width += end - 39; }   // a long name widens the module rather than shortening its sliders
  allDefs().push_back(d);
}   // +8: room for the - and + beside every slider
const ModuleDef* findDef(const std::string& type) { for (auto* d : allDefs()) if (d->type == type) return d; return nullptr; }

Module* Engine::create(const std::string& type, int id) {
  const ModuleDef* d = findDef(type); if (!d) return nullptr;
  Module* m = d->make(); m->def = d; m->id = id; m->eng = this; m->sr = sr; m->rng = Rng(0x1234567u + id * 7919u);
  const size_t np = d->params.size();
  m->p.reset(new std::atomic<float>[np]); for (size_t i = 0; i < np; i++) m->p[i].store(d->params[i].def);
  m->e.assign(np, 0.f); for (size_t i = 0; i < np; i++) m->e[i] = d->params[i].def;
  m->eff.reset(new std::atomic<float>[np]); for (size_t i = 0; i < np; i++) m->eff[i].store(d->params[i].def);
  m->in.resize(d->ins.size()); m->out.resize(d->outs.size());
  for (auto& b : m->in) for (auto& c : b) c.fill(0.f);
  for (auto& b : m->out) for (auto& c : b) c.fill(0.f);
  m->prepare();
  return m;
}

void Engine::collectGarbage() { Module* m; while (garbage.pop(m)) delete m; AudioBuf* b; while (bufGarbage.pop(b)) delete b; }

bool Module::patched(int inPort) const { for (auto& c : eng->cables) if (c.to == id && c.toPort == inPort && c.type != PType::Mod) return true; return false; }
int Module::root() const { if (def->keyIdx >= 0) { const int k = (int)e[def->keyIdx]; if (k >= 1) return k - 1; } return eng->key.root.load(std::memory_order_relaxed); }
int Module::scale() const { if (def->scaleIdx >= 0) { const int k = (int)e[def->scaleIdx]; if (k >= 1 && k <= (int)scales().size()) return k - 1; } return eng->key.scale.load(std::memory_order_relaxed); }
float Module::note(int oct, int step) const { return midiOf(degOf(oct, step, scale()), root(), scale()); }

void Module::emit(int port, Event ev) {
  if (mute.load(std::memory_order_relaxed)) return;
  // VELOCITY 0: a sequencer's note at no velocity isn't sent at all (so it's silent whatever the voice's VEL SENS); clock pulses still go
  if (def->cat == Cat::Trig && ev.sw < 0 && ev.vel <= 0.001f && def->outs[port].name != "PULSE" && def->outs[port].name != "BAR") return;
  // CHANCE: each note a sequencer plays has this chance of sounding (clock pulses always pass)
  if (def->cat == Cat::Trig && def->outs[port].name == "TRIG" && def->type != "tools" && def->type != "tracker") { const int ci = def->paramIndex("chance"); if (ci >= 0 && rng.uni() >= e[ci]) return; }
  flash.store(0.12f, std::memory_order_relaxed); if (port >= 0 && port < 8) fires[port].fetch_add(1, std::memory_order_relaxed);
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
      case Command::ADD:   // (undo can bring a module back while its old self is still ringing out: that one goes now)
        for (size_t i = 0; i < mods.size(); i++) if (mods[i]->id == c.mod->id && mods[i]->retiring) { garbage.push(mods[i]); mods.erase(mods.begin() + i); cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const Cable& k) { return k.from == c.mod->id || k.to == c.mod->id; }), cables.end()); break; }
        mods.push_back(c.mod); dirty = true; break;
      case Command::FADE: for (auto& k : cables) if (k.same(c.cable)) { k.gt = c.cable.gt; k.dieAtZero = c.cable.dieAtZero; } break;
      case Command::RETIRE: if (Module* m = byId(c.id)) { m->retiring = true; m->retireT = m->quietT = 0;
          cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const Cable& k) { return (k.to == c.id || k.from == c.id) && k.type != PType::Audio; }), cables.end());
          for (auto& k : cables) if (k.to == c.id) { k.gt = 0; k.dieAtZero = true; }   // what comes in fades away; what goes out stays while it rings
          dirty = true; } break;
      case Command::REMOVE: {
        for (size_t i = 0; i < mods.size(); i++) if (mods[i]->id == c.id) { garbage.push(mods[i]); mods.erase(mods.begin() + i); break; }
        cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const Cable& k) { return k.from == c.id || k.to == c.id; }), cables.end());
        dirty = true; break; }
      case Command::CONNECT: { bool revived = false; for (auto& k : cables) if (k.same(c.cable) && k.dieAtZero) { k.gt = 1; k.dieAtZero = false; revived = true; }   // plugged back in while still fading out
        if (!revived) cables.push_back(c.cable); dirty = true; break; }
      case Command::DISCONNECT:
        cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const Cable& k) { return k.same(c.cable); }), cables.end());
        dirty = true; break;
      case Command::ACTION: if (Module* m = byId(c.id)) m->action(c.key); break;
      case Command::PREVIEW: if (prevBuf) bufGarbage.push(prevBuf); prevBuf = c.buf; prevPos = 0; previewAt.store(c.buf ? 0.f : -1.f, std::memory_order_relaxed); break;
      case Command::SETBUF: if (Module* m = byId(c.id)) { AudioBuf* old = m->swapBuf(c.buf, c.port); if (old && !bufGarbage.push(old)) {} } else if (c.buf) bufGarbage.push(c.buf); break;
      case Command::TRIGGER: if (Module* m = byId(c.id)) { Event e = c.ev; e.offset = 0; m->events.push(c.port, e); } break;
      case Command::CLEAR: for (auto* m : mods) garbage.push(m); mods.clear(); cables.clear(); dirty = true; break;
    }
  }
  // cables that have faded right out go; a module ringing out goes once it has no cables left
  const size_t nc = cables.size(); cables.erase(std::remove_if(cables.begin(), cables.end(), [](const Cable& k) { return k.dieAtZero && k.g <= 0 && k.gt <= 0; }), cables.end()); if (cables.size() != nc) dirty = true;
  for (size_t i = 0; i < mods.size(); i++) if (mods[i]->retiring) { bool has = false; for (auto& k : cables) if (k.from == mods[i]->id) has = true;
    if (!has) { garbage.push(mods[i]); mods.erase(mods.begin() + i); i--; dirty = true; } }
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

// SOLO (as in SunVox): only sound-making modules (voices) can be soloed. While any voice is soloed, the other voices
// fall silent; effects, mixers and the rest keep working on whatever still reaches them
void Engine::soloGates() {
  anySolo = false; for (auto* m : mods) if (m->def->cat == Cat::Voice && m->solo.load()) anySolo = true;
  for (auto& c : cables) c.open = true;
}

void Engine::render(float* L, float* R, int n) {
  const auto t0 = std::chrono::steady_clock::now();
  int done = 0;
  while (done < n) { const int k = std::min(BLOCK, n - done); block(L + done, R + done, k); done += k; }
  const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), avail = n / (double)sr;
  cpuLoad.store(cpuLoad.load() * 0.95f + (float)(used / avail) * 0.05f, std::memory_order_relaxed);
}

double wallSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void Engine::block(float* L, float* R, int n) {
  applyCommands();
  // MIDI arriving from the devices: clock ticks become pulses (24 per beat, passed on as eighth notes); the rest is kept for MIDI IN modules
  midiN = 0; midiClock = false;
  for (auto& q : midiInQ) { MidiMsg mm; while (q.pop(mm)) {
    if (mm.st == 0xF8) { if (midiLastT >= 0) { const double dt = now - midiLastT; if (dt > 0.001 && dt < 0.5) midiPer = (float)(midiPer * 0.9 + dt * 12 * 0.1); } midiLastT = now;
      midiTick = (midiTick + 1) % 12; if (midiTick == 0) midiClock = true; }
    else if (mm.st == 0xFA) midiTick = -1;
    else if (midiN < 256) midiNow[midiN++] = mm; } }
  now += n / sr;
  if (dirty) reorder();
  { int s = 0; for (auto* m : mods) if (m->def->cat == Cat::Voice && m->solo.load(std::memory_order_relaxed)) s += m->id; if (s != soloSig) { soloSig = s; soloGates(); } }   // re-gate only when solo changes
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
      m->eff[k].store(m->e[k], std::memory_order_relaxed);
    }
    // audio inputs: the sum of every open cable into each input
    for (size_t i = 0; i < d->ins.size(); i++) { if (d->ins[i].type != PType::Audio) continue; for (auto& ch : m->in[i]) std::fill(ch.begin(), ch.begin() + n, 0.f); }
    for (auto& c : cables) {
      if (c.type != PType::Audio || c.to != m->id || !c.open || c.toPort < 0) continue;
      Module* s = byId(c.from); if (!s || c.fromPort >= (int)s->out.size()) continue;
      if (c.g == 1 && c.gt == 1) { for (int ch = 0; ch < 2; ch++) { float* dst = m->in[c.toPort][ch].data(); const float* src = s->out[c.fromPort][ch].data(); for (int i = 0; i < n; i++) dst[i] += src[i]; } }
      else {   // a cable fading in or out: about a tenth of a second end to end
        const float g0 = c.g, step = n / sr / 0.1f, g1 = c.gt > g0 ? std::min(c.gt, g0 + step) : std::max(c.gt, g0 - step);
        for (int ch = 0; ch < 2; ch++) { float* dst = m->in[c.toPort][ch].data(); const float* src = s->out[c.fromPort][ch].data(); for (int i = 0; i < n; i++) dst[i] += src[i] * (g0 + (g1 - g0) * (i + 1) / n); }
        const_cast<Cable&>(c).g = g1; }
    }
    m->blockStart(n);
    // render, stopping at each trigger so notes start on the exact sample
    EventQueue q = m->events; m->events.n = 0;
    int idx[MAX_EVENTS]; for (int i = 0; i < q.n; i++) { int j = i; while (j > 0 && q.ev[idx[j - 1]].offset > q.ev[i].offset) { idx[j] = idx[j - 1]; j--; } idx[j] = i; }   // in time order
    int at = 0;
    for (int ii = 0; ii < q.n; ii++) { const int i = idx[ii]; const int off = std::min(std::max(q.ev[i].offset, 0), n - 1);
      if (off > at) { pos = at; m->render(at, off); at = off; }
      pos = at;
      if (q.port[i] == SWITCH_PORT) { const int a = q.ev[i].sw >= 0 ? q.ev[i].sw : 2; m->mute.store(a == 0 ? true : a == 1 ? false : !m->mute.load()); continue; }   // the SWITCH socket: a Timeline says ON (1) or OFF (0); any other trigger turns it over
      m->trig(q.port[i], q.ev[i]); }
    if (at < n) { pos = at; m->render(at, n); }
    if (d->lpCut >= 0 && !m->out.empty()) {   // CUTOFF and RES (fully open: nothing)
      const float fc = m->e[d->lpCut], rs = m->e[d->lpRes];
      if (fc < 19500 || rs > 0.01f) { if (fc != m->lpSig[0] || rs != m->lpSig[1]) { m->lpSig[0] = fc; m->lpSig[1] = rs; m->postLp.set(Biquad::LP, fc, 0.7f + rs * rs * 9, sr); }
        for (int ch = 0; ch < 2; ch++) { float* o = m->out[0][ch].data(); for (int i = 0; i < n; i++) o[i] = (float)m->postLp.run(o[i], ch); } }
      else m->lpSig[0] = -1; }
    { const float b1 = m->bypass.load(std::memory_order_relaxed) ? 1.f : 0.f, b0 = m->byNow;   // BYPASS: fade over to the dry sound
      if ((b0 > 0 || b1 > 0) && !d->ins.empty() && d->ins[0].type == PType::Audio && !m->out.empty()) { const float step = n / sr / 0.08f, bb = b1 > b0 ? std::min(b1, b0 + step) : std::max(b1, b0 - step); m->byNow = bb;
        for (int ch = 0; ch < 2; ch++) { float* o = m->out[0][ch].data(); const float* x = m->in[0][ch].data(); for (int i = 0; i < n; i++) { const float t = b0 + (bb - b0) * (i + 1) / n; o[i] = o[i] * (1 - t) + x[i] * t; } } } }
    if (d->panIdx >= 0) {   // PAN, ramped across the block so an LFO can sweep it smoothly. Voices move across; effects balance
      const float p1 = clampf(m->e[d->panIdx], -1, 1), p0 = m->panNow; m->panNow = p1;
      if (std::fabs(p0) > 1e-4f || std::fabs(p1) > 1e-4f) { const bool voice = d->cat == Cat::Voice;
        for (auto& o : m->out) { float* L = o[0].data(); float* R = o[1].data();
          for (int i = 0; i < n; i++) { const float pp = p0 + (p1 - p0) * (i + 1) / n, a = std::fabs(pp), l = L[i], r = R[i];
            if (pp > 0) { L[i] = l * (1 - a); R[i] = voice ? r + l * a * 0.5f : r; } else { R[i] = r * (1 - a); L[i] = voice ? l + r * a * 0.5f : l; } } } } }
    for (size_t op = 0; op < d->outs.size() && op < 8; op++) if (d->outs[op].type == PType::Audio && op < m->out.size()) {
      float pk = 0; for (int ch = 0; ch < 2; ch++) { const float* o = m->out[op][ch].data(); for (int i = 0; i < n; i += 4) pk = std::max(pk, std::fabs(o[i])); }
      const float was = m->outLev[op].load(std::memory_order_relaxed); m->outLev[op].store(std::max(pk, was * 0.97f), std::memory_order_relaxed); }
    m->t += n / sr; m->processed = blockNo;
    if (m->retiring) {   // ringing out: once it's quiet (or after 20 s), fade its cables away; when none are left, it goes
      float pk = 0; for (auto& op : m->out) for (auto& ch : op) for (int i = 0; i < n; i++) pk = std::max(pk, std::fabs(ch[i]));
      m->retireT += n / sr; m->quietT = pk < 1e-4f ? m->quietT + n / sr : 0;
      if (m->quietT > 0.3f || m->retireT > 20) for (auto& k : cables) if (k.from == m->id && !k.dieAtZero) { k.gt = 0; k.dieAtZero = true; } }
    const bool silent = (m->mute.load(std::memory_order_relaxed) && d->cat != Cat::Out) || (anySolo && d->cat == Cat::Voice && !m->solo.load(std::memory_order_relaxed));
    if (silent) { for (auto& o : m->out) for (auto& ch : o) std::fill(ch.begin(), ch.begin() + n, 0.f); for (auto& lv : m->outLev) lv.store(0, std::memory_order_relaxed); }
    const float f = m->flash.load(std::memory_order_relaxed); if (f > 0) m->flash.store(std::max(0.f, f - n / sr), std::memory_order_relaxed);
  }
  // the master: a gentle compressor and a safety limit, then out to the sound card
  comp.sr = sr; comp.process(masterL, masterR, n);
  float pk = 0;
  for (int i = 0; i < n; i++) { L[i] = masterL[i]; R[i] = masterR[i]; pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i]))); }
  if (recOn.load(std::memory_order_acquire) && !recBuf.empty()) {   // RECORD: copy the master into the ring the screen thread empties
    const long N = (long)recBuf.size() / 2; long w = recW.load(std::memory_order_relaxed);
    for (int i = 0; i < n; i++, w++) { recBuf[(w % N) * 2] = L[i]; recBuf[(w % N) * 2 + 1] = R[i]; }
    recW.store(w, std::memory_order_release); }
  if (prevBuf) {   // the sound browser's preview, added after the recorder so it never ends up in a recording
    const AudioBuf& b = *prevBuf; const double step = b.sr / sr; const long len = (long)b.d.size();
    for (int i = 0; i < n; i++) { const long k = (long)prevPos; if (k + 1 >= len) break; const float fr = (float)(prevPos - k), x = (b.d[k] + (b.d[k + 1] - b.d[k]) * fr) * 0.7f;
      const float fade = std::min(1.f, std::min((float)k, (float)(len - k)) / 64.f);
      L[i] = clampf(L[i] + x * fade, -1, 1); R[i] = clampf(R[i] + x * fade, -1, 1); prevPos += step; }
    if ((long)prevPos + 1 >= len) { bufGarbage.push(prevBuf); prevBuf = nullptr; previewAt.store(-1, std::memory_order_relaxed); }
    else previewAt.store((float)(prevPos / len), std::memory_order_relaxed); }
  outPeak.store(std::max(pk, outPeak.load(std::memory_order_relaxed) * 0.995f), std::memory_order_relaxed);
}

}  // namespace tb
