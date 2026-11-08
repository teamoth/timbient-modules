// Timbient native: drawing the patch and handling the pointer (see ui.h)
#include "ui.h"
#include "../engine_mi/mi_labels.h"
#include "../engine/params.h"
#include <cmath>
#include <cstdio>

namespace tb {
static inline bool modulatable(const ParamDef& q) { return q.opts.empty() && !q.act; }   // levels can be moved by mod cables; choices and buttons cannot


static const int HEAD = 8, ROW = 7, FOOT = 9, TOP = 10;
static int catCol(Cat c) { switch (c) { case Cat::Trig: return 24; case Cat::Voice: return 7; case Cat::Proc: return 17; case Cat::Mod: return 11; default: return 9; } }
static int typeCol(PType t) { return t == PType::Audio ? 43 : t == PType::Trig ? 44 : 45; }

// the ADD menu, in the web version's order (types not built yet are skipped)
static const std::vector<std::pair<std::string, std::vector<std::string>>> MENU = {
  {"RHYTHM", {"clock", "tracker", "keys", "clump", "bursts", "orbits", "hops", "walker", "tombola", "tools", "midi", "midiout"}},
  {"VOICES", {"tone", "pluck", "bell", "mi", "grains", "glass", "click", "analog", "pad", "formant", "breath", "additive", "fold", "sample"}},
  {"PROCESS", {"amp", "vca", "filter", "phaser", "echo", "reverb", "hall", "stretch", "resonator", "ducker", "looper", "tloop", "tape", "crush", "mixer", "listen"}},
  {"MOD", {"lfo", "env", "rand", "follow"}}};

// ---------------- building ----------------
UiMod* Ui::add(const std::string& type, int x, int y, int id) {
  if (id <= 0 || byId(id)) id = nextId++; else nextId = std::max(nextId, id + 1);
  Module* m = eng.create(type, id); if (!m) return nullptr;
  auto u = std::make_unique<UiMod>(); u->m = m; u->x = x; u->y = y; UiMod* r = u.get(); mods.push_back(std::move(u)); eng.add(m); return r;
}
void Ui::remove(UiMod* u) {
  const int id = u->m->id;
  cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const UiCable& c) { return c.from == id || c.to == id; }), cables.end());
  eng.remove(id);   // the engine frees the module once it has stopped using it
  mods.erase(std::remove_if(mods.begin(), mods.end(), [&](const std::unique_ptr<UiMod>& p) { return p.get() == u; }), mods.end());
  drag = Drag();
}
bool Ui::connect(UiMod* a, int fp, UiMod* b, int tp) {
  if (!a || !b || a == b || fp < 0 || tp < 0) return false;
  for (auto& c : cables) if (c.from == a->m->id && c.fromPort == fp && c.to == b->m->id && c.toPort == tp) return false;
  Cable c; c.from = a->m->id; c.fromPort = fp; c.to = b->m->id; c.toPort = tp; c.type = a->m->def->outs[fp].type; eng.connect(c);
  cables.push_back({c.from, fp, c.to, tp, -1, c.type}); return true;
}
bool Ui::connect(UiMod* a, const std::string& op, UiMod* b, const std::string& ip) { return a && b && connect(a, a->m->def->outIndex(op), b, b->m->def->inIndex(ip)); }
bool Ui::modulate(UiMod* s, UiMod* d, int pk) {
  const int op = s->m->def->outIndex("MOD") >= 0 ? s->m->def->outIndex("MOD") : s->m->def->outIndex("CC"); if (pk < 0 || op < 0 || s == d) return false;
  for (auto& c : cables) if (c.type == PType::Mod && c.from == s->m->id && c.to == d->m->id && c.toParam == pk) return false;
  Cable c; c.from = s->m->id; c.fromPort = op; c.to = d->m->id; c.toParam = pk; c.type = PType::Mod; eng.connect(c);
  cables.push_back({c.from, op, c.to, -1, pk, PType::Mod}); return true;
}
void Ui::unplug(const UiCable& k) {
  Cable c; c.from = k.from; c.fromPort = k.fromPort; c.to = k.to; c.toPort = k.toPort; c.toParam = k.toParam; c.type = k.type; eng.disconnect(c);
  cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const UiCable& x) { return x.from == k.from && x.fromPort == k.fromPort && x.to == k.to && x.toPort == k.toPort && x.toParam == k.toParam && x.type == k.type; }), cables.end());
}
void Ui::set(UiMod* m, const std::string& k, float v) { const int i = m->m->def->paramIndex(k); if (i >= 0) m->m->p[i].store(v); }
void Ui::clearPatch() { for (auto& u : mods) eng.remove(u->m->id); mods.clear(); cables.clear(); drag = Drag(); dd.reset(); menuOpen = false; }

// a patch in the spirit of your session, so there's something to listen to straight away
void Ui::demoPatch() {
  auto col = [&](int x, std::vector<std::string> types) { std::vector<UiMod*> v; int y = 16; for (auto& t : types) { UiMod* u = add(t, x, y); v.push_back(u); y += geom(*u).h + 5; } return v; };
  auto c1 = col(6, {"clump", "orbits", "clock"}), c2 = col(86, {"pluck", "glass"}), c3 = col(166, {"pad", "bell"}), c4 = col(246, {"echo", "filter", "lfo"}), c5 = col(326, {"mixer"}), c6 = col(406, {"reverb", "ducker"}), c7 = col(486, {"out"});
  UiMod *clump = c1[0], *orb = c1[1], *clk = c1[2], *pl = c2[0], *fm = c2[1], *pad = c3[0], *bell = c3[1], *echo = c4[0], *filt = c4[1], *lfo = c4[2], *mix = c5[0], *verb = c6[0], *duck = c6[1], *out = c7[0];
  set(clump, "oct", 5); set(clump, "mutate", 1); set(clump, "speed", 2.5f); set(pl, "tone", 0.08f); set(pad, "cut", 420); set(verb, "size", 10); set(verb, "mix", 0.5f);
  set(duck, "depth", 0.9f); set(duck, "curve", 1); set(duck, "rbeats", 3); set(clk, "bpm", 60); set(clk, "sync", 0); set(fm, "decay", 1.6f);
  set(lfo, "rate", 20); set(pad, "air", 0.05f);
  connect(clump, "TRIG", pl, "TRIG"); connect(clump, "TRIG", fm, "TRIG"); connect(orb, "TRIG", pad, "TRIG"); connect(orb, "TRIG", bell, "TRIG");
  connect(pl, "OUT", echo, "IN"); connect(fm, "OUT", echo, "IN"); connect(echo, "OUT", mix, "1"); connect(pad, "OUT", filt, "IN"); connect(filt, "OUT", mix, "2"); connect(bell, "OUT", mix, "3");
  connect(mix, "OUT", verb, "IN"); connect(verb, "OUT", duck, "IN"); connect(duck, "OUT", out, "A"); connect(clk, "BAR", duck, "KEY");
  modulate(lfo, filt, filt->m->def->paramIndex("cut"));
}

// ---------------- geometry and hit testing ----------------
static int trLen(Module* m) { return std::max(1, std::min(Module::SEQ_MAX, (int)m->p[3].load())); }
static int trShown(Module* m) { int last = -1; for (int i = Module::SEQ_MAX - 1; i >= 0; i--) if (m->seq[i * 2].load() >= 0) { last = i; break; } return std::max(trLen(m), last + 1); }
static int trRows(Module* m) { return std::min(16, trShown(m)); }   // the Tracker grows with the rows in use, up to 16; beyond that it scrolls
Ui::Geom Ui::geom(const UiMod& u) const {
  const ModuleDef* d = u.m->def; const int jr = (int)std::max(d->ins.size(), d->outs.size()), np = u.fold ? 0 : (int)d->params.size();
  const int vh = d->type == "tracker" && d->vis ? trRows(u.m) * 6 + 1 : d->vis;
  Geom g; g.jr = jr; g.vh = vh; g.vy = HEAD + jr * ROW + (vh ? 1 : 0); g.py = HEAD + jr * ROW + (vh ? vh + 2 : 0); g.h = g.py + np * ROW + FOOT; g.w = d->width; return g;
}
UiMod* Ui::hit(int wx, int wy, int& lx, int& ly) {
  for (int i = (int)mods.size() - 1; i >= 0; i--) { UiMod& u = *mods[i]; const Geom g = geom(u);
    if (wx >= u.x && wx < u.x + g.w && wy >= u.y && wy < u.y + g.h) { lx = wx - u.x; ly = wy - u.y; return &u; } }
  return nullptr;
}
Ui::JackHit Ui::jackAt(int wx, int wy, int tol) {
  // a jack only counts where it can be seen: not when another module sits on top of it, or of the pointer
  auto covered = [this](int i, int x, int y) { for (size_t j = (size_t)i + 1; j < mods.size(); j++) { const UiMod& o = *mods[j]; const Geom g = geom(o);
      if (x >= o.x && x < o.x + g.w && y >= o.y && y < o.y + g.h) return true; } return false; };
  for (int i = (int)mods.size() - 1; i >= 0; i--) { UiMod& u = *mods[i]; const ModuleDef* d = u.m->def;
    if (covered(i, wx, wy)) continue;
    for (size_t k = 0; k < d->ins.size(); k++) { const int jx = inJackX(u), jy = u.y + jackY((int)k); if (std::abs(wx - jx) <= tol && std::abs(wy - jy) <= tol && !covered(i, jx, jy)) return {&u, false, (int)k, d->ins[k].type, jx, jy}; }
    for (size_t k = 0; k < d->outs.size(); k++) { const int jx = outJackX(u), jy = u.y + jackY((int)k); if (std::abs(wx - jx) <= tol && std::abs(wy - jy) <= tol && !covered(i, jx, jy)) return {&u, true, (int)k, d->outs[k].type, jx, jy}; }
    if (!u.fold) { const Geom g = geom(u); const int st = std::min(tol, 3);   // the mod socket beside each setting that can be moved
      for (size_t k = 0; k < d->params.size(); k++) { if (!modulatable(d->params[k])) continue; const int jx = sockX(u), jy = u.y + g.py + (int)k * ROW + 3;
        if (std::abs(wx - jx) <= st && std::abs(wy - jy) <= st && !covered(i, jx, jy)) { JackHit h{&u, false, -1, PType::Mod, jx, jy}; h.param = (int)k; return h; } } } }
  return {};
}
int Ui::paramAt(UiMod* u, int ly) { const Geom g = geom(*u); const int j = (ly - g.py) / ROW; return !u->fold && ly >= g.py && j >= 0 && j < (int)u->m->def->params.size() ? j : -1; }

// ---------------- drawing ----------------
// a cable hangs as a curve that sags between its ends (the same shape as the web version)
static inline void cablePt(float ax, float ay, float bx, float by, float t, float& x, float& y) {
  const float sag = std::min(28.f, std::hypot(bx - ax, by - ay) * 0.3f + 4), mx = (ax + bx) / 2, my = (ay + by) / 2 + sag;
  x = (1 - t) * (1 - t) * ax + 2 * (1 - t) * t * mx + t * t * bx; y = (1 - t) * (1 - t) * ay + 2 * (1 - t) * t * my + t * t * by;
}
void Ui::drawCable(Canvas& c, int ax, int ay, int bx, int by, int col) {
  int px0 = ax, py0 = ay;
  for (int i = 1; i <= 20; i++) { float x, y; cablePt((float)ax, (float)ay, (float)bx, (float)by, i / 20.f, x, y); const int xi = (int)std::lround(x), yi = (int)std::lround(y); c.line(px0, py0, xi, yi, col); px0 = xi; py0 = yi; }
}
bool Ui::cableEnds(const UiCable& cb, float& ax, float& ay, float& bx, float& by) {
  UiMod *a = byId(cb.from), *b = byId(cb.to); if (!a || !b) return false;
  ax = (float)outJackX(*a); ay = (float)(a->y + jackY(cb.fromPort));
  if (cb.type == PType::Mod) { const Geom g = geom(*b); bx = (float)sockX(*b); by = (float)(b->y + (b->fold ? g.h - FOOT : g.py + cb.toParam * ROW + 3)); }
  else { bx = (float)inJackX(*b); by = (float)(b->y + jackY(cb.toPort)); }
  return true;
}
int Ui::cableAt(int wx, int wy, float maxD, bool audioOnly) {   // the nearest cable within maxD pixels, or -1
  int best = -1; float bd = maxD;
  for (size_t k = 0; k < cables.size(); k++) { const UiCable& cb = cables[k]; if (audioOnly && cb.type != PType::Audio) continue; float ax, ay, bx, by; if (!cableEnds(cb, ax, ay, bx, by)) continue;
    float x0 = ax, y0 = ay;
    for (int i = 1; i <= 20; i++) { float x1, y1; cablePt(ax, ay, bx, by, i / 20.f, x1, y1);
      const float vx = x1 - x0, vy = y1 - y0, l = std::max(1e-6f, vx * vx + vy * vy), t = clampf(((wx - x0) * vx + (wy - y0) * vy) / l, 0, 1), d = std::hypot(wx - x0 - t * vx, wy - y0 - t * vy);
      if (d < bd) { bd = d; best = (int)k; } x0 = x1; y0 = y1; } }
  return best;
}
std::string Ui::cableName(const UiCable& cb) {
  UiMod *a = byId(cb.from), *b = byId(cb.to); if (!a || !b) return "";
  std::string s = a->m->def->name + " > " + b->m->def->name;
  if (cb.type == PType::Mod) s += " " + b->m->def->params[cb.toParam].label; else if (b->m->def->ins[cb.toPort].name != "IN") s += " " + b->m->def->ins[cb.toPort].name;
  return s;
}
static bool sameCable(const UiCable& x, const UiCable& k) { return x.from == k.from && x.fromPort == k.fromPort && x.to == k.to && x.toPort == k.toPort && x.toParam == k.toParam && x.type == k.type; }
void Ui::drawJack(Canvas& c, int jx, int jy, PType t, bool conn, bool lit) {
  const int col = lit ? 46 : typeCol(t);
  if (t == PType::Trig) { for (int k = -2; k <= 2; k++) { const int s = 2 - std::abs(k); c.px(jx + k, jy - s, col); c.px(jx + k, jy + s, col); if (conn) for (int q = -s; q <= s; q++) c.px(jx + k, jy + q, col); } }
  else if (t == PType::Mod) {   // round
    for (int k = -1; k <= 1; k++) { c.px(jx + k, jy - 2, col); c.px(jx + k, jy + 2, col); c.px(jx - 2, jy + k, col); c.px(jx + 2, jy + k, col); }
    if (conn) c.rect(jx - 1, jy - 1, 3, 3, col); }
  else { if (conn) c.rect(jx - 2, jy - 2, 5, 5, col); else c.frame(jx - 2, jy - 2, 5, 5, col); }   // audio: square
}


// each module's own little display
void Ui::drawDisplay(Canvas& c, UiMod& u, int x, int y, int w, float dt) {
  Module* m = u.m; const std::string& ty = m->def->type; auto D = [&](int i) { return m->disp[i].load(std::memory_order_relaxed); };
  const int vh = geom(u).vh;
  if (ty == "clump") {   // the loop as a line; each step a bar, taller when stronger, lit when it fires
    const int base = y + vh - 3;
    for (int i = 0; i < w; i += 2) c.px(x + i, base + 1, 18);
    const int n = (int)D(1); for (int k = 0; k < n && k < 16; k++) { const int sx = x + (int)std::lround(D(2 + k) * (w - 3)), h = 2 + (int)std::lround(D(18 + k) * (vh - 6)); c.rect(sx, base + 1 - h, 3, h, D(34 + k) > 0 ? 4 : 24); }
    const int hx = x + (int)std::lround(D(0) * (w - 1)); for (int j = 0; j < vh - 1; j++) if (j % 2 == 0 || j > vh - 5) c.px(hx, y + j, 17);
  } else if (ty == "orbits") {   // points of light going round (the rings themselves aren't drawn)
    const float cx = x + w / 2.f, cy = y + vh / 2.f - 0.5f; const int n = std::max(1, (int)D(0));
    const float RX = w / 2.f - 2, RY = vh / 2.f - 2;
    for (int i = 0; i < n && i < 8; i++) { const float f = (i + 1.f) / n, rx = 3 + (RX - 3) * f, ry = 2 + (RY - 2) * f, a0 = D(1 + i);
      const bool lit = D(9 + i) > 0; const int px = (int)std::lround(cx + rx * std::cos(a0)), py = (int)std::lround(cy + ry * std::sin(a0));
      c.rect(px - 1, py - 1, lit ? 3 : 2, lit ? 3 : 2, lit ? 4 : 24); }
  } else if (ty == "tombola") {   // the turning drum and its balls
    const float cx = x + w / 2.f, cy = y + vh / 2.f, R = std::min(w, vh) / 2.f - 1.5f, ang = D(0); const int S = std::max(3, (int)D(1)), n = (int)D(2);
    for (int s = 0; s < S; s++) { const float a0 = ang + s * TAU / S, a1 = ang + (s + 1) * TAU / S;
      c.line((int)std::lround(cx + R * std::cos(a0)), (int)std::lround(cy + R * std::sin(a0)), (int)std::lround(cx + R * std::cos(a1)), (int)std::lround(cy + R * std::sin(a1)), 18); }
    for (int i = 0; i < n && i < 12; i++) { const int bx = (int)std::lround(cx + D(3 + i * 4) * R), by = (int)std::lround(cy + D(4 + i * 4) * R); const float lifeK = D(5 + i * 4); const bool hitNow = D(6 + i * 4) > 0;
      const int col = hitNow ? 4 : lifeK > 0.66f ? 24 : lifeK > 0.33f ? 44 : 18; c.rect(bx - 1, by - 1, 3, 3, col); }
  } else if (ty == "hops") {   // each hop a stepping stone, placed by pitch, drifting left and fading
    const int fired = (int)D(0); if (fired != u.hopSeen) { u.hopSeen = fired; if (u.dots.size() > 40) u.dots.erase(u.dots.begin()); u.dots.push_back({0.f, D(1)}); }
    for (auto& dt2 : u.dots) dt2.first += dt; u.dots.erase(std::remove_if(u.dots.begin(), u.dots.end(), [](const std::pair<float, float>& q) { return q.first > 4; }), u.dots.end());
    for (int i = 0; i < w; i += 4) c.px(x + i, y + vh - 1, 21);
    for (auto& q : u.dots) { const int px = x + w - 3 - (int)(q.first * (w - 4) / 4), py = y + vh - 3 - (int)std::lround(q.second * (vh - 5)), col = q.first < 0.15f ? 4 : q.first < 1 ? 24 : q.first < 2.5f ? 44 : 18;
      c.rect(px, py, q.first < 0.15f ? 3 : 2, 2, col); }
  } else if (ty == "ducker") {   // how far the sound is being pushed down, over the last few seconds
    u.ht += dt; if (u.ht > 0.03f) { u.ht = 0; u.hist[u.hi] = D(0); u.hi = (u.hi + 1) % 48; }
    for (int k = 0; k < 48; k++) { const float v = clampf(u.hist[(u.hi + k) % 48], 0, 1); const int xx = x + (int)std::lround(k * (w - 2) / 47.f), hh = (int)std::lround(v * (vh - 2));
      c.px(xx, y + vh - 2, 21); c.px(xx + 1, y + vh - 2, 21); if (hh > 0) c.rect(xx, y, 2, hh, k == 47 ? 4 : 12); }
  } else if (ty == "clock") {
    for (int i = 0; i < 4; i++) c.rect(x + 4 + i * 12, y + 1, 8, 3, D(1) > 0.5f && i == (int)D(0) ? 4 : 21);
  } else if (ty == "lfo" || ty == "env" || ty == "rand" || ty == "follow") {   // the modulation's recent path
    float every = ty == "lfo" ? 0.05f : 0.03f; if (ty == "rand") every = std::min(0.08f, m->p[0].load() / 12);
    u.ht += dt; if (u.ht > every) { u.ht = 0; u.hist[u.hi] = D(0); u.hi = (u.hi + 1) % 48; }
    int py = -1; for (int k = 0; k < 48; k++) { const float v = u.hist[(u.hi + k) % 48]; const int yy = y + 3 - (int)std::lround(v * 3), xx = x + (int)std::lround(k * (w - 1) / 47.f); if (py >= 0) c.line(xx - 1, py, xx, yy, 11); py = yy; }
  } else if (ty == "bursts") {   // the burst's notes along a line, and where it has got to
    for (int i = 0; i < w; i += 2) c.px(x + i, y + 5, 18);
    const int n = (int)D(0); for (int i = 0; i < n && i < 9; i++) c.rect(x + (int)std::lround(D(1 + i) * (w - 2)), y + 1, 2, 5, 7);
    const float k = D(10); if (k >= 0 && k <= 1) for (int j = 0; j < 7; j++) c.px(x + (int)std::lround(k * (w - 2)), y + j, 24);
  } else if (ty == "walker") {
    const int range = std::max(1, (int)m->p[4].load());
    for (int i = 0; i < w; i++) c.px(x + i, y + 4, 21); for (int i = 0; i < range; i++) c.px(x + (int)std::lround((float)i / range * (w - 1)), y + 5, 18);
    c.rect(x + (int)std::lround(D(0) * (w - 3)), y + 2, 3, 2, 24);
  } else if (ty == "tools") {   // which of the last triggers got through
    const int n = (int)D(0); const unsigned h = (unsigned)D(1);
    for (int i = 0; i < 14; i++) c.rect(x + 1 + i * 4, y + 1, 3, 2, i < n ? (((h >> (n - 1 - i)) & 1u) ? 24 : 18) : 21);
  } else if (ty == "phaser") {
    for (int i = 0; i < w; i += 2) c.px(x + i, y + 2, 18);
    const float k = 0.5f + 0.5f * std::sin(D(0) * TAU) * m->p[1].load(); c.rect(x + (int)std::lround(k * (w - 3)), y, 3, 5, 17);
    if (m->p[5].load() > 0.5f) c.rect(x + (int)std::lround((1 - k) * (w - 3)), y + 1, 3, 3, 3);
  } else if (ty == "tape") {   // two turning reels
    const float ph = D(0);
    for (int cx : {x + 12, x + w - 13}) { for (float a = 0; a < TAU; a += 0.5f) c.pxf(cx + 4 * std::cos(a), y + 2 + 2 * std::sin(a), 18); c.pxf(cx + 2 * std::cos(ph * TAU), y + 2 + std::sin(ph * TAU), 4); }
    for (int i = x + 16; i < x + w - 16; i++) c.px(i, y + 4, 7);
  } else if (ty == "sample" || ty == "looper" || ty == "stretch") {   // the sound's outline; the region or slices (Sample); playheads and grains
    const bool smp = ty == "sample"; const int kind = smp ? (int)m->p[1].load() : 0, multi = kind >= 2; int top = y;
    if (multi) {   // KIT / MULTI: a numbered place for each sound; tap one to load, replace, set its ROOT or remove it
      for (int k = 0; k < UiMod::SLOTS; k++) { const int bx = x + k * 6; const bool has = !u.snd[k].id.empty(), sel = k == u.shown;
        if (sel) c.rect(bx, y, 5, 5, has ? 24 : 18); else c.frame(bx, y, 5, 5, has ? 24 : 21); c.text(std::to_string(k + 1), bx + 1, y, sel ? 15 : has ? 9 : 18); }
      top = y + 6; }
    const UiMod::Snd& sd = u.snd[multi ? u.shown : 0]; const int mid = multi ? top + 3 : y + 6, amp = multi ? 2 : 5;
    if (ty == "stretch" && m->p[0].load() < 0.5f) { c.text("LIVE: WHAT COMES IN", x + 2, y + 4, 18); }
    else if (sd.peaks.empty()) { c.text(sd.missing ? "LOAD IT AGAIN" : multi ? (u.snd[u.shown].id.empty() ? "TAP A NUMBER" : "") : smp || ty == "stretch" ? "TAP LOAD FILE" : "LISTENING", x + (multi ? 2 : 2), multi ? top : y + 4, sd.missing ? 14 : 18); }
    else {
      const float a0 = smp && kind != 1 ? m->p[5].load() : 0, b0 = smp && kind != 1 ? a0 + m->p[6].load() * (1 - a0) : 1;
      for (int i = 0; i < w; i++) { const float fx = (float)i / w; const int h = (int)std::lround(sd.peaks[std::min(sd.peaks.size() - 1, (size_t)(fx * sd.peaks.size()))] * amp), col = fx >= a0 && fx <= b0 ? 48 : 21;
        for (int j = -h; j <= h; j++) c.px(x + i, mid + j, col); }
      if (smp && kind == 1) { const int N = std::max(1, (int)m->p[2].load()); for (int k = 1; k < N; k++) { const int sx = x + (int)std::lround((float)k / N * (w - 1)); for (int j = 0; j < 12; j += 2) c.px(sx, y + j, 24); } }   // where the slices are cut
      else if (smp) for (float xx : {a0, b0}) for (int j = top - y; j < 12; j += 2) c.px(x + (int)std::lround(xx * (w - 1)), y + j, 24);
      if (smp) { const int n = (int)D(0); for (int k = 0; k < n && k < 12; k++) { if (multi && (int)D(20 + k) != u.shown) continue; const int px = x + (int)std::lround(clampf(D(1 + k), 0, 1) * (w - 1)); for (int j = top - y; j < 12; j++) c.px(px, y + j, 12); } }
    }
    if (!smp && D(0) >= 0) { const int hx = x + (int)std::lround(clampf(D(0), 0, 1) * (w - 1)); for (int j = 0; j < 12; j++) c.px(hx, y + j, 12);
      const int n = (int)D(1); for (int k = 0; k < n && k < 30; k++) c.px(x + (int)std::lround(clampf(D(2 + k), 0, 1) * (w - 1)), y + 6 + (int)(std::rand() % 9) - 4, 4); }
  } else if (ty == "tloop") {   // the tape: how loud each part is, the piece being played, and the head (red while recording)
    const float a0 = D(2), n0 = D(3); for (int k = 0; k < 32; k++) { const int x0 = x + k * w / 32, x1 = x + (k + 1) * w / 32; const float fx = (k + 0.5f) / 32;
      const int h = std::max(0, std::min(5, (int)std::lround(std::sqrt(D(4 + k)) * 5))), col = n0 < 0.999f && (fx < a0 || fx > a0 + n0) ? 21 : 48;
      for (int xx = x0; xx < x1 - (w >= 64 ? 1 : 0); xx++) for (int j = -h; j <= h; j++) c.px(xx, y + 6 + j, col); }
    const int hx = x + (int)std::lround(clampf(D(0), 0, 1) * (w - 1)); for (int j = 0; j < 12; j++) c.px(hx, y + j, D(1) > 0.5f ? 14 : 12);
  } else if (ty == "listen") {   // what key and scale it hears, how sure it is, the runner-up, and the 12 notes it's hearing
    const int r = (int)D(0), sc = std::max(0, std::min((int)scales().size() - 1, (int)D(1))), r2 = (int)D(3), sc2 = std::max(0, std::min((int)scales().size() - 1, (int)D(4)));
    if (D(2) <= 0 && D(18) < 1e-6f) c.text("LISTENING...", x + 1, y + 1, 18);
    else { const std::string b1 = std::string(NOTE_NAMES[r % 12]) + " " + scales()[sc].name, p1 = std::to_string((int)std::lround(D(2) * 100)) + "%";
      c.text(b1.substr(0, 12), x + 1, y + 1, 4); c.text(p1, x + w - 1 - Canvas::tw(p1), y + 1, 9);
      const std::string b2 = std::string("OR ") + NOTE_NAMES[r2 % 12] + " " + scales()[sc2].name; c.text(b2.substr(0, 15), x + 1, y + 8, 18); }
    bool inSc[12] = {false}; for (int st : scales()[sc].steps) inSc[(r + st) % 12] = true;
    const int bw = std::max(2, (w - 2) / 12);
    for (int i = 0; i < 12; i++) { const int h = (int)std::lround(clampf(D(6 + i), 0, 1) * 9), bx = x + 1 + i * bw; c.px(bx, y + 25, 21);
      if (h > 0) c.rect(bx, y + 25 - h, bw - 1, h, inSc[i] ? (i == r ? 4 : 11) : 21); }
  } else if (ty == "tracker") {
    drawTracker(c, u, x, y, w, dt);
  } else if (ty == "keys") {
    const int n = (int)scales()[eng.key.scale.load()].steps.size(), root = eng.key.root.load(), sc = eng.key.scale.load();
    const int oct = (int)m->p[1].load(), rowStep = 8;   // each row carries on up the scale from the row below
    for (int i = 0; i < 32; i++) { const int deg = (i / 8) * rowStep + (i % 8), note = (int)midiOf(degOf(oct, deg, sc), root, sc), cx = (i % 8) * 13, cy = (3 - i / 8) * 9;
      u.lit[i] = std::max(0.f, u.lit[i] - dt);
      const bool lit = u.lit[i] > 0, isRoot = deg % n == 0;
      c.rect(x + cx, y + cy, 12, 8, lit ? 4 : isRoot ? 24 : 21); const std::string t = noteName(note); c.text(t, x + cx + 6 - Canvas::tw(t) / 2, y + cy + 2, lit || isRoot ? 15 : 9); }
  } else if (ty == "mixer") {
    for (int k = 0; k < 4; k++) { const int cx = x + k * 13; const float v = clampf((20 * std::log10(D(k) + 1e-6f) + 48) / 48, 0, 1);
      c.text(std::to_string(k + 1), cx, y + 1, 18); for (int j = 0; j < 7; j++) { const bool on = j < (int)std::lround(v * 7); c.rect(cx + 5, y + 6 - j, 6, 1, on ? (j > 5 ? 14 : j > 3 ? 12 : 48) : 21); } }
  } else if (ty == "amp") {
    const float n = clampf((20 * std::log10(D(0) + 1e-6f) + 48) / 54, 0, 1); const int fw = (int)std::lround(n * w), un = (int)std::lround(w * 48 / 54.f);
    for (int i = 0; i < w; i += 2) c.px(x + i, y + 3, 18); for (int i = 0; i < fw; i++) { const int col = i >= un ? 14 : i > un * 0.75f ? 12 : 3; c.px(x + i, y + 1, col); c.px(x + i, y + 2, col); }
  } else if (ty == "out") {
    const float n = clampf((20 * std::log10(D(0) + 1e-6f) + 48) / 48, 0, 1); const int fw = (int)(n * w);
    for (int i = 0; i < w; i += 2) c.px(x + i, y + 3, 18); for (int i = 0; i < fw; i++) { const int col = i > w * 0.92f ? 14 : i > w * 0.7f ? 12 : 3; c.px(x + i, y + 1, col); c.px(x + i, y + 2, col); }
    if (m->disp[1].load() > 0.5f) { const int sec = (int)m->disp[2].load(); char b[24]; std::snprintf(b, 24, "REC %d:%02d", sec / 60, sec % 60);
      if (((int)(blink * 2)) % 2) c.rect(x + 2, y + 6, 3, 3, 14); c.text(b, x + 8, y + 5, 14); }
    else c.text("NOT RECORDING", x + 2, y + 5, 18);
    const int sy = y + 14, tx0 = x + 6, tw0 = w - 12; const float xf = clampf(m->p[2].load(), 0, 1); const int hx = tx0 + (int)std::lround(xf * tw0);
    c.text("A", x, sy - 2, xf < 0.5f ? 9 : 18); c.text("B", x + w - 3, sy - 2, xf > 0.5f ? 9 : 18);
    for (int i = 0; i <= tw0; i++) { c.px(tx0 + i, sy, 21); c.px(tx0 + i, sy + 1, 21); }
    for (float k : {0.f, 0.5f, 1.f}) c.px(tx0 + (int)std::lround(k * tw0), sy + 4, 18);
    c.rect(hx - 2, sy - 3, 5, 8, 4); c.px(hx, sy - 2, 15); c.px(hx, sy + 3, 15);
  }
}

// ---------------- the Tracker's list of steps ----------------
static void trClamp(UiMod& u) { u.scroll = clampf(std::round(u.scroll), 0, (float)std::max(0, trShown(u.m) - trRows(u.m))); }
static void trShow(UiMod& u, int row) { if (row < u.scroll) u.scroll = (float)row; else if (row >= u.scroll + trRows(u.m)) u.scroll = (float)(row - trRows(u.m) + 1); trClamp(u); }
void Ui::drawTracker(Canvas& c, UiMod& u, int x, int y, int w, float dt) {
  Module* m = u.m; const int TR_ROWS = trRows(m), len = trLen(m), shown = trShown(m), pos = (int)m->disp[0].load(), div = std::max(1, (int)m->disp[1].load()), cur = m->cursor.load();
  u.hold = std::max(0.f, u.hold - dt);
  if (u.hold <= 0 && pos >= 0 && (pos < u.scroll || pos >= u.scroll + TR_ROWS)) u.scroll = (float)pos;   // follows the playhead, except for a few seconds after you touch it
  trClamp(u); const int sc = (int)u.scroll;
  auto num = [](int r) { char b[16]; std::snprintf(b, 16, "%03d", r + 1); return std::string(b); };
  for (int i = 0; i < TR_ROWS; i++) { const int r = sc + i, ry = y + 1 + i * 6; if (r >= shown) break;
    const float nt = m->seq[r * 2].load(), vel = m->seq[r * 2 + 1].load(); const bool has = nt >= 0;
    if (r >= len) { if (r == len) { for (int k = 0; k < w; k += 2) c.px(x + k, ry - 1, 18); c.text("OFF", x + w - 13, ry, 18); }
      c.text(num(r), x + 1, ry, 21); c.text(has ? noteName((int)nt) : "---", x + 15, ry, 21); continue; }
    const bool play = r == pos, isCur = r == cur, beat = r % div == 0;
    if (play) c.rect(x, ry - 1, w, 7, 17); else if (isCur) c.rect(x, ry - 1, w, 7, 21);
    c.text(num(r), x + 1, ry, play ? 15 : beat ? 9 : 18);
    c.text(has ? noteName((int)nt) : "---", x + 15, ry, has ? (play ? 15 : 9) : play ? 15 : 18);
    if (has) { const int vw = (int)std::lround(vel * 10); for (int k = 0; k < vw; k++) c.px(x + w - 12 + k, ry + 2, play ? 15 : 7); }
    if (isCur) { c.px(x - 2, ry + 1, 4); c.px(x - 2, ry + 2, 4); c.px(x - 2, ry + 3, 4); c.px(x - 1, ry + 2, 4); }
  }
  if (shown > TR_ROWS) {   // where the view is, along the right edge
    const int H = TR_ROWS * 6, h = std::max(3, (int)std::lround((float)TR_ROWS / shown * H)), sy = (int)std::lround(u.scroll / (shown - TR_ROWS) * (H - h));
    for (int j = 0; j < H; j++) c.px(x + w + 1, y + j - 1, j >= sy && j < sy + h ? 9 : 21);
    if (pos >= 0) { const int py = (int)std::lround((float)pos / std::max(1, shown - 1) * (H - 2)); c.px(x + w + 1, y + py - 1, 17); c.px(x + w + 1, y + py, 17); c.px(x + w, y + py, 17); }
  }
}
void Ui::trackerWatch(UiMod& u) {   // notes typed in through ENTER arrive from the audio side: show and save them
  const int v = u.m->seqVer.load(); if (v == u.seqVer) return; u.seqVer = v;
  const int en = (int)u.m->disp[4].load();
  if (en != u.entered) { u.entered = en; char b[32]; std::snprintf(b, 32, "STEP %03d  ", (int)u.m->disp[2].load() + 1); status(b + noteName((int)u.m->disp[3].load()), 1.2f); }
  u.hold = 6; trShow(u, u.m->cursor.load()); changed();
}

// a setting's name for the status line. MI VOICES: Plaits' names, plus what HARMONICS / TIMBRE / MORPH do in this model
static std::string statusLabel(const UiMod& u, const ParamDef& q) {
  if (u.m->def->type != "mi") return q.label;
  const int w3 = q.key == "harm" ? 0 : q.key == "timb" ? 1 : q.key == "morph" ? 2 : -1; if (w3 < 0) return q.label;
  const MiEngine& e = miEngines()[std::max(0, std::min(MI_ENGINES - 1, (int)u.m->p[0].load()))];
  return q.label + " (" + e.k[w3] + ")";
}
int Ui::minusX(const UiMod& u, int j) const {   // after the name: normally at 39, further for a long name or an inside socket
  const ParamDef& q = u.m->def->params[j]; std::string lab = q.label;
  const int ins = jacksIn && modulatable(q) ? 5 : 0;
  return std::max(39, 3 + Canvas::tw(lab.substr(0, 11)) + 2) + ins;
}
void Ui::drawModule(Canvas& c, UiMod& u, float dt) {
  if (u.m->seq) trackerWatch(u);
  const ModuleDef* d = u.m->def; const Geom g = geom(u); const int x = u.x - viewX, y = u.y - viewY, w = g.w;
  if (x > c.W || y > c.H || x + w < 0 || y + g.h < 0) return;
  static uint8_t inkTbl[128]; static bool init = false; if (!init) { for (int i = 0; i < 128; i++) inkTbl[i] = (uint8_t)i; inkTbl[9] = 28; inkTbl[18] = 29; inkTbl[21] = 30; inkTbl[4] = 47; init = true; }
  c.rect(x + 2, y + 2, w, g.h, 26);
  c.ink = inkTbl; c.rect(x, y, w, g.h, 27); c.frame(x, y, w, g.h, 21);
  c.ink = nullptr; const bool fl = u.m->flash.load() > 0, mu = u.m->mute.load();
  c.rect(x, y, w, HEAD, fl ? 4 : mu ? 18 : catCol(d->cat)); c.text(d->name, x + 2, y + 2, 15); if (!d->fixed) c.text("x", x + w - 6, y + 2, 15);
  c.ink = inkTbl;
  for (size_t k = 0; k < d->ins.size(); k++) { bool conn = false; for (auto& cb : cables) if (cb.to == u.m->id && cb.toPort == (int)k) conn = true;
    drawJack(c, inJackX(u) - viewX, y + jackY((int)k), d->ins[k].type, conn, false); c.text(d->ins[k].name, x + 4 + (jacksIn ? 5 : 0), y + jackY((int)k) - 2, 9); }
  for (size_t k = 0; k < d->outs.size(); k++) { bool conn = false; for (auto& cb : cables) if (cb.from == u.m->id && cb.fromPort == (int)k) conn = true;
    drawJack(c, outJackX(u) - viewX, y + jackY((int)k), d->outs[k].type, conn, fl && d->outs[k].type == PType::Trig); c.text(d->outs[k].name, x + w - 4 - (jacksIn ? 5 : 0) - Canvas::tw(d->outs[k].name), y + jackY((int)k) - 2, 9); }
  // each cable's plug: the cable itself, drawn over the panel from its jack to the edge, so it leaves at the cable's own angle
  for (auto& cb : cables) { if (cb.from != u.m->id && cb.to != u.m->id) continue; float ax, ay, bx, by; if (!cableEnds(cb, ax, ay, bx, by)) continue;
    const int col = (selT > 0 && sameCable(cb, sel)) ? 4 : typeCol(cb.type);
    for (int end = 0; end < 2; end++) { if ((end == 0 ? cb.from : cb.to) != u.m->id) continue;
      const float jx = end == 0 ? ax : bx, jy = end == 0 ? ay : by; float px = jx, py = jy; bool out = false;
      for (int i = 1; i <= 20 && !out; i++) { float qx, qy; cablePt(ax, ay, bx, by, end == 0 ? i / 20.f : 1 - i / 20.f, qx, qy);
        const float dx = qx - px, dy = qy - py; const int n = std::max(1, (int)std::ceil(std::max(std::fabs(dx), std::fabs(dy))));
        for (int s2 = 1; s2 <= n; s2++) { const int wx = (int)std::lround(px + dx * s2 / n), wy = (int)std::lround(py + dy * s2 / n);
          const float dj = std::hypot(wx - jx, wy - jy); if (wx < u.x || wx >= u.x + w || wy < u.y || wy >= u.y + g.h || dj > 6.5f) { out = true; break; }   // a short plug; then it goes under the panel
          if (dj >= 2.5f) c.px(wx - viewX, wy - viewY, col); }
        px = qx; py = qy; } } }
  if (d->vis) drawDisplay(c, u, x + 3, y + g.vy, w - 6, dt);
  const int root = eng.key.root.load(); const bool clocked = u.m->clockedIn.load();
  for (size_t j = 0; j < (u.fold ? 0 : d->params.size()); j++) {
    const ParamDef& q = d->params[j]; const int ry = y + g.py + (int)j * ROW; const float v = u.m->p[j].load(); const bool active = drag.kind == D_PARAM && drag.m == &u && drag.k == (int)j;
    if (q.act) { const bool recOn = d->type == "out" && q.key == "rec" && u.m->disp[1].load() > 0.5f; c.text(recOn ? "STOP + SAVE" : q.label, x + 3, ry + 1, recOn ? 14 : 9); c.text(">", x + w - 6, ry + 1, 9); continue; }
    if (q.play) { const bool on = v > 0.5f; c.rect(x + 3, ry, w - 6, 7, on ? 17 : 21); const int ix = x + 6, iy = ry + 1;
      if (on) c.rect(ix, iy + 1, 3, 3, 15); else for (int k = 0; k < 3; k++) for (int jj = k; jj < 5 - k; jj++) c.px(ix + k, iy + jj, 9);
      c.text(on ? "PLAYING" : "PLAY", x + 13, ry + 1, on ? 15 : 9); continue; }
    int syncI = d->paramIndex("sync"); const bool syncOn = syncI >= 0 && d->params[syncI].tog && u.m->p[syncI].load() > 0.5f;
    bool idle = (q.only == "free" && clocked) || (q.only == "clock" && !clocked) || (q.only == "own" && (syncOn || clocked));
    if (q.only == "adsr" && u.m->p[6].load() < 0.5f) idle = true;
    if (q.only == "file" && u.m->p[0].load() < 0.5f) idle = true;   // STRETCH: only for a loaded file   // MI VOICES: the envelope is only used in ADSR
    if (d->type == "sample") { const int kind = (int)u.m->p[1].load(); const bool once = u.m->p[3].load() > 0.5f;   // settings that don't apply to this kind of sound or way of playing
      idle = idle || (q.only == "slices" && kind != 1) || (q.only == "one" && kind != 0) || (q.only == "region" && kind == 1) || (q.only == "once" && !once) || (q.only == "loop" && once); }
    std::string lab = q.label;
    c.text(lab.substr(0, 11), x + 3 + (jacksIn && modulatable(q) ? 5 : 0), ry + 1, active ? 4 : idle ? 21 : 18);
    bool modded = false; for (auto& cb : cables) if (cb.type == PType::Mod && cb.to == u.m->id && cb.toParam == (int)j) modded = true;
    if (modulatable(q)) drawJack(c, sockX(u) - viewX, ry + 3, PType::Mod, modded, false);   // its mod socket: filled when something moves it
    if (!q.opts.empty()) { std::string s = fmtParam(q, v, root).substr(0, (size_t)std::max(6, (w - Canvas::tw(lab.substr(0, 11)) - 10 - (jacksIn ? 5 : 0)) / 4)); /* as much of the choice as fits beside its name */ c.text(s, x + w - 3 - Canvas::tw(s), ry + 1, active ? 4 : idle ? 21 : 9); c.px(x + w - 3 - Canvas::tw(s) - 3, ry + 3, idle ? 21 : 18); continue; }
    const bool stepping = drag.kind == D_STEP && drag.m == &u && drag.k == (int)j;
    const int mx = x + minusX(u, (int)j); c.text("-", mx, ry + 1, stepping && drag.dir < 0 ? 4 : idle ? 21 : 9); c.text("+", x + w - 6, ry + 1, stepping && drag.dir > 0 ? 4 : idle ? 21 : 9);   // nudge it a little at a time
    const int bx = mx + 5, bw = x + w - 8 - bx; const float nb = clampf(q.toNorm(v), 0, 1), n = modded ? clampf(q.toNorm(u.m->eff[j].load(std::memory_order_relaxed)), 0, 1) : nb;
    for (int i = 0; i < bw; i += 2) c.px(bx + i, ry + 4, 18);
    const int fw = (int)std::lround(n * bw); for (int i = 0; i < fw; i++) { c.px(bx + i, ry + 4, active ? 4 : idle ? 21 : modded ? 11 : 7); if (active) c.px(bx + i, ry + 3, 4); }   // modulated: green, and moving with it
    if (modded) { const int bxm = bx + (int)std::lround(nb * (bw - 1)); c.px(bxm, ry + 2, 9); c.px(bxm, ry + 3, 9); c.px(bxm, ry + 5, 9); }   // where you set it
  }
  if (!d->fixed) { const int fy = y + g.h - FOOT + 1; for (int i = 3; i < w - 3; i += 2) c.px(x + i, fy - 2, 21);
    auto box = [&](int bx, bool on, const char* l, int col) { if (on) c.rect(bx, fy, 9, 7, col); else c.frame(bx, fy, 9, 7, 18); c.text(l, bx + 3, fy + 1, on ? 15 : 9); };
    if (d->cat == Cat::Trig || d->cat == Cat::Mod) {   // sequencers and mod sources: simply on or off
      const bool on = !mu; if (on) c.rect(x + 3, fy, 15, 7, 17); else c.frame(x + 3, fy, 15, 7, 18); c.text(on ? "ON" : "OFF", x + (on ? 7 : 5), fy + 1, on ? 15 : 9); }
    else { box(x + 3, mu, "M", 14); if (d->cat == Cat::Voice) box(x + 14, u.m->solo.load(), "S", 4); }   // only sound makers can be soloed
    box(x + w - 12, false, u.fold ? "+" : "-", 9);
    if (clocked) c.text("CLOCKED", x + w - 15 - Canvas::tw("CLOCKED"), fy + 1, 24); }
  c.ink = nullptr;
}

void Ui::draw(Canvas& c, float dt) {
  blink += dt;
  lastW = c.W;
  c.rect(0, 0, c.W, c.H, 25);
  // cables first, so modules sit on top of them
  for (auto& cb : cables) {
    UiMod *a = byId(cb.from), *b = byId(cb.to); if (!a || !b) continue;
    const int ax = outJackX(*a) - viewX, ay = a->y + jackY(cb.fromPort) - viewY;
    int bx, by; if (cb.type == PType::Mod) { const Geom g = geom(*b); bx = sockX(*b) - viewX; by = b->y + (b->fold ? g.h - FOOT : g.py + cb.toParam * ROW + 3) - viewY; } else { bx = inJackX(*b) - viewX; by = b->y + jackY(cb.toPort) - viewY; }
    const bool hi = (selT > 0 && sameCable(cb, sel)) || (drag.kind == D_MOVE && drag.splice >= 0 && drag.splice < (int)cables.size() && sameCable(cb, cables[drag.splice]));
    drawCable(c, ax, ay, bx, by, hi ? 4 : a->m->flash.load() > 0 && cb.type == PType::Trig ? 46 : typeCol(cb.type));
  }
  for (auto& u : mods) drawModule(c, *u, dt);
  if (selOn) { selT = 1;   // a picked cable: its UNPLUG button, until you tap anywhere else
    bool alive = false; for (auto& cb : cables) if (sameCable(cb, sel)) alive = true; if (!alive) selOn = false; }
  selT = std::max(0.f, selT - dt);
  if (selOn) { const int bw = Canvas::tw("UNPLUG") + 6; ubx = std::max(2, std::min(c.W - bw - 2, ubx)); uby = std::max(TOP + 2, std::min(c.H - 11, uby));
    c.rect(ubx + 1, uby + 1, bw, 9, 26); c.rect(ubx, uby, bw, 9, 14); c.text("UNPLUG", ubx + 3, uby + 2, 15); }
  // holding - or + keeps nudging: a short wait, then steadily
  if (drag.kind == D_STEP) { drag.holdT += dt; while (drag.holdT > 0.45f) { drag.holdT -= 0.06f; nudge(drag.m, drag.k, drag.dir); } }
  // a cable being dragged, and the jacks it could go to
  if (drag.kind == D_CABLE) {
    drawCable(c, drag.from.x - viewX, drag.from.y - viewY, drag.wx - viewX, drag.wy - viewY, 4);
    for (auto& up : mods) { UiMod& u = *up; const auto& list = drag.rev ? u.m->def->outs : u.m->def->ins;
      for (size_t k = 0; k < list.size(); k++) if (list[k].type == drag.from.type && &u != drag.from.u) drawJack(c, (drag.rev ? outJackX(u) : inJackX(u)) - viewX, u.y + jackY((int)k) - viewY, list[k].type, true, true); }
  }
  // top bar
  barHits.clear();
  c.rect(0, 0, c.W, TOP, 15); for (int x = 0; x < c.W; x += 2) c.px(x, TOP, 21);
  int bx = 3;
  auto btn = [&](const std::string& l, int col, std::function<void()> fn) { c.text(l, bx, 3, col); barHits.push_back({bx, bx + Canvas::tw(l), fn}); bx += Canvas::tw(l) + 8; };
  btn("+ ADD", 4, [this] { menuOpen = !menuOpen; dd.reset(); });
  for (auto& b : bar) { const int x0 = bx; auto fn = b.fn; btn(b.label, b.col, [fn, x0] { fn(x0); }); }
  { bool open = false; for (auto& u : mods) if (!u->m->def->fixed && !u->fold) open = true;
    btn(open ? "FOLD ALL" : "OPEN ALL", 9, [this, open] { for (auto& u : mods) if (!u->m->def->fixed) u->fold = open; changed(); }); }
  const std::string fs = isFullscreen ? "WINDOW" : "FULLSCREEN"; const int fx = c.W - Canvas::tw(fs) - 4; c.text(fs, fx, 3, 9); barHits.push_back({fx, fx + Canvas::tw(fs), [this] { wantFullscreen = !isFullscreen; }});
  int ix0 = fx;
  if (onZoom) {   // zoom out / in
    const int zx = fx - 24; for (int i = 0; i < 5; i++) c.px(zx + i, 5, 9); for (int i = 0; i < 5; i++) { c.px(zx + 10 + i, 5, 9); c.px(zx + 12, 3 + i, 9); }
    barHits.push_back({zx - 1, zx + 5, [this] { onZoom(-1); }}); barHits.push_back({zx + 9, zx + 15, [this] { onZoom(1); }}); ix0 = zx;
  }
  char info[64]; std::snprintf(info, sizeof info, "AUDIO %d%%", (int)std::lround(eng.cpuLoad.load() * 100));
  const int ix = ix0 - Canvas::tw(info) - 10;
  if (statusT > 0) { statusT -= dt; std::string s = statusMsg; const int room = (ix - bx - 4) / 4; if ((int)s.size() > room) s = s.substr(0, std::max(0, room)); c.text(s, bx + 4, 3, 17); }
  else if (ix > bx) c.text(info, ix, 3, 18);
  // the ADD menu
  menuItems.clear();
  if (menuOpen) {
    std::vector<std::pair<bool, std::string>> lines; for (auto& cat : MENU) { lines.push_back({true, cat.first}); for (auto& t : cat.second) if (findDef(t)) lines.push_back({false, t}); }
    const int colW = 66, cols = std::max(1, std::min(4, (c.W - 6) / colW)), per = ((int)lines.size() + cols - 1) / cols, mh = per * 8 + 6;
    c.rect(1, TOP + 1, cols * colW + 4, mh, 15); c.frame(1, TOP + 1, cols * colW + 4, mh, 21);
    for (size_t i = 0; i < lines.size(); i++) { const int cc = (int)i / per, r = (int)i % per, x = 4 + cc * colW, y = TOP + 4 + r * 8;
      if (lines[i].first) c.text(lines[i].second, x, y, 18);
      else { const ModuleDef* d = findDef(lines[i].second); c.rect(x, y + 1, 3, 3, catCol(d->cat)); c.text(d->name, x + 6, y, 9); menuItems.push_back({d->type, x, y, colW - 6}); } }
  }
  // a dropdown list
  if (dd) {
    // a long list wraps into columns so it always fits on screen
    int cw = 0; for (auto& it : dd->items) cw = std::max(cw, Canvas::tw(it)); cw += dd->sw.empty() ? 8 : 16;
    const int n = (int)dd->items.size(), rowsFit = std::max(1, (c.H - TOP - 8) / 8), cols = (n + rowsFit - 1) / rowsFit, rows = (n + cols - 1) / cols;
    const int w = cw * cols, h = rows * 8 + 4;
    dd->x = std::max(2, std::min(dd->x, c.W - w)); dd->y = std::max(TOP + 1, std::min(dd->y, c.H - h - 1)); dd->w = w; dd->rows = rows; dd->colW = cw;
    c.rect(dd->x, dd->y + 2, w, h, 26); c.rect(dd->x - 2, dd->y, w, h, 15); c.frame(dd->x - 2, dd->y, w, h, 21);
    for (int i = 0; i < n; i++) { const int cx = dd->x + (i / rows) * cw, y = dd->y + 2 + (i % rows) * 8; if (i == dd->cur) c.rect(cx - 1, y - 1, cw - 2, 7, 21);
      const int sw = i < (int)dd->sw.size() ? dd->sw[i] : -1, tx = dd->sw.empty() ? cx + 1 : cx + 9;   // a colour swatch in front, in the theme menus
      if (sw >= 0) { c.rect(cx + 1, y, 6, 5, sw); c.frame(cx + 1, y, 6, 5, 21); }
      c.text(dd->items[i], tx, y, i == dd->title ? 18 : i == dd->danger ? 14 : i == dd->cur ? 4 : 9); }
  }
  // the tempo panel
  if (tp.open) {
    tp.x = std::max(2, std::min(tp.x, c.W - TPW - 2)); tp.y = std::max(TOP + 1, std::min(tp.y, c.H - TPH - 2)); const int x = tp.x, y = tp.y, bpm = (int)std::lround(eng.tempo.bpm.load());
    c.rect(x + 2, y + 2, TPW, TPH, 26); c.rect(x, y, TPW, TPH, 15); c.frame(x, y, TPW, TPH, 21);
    c.text("SHARED TEMPO", x + 4, y + 3, 18); const std::string v = std::to_string(bpm) + " BPM"; c.text(v, x + TPW - 4 - Canvas::tw(v), y + 3, 4);
    auto btn = [&](int bx, int by, int w, const char* l) { c.frame(bx, by, w, 7, 18); c.text(l, bx + (w - Canvas::tw(l)) / 2, by + 1, 9); };
    btn(x + 4, y + 11, 9, "-"); btn(x + TPW - 13, y + 11, 9, "+");
    const int tx = x + 17, tw = TPW - 34, fx = tx + (int)std::lround((bpm - 20) / 220.f * (tw - 1));
    for (int i = 0; i < tw; i += 2) c.px(tx + i, y + 14, 18); for (int i = tx; i < fx; i++) c.px(i, y + 14, 7); c.rect(fx - 1, y + 12, 3, 5, 4);
    btn(x + 4, y + 21, 21, "TAP"); btn(x + 29, y + 21, 17, "-10"); btn(x + 50, y + 21, 17, "+10");
  }
  drawBrowser(c);
  // the name box
  if (ask) {
    const int w = std::min(c.W - 10, 150), h = 34, x = (c.W - w) / 2, y = TOP + 30;
    c.rect(x + 2, y + 2, w, h, 26); c.rect(x, y, w, h, 15); c.frame(x, y, w, h, 21);
    c.text(ask->title, x + 4, y + 4, 18); c.rect(x + 4, y + 12, w - 8, 9, 21);
    std::string t = ask->text; const int room = (w - 14) / 4; if ((int)t.size() > room) t = t.substr(t.size() - room);
    c.text(t, x + 6, y + 14, 4); if (std::fmod(blink, 1.f) < 0.5f) c.rect(x + 6 + Canvas::tw(t) + 2, y + 13, 1, 7, 4);
    c.text("OK", x + 6, y + 25, 9); c.text("CANCEL", x + 24, y + 25, 9);
  }
  c.toRGBA();
}

void Ui::typed(const std::string& t) { if (!ask) return; for (char ch : t) { const char u = (char)std::toupper((unsigned char)ch);
    if (((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || std::string(" #-/+.").find(u) != std::string::npos) && ask->text.size() < 24) ask->text += u; } }
void Ui::keyBackspace() { if (ask && !ask->text.empty()) ask->text.pop_back(); }
void Ui::keyEscape() { ask.reset(); }
void Ui::keyEnter() { if (!ask) return; auto a = std::move(ask); std::string t = a->text; while (!t.empty() && t.back() == ' ') t.pop_back(); if (!t.empty() && a->done) a->done(t); }

// ---------------- input ----------------
void Ui::keysPress(UiMod& u, int x, int y) {
  const int col = x / 13, row = 3 - y / 9; if (col < 0 || col > 7 || row < 0 || row > 3) return;
  const int i = row * 8 + col, sc = eng.key.scale.load(), n = (int)scales()[sc].steps.size(), rowStep = 8;
  Event ev; ev.note = midiOf(degOf((int)u.m->p[1].load(), row * rowStep + col, sc), eng.key.root.load(), sc); ev.vel = u.m->p[0].load();
  eng.trigger(u.m->id, -1, ev); u.lit[i] = 0.25f;
}

void Ui::setBpm(float v) { const float b = std::round(clampf(v, 20, 240)); eng.tempo.bpm.store(b); char t[40]; std::snprintf(t, 40, "SHARED TEMPO %d BPM", (int)b); status(t, 1.5f); changed(); }
bool Ui::tempoDown(float sx, float sy) {   // true if the tap was inside the panel
  const int x = tp.x, y = tp.y; if (sx < x || sx >= x + TPW || sy < y || sy >= y + TPH) return false;
  const float bpm = eng.tempo.bpm.load();
  if (sy >= y + 10 && sy < y + 19) {
    if (sx < x + 15) setBpm(bpm - 1); else if (sx >= x + TPW - 15) setBpm(bpm + 1);
    else { drag.kind = D_TEMPO; pointerMove(sx, sy); }
  } else if (sy >= y + 20 && sy < y + 29) {
    if (sx >= x + 4 && sx < x + 25) { taps.erase(std::remove_if(taps.begin(), taps.end(), [&](float t) { return blink - t > 3; }), taps.end()); taps.push_back(blink);
      if (taps.size() > 1) setBpm(60.f / ((taps.back() - taps.front()) / (taps.size() - 1))); else status("KEEP TAPPING", 1.5f); }
    else if (sx >= x + 29 && sx < x + 46) setBpm(bpm - 10); else if (sx >= x + 50 && sx < x + 67) setBpm(bpm + 10);
  }
  return true;
}

void Ui::pointerDown(float sx, float sy) {
  const int wx = (int)sx + viewX, wy = (int)sy + viewY;
  drag = Drag(); drag.sx = sx; drag.sy = sy; drag.wx = wx; drag.wy = wy;
  if (ask) {   // OK / CANCEL in the name box
    const int w = std::min(lastW - 10, 150), x = (lastW - w) / 2, y = TOP + 30;
    if (sy >= y + 22 && sy <= y + 32) { if (sx >= x + 3 && sx <= x + 16) keyEnter(); else if (sx >= x + 21 && sx <= x + 50) keyEscape(); }
    return;
  }
  if (selOn) { const int bw = Canvas::tw("UNPLUG") + 6; selOn = false; selT = 0.3f;   // the picked cable's UNPLUG button; any other tap just lets go of it
    if (sx >= ubx - 1 && sx < ubx + bw + 1 && sy >= uby - 1 && sy < uby + 10) { selOn = true; unplugSelected(); return; } }
  if (tp.open) { if (!tempoDown(sx, sy)) tp.open = false; return; }   // a tap away closes it
  if (br.open) { if (!browserDown(sx, sy)) closeBrowser(); return; }
  if (dd) {   // a tap picks from the open list, or closes it
    auto d0 = std::move(dd); const int r = (int)std::floor((sy - d0->y - 1) / 8), col = (int)std::floor((sx - d0->x + 2) / std::max(1, d0->colW)), i = col * d0->rows + r;
    if (sx >= d0->x - 2 && sx < d0->x - 2 + d0->w && r >= 0 && r < d0->rows && i >= 0 && i < (int)d0->items.size() && i != d0->title && d0->pick) d0->pick(i);
    return;
  }
  if (menuOpen) { for (auto& it : menuItems) if (sx >= it.x - 1 && sx < it.x + it.w && sy >= it.y - 1 && sy < it.y + 7) {
      UiMod* u = add(it.type, viewX + 120 + (int)(std::rand() % 40), viewY + TOP + 20 + (int)(std::rand() % 30)); if (u) status("ADDED " + u->m->def->name); changed(); break; }
    menuOpen = false; return; }
  if (sy < TOP) { for (auto& b : barHits) if (sx >= b.x0 - 2 && sx <= b.x1 + 2) { b.fn(); return; } return; }
  // jacks: drag a cable out of an output. A plugged input or socket: tap to pick its cable (then UNPLUG), or drag the cable to another jack.
  // Nothing comes out by accident: a cable dropped anywhere else springs back to where it was
  const JackHit j = jackAt(wx, wy);
  if (j.u) {
    drag.kind = D_CABLE;
    if (j.out) { drag.from = j; return; }
    for (int k = (int)cables.size() - 1; k >= 0; k--) { const UiCable& cb = cables[k];
      const bool mine = j.param >= 0 ? (cb.type == PType::Mod && cb.to == j.u->m->id && cb.toParam == j.param) : (cb.type != PType::Mod && cb.to == j.u->m->id && cb.toPort == j.port);
      if (mine) { drag.kind = D_PULL; drag.grab = cb; return; } }
    drag.from = j; drag.rev = true; return;
  }
  int lx, ly; UiMod* u = hit(wx, wy, lx, ly);
  // a cable under the pointer: tap to see it, pull it away to unplug it. Title bars and a module's own controls still win
  if (!u) {   // cables run behind modules, so only where you can see them
    bool onControl = false;
    if (u) { const Geom g0 = geom(*u); const ModuleDef* d0 = u->m->def;
      onControl = (!d0->fixed && ly >= g0.h - FOOT) || paramAt(u, ly) >= 0 || (d0->vis && ly >= g0.vy && ly < g0.vy + g0.vh && (d0->type == "keys" || d0->type == "out" || d0->type == "tracker" || d0->type == "sample")); }
    const int ci = onControl ? -1 : cableAt(wx, wy, u ? 2.5f : 4.f);
    if (ci >= 0) { drag.kind = D_GRAB; drag.grab = cables[ci]; return; }
  }
  if (!u) { drag.kind = D_PAN; drag.vx0 = viewX; drag.vy0 = viewY; return; }
  // bring it to the front
  for (size_t i = 0; i < mods.size(); i++) if (mods[i].get() == u) { auto p = std::move(mods[i]); mods.erase(mods.begin() + i); mods.push_back(std::move(p)); break; }
  const Geom g = geom(*u); const ModuleDef* d = u->m->def;
  if (ly < HEAD) {
    if (!d->fixed && lx >= g.w - 8) {   // delete, after asking
      Dropdown q; q.items = {"DELETE " + d->name + "?", "YES, DELETE", "NO, KEEP IT"}; q.title = 0; q.danger = 1; q.x = u->x - viewX + g.w - 40; q.y = u->y - viewY + HEAD;
      q.pick = [this, u](int i) { if (i == 1) { status("REMOVED " + u->m->def->name); remove(u); changed(); } }; openDropdown(q); return; }
    drag.kind = D_MOVE; drag.m = u; drag.ox = wx - u->x; drag.oy = wy - u->y; return; }
  if (!d->fixed && ly >= g.h - FOOT) {
    const bool onOff = d->cat == Cat::Trig || d->cat == Cat::Mod;
    if (onOff && lx >= 3 && lx < 18) { u->m->mute.store(!u->m->mute.load()); status(d->name + (u->m->mute.load() ? " OFF" : " ON")); changed(); return; }
    if (!onOff && lx >= 3 && lx < 12) { u->m->mute.store(!u->m->mute.load()); status(std::string(u->m->mute.load() ? "MUTED " : "UNMUTED ") + d->name); changed(); return; }
    if (d->cat == Cat::Voice && lx >= 14 && lx < 23) { u->m->solo.store(!u->m->solo.load());
      bool any = false; for (auto& up : mods) if (up->m->def->cat == Cat::Voice && up->m->solo.load()) any = true;
      status(u->m->solo.load() ? "SOLO " + d->name + ": OTHER VOICES SILENT, EFFECTS STILL WORKING" : any ? "UNSOLO " + d->name : "SOLO OFF: EVERY VOICE PLAYS", 3); changed(); return; }
    if (lx >= g.w - 13 && lx < g.w - 2) { u->fold = !u->fold; changed(); return; }
  }
  // the module's own display (Note Grid pads, the Output's crossfader)
  if (d->vis && ly >= g.vy && ly < g.vy + g.vh) {
    const int vx = lx - 3, vy = ly - g.vy;
    if (d->type == "keys") { keysPress(*u, vx, vy); return; }
    if (d->type == "out" && vy >= 11) { drag.kind = D_XFADE; drag.m = u; pointerMove(sx, sy); return; }
    if (d->type == "sample" && (int)u->m->p[1].load() >= 2 && vy < 6) {   // KIT / MULTI: a sound's place
      const int k = std::min(UiMod::SLOTS - 1, std::max(0, lx - 3) / 6); u->shown = k; const bool has = !u->snd[k].id.empty(), multi = (int)u->m->p[1].load() == 3;
      Dropdown z; z.x = u->x - viewX + 4; z.y = u->y - viewY + g.vy + 6;
      z.items = {"SOUND " + std::to_string(k + 1) + (has ? "  " + (u->snd[k].name.empty() ? std::string("(CAUGHT)") : u->snd[k].name.substr(0, 20)) : std::string("  (EMPTY)")), has ? "REPLACE..." : "LOAD..."};
      if (has && multi) z.items.push_back("ROOT  " + noteName((int)u->snd[k].root)); if (has) z.items.push_back("REMOVE");
      z.title = 0; const int id = u->m->id;
      z.pick = [this, id, k, has, multi](int i) { UiMod* t = byId(id); if (!t) return;
        if (i == 1) { if (onAction) onAction(t, "load:" + std::to_string(k)); return; }
        if (has && multi && i == 2) { Dropdown r; r.x = t->x - viewX + 4; r.y = TOP + 2; for (int n = 24; n <= 96; n++) r.items.push_back(noteName(n)); r.cur = (int)t->snd[k].root - 24;
          r.pick = [this, id, k](int j) { UiMod* t2 = byId(id); if (!t2) return; t2->snd[k].root = (float)(24 + j); t2->m->roots[k].store((float)(24 + j)); status("SOUND " + std::to_string(k + 1) + " ROOT " + noteName(24 + j)); changed(); }; openDropdown(r); return; }
        if (has) { eng.setBuffer(id, nullptr, k); t->snd[k] = UiMod::Snd(); status("REMOVED SOUND " + std::to_string(k + 1)); changed(); } };
      openDropdown(z); return; }
    if (d->type == "tracker") {   // tap a row to put the cursor there; drag up and down to scroll
      u->hold = 6; const int r = (int)u->scroll + std::max(0, (vy - 1) / 6);
      if (r < trLen(u->m)) { u->m->cursor.store(r); char b[32]; std::snprintf(b, 32, "CURSOR AT STEP %03d", r + 1); status(b, 1.2f); }
      drag.kind = D_SCROLL; drag.m = u; drag.n0 = u->scroll; return; }
  }
  const int pj = paramAt(u, ly);
  if (pj >= 0) {
    const ParamDef& q = d->params[pj];
    if (q.act) {
      if (d->type == "tracker") { u->hold = 6; trShow(*u, u->m->cursor.load()); }
      if (d->type == "tracker" && q.key == "clear") {   // ask first: this wipes every step
        Dropdown z; z.items = {"CLEAR EVERY STEP?", "YES, CLEAR ALL", "NO, KEEP THEM"}; z.title = 0; z.danger = 1; z.x = u->x - viewX + 8; z.y = u->y - viewY + g.py + pj * ROW + 4;
        z.pick = [this, u](int i) { if (i == 1) { eng.action(u->m->id, "clear"); u->scroll = 0; status("TRACKER CLEARED"); } }; openDropdown(z); return; }
      if (d->type == "listen" && q.key == "use") {   // set the app's key and scale to what it hears
        if (u->m->disp[2].load() <= 0) { status("NOTHING HEARD YET"); return; }
        const int r = (int)u->m->disp[0].load(), sc = (int)u->m->disp[1].load(); eng.key.root.store(r); eng.key.scale.store(sc);
        status(std::string("KEY ") + NOTE_NAMES[r % 12] + " " + scales()[sc].name + ": THE SEQUENCERS FOLLOW IT NOW", 4); changed(); return; }
      if (onAction && onAction(u, q.key)) return;
      eng.action(u->m->id, q.key); return; }
    if (!q.opts.empty()) {
      if (q.opts.size() == 2 && (q.play || q.tog || q.opts[0] == "OFF")) { const float v = u->m->p[pj].load() > 0.5f ? 0.f : 1.f; u->m->p[pj].store(v); status(d->name + "  " + q.label + "  " + q.opts[(int)v]); changed(); return; }
      Dropdown l; l.items = q.opts; l.cur = (int)std::lround(u->m->p[pj].load()); l.x = u->x - viewX + 12; l.y = u->y - viewY + g.py + pj * ROW + 4;
      l.pick = [this, u, pj](int i) { u->m->p[pj].store((float)i); const ParamDef& qq = u->m->def->params[pj]; status(u->m->def->name + "  " + qq.label + "  " + qq.opts[i]); changed(); };
      openDropdown(l); return; }
    if (lx >= minusX(*u, pj) - 2 && lx < minusX(*u, pj) + 5) { drag.kind = D_STEP; drag.m = u; drag.k = pj; drag.dir = -1; drag.holdT = 0; nudge(u, pj, -1); return; }
    if (lx >= g.w - 8) { drag.kind = D_STEP; drag.m = u; drag.k = pj; drag.dir = 1; drag.holdT = 0; nudge(u, pj, 1); return; }
    drag.kind = D_PARAM; drag.m = u; drag.k = pj; drag.n0 = q.toNorm(u->m->p[pj].load()); return;
  }
  drag.kind = D_MOVE; drag.m = u; drag.ox = wx - u->x; drag.oy = wy - u->y;
}

// - and +: one step for settings that move in steps, otherwise about a hundredth of the way along
void Ui::nudge(UiMod* u, int k, int dir) {
  const ParamDef& q = u->m->def->params[k]; const float v0 = u->m->p[k].load();
  float v = q.step > 0 ? clampf(v0 + dir * q.step, std::min(q.min, q.max), std::max(q.min, q.max)) : q.fromNorm(q.toNorm(v0) + dir * 0.01f);
  if (q.step > 0 && q.rev) v = clampf(v0 - dir * q.step, q.min, q.max);
  u->m->p[k].store(v); status(u->m->def->name + "  " + statusLabel(*u, q) + "  " + fmtParam(q, v, eng.key.root.load()), 1.5f);
}
void Ui::selectCable(const UiCable& c, float sx, float sy) {
  sel = c; selOn = true; selT = 1; ubx = (int)sx + 4; uby = (int)sy - 12; status(cableName(c), 3);
}
bool Ui::unplugSelected() {
  if (!selOn) return false; selOn = false; selT = 0;
  for (auto& cb : cables) if (sameCable(cb, sel)) { const UiCable c0 = cb; unplug(c0); status("UNPLUGGED " + cableName(c0), 2.5f); changed(); return true; }
  return false;
}
void Ui::pointerMove(float sx, float sy) {
  const int wx = (int)sx + viewX, wy = (int)sy + viewY; drag.wx = wx; drag.wy = wy;
  if (drag.kind == D_PAN) { viewX = drag.vx0 - (int)(sx - drag.sx); viewY = drag.vy0 - (int)(sy - drag.sy); }
  else if (drag.kind == D_MOVE) { drag.m->x = wx - drag.ox; drag.m->y = std::max(viewY + TOP + 1, wy - drag.oy);
    // an unpatched effect dragged over an audio cable can be dropped into it
    const ModuleDef* d = drag.m->m->def; bool free = d->inIndex("IN") >= 0 && d->outIndex("OUT") >= 0 && d->ins[d->inIndex("IN")].type == PType::Audio && d->outs[d->outIndex("OUT")].type == PType::Audio;
    for (auto& cb : cables) if (cb.from == drag.m->m->id || cb.to == drag.m->m->id) free = false;
    drag.splice = free ? cableAt(wx, wy, 5, true) : -1; if (drag.splice >= 0) status("DROP TO INSERT INTO " + cableName(cables[drag.splice]), 0.3f); }
  else if (drag.kind == D_GRAB || drag.kind == D_PULL) {
    if (std::hypot(sx - drag.sx, sy - drag.sy) > 5) {   // moved far enough to mean it: carry the cable's end (it stays plugged in until it lands)
      const UiCable c0 = drag.grab; UiMod* src = byId(c0.from);
      if (src) { drag.kind = D_CABLE; drag.from = {src, true, c0.fromPort, c0.type, outJackX(*src), src->y + jackY(c0.fromPort)}; drag.rev = false; drag.hasOrig = true; drag.orig = c0;
        status("DROP IT ON ANOTHER " + std::string(c0.type == PType::Mod ? "SETTING" : "INPUT") + " TO MOVE IT", 2); } else drag.kind = D_NONE; } }
  else if (drag.kind == D_PARAM && !drag.moved && !drag.held && std::fabs(sx - drag.sx) < 2) {}   // not moved far enough to count as a drag yet
  else if (drag.kind == D_PARAM && drag.held) {}
  else if (drag.kind == D_PARAM) { drag.moved = true; const ParamDef& q = drag.m->m->def->params[drag.k]; const float v = q.fromNorm(drag.n0 + (sx - drag.sx) / (q.step > 0 ? 40.f : 70.f));
    drag.m->m->p[drag.k].store(v); status(drag.m->m->def->name + "  " + statusLabel(*drag.m, q) + "  " + fmtParam(q, v, eng.key.root.load()), 1.5f); }
  else if (drag.kind == D_TEMPO) { const float tx = tp.x + 17.f, tw = TPW - 34.f; const float b = std::round(20 + clampf((sx - tx) / (tw - 1), 0, 1) * 220); if ((int)b != (int)std::lround(eng.tempo.bpm.load())) { eng.tempo.bpm.store(b); char t[40]; std::snprintf(t, 40, "SHARED TEMPO %d BPM", (int)b); status(t, 1.5f); } }
  else if (drag.kind == D_BROWSE) { if (std::fabs(sy - drag.sy) > 2) drag.moved = true; if (drag.moved) { br.scroll = drag.n0; browserScroll(-(sy - drag.sy) / 8); } }
  else if (drag.kind == D_SCROLL) { drag.m->hold = 6; drag.m->scroll = drag.n0 - (sy - drag.sy) / 6; trClamp(*drag.m); }
  else if (drag.kind == D_XFADE) { const Geom g = geom(*drag.m); const float x = (float)(wx - drag.m->x - 3); const float v = clampf((x - 6) / (g.w - 6 - 12), 0, 1);
    drag.m->m->p[drag.m->m->def->paramIndex("xf")].store(v); char b[32]; std::snprintf(b, 32, "CROSSFADE  A %d / B %d", (int)std::lround((1 - v) * 100), (int)std::lround(v * 100)); status(b, 1); }
}

void Ui::pointerUp(float sx, float sy) {
  const int wx = (int)sx + viewX, wy = (int)sy + viewY; const Drag d = drag; drag = Drag();
  if (d.kind == D_TEMPO) { changed(); return; }
  if (d.kind == D_BROWSE) { if (!d.moved) browserPick(d.k); return; }
  if (d.kind == D_GRAB || d.kind == D_PULL) { selectCable(d.grab, sx, sy); return; }
  if (d.kind == D_MOVE && d.splice >= 0 && d.splice < (int)cables.size()) {   // drop an effect into a cable
    const UiCable c0 = cables[d.splice]; UiMod *a = byId(c0.from), *b = byId(c0.to), *m = d.m;
    if (a && b) { unplug(c0); connect(a, c0.fromPort, m, m->m->def->inIndex("IN")); connect(m, m->m->def->outIndex("OUT"), b, c0.toPort);
      const int mi = m->m->def->paramIndex("mix"); if (mi >= 0) m->m->p[mi].store(0);   // fully dry, as the web version does, so nothing jumps out
      status("INSERTED " + m->m->def->name + (mi >= 0 ? " (FULLY DRY: RAISE ITS MIX)" : ""), 4); }
    changed(); return; }
  if (d.kind == D_PARAM && !d.moved && !d.held && (d.m->m->def->params[d.k].key == "pitch" || (d.m->m->def->type == "midiout" && d.m->m->def->params[d.k].key == "trans"))) {
    // a tap on PITCH: jump by octaves (on Sample, pitch is speed, so the speeds are named too)
    const ModuleDef* df = d.m->m->def; const int k = d.k, id = d.m->m->id; const bool smp = df->type == "sample", grains = df->type == "looper";
    static const int V[5] = {-24, -12, 0, 12, 24};
    static const char* SMP[5] = {"QUARTER SPEED  -24", "HALF SPEED  -12", "NORMAL SPEED  0", "DOUBLE SPEED  +12", "4X SPEED  +24"};
    static const char* GRN[5] = {"-24  GRAINS 2 OCT DOWN", "-12  GRAINS 1 OCT DOWN", "0  AS RECORDED", "+12  GRAINS 1 OCT UP", "+24  GRAINS 2 OCT UP"};
    static const char* NRM[5] = {"-24  2 OCTAVES DOWN", "-12  1 OCTAVE DOWN", "0", "+12  1 OCTAVE UP", "+24  2 OCTAVES UP"};
    Dropdown z; z.x = d.m->x - viewX + 30; z.y = d.m->y - viewY + geom(*d.m).py + k * ROW + 4;
    for (int i = 0; i < 5; i++) { z.items.push_back(smp ? SMP[i] : grains ? GRN[i] : NRM[i]); if ((int)std::lround(d.m->m->p[k].load()) == V[i]) z.cur = i; }
    z.pick = [this, id, k, smp](int i) { UiMod* t = byId(id); if (!t) return; t->m->p[k].store((float)V[i]);
      status(t->m->def->name + " " + t->m->def->params[k].label + " " + (smp ? std::string(SMP[i]) : std::to_string(V[i])), 2); changed(); };
    openDropdown(z); return; }
  if (d.kind == D_PARAM && !d.moved && !d.held) { const ParamDef& q = d.m->m->def->params[d.k]; status(d.m->m->def->name + "  " + statusLabel(*d.m, q) + "  " + fmtParam(q, d.m->m->p[d.k].load(), eng.key.root.load()) + "  (DRAG, OR TAP - +)", 2.5f); return; }
  if (d.kind == D_MOVE || d.kind == D_PARAM || d.kind == D_XFADE || d.kind == D_STEP) { changed(); return; }
  if (d.kind != D_CABLE) return;
  bool made = false;
  [&]() {
  if (!d.rev && d.from.type == PType::Mod) {   // a mod cable goes onto a setting
    int lx, ly; UiMod* u = hit(wx, wy, lx, ly); int pj = u ? paramAt(u, ly) : -1;
    { const JackHit sk = jackAt(wx, wy, 4); if (sk.u && sk.param >= 0) { u = sk.u; pj = sk.param; } }   // dropped on a socket
    if (u && pj >= 0 && !modulatable(u->m->def->params[pj])) { status(u->m->def->params[pj].label + " IS A CHOICE, NOT A LEVEL: IT CAN'T BE MODULATED"); return; }
    if (u && pj >= 0 && u != d.from.u) { if (modulate(d.from.u, u, pj)) { status(d.from.u->m->def->name + " NOW MOVES " + u->m->def->name + " " + u->m->def->params[pj].label); made = true; } }
    else status("DROP A MOD CABLE ON A SETTING");
    return;
  }
  JackHit j = jackAt(wx, wy); if (!j.u) j = jackAt(wx, wy, 6);   // a little forgiving: near enough to a jack counts
  if (!j.u) return;
  if (j.type != d.from.type) { status(std::string("THAT ") + (j.out ? "OUTPUT" : "INPUT") + " TAKES A DIFFERENT KIND OF CABLE"); return; }
  if (d.rev && d.from.param >= 0) { if (j.out && j.u != d.from.u && modulate(j.u, d.from.u, d.from.param)) { status(j.u->m->def->name + " NOW MOVES " + d.from.u->m->def->name + " " + d.from.u->m->def->params[d.from.param].label); made = true; } return; }
  if (!d.rev && j.param >= 0) return;
  bool ok = false;
  if (!d.rev && !j.out) ok = connect(d.from.u, d.from.port, j.u, j.port);
  else if (d.rev && j.out) ok = connect(j.u, j.port, d.from.u, d.from.port);
  made = ok;
  }();
  if (made) { if (d.hasOrig) { unplug(d.orig); status("MOVED THE CABLE", 2); } changed(); }
  else if (d.hasOrig) status("NOT MOVED. TO TAKE A CABLE OUT: TAP IT, THEN UNPLUG", 3);
}

}  // namespace tb
