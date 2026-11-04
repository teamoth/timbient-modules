// Timbient native: drawing the patch and handling the pointer (see ui.h)
#include "ui.h"
#include "../engine/params.h"
#include <cmath>
#include <cstdio>

namespace tb {

static const int HEAD = 8, ROW = 7, FOOT = 9, TOP = 10;
static int catCol(Cat c) { switch (c) { case Cat::Trig: return 24; case Cat::Voice: return 7; case Cat::Proc: return 17; case Cat::Mod: return 11; default: return 9; } }
static int typeCol(PType t) { return t == PType::Audio ? 43 : t == PType::Trig ? 44 : 45; }
static std::string noteName(int n) { return std::string(NOTE_NAMES[((n % 12) + 12) % 12]) + std::to_string(n / 12 - 1); }

// the ADD menu, in the web version's order (types not built yet are skipped)
static const std::vector<std::pair<std::string, std::vector<std::string>>> MENU = {
  {"RHYTHM", {"clock", "tracker", "keys", "clump", "bursts", "orbits", "hops", "walker", "tools", "midi", "midiout"}},
  {"VOICES", {"tone", "pluck", "bell", "grains", "fm2", "glass", "click", "analog", "pad", "formant", "breath", "additive", "fold", "sample"}},
  {"PROCESS", {"amp", "vca", "filter", "phaser", "echo", "reverb", "stretch", "resonator", "ducker", "looper", "tape", "crush", "mixer"}},
  {"MOD", {"lfo", "env", "rand", "follow"}}};

// ---------------- building ----------------
UiMod* Ui::add(const std::string& type, int x, int y) {
  Module* m = eng.create(type, nextId++); if (!m) return nullptr;
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
  auto c1 = col(6, {"clump", "orbits", "clock"}), c2 = col(68, {"pluck", "fm2"}), c3 = col(130, {"pad", "bell"}), c4 = col(192, {"echo", "filter", "lfo"}), c5 = col(254, {"mixer"}), c6 = col(316, {"reverb", "ducker"}), c7 = col(378, {"out"});
  UiMod *clump = c1[0], *orb = c1[1], *clk = c1[2], *pl = c2[0], *fm = c2[1], *pad = c3[0], *bell = c3[1], *echo = c4[0], *filt = c4[1], *lfo = c4[2], *mix = c5[0], *verb = c6[0], *duck = c6[1], *out = c7[0];
  set(clump, "oct", 5); set(clump, "mutate", 1); set(clump, "speed", 2.5f); set(pl, "tone", 0.08f); set(pad, "cut", 420); set(verb, "size", 10); set(verb, "mix", 0.5f);
  set(duck, "depth", 0.9f); set(duck, "curve", 1); set(duck, "rbeats", 3); set(clk, "bpm", 60); set(clk, "sync", 0); set(fm, "index", 2.5f); set(fm, "ratio", 3.5f); set(fm, "isus", 0);
  set(fm, "hold", 0.02f); set(fm, "rel", 0.6f); set(lfo, "rate", 20); set(pad, "air", 0.05f);
  connect(clump, "TRIG", pl, "TRIG"); connect(clump, "TRIG", fm, "TRIG"); connect(orb, "TRIG", pad, "TRIG"); connect(orb, "TRIG", bell, "TRIG");
  connect(pl, "OUT", echo, "IN"); connect(fm, "OUT", echo, "IN"); connect(echo, "OUT", mix, "1"); connect(pad, "OUT", filt, "IN"); connect(filt, "OUT", mix, "2"); connect(bell, "OUT", mix, "3");
  connect(mix, "OUT", verb, "IN"); connect(verb, "OUT", duck, "IN"); connect(duck, "OUT", out, "A"); connect(clk, "BAR", duck, "KEY");
  modulate(lfo, filt, filt->m->def->paramIndex("cut"));
}

// ---------------- geometry and hit testing ----------------
Ui::Geom Ui::geom(const UiMod& u) const {
  const ModuleDef* d = u.m->def; const int jr = (int)std::max(d->ins.size(), d->outs.size()), np = u.fold ? 0 : (int)d->params.size();
  Geom g; g.jr = jr; g.vy = HEAD + jr * ROW + (d->vis ? 1 : 0); g.py = HEAD + jr * ROW + (d->vis ? d->vis + 2 : 0); g.h = g.py + np * ROW + FOOT; g.w = d->width; return g;
}
UiMod* Ui::hit(int wx, int wy, int& lx, int& ly) {
  for (int i = (int)mods.size() - 1; i >= 0; i--) { UiMod& u = *mods[i]; const Geom g = geom(u);
    if (wx >= u.x && wx < u.x + g.w && wy >= u.y && wy < u.y + g.h) { lx = wx - u.x; ly = wy - u.y; return &u; } }
  return nullptr;
}
Ui::JackHit Ui::jackAt(int wx, int wy) {
  for (int i = (int)mods.size() - 1; i >= 0; i--) { UiMod& u = *mods[i]; const ModuleDef* d = u.m->def;
    for (size_t k = 0; k < d->ins.size(); k++) { const int jx = u.x, jy = u.y + jackY((int)k); if (std::abs(wx - jx) <= 3 && std::abs(wy - jy) <= 3) return {&u, false, (int)k, d->ins[k].type, jx, jy}; }
    for (size_t k = 0; k < d->outs.size(); k++) { const int jx = u.x + d->width - 1, jy = u.y + jackY((int)k); if (std::abs(wx - jx) <= 3 && std::abs(wy - jy) <= 3) return {&u, true, (int)k, d->outs[k].type, jx, jy}; } }
  return {};
}
int Ui::paramAt(UiMod* u, int ly) { const Geom g = geom(*u); const int j = (ly - g.py) / ROW; return !u->fold && ly >= g.py && j >= 0 && j < (int)u->m->def->params.size() ? j : -1; }

// ---------------- drawing ----------------
void Ui::drawCable(Canvas& c, int ax, int ay, int bx, int by, int col) {
  const float d = std::hypot((float)(bx - ax), (float)(by - ay)), sag = std::min(30.f, d * 0.25f) + 6;
  const int steps = std::max(8, (int)(d / 2)); int px0 = ax, py0 = ay;
  for (int i = 1; i <= steps; i++) { const float t = (float)i / steps, x = ax + (bx - ax) * t, y = ay + (by - ay) * t + sag * 4 * t * (1 - t);
    const int xi = (int)std::lround(x), yi = (int)std::lround(y); c.line(px0, py0, xi, yi, col); px0 = xi; py0 = yi; }
}
void Ui::drawJack(Canvas& c, int jx, int jy, PType t, bool conn, bool lit) {
  const int col = lit ? 46 : typeCol(t);
  if (t == PType::Trig) { for (int k = -2; k <= 2; k++) { const int s = 2 - std::abs(k); c.px(jx + k, jy - s, col); c.px(jx + k, jy + s, col); if (conn) for (int q = -s; q <= s; q++) c.px(jx + k, jy + q, col); } }
  else { if (conn) c.rect(jx - 2, jy - 2, 5, 5, col); else c.frame(jx - 2, jy - 2, 5, 5, col); }
}

// each module's own little display
void Ui::drawDisplay(Canvas& c, UiMod& u, int x, int y, int w, float dt) {
  Module* m = u.m; const std::string& ty = m->def->type; auto D = [&](int i) { return m->disp[i].load(std::memory_order_relaxed); };
  const int vh = m->def->vis;
  if (ty == "clump") {   // the loop as a line; each step a bar, taller when stronger, lit when it fires
    const int base = y + vh - 3;
    for (int i = 0; i < w; i += 2) c.px(x + i, base + 1, 18);
    const int n = (int)D(1); for (int k = 0; k < n && k < 16; k++) { const int sx = x + (int)std::lround(D(2 + k) * (w - 3)), h = 2 + (int)std::lround(D(18 + k) * (vh - 6)); c.rect(sx, base + 1 - h, 3, h, D(34 + k) > 0 ? 4 : 24); }
    const int hx = x + (int)std::lround(D(0) * (w - 1)); for (int j = 0; j < vh - 1; j++) if (j % 2 == 0 || j > vh - 5) c.px(hx, y + j, 17);
  } else if (ty == "orbits") {   // one ellipse per ring, a planet on each; a planet flashes as it crosses the top
    const float cx = x + w / 2.f, cy = y + vh / 2.f - 0.5f; const int n = std::max(1, (int)D(0));
    const float RX = w / 2.f - 2, RY = vh / 2.f - 2;
    c.rect((int)cx - 1, (int)cy - 1, 2, 2, 18);
    for (int i = 0; i < n && i < 8; i++) { const float f = (i + 1.f) / n, rx = 3 + (RX - 3) * f, ry = 2 + (RY - 2) * f;
      const float stepA = 1.5f / std::max(rx, 1.f);
      for (float a = 0; a < TAU; a += stepA * 2) c.pxf(cx + rx * std::cos(a), cy + ry * std::sin(a), 21);
      const bool lit = D(9 + i) > 0; const int px = (int)std::lround(cx + rx * std::cos(D(1 + i))), py = (int)std::lround(cy + ry * std::sin(D(1 + i)));
      c.rect(px - 1, py - 1, lit ? 3 : 2, lit ? 3 : 2, lit ? 4 : 24); }
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
  } else if (ty == "sample" || ty == "looper") {   // the sound's outline; the chosen region (Sample); playheads and grains
    const bool smp = ty == "sample";
    if (u.peaks.empty()) { c.text(u.missing ? "LOAD IT AGAIN" : smp ? "TAP LOAD FILE" : "LISTENING", x + 2, y + 4, u.missing ? 14 : 18); }
    else {
      const float a = smp ? m->p[3].load() : 0, b = smp ? a + m->p[4].load() * (1 - a) : 1;
      for (int i = 0; i < w; i++) { const float fx = (float)i / w; const int h = (int)std::lround(u.peaks[std::min(u.peaks.size() - 1, (size_t)(fx * u.peaks.size()))] * 5), col = fx >= a && fx <= b ? 48 : 21;
        for (int j = -h; j <= h; j++) c.px(x + i, y + 6 + j, col); }
      if (smp) { for (float xx : {a, b}) for (int j = 0; j < 12; j += 2) c.px(x + (int)std::lround(xx * (w - 1)), y + j, 24);
        const int n = (int)D(0); for (int k = 0; k < n && k < 12; k++) { const int px = x + (int)std::lround(clampf(D(1 + k), 0, 1) * (w - 1)); for (int j = 0; j < 12; j++) c.px(px, y + j, 12); } }
    }
    if (!smp) { const int hx = x + (int)std::lround(clampf(D(0), 0, 1) * (w - 1)); for (int j = 0; j < 12; j++) c.px(hx, y + j, 12);
      const int n = (int)D(1); for (int k = 0; k < n && k < 30; k++) c.px(x + (int)std::lround(clampf(D(2 + k), 0, 1) * (w - 1)), y + 6 + (int)(std::rand() % 9) - 4, 4); }
  } else if (ty == "tracker") {
    drawTracker(c, u, x, y, w, dt);
  } else if (ty == "keys") {
    const int n = (int)scales()[eng.key.scale.load()].steps.size(), root = eng.key.root.load(), sc = eng.key.scale.load();
    const int oct = (int)m->p[0].load(), rowStep = m->p[1].load() > 0.5f ? 8 : (n >= 6 ? 3 : 2);
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
static const int TR_ROWS = 14;
static int trLen(Module* m) { return std::max(1, std::min(Module::SEQ_MAX, (int)m->p[4].load())); }
static int trShown(Module* m) { int last = -1; for (int i = Module::SEQ_MAX - 1; i >= 0; i--) if (m->seq[i * 2].load() >= 0) { last = i; break; } return std::max(trLen(m), last + 1); }
static void trClamp(UiMod& u) { u.scroll = clampf(std::round(u.scroll), 0, (float)std::max(0, trShown(u.m) - TR_ROWS)); }
static void trShow(UiMod& u, int row) { if (row < u.scroll) u.scroll = (float)row; else if (row >= u.scroll + TR_ROWS) u.scroll = (float)(row - TR_ROWS + 1); trClamp(u); }
void Ui::drawTracker(Canvas& c, UiMod& u, int x, int y, int w, float dt) {
  Module* m = u.m; const int len = trLen(m), shown = trShown(m), pos = (int)m->disp[0].load(), div = std::max(1, (int)m->disp[1].load()), cur = m->cursor.load();
  u.hold = std::max(0.f, u.hold - dt);
  if (m->p[9].load() > 0.5f && u.hold <= 0 && pos >= 0 && (pos < u.scroll || pos >= u.scroll + TR_ROWS)) u.scroll = (float)pos;   // FOLLOW the playhead
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

void Ui::drawModule(Canvas& c, UiMod& u, float dt) {
  if (u.m->seq) trackerWatch(u);
  const ModuleDef* d = u.m->def; const Geom g = geom(u); const int x = u.x - viewX, y = u.y - viewY, w = g.w;
  if (x > c.W || y > c.H || x + w < 0 || y + g.h < 0) return;
  static uint8_t inkTbl[64]; static bool init = false; if (!init) { for (int i = 0; i < 64; i++) inkTbl[i] = (uint8_t)i; inkTbl[9] = 28; inkTbl[18] = 29; inkTbl[21] = 30; inkTbl[4] = 47; init = true; }
  c.rect(x + 2, y + 2, w, g.h, 26);
  c.ink = inkTbl; c.rect(x, y, w, g.h, 27); c.frame(x, y, w, g.h, 21);
  c.ink = nullptr; const bool fl = u.m->flash.load() > 0, mu = u.m->mute.load();
  c.rect(x, y, w, HEAD, fl ? 4 : mu ? 18 : catCol(d->cat)); c.text(d->name, x + 2, y + 2, 15); if (!d->fixed) c.text("x", x + w - 6, y + 2, 15);
  c.ink = inkTbl;
  for (size_t k = 0; k < d->ins.size(); k++) { bool conn = false; for (auto& cb : cables) if (cb.to == u.m->id && cb.toPort == (int)k) conn = true;
    drawJack(c, x, y + jackY((int)k), d->ins[k].type, conn, false); c.text(d->ins[k].name, x + 4, y + jackY((int)k) - 2, 9); }
  for (size_t k = 0; k < d->outs.size(); k++) { bool conn = false; for (auto& cb : cables) if (cb.from == u.m->id && cb.fromPort == (int)k) conn = true;
    drawJack(c, x + w - 1, y + jackY((int)k), d->outs[k].type, conn, fl && d->outs[k].type == PType::Trig); c.text(d->outs[k].name, x + w - 4 - Canvas::tw(d->outs[k].name), y + jackY((int)k) - 2, 9); }
  if (d->vis) drawDisplay(c, u, x + 3, y + g.vy, w - 6, dt);
  const int root = eng.key.root.load(); const bool clocked = u.m->clockedIn.load();
  for (size_t j = 0; j < (u.fold ? 0 : d->params.size()); j++) {
    const ParamDef& q = d->params[j]; const int ry = y + g.py + (int)j * ROW; const float v = u.m->p[j].load(); const bool active = drag.kind == D_PARAM && drag.m == &u && drag.k == (int)j;
    if (q.act) { const bool recOn = d->type == "out" && q.key == "rec" && u.m->disp[1].load() > 0.5f; c.text(recOn ? "STOP + SAVE" : q.label, x + 3, ry + 1, recOn ? 14 : 9); c.text(">", x + w - 6, ry + 1, 9); continue; }
    if (q.play) { const bool on = v > 0.5f; c.rect(x + 3, ry, w - 6, 7, on ? 17 : 21); const int ix = x + 6, iy = ry + 1;
      if (on) c.rect(ix, iy + 1, 3, 3, 15); else for (int k = 0; k < 3; k++) for (int jj = k; jj < 5 - k; jj++) c.px(ix + k, iy + jj, 9);
      c.text(on ? "PLAYING" : "PLAY", x + 13, ry + 1, on ? 15 : 9); continue; }
    int syncI = d->paramIndex("sync"); const bool syncOn = syncI >= 0 && d->params[syncI].tog && u.m->p[syncI].load() > 0.5f;
    const bool idle = (q.only == "free" && clocked) || (q.only == "clock" && !clocked) || (q.only == "own" && (syncOn || clocked));
    c.text(q.label.substr(0, 8), x + 3, ry + 1, active ? 4 : idle ? 21 : 18);
    bool modded = false; for (auto& cb : cables) if (cb.type == PType::Mod && cb.to == u.m->id && cb.toParam == (int)j) modded = true;
    if (modded) c.px(x + 2, ry + 3, 11);
    if (!q.opts.empty()) { std::string s = fmtParam(q, v, root).substr(0, 6); c.text(s, x + w - 3 - Canvas::tw(s), ry + 1, active ? 4 : idle ? 21 : 9); c.px(x + w - 3 - Canvas::tw(s) - 3, ry + 3, idle ? 21 : 18); continue; }
    const int bx = x + 35, bw = w - 38; const float nb = clampf(q.toNorm(v), 0, 1);
    for (int i = 0; i < bw; i += 2) c.px(bx + i, ry + 4, 18);
    const int fw = (int)std::lround(nb * bw); for (int i = 0; i < fw; i++) { c.px(bx + i, ry + 4, active ? 4 : idle ? 21 : modded ? 11 : 7); if (active) c.px(bx + i, ry + 3, 4); }
  }
  if (!d->fixed) { const int fy = y + g.h - FOOT + 1; for (int i = 3; i < w - 3; i += 2) c.px(x + i, fy - 2, 21);
    auto box = [&](int bx, bool on, const char* l, int col) { if (on) c.rect(bx, fy, 9, 7, col); else c.frame(bx, fy, 9, 7, 18); c.text(l, bx + 3, fy + 1, on ? 15 : 9); };
    box(x + 3, mu, "M", 14);
    bool hasAudioOut = false; for (auto& o : d->outs) if (o.type == PType::Audio) hasAudioOut = true;
    if (hasAudioOut) box(x + 14, u.m->solo.load(), "S", 4);
    box(x + w - 12, false, u.fold ? "+" : "-", 9);
    if (clocked) c.text("CLOCKED", x + w - 15 - Canvas::tw("CLOCKED"), fy + 1, 24); }
  c.ink = nullptr;
}

void Ui::draw(Canvas& c, float dt) {
  blink += dt;
  lastW = c.W;
  c.rect(0, 0, c.W, c.H, 25);
  for (auto& u : mods) drawModule(c, *u, dt);
  // cables on top
  for (auto& cb : cables) {
    UiMod *a = byId(cb.from), *b = byId(cb.to); if (!a || !b) continue;
    const int ax = a->x + a->m->def->width - 1 - viewX, ay = a->y + jackY(cb.fromPort) - viewY;
    int bx, by; if (cb.type == PType::Mod) { const Geom g = geom(*b); bx = b->x + 2 - viewX; by = b->y + (b->fold ? g.h - FOOT : g.py + cb.toParam * ROW + 3) - viewY; } else { bx = b->x - viewX; by = b->y + jackY(cb.toPort) - viewY; }
    drawCable(c, ax, ay, bx, by, a->m->flash.load() > 0 && cb.type == PType::Trig ? 46 : typeCol(cb.type));
  }
  // a cable being dragged, and the jacks it could go to
  if (drag.kind == D_CABLE) {
    drawCable(c, drag.from.x - viewX, drag.from.y - viewY, drag.wx - viewX, drag.wy - viewY, 4);
    for (auto& up : mods) { UiMod& u = *up; const auto& list = drag.rev ? u.m->def->outs : u.m->def->ins;
      for (size_t k = 0; k < list.size(); k++) if (list[k].type == drag.from.type && &u != drag.from.u) drawJack(c, (drag.rev ? u.x + u.m->def->width - 1 : u.x) - viewX, u.y + jackY((int)k) - viewY, list[k].type, true, true); }
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
    int w = 0; for (auto& it : dd->items) w = std::max(w, Canvas::tw(it)); w += 8; const int h = (int)dd->items.size() * 8 + 4;
    dd->x = std::max(2, std::min(dd->x, c.W - w)); dd->y = std::max(TOP + 1, std::min(dd->y, c.H - h - 1)); dd->w = w;
    c.rect(dd->x, dd->y + 2, w, h, 26); c.rect(dd->x - 2, dd->y, w, h, 15); c.frame(dd->x - 2, dd->y, w, h, 21);
    for (size_t i = 0; i < dd->items.size(); i++) { const int y = dd->y + 2 + (int)i * 8; if ((int)i == dd->cur) c.rect(dd->x - 1, y - 1, w - 2, 7, 21);
      c.text(dd->items[i], dd->x + 1, y, (int)i == dd->title ? 18 : (int)i == dd->danger ? 14 : (int)i == dd->cur ? 4 : 9); }
  }
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
  const int i = row * 8 + col, sc = eng.key.scale.load(), n = (int)scales()[sc].steps.size(), rowStep = u.m->p[1].load() > 0.5f ? 8 : (n >= 6 ? 3 : 2);
  Event ev; ev.note = midiOf(degOf((int)u.m->p[0].load(), row * rowStep + col, sc), eng.key.root.load(), sc); ev.vel = u.m->p[2].load();
  eng.trigger(u.m->id, -1, ev); u.lit[i] = 0.25f;
}

void Ui::pointerDown(float sx, float sy) {
  const int wx = (int)sx + viewX, wy = (int)sy + viewY;
  drag = Drag(); drag.sx = sx; drag.sy = sy; drag.wx = wx; drag.wy = wy;
  if (ask) {   // OK / CANCEL in the name box
    const int w = std::min(lastW - 10, 150), x = (lastW - w) / 2, y = TOP + 30;
    if (sy >= y + 22 && sy <= y + 32) { if (sx >= x + 3 && sx <= x + 16) keyEnter(); else if (sx >= x + 21 && sx <= x + 50) keyEscape(); }
    return;
  }
  if (dd) {   // a tap picks from the open list, or closes it
    auto d0 = std::move(dd); const int i = (int)((sy - d0->y - 1) / 8);
    if (sx >= d0->x - 2 && sx < d0->x - 2 + d0->w && i >= 0 && i < (int)d0->items.size() && i != d0->title && d0->pick) d0->pick(i);
    return;
  }
  if (menuOpen) { for (auto& it : menuItems) if (sx >= it.x - 1 && sx < it.x + it.w && sy >= it.y - 1 && sy < it.y + 7) {
      UiMod* u = add(it.type, viewX + 120 + (int)(std::rand() % 40), viewY + TOP + 20 + (int)(std::rand() % 30)); if (u) status("ADDED " + u->m->def->name); changed(); break; }
    menuOpen = false; return; }
  if (sy < TOP) { for (auto& b : barHits) if (sx >= b.x0 - 2 && sx <= b.x1 + 2) { b.fn(); return; } return; }
  // jacks: drag a cable out of an output; pull an existing cable off an input
  const JackHit j = jackAt(wx, wy);
  if (j.u) {
    drag.kind = D_CABLE;
    if (j.out) { drag.from = j; return; }
    for (int k = (int)cables.size() - 1; k >= 0; k--) if (cables[k].to == j.u->m->id && cables[k].toPort == j.port && cables[k].type != PType::Mod) {
      const UiCable c0 = cables[k]; unplug(c0); UiMod* src = byId(c0.from);
      drag.from = {src, true, c0.fromPort, c0.type, src->x + src->m->def->width - 1, src->y + jackY(c0.fromPort)}; status("UNPLUGGED"); changed(); return; }
    drag.from = j; drag.rev = true; return;
  }
  int lx, ly; UiMod* u = hit(wx, wy, lx, ly);
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
    if (lx >= 3 && lx < 12) { u->m->mute.store(!u->m->mute.load()); status(std::string(u->m->mute.load() ? "MUTED " : "UNMUTED ") + d->name); changed(); return; }
    if (lx >= 14 && lx < 23) { u->m->solo.store(!u->m->solo.load()); status(std::string(u->m->solo.load() ? "SOLO " : "UNSOLO ") + d->name); changed(); return; }
    if (lx >= g.w - 13 && lx < g.w - 2) { u->fold = !u->fold; changed(); return; }
  }
  // the module's own display (Note Grid pads, the Output's crossfader)
  if (d->vis && ly >= g.vy && ly < g.vy + d->vis) {
    const int vx = lx - 3, vy = ly - g.vy;
    if (d->type == "keys") { keysPress(*u, vx, vy); return; }
    if (d->type == "out" && vy >= 11) { drag.kind = D_XFADE; drag.m = u; pointerMove(sx, sy); return; }
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
      if (onAction && onAction(u, q.key)) return;
      eng.action(u->m->id, q.key); return; }
    if (!q.opts.empty()) {
      if (q.opts.size() == 2 && (q.play || q.tog || q.opts[0] == "OFF")) { const float v = u->m->p[pj].load() > 0.5f ? 0.f : 1.f; u->m->p[pj].store(v); status(d->name + "  " + q.label + "  " + q.opts[(int)v]); changed(); return; }
      Dropdown l; l.items = q.opts; l.cur = (int)std::lround(u->m->p[pj].load()); l.x = u->x - viewX + 12; l.y = u->y - viewY + g.py + pj * ROW + 4;
      l.pick = [this, u, pj](int i) { u->m->p[pj].store((float)i); const ParamDef& qq = u->m->def->params[pj]; status(u->m->def->name + "  " + qq.label + "  " + qq.opts[i]); changed(); };
      openDropdown(l); return; }
    drag.kind = D_PARAM; drag.m = u; drag.k = pj; drag.n0 = q.toNorm(u->m->p[pj].load()); return;
  }
  drag.kind = D_MOVE; drag.m = u; drag.ox = wx - u->x; drag.oy = wy - u->y;
}

void Ui::pointerMove(float sx, float sy) {
  const int wx = (int)sx + viewX, wy = (int)sy + viewY; drag.wx = wx; drag.wy = wy;
  if (drag.kind == D_PAN) { viewX = drag.vx0 - (int)(sx - drag.sx); viewY = drag.vy0 - (int)(sy - drag.sy); }
  else if (drag.kind == D_MOVE) { drag.m->x = wx - drag.ox; drag.m->y = std::max(viewY + TOP + 1, wy - drag.oy); }
  else if (drag.kind == D_PARAM) { const ParamDef& q = drag.m->m->def->params[drag.k]; const float v = q.fromNorm(drag.n0 + (sx - drag.sx) / (q.step > 0 ? 40.f : 70.f));
    drag.m->m->p[drag.k].store(v); status(drag.m->m->def->name + "  " + q.label + "  " + fmtParam(q, v, eng.key.root.load()), 1.5f); }
  else if (drag.kind == D_SCROLL) { drag.m->hold = 6; drag.m->scroll = drag.n0 - (sy - drag.sy) / 6; trClamp(*drag.m); }
  else if (drag.kind == D_XFADE) { const Geom g = geom(*drag.m); const float x = (float)(wx - drag.m->x - 3); const float v = clampf((x - 6) / (g.w - 6 - 12), 0, 1);
    drag.m->m->p[drag.m->m->def->paramIndex("xf")].store(v); char b[32]; std::snprintf(b, 32, "CROSSFADE  A %d / B %d", (int)std::lround((1 - v) * 100), (int)std::lround(v * 100)); status(b, 1); }
}

void Ui::pointerUp(float sx, float sy) {
  const int wx = (int)sx + viewX, wy = (int)sy + viewY; const Drag d = drag; drag = Drag();
  if (d.kind == D_MOVE || d.kind == D_PARAM || d.kind == D_XFADE) { changed(); return; }
  if (d.kind != D_CABLE) return;
  if (!d.rev && d.from.type == PType::Mod) {   // a mod cable goes onto a setting
    int lx, ly; UiMod* u = hit(wx, wy, lx, ly); const int pj = u ? paramAt(u, ly) : -1;
    if (u && pj >= 0 && !u->m->def->params[pj].act && u != d.from.u) { if (modulate(d.from.u, u, pj)) { status(d.from.u->m->def->name + " NOW MOVES " + u->m->def->name + " " + u->m->def->params[pj].label); changed(); } }
    else status("DROP A MOD CABLE ON A SETTING");
    return;
  }
  const JackHit j = jackAt(wx, wy); if (!j.u) return;
  if (j.type != d.from.type) { status(std::string("THAT ") + (j.out ? "OUTPUT" : "INPUT") + " TAKES A DIFFERENT KIND OF CABLE"); return; }
  bool ok = false;
  if (!d.rev && !j.out) ok = connect(d.from.u, d.from.port, j.u, j.port);
  else if (d.rev && j.out) ok = connect(j.u, j.port, d.from.u, d.from.port);
  if (ok) changed();
}

}  // namespace tb
