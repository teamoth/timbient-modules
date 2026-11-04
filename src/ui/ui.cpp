// Timbient native: drawing the patch and handling the pointer (see ui.h)
#include "ui.h"
#include "../engine/params.h"
#include <cmath>
#include <cstdio>

namespace tb {

static const int HEAD = 8, ROW = 7, FOOT = 9, TOP = 10;
static int catCol(Cat c) { switch (c) { case Cat::Trig: return 24; case Cat::Voice: return 7; case Cat::Proc: return 17; case Cat::Mod: return 11; default: return 9; } }
static int typeCol(PType t) { return t == PType::Audio ? 43 : t == PType::Trig ? 44 : 45; }

UiMod* Ui::add(const std::string& type, int x, int y) {
  Module* m = eng.create(type, nextId++); if (!m) return nullptr;
  mods.push_back({m, x, y}); eng.add(m); return &mods.back();
}
void Ui::connect(UiMod* a, const std::string& op, UiMod* b, const std::string& ip) {
  const int fp = a->m->def->outIndex(op), tp = b->m->def->inIndex(ip); if (fp < 0 || tp < 0) return;
  Cable c; c.from = a->m->id; c.fromPort = fp; c.to = b->m->id; c.toPort = tp; c.type = a->m->def->outs[fp].type; eng.connect(c);
  cables.push_back({c.from, fp, c.to, tp, -1, c.type});
}
void Ui::modulate(UiMod* s, UiMod* d, const std::string& k) {
  const int pk = d->m->def->paramIndex(k), op = s->m->def->outIndex("MOD"); if (pk < 0 || op < 0) return;
  Cable c; c.from = s->m->id; c.fromPort = op; c.to = d->m->id; c.toParam = pk; c.type = PType::Mod; eng.connect(c);
  cables.push_back({c.from, op, c.to, -1, pk, PType::Mod});
}
void Ui::set(UiMod* m, const std::string& k, float v) { const int i = m->m->def->paramIndex(k); if (i >= 0) m->m->p[i].store(v); }

// a patch in the spirit of your session, so there's something to listen to straight away
void Ui::demoPatch() {
  mods.reserve(64);
  // columns, each module stacked under the one above
  auto col = [&](int x, std::vector<std::string> types) { std::vector<UiMod*> v; int y = 16; for (auto& t : types) { UiMod* u = add(t, x, y); v.push_back(u); y += geom(*u).h + 5; } return v; };
  auto c1 = col(6, {"clump", "orbits", "clock"}), c2 = col(68, {"pluck", "fm2"}), c3 = col(130, {"pad", "bell"}), c4 = col(192, {"echo", "filter", "lfo"}), c5 = col(254, {"mixer"}), c6 = col(316, {"reverb", "ducker"}), c7 = col(378, {"out"});
  UiMod *clump = c1[0], *orb = c1[1], *clk = c1[2], *pl = c2[0], *fm = c2[1], *pad = c3[0], *bell = c3[1], *echo = c4[0], *filt = c4[1], *lfo = c4[2], *mix = c5[0], *verb = c6[0], *duck = c6[1], *out = c7[0];
  set(clump, "oct", 5); set(clump, "mutate", 1); set(clump, "speed", 2.5f); set(pl, "tone", 0.08f); set(pad, "cut", 420); set(verb, "size", 10); set(verb, "mix", 0.5f);
  set(duck, "depth", 0.9f); set(duck, "curve", 1); set(duck, "rbeats", 3); set(clk, "bpm", 60); set(clk, "sync", 0); set(fm, "index", 2.5f); set(fm, "ratio", 3.5f); set(fm, "isus", 0);
  set(fm, "hold", 0.02f); set(fm, "rel", 0.6f); set(lfo, "rate", 20); set(pad, "air", 0.05f);
  connect(clump, "TRIG", pl, "TRIG"); connect(clump, "TRIG", fm, "TRIG"); connect(orb, "TRIG", pad, "TRIG"); connect(orb, "TRIG", bell, "TRIG");
  connect(pl, "OUT", echo, "IN"); connect(fm, "OUT", echo, "IN"); connect(echo, "OUT", mix, "1"); connect(pad, "OUT", filt, "IN"); connect(filt, "OUT", mix, "2"); connect(bell, "OUT", mix, "3");
  connect(mix, "OUT", verb, "IN"); connect(verb, "OUT", duck, "IN"); connect(duck, "OUT", out, "A"); connect(clk, "BAR", duck, "KEY");
  modulate(lfo, filt, "cut");
}

Ui::Geom Ui::geom(const UiMod& u) const {
  const ModuleDef* d = u.m->def; const int jr = (int)std::max(d->ins.size(), d->outs.size()), np = u.fold ? 0 : (int)d->params.size();
  Geom g; g.jr = jr; g.vy = HEAD + jr * ROW + (d->vis ? 1 : 0); g.py = HEAD + jr * ROW + (d->vis ? d->vis + 2 : 0); g.h = g.py + np * ROW + FOOT; g.w = d->width; return g;
}
UiMod* Ui::hit(int wx, int wy, int& lx, int& ly) {
  for (int i = (int)mods.size() - 1; i >= 0; i--) { UiMod& u = mods[i]; const Geom g = geom(u);
    if (wx >= u.x && wx < u.x + g.w && wy >= u.y && wy < u.y + g.h) { lx = wx - u.x; ly = wy - u.y; return &u; } }
  return nullptr;
}

void Ui::drawCable(Canvas& c, int ax, int ay, int bx, int by, int col) {
  const float d = std::hypot((float)(bx - ax), (float)(by - ay)), sag = std::min(30.f, d * 0.25f) + 6;
  const int steps = std::max(8, (int)(d / 2)); int px0 = ax, py0 = ay;
  for (int i = 1; i <= steps; i++) { const float t = (float)i / steps, x = ax + (bx - ax) * t, y = ay + (by - ay) * t + sag * 4 * t * (1 - t);
    const int xi = (int)std::lround(x), yi = (int)std::lround(y); c.line(px0, py0, xi, yi, col); px0 = xi; py0 = yi; }
}

void Ui::drawModule(Canvas& c, UiMod& u) {
  const ModuleDef* d = u.m->def; const Geom g = geom(u); const int x = u.x - viewX, y = u.y - viewY, w = g.w;
  if (x > c.W || y > c.H || x + w < 0 || y + g.h < 0) return;
  static uint8_t inkTbl[64]; for (int i = 0; i < 64; i++) inkTbl[i] = (uint8_t)i; inkTbl[9] = 28; inkTbl[18] = 29; inkTbl[21] = 30; inkTbl[4] = 47;
  c.rect(x + 2, y + 2, w, g.h, 26);
  c.ink = inkTbl;
  c.rect(x, y, w, g.h, 27); c.frame(x, y, w, g.h, 21);
  c.ink = nullptr; const bool fl = u.m->flash.load() > 0, mu = u.m->mute.load();
  c.rect(x, y, w, HEAD, fl ? 4 : mu ? 18 : catCol(d->cat)); c.text(d->name, x + 2, y + 2, 15); if (!d->fixed) c.text("x", x + w - 6, y + 2, 15);
  c.ink = inkTbl;
  auto jack = [&](int jx, int jy, PType t, bool conn) { const int col = typeCol(t);
    if (t == PType::Trig) { for (int k = -2; k <= 2; k++) { const int s = 2 - std::abs(k); c.px(jx + k, jy - s, col); c.px(jx + k, jy + s, col); if (conn) for (int q = -s; q <= s; q++) c.px(jx + k, jy + q, col); } }
    else { if (conn) c.rect(jx - 2, jy - 2, 5, 5, col); else c.frame(jx - 2, jy - 2, 5, 5, col); } };
  for (size_t k = 0; k < d->ins.size(); k++) { bool conn = false; for (auto& cb : cables) if (cb.to == u.m->id && cb.toPort == (int)k) conn = true;
    jack(x, y + jackY((int)k), d->ins[k].type, conn); c.text(d->ins[k].name, x + 4, y + jackY((int)k) - 2, 9); }
  for (size_t k = 0; k < d->outs.size(); k++) { bool conn = false; for (auto& cb : cables) if (cb.from == u.m->id && cb.fromPort == (int)k) conn = true;
    jack(x + w - 1, y + jackY((int)k), d->outs[k].type, conn); c.text(d->outs[k].name, x + w - 4 - Canvas::tw(d->outs[k].name), y + jackY((int)k) - 2, 9); }
  // display area (stage 1 shows the output meter; the other displays come with the full interface)
  if (d->type == "out") { const float pk = u.m->disp[0].load(); const float n = clampf((20 * std::log10(pk + 1e-6f) + 48) / 48, 0, 1); const int fw = (int)(n * (w - 6));
    for (int i = 0; i < w - 6; i += 2) c.px(x + 3 + i, y + g.vy + 3, 18); for (int i = 0; i < fw; i++) { const int col = i > (w - 6) * 0.92f ? 14 : i > (w - 6) * 0.7f ? 12 : 3; c.px(x + 3 + i, y + g.vy + 1, col); c.px(x + 3 + i, y + g.vy + 2, col); } }
  // settings
  const int root = eng.key.root.load();
  for (size_t j = 0; j < (u.fold ? 0 : d->params.size()); j++) {
    const ParamDef& q = d->params[j]; const int ry = y + g.py + (int)j * ROW; const float v = u.m->p[j].load(); const bool active = drag.kind == 3 && drag.m == &u && drag.k == (int)j;
    if (q.act) { c.text(q.label, x + 3, ry + 1, 9); c.text(">", x + w - 6, ry + 1, 9); continue; }
    if (q.play) { const bool on = v > 0.5f; c.rect(x + 3, ry, w - 6, 7, on ? 17 : 21); const int ix = x + 6, iy = ry + 1;
      if (on) c.rect(ix, iy + 1, 3, 3, 15); else for (int k = 0; k < 3; k++) for (int jj = k; jj < 5 - k; jj++) c.px(ix + k, iy + jj, 9);
      c.text(on ? "PLAYING" : "PLAY", x + 13, ry + 1, on ? 15 : 9); continue; }
    c.text(q.label.substr(0, 8), x + 3, ry + 1, active ? 4 : 18);
    if (!q.opts.empty()) { std::string s = fmtParam(q, v, root).substr(0, 6); c.text(s, x + w - 3 - Canvas::tw(s), ry + 1, active ? 4 : 9); c.px(x + w - 3 - Canvas::tw(s) - 3, ry + 3, 18); continue; }
    bool modded = false; for (auto& cb : cables) if (cb.type == PType::Mod && cb.to == u.m->id && cb.toParam == (int)j) modded = true;
    const int bx = x + 35, bw = w - 38; const float nb = clampf(q.toNorm(v), 0, 1);
    for (int i = 0; i < bw; i += 2) c.px(bx + i, ry + 4, 18);
    const int fw = (int)std::lround(nb * bw); for (int i = 0; i < fw; i++) { c.px(bx + i, ry + 4, active ? 4 : modded ? 11 : 7); if (active) c.px(bx + i, ry + 3, 4); }
    if (modded) c.px(x + 2, ry + 3, 11);
  }
  if (!d->fixed) { const int fy = y + g.h - FOOT + 1; for (int i = 3; i < w - 3; i += 2) c.px(x + i, fy - 2, 21);
    auto box = [&](int bx, bool on, const char* l, int col) { if (on) c.rect(bx, fy, 9, 7, col); else c.frame(bx, fy, 9, 7, 18); c.text(l, bx + 3, fy + 1, on ? 15 : 9); };
    box(x + 3, mu, "M", 14); }
  c.ink = nullptr;
}

void Ui::draw(Canvas& c, float dt) {
  c.rect(0, 0, c.W, c.H, 25);
  for (auto& u : mods) drawModule(c, u);
  // cables on top
  for (auto& cb : cables) {
    UiMod *a = nullptr, *b = nullptr; for (auto& u : mods) { if (u.m->id == cb.from) a = &u; if (u.m->id == cb.to) b = &u; } if (!a || !b) continue;
    const int ax = a->x + a->m->def->width - 1 - viewX, ay = a->y + jackY(cb.fromPort) - viewY;
    int bx, by; if (cb.type == PType::Mod) { const Geom g = geom(*b); bx = b->x + 2 - viewX; by = b->y + (b->fold ? g.h - FOOT : g.py + cb.toParam * ROW + 3) - viewY; } else { bx = b->x - viewX; by = b->y + jackY(cb.toPort) - viewY; }
    drawCable(c, ax, ay, bx, by, a->m->flash.load() > 0 && cb.type == PType::Trig ? 46 : typeCol(cb.type));
  }
  // top bar
  c.rect(0, 0, c.W, TOP, 15); for (int x = 0; x < c.W; x += 2) c.px(x, TOP, 21);
  c.text("TIMBIENT MODULES", 3, 3, 4); c.text("NATIVE PREVIEW", 72, 3, 18);
  char info[96]; std::snprintf(info, sizeof info, "AUDIO %d%%   %.0f HZ", (int)std::lround(eng.cpuLoad.load() * 100), eng.sr); c.text(info, 135, 3, 17);
  const std::string fs = isFullscreen ? "WINDOW" : "FULLSCREEN"; const int fx = c.W - Canvas::tw(fs) - 4; c.text(fs, fx, 3, 9); fsRect = {{fx, fx + Canvas::tw(fs)}};
  if (statusT > 0) { statusT -= dt; c.text(statusMsg, 250, 3, 17); }
  c.toRGBA();
}

void Ui::pointerDown(float sx, float sy) {
  if (sy < TOP) { if (!fsRect.empty() && sx >= fsRect[0].first - 2 && sx <= fsRect[0].second + 2) wantFullscreen = !isFullscreen; return; }
  const int wx = (int)sx + viewX, wy = (int)sy + viewY; int lx, ly; UiMod* u = hit(wx, wy, lx, ly);
  drag = Drag(); drag.sx = sx; drag.sy = sy;
  if (!u) { drag.kind = 1; drag.vx0 = viewX; drag.vy0 = viewY; return; }
  const Geom g = geom(*u);
  if (ly < HEAD) { drag.kind = 2; drag.m = u; drag.ox = wx - u->x; drag.oy = wy - u->y; return; }
  if (!u->m->def->fixed && ly >= g.h - FOOT && lx >= 3 && lx < 12) { u->m->mute.store(!u->m->mute.load()); status(std::string(u->m->mute.load() ? "MUTED " : "UNMUTED ") + u->m->def->name); return; }
  const int j = (ly - g.py) / ROW;
  if (!u->fold && ly >= g.py && j >= 0 && j < (int)u->m->def->params.size()) {
    const ParamDef& q = u->m->def->params[j];
    if (q.act) { eng.action(u->m->id, q.key); return; }
    if (!q.opts.empty()) { float v = u->m->p[j].load() + 1; if (v > q.max) v = 0; u->m->p[j].store(v); status(u->m->def->name + "  " + q.label + "  " + fmtParam(q, v, eng.key.root.load())); return; }
    drag.kind = 3; drag.m = u; drag.k = j; drag.n0 = q.toNorm(u->m->p[j].load()); return;
  }
  drag.kind = 2; drag.m = u; drag.ox = wx - u->x; drag.oy = wy - u->y;
}
void Ui::pointerMove(float sx, float sy) {
  if (drag.kind == 1) { viewX = drag.vx0 - (int)(sx - drag.sx); viewY = drag.vy0 - (int)(sy - drag.sy); }
  else if (drag.kind == 2) { drag.m->x = (int)sx + viewX - drag.ox; drag.m->y = std::max(viewY + TOP + 1, (int)sy + viewY - drag.oy); }
  else if (drag.kind == 3) { const ParamDef& q = drag.m->m->def->params[drag.k]; const float v = q.fromNorm(drag.n0 + (sx - drag.sx) / (q.step > 0 ? 40.f : 70.f));
    drag.m->m->p[drag.k].store(v); status(drag.m->m->def->name + "  " + q.label + "  " + fmtParam(q, v, eng.key.root.load()), 1.5f); }
}
void Ui::pointerUp(float, float) { drag = Drag(); }

}  // namespace tb
