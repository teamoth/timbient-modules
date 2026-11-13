// The sound editor: the whole sound, zoomable; drag START and END to choose a part. PLAY hears the part (LOOP: round
// and round, and the handles can move while it plays). REVERSE, NORMALISE, FADE IN, FADE OUT, SILENCE and DELETE change
// the part (each shows straight away, and the editor's UNDO / REDO take them back). KEEP ALL makes the whole edited sound
// the module's sound; KEEP PART keeps only the part. The file it came from is never changed. A tiny fade guards every cut
// and join against clicks.
#include "ui.h"
#include <algorithm>
#include <cmath>

namespace tb {
static const int TOP = 10, WH = 48;   // the top bar; the waveform's height
enum { B_ZOUT, B_ZIN, B_VALL, B_VPART, B_PLAY, B_LOOP, B_UNDO, B_REDO, B_REV, B_NORM, B_FIN, B_FOUT, B_SIL, B_DEL, B_KPART, B_KALL, B_CLOSE };
static const char* B_LABEL[] = {"ZOOM -", "ZOOM +", "VIEW ALL", "VIEW PART", "PLAY", "LOOP", "UNDO", "REDO", "REVERSE", "NORMALISE",
                                "FADE IN", "FADE OUT", "SILENCE", "DELETE", "KEEP PART", "KEEP ALL", "CLOSE"};
static const size_t UNDO_FLOATS = 24u << 20;   // what the editor's undo may hold (about 100 MB)

static std::string tText(double t) { char b[24]; if (t >= 60) std::snprintf(b, 24, "%d:%05.2f", (int)(t / 60), t - 60 * (int)(t / 60)); else std::snprintf(b, 24, "%.2fS", t); return b; }

static void rescan(Ui::Editor& e) {   // the drawing summary, after any change
  const size_t n = e.d.size(); e.mipLo.assign(n / 256 + 1, 0.f); e.mipHi.assign(n / 256 + 1, 0.f);
  for (size_t i = 0; i < n; i++) for (int c = 0; c < (e.r.empty() ? 1 : 2); c++) { const float v = c ? e.r[i] : e.d[i]; float& lo = e.mipLo[i / 256]; float& hi = e.mipHi[i / 256]; lo = std::min(lo, v); hi = std::max(hi, v); }
}
static double durOf(const Ui::Editor& e) { return e.d.size() / e.sr; }
static void span(const Ui::Editor& e, size_t& i0, size_t& i1) {
  const size_t n = e.d.size(); i0 = (size_t)std::max(0.0, std::min((double)n, std::round(e.a * e.sr))); i1 = (size_t)std::max((double)i0, std::min((double)n, std::round(e.b * e.sr))); }

void Ui::openEditor(int targetId, int slot, const std::string& name, std::vector<float>&& data, float sr, std::vector<float>&& right) {
  dd.reset(); menuOpen = false; tp.open = false; if (br.open) closeBrowser();
  if (ed.open && onEditPlay) onEditPlay({}, {}, ed.sr, 0, -1, false);
  ed = Editor(); ed.open = true; ed.target = targetId; ed.slot = slot; ed.name = name; ed.d = std::move(data); ed.r = std::move(right); if (ed.r.size() != ed.d.size()) ed.r.clear(); ed.sr = sr;
  rescan(ed); const double dur = durOf(ed); ed.a = 0; ed.b = dur; ed.v0 = 0; ed.v1 = dur;
}
void Ui::closeEditor(bool force) {   // closing always stops what it was playing; edits not kept need a second CLOSE
  if (!force && ed.unkept && !ed.closeAsked) { ed.closeAsked = true; status("THE EDITS AREN'T KEPT YET: CLOSE AGAIN TO DROP THEM, OR KEEP ALL", 5); return; }
  if (onEditPlay) onEditPlay({}, {}, ed.sr, 0, -1, false); ed = Editor(); }

static void edStop(Ui& u) { if (u.ed.playing && u.onEditPlay) u.onEditPlay({}, {}, u.ed.sr, 0, -1, false); u.ed.playing = false; }
static void edPlay(Ui& u) { if (!u.onEditPlay || u.ed.d.size() < 2) return; u.onEditPlay(u.ed.d, u.ed.r, u.ed.sr, u.ed.a * u.ed.sr, u.ed.b * u.ed.sr, u.ed.loop); u.ed.playing = true; }
static void edLive(Ui& u) {   // the handles moved (or LOOP changed) while it plays: it follows at once
  if (!u.ed.playing) return; u.eng.prevA.store(u.ed.a * u.ed.sr); u.eng.prevE.store(u.ed.b * u.ed.sr); u.eng.prevLoop.store(u.ed.loop); }

static void snapshot(Ui::Editor& e) {   // before a change: remember the sound, for UNDO (as much as fits)
  e.redo.clear(); const size_t one = e.d.size() + e.r.size(); if (one > UNDO_FLOATS) { e.undo.clear(); return; }
  e.undo.push_back({e.d, e.r, e.a, e.b}); size_t tot = 0; for (auto& s : e.undo) tot += s.d.size() + s.r.size();
  while (e.undo.size() > 1 && (tot > UNDO_FLOATS || e.undo.size() > 24)) { tot -= e.undo.front().d.size() + e.undo.front().r.size(); e.undo.erase(e.undo.begin()); }
}
void Ui::editorUndo(int dir) {
  auto& from = dir < 0 ? ed.undo : ed.redo; auto& to = dir < 0 ? ed.redo : ed.undo;
  if (from.empty()) { status(dir < 0 ? "NOTHING TO UNDO IN THE EDITOR" : "NOTHING TO REDO IN THE EDITOR", 1.5f); return; }
  to.push_back({std::move(ed.d), std::move(ed.r), ed.a, ed.b}); Editor::Snap s = std::move(from.back()); from.pop_back();
  ed.d = std::move(s.d); ed.r = std::move(s.r); ed.a = s.a; ed.b = s.b; rescan(ed); ed.unkept = true; ed.closeAsked = false;
  const double dur = durOf(ed); ed.v1 = std::min(ed.v1, dur); ed.v0 = std::max(0.0, std::min(ed.v0, ed.v1 - 0.01)); if (ed.v1 - ed.v0 < 0.01) { ed.v0 = 0; ed.v1 = dur; }
  if (ed.playing) edPlay(*this); status(dir < 0 ? "EDIT UNDONE" : "EDIT REDONE", 1.2f);
}

// change the part. Each joins back onto the untouched sound with a 5 ms crossfade, so nothing clicks
static bool apply(Ui& u, int op) {
  Ui::Editor& e = u.ed; size_t i0, i1; span(e, i0, i1); const size_t n = e.d.size(), len = i1 - i0;
  if (len < 16) { u.status("CHOOSE A LONGER PART", 2); return false; }
  std::vector<float>* ch[2] = {&e.d, e.r.empty() ? nullptr : &e.r};
  if (op == B_DEL) {
    if (i0 == 0 && i1 == n) { u.status("THAT'S ALL OF IT: KEEP PART OR CHOOSE LESS", 3); return false; }
    snapshot(e); const size_t xf = std::min({(size_t)(0.005 * e.sr), i0, n - i1});
    for (auto* p : ch) if (p) { std::vector<float>& v = *p; std::vector<float> o; o.reserve(n - len - xf); o.insert(o.end(), v.begin(), v.begin() + (i0 - xf));
      for (size_t k = 0; k < xf; k++) { const float t = (k + 0.5f) / xf; o.push_back(v[i0 - xf + k] * (1 - t) + v[i1 + k] * t); }
      o.insert(o.end(), v.begin() + i1 + xf, v.end()); v.swap(o); }
    const double join = (i0 - xf) / (double)e.sr, dur = durOf(e); e.a = std::max(0.0, join - 0.25); e.b = std::min(dur, join + 0.25); if (e.b - e.a < 0.02) { e.a = 0; e.b = dur; }   // the handles around the join, to hear it
    e.v1 = std::min(e.v1, dur); if (e.v1 - e.v0 < 0.01) { e.v0 = 0; e.v1 = dur; }
    return true; }
  float pk = 0; if (op == B_NORM) { for (auto* p : ch) if (p) for (size_t i = i0; i < i1; i++) pk = std::max(pk, std::fabs((*p)[i])); if (pk < 1e-5f) { u.status("THE PART IS SILENT", 2); return false; } }
  snapshot(e); const size_t g = std::min(len / 4, (size_t)(0.005 * e.sr));
  for (auto* p : ch) if (p) { std::vector<float>& v = *p; const std::vector<float> h0(v.begin() + i0, v.begin() + i0 + g), h1(v.begin() + i1 - g, v.begin() + i1);   // the edges as they were
    if (op == B_REV) std::reverse(v.begin() + i0, v.begin() + i1);
    else if (op == B_NORM) { const float k = 0.95f / pk; for (size_t i = i0; i < i1; i++) v[i] *= k; }
    else if (op == B_SIL) std::fill(v.begin() + i0, v.begin() + i1, 0.f);
    else for (size_t i = 0; i < len; i++) { const float t = (i + 0.5f) / len; v[i0 + i] *= op == B_FIN ? t : 1 - t; }   // again: steeper (t, t², t³...)
    if (i0 > 0) for (size_t k = 0; k < g; k++) { const float t = (float)k / g; v[i0 + k] = h0[k] * (1 - t) + v[i0 + k] * t; }
    if (i1 < n) for (size_t k = 0; k < g; k++) { const float t = (float)(g - k) / g; v[i1 - g + k] = h1[k] * (1 - t) + v[i1 - g + k] * t; } }
  return true;
}

double Ui::edTimeAt(float sx) const { const int wx = edX + 4, ww = edW - 8; return ed.v0 + (ed.v1 - ed.v0) * clampf((sx - wx) / std::max(1, ww - 1), 0, 1); }

void Ui::editorWheel(float sx, float wheel) {
  const double dur = durOf(ed), t = edTimeAt(sx), k = wheel > 0 ? 0.8 : 1.25; double span = std::max(0.01, std::min(dur, (ed.v1 - ed.v0) * k));
  double v0 = t - (t - ed.v0) * span / (ed.v1 - ed.v0); v0 = std::max(0.0, std::min(dur - span, v0)); ed.v0 = v0; ed.v1 = v0 + span;
}

void Ui::editorKey(int key) {
  if (key == 27) closeEditor(false);
  else if (key == ' ') { if (ed.playing) edStop(*this); else edPlay(*this); }
}

static void press(Ui& u, int id) {
  Ui::Editor& e = u.ed; const double dur = durOf(e);
  auto zoom = [&](double k) { const double c0 = (e.v0 + e.v1) / 2, sp = std::max(0.01, std::min(dur, (e.v1 - e.v0) * k)); e.v0 = std::max(0.0, std::min(dur - sp, c0 - sp / 2)); e.v1 = e.v0 + sp; };
  if (id != B_CLOSE) e.closeAsked = false;
  switch (id) {
    case B_ZOUT: zoom(1.6); break;
    case B_ZIN: zoom(1 / 1.6); break;
    case B_VALL: e.v0 = 0; e.v1 = dur; break;
    case B_VPART: { const double pad = (e.b - e.a) * 0.1; e.v0 = std::max(0.0, e.a - pad); e.v1 = std::min(dur, e.b + pad); break; }
    case B_PLAY: u.editorKey(' '); break;
    case B_LOOP: e.loop = !e.loop; edLive(u); break;
    case B_UNDO: u.editorUndo(-1); break;
    case B_REDO: u.editorUndo(1); break;
    case B_CLOSE: u.closeEditor(false); break;
    case B_KPART: case B_KALL: {
      size_t i0 = 0, i1 = e.d.size(); if (id == B_KPART) span(e, i0, i1);
      if (i1 - i0 < 64) { u.status("CHOOSE A LONGER PART", 3); return; }
      std::vector<float> L(e.d.begin() + i0, e.d.begin() + i1), R; if (!e.r.empty()) R.assign(e.r.begin() + i0, e.r.begin() + i1);
      const size_t f = std::min(L.size() / 4, (size_t)(0.003 * e.sr));   // a cut: a tiny fade where it was cut
      for (auto* p : {&L, &R}) if (!p->empty()) for (size_t k = 0; k < f; k++) { const float t = (float)k / f; if (i0 > 0) (*p)[k] *= t; if (i1 < e.d.size()) (*p)[p->size() - 1 - k] *= t; }
      std::string nm = e.name.empty() ? "EDITED" : e.name; if (!(nm.size() > 4 && (nm.substr(nm.size() - 4) == " (E)" || nm.substr(nm.size() - 4) == " (C)"))) nm += " (E)";
      if (id == B_KPART && (i0 > 0 || i1 < e.d.size())) {   // the editor now holds just the part too (its UNDO can bring the rest back)
        snapshot(e); e.d = L; e.r = R; rescan(e); e.a = 0; e.b = durOf(e); e.v0 = 0; e.v1 = e.b; if (e.playing) edPlay(u); }
      e.name = nm; e.unkept = false;
      if (u.onEditCrop) u.onEditCrop(e.target, e.slot, std::move(L), std::move(R), e.sr, nm);
      break; }
    default:
      if (apply(u, id)) { rescan(e); e.unkept = true; if (e.playing) edPlay(u); }
  }
}

void Ui::drawEditor(Canvas& c) {
  if (!ed.open) return;
  if (ed.playing && eng.previewAt.load() < 0) ed.playing = false;   // it finished
  edW = std::min(c.W - 8, 320); edX = (c.W - edW) / 2;
  const int x = edX, w = edW, wx = x + 4, ww = w - 8; const double dur = durOf(ed);
  // the buttons, in two rows (each with some on the right); a narrow window moves the right ones onto rows of their own
  auto lab = [&](int id) -> std::string { return id == B_PLAY && ed.playing ? "STOP" : B_LABEL[id]; };
  auto bw = [&](int id) { return Canvas::tw(id == B_PLAY ? "STOP" : B_LABEL[id]) + 6; };
  const std::vector<std::pair<std::vector<int>, std::vector<int>>> rows = {{{B_ZOUT, B_ZIN, B_VALL, B_VPART, B_PLAY, B_LOOP}, {B_UNDO, B_REDO}},
                                                                            {{B_REV, B_NORM, B_FIN, B_FOUT, B_SIL, B_DEL}, {B_KPART, B_KALL}}};
  edBtns.clear(); int ry0 = 0;   // rows below the START / END line (placed first, moved down after)
  for (auto& [left, right] : rows) { int bx = wx; for (int id : left) { edBtns.push_back({bx, ry0, bw(id), id}); bx += bw(id) + 3; }
    int rw = -3; for (int id : right) rw += bw(id) + 3; int rx = wx + ww - rw; if (rx < bx + 4) { ry0 += 11; rx = wx + ww - rw; }
    for (int id : right) { edBtns.push_back({rx, ry0, bw(id), id}); rx += bw(id) + 3; } ry0 += 11; }
  edH = 76 + 11 + ry0 + 10; edY = TOP + std::max(2, (c.H - TOP - edH) / 3);
  const int y = edY, h = edH, wy = y + 12;
  c.rect(x + 2, y + 2, w, h, 26); c.rect(x, y, w, h, MENU_BG); c.frame(x, y, w, h, 21);
  UiMod* t = byId(ed.target); std::string title = "EDIT SOUND"; if (t) title += " FOR " + t->m->def->name; if (!ed.name.empty()) title += ": " + ed.name;
  const int cw = Canvas::tw("CLOSE") + 6, room = (w - 12 - cw) / 4; if ((int)title.size() > room) title = title.substr(0, room - 2) + "..";
  c.text(title, x + 4, y + 4, 18);
  // the waveform at full scale (so NORMALISE shows): the chosen part bright, the rest dim
  const uint8_t* ink = c.ink; c.ink = nullptr;   // the waveform in the theme's own colours, whatever the MENUS style
  c.rect(wx, wy, ww, WH, 25);
  const double spc = (ed.v1 - ed.v0) * ed.sr / ww; const int mid = wy + WH / 2;
  for (int k = 0; k < ww; k++) { const double t0 = ed.v0 + (ed.v1 - ed.v0) * k / ww, t1 = ed.v0 + (ed.v1 - ed.v0) * (k + 1) / ww; float lo = 0, hi = 0;
    const size_t i0 = (size_t)std::max(0.0, t0 * ed.sr), i1 = std::min(ed.d.size(), (size_t)std::max(0.0, t1 * ed.sr) + 1);
    if (spc >= 512) { for (size_t q = i0 / 256; q <= std::min(ed.mipLo.size() - 1, i1 / 256); q++) { lo = std::min(lo, ed.mipLo[q]); hi = std::max(hi, ed.mipHi[q]); } }
    else for (size_t q = i0; q < i1; q++) { lo = std::min(lo, ed.d[q]); hi = std::max(hi, ed.d[q]); if (!ed.r.empty()) { lo = std::min(lo, ed.r[q]); hi = std::max(hi, ed.r[q]); } }
    lo = std::max(lo, -1.f); hi = std::min(hi, 1.f);
    const bool in = t1 > ed.a && t0 < ed.b; const int ya = mid - (int)std::lround(hi * (WH / 2 - 1)), yb = mid - (int)std::lround(lo * (WH / 2 - 1));
    if (in) c.rect(wx + k, wy, 1, WH, 26);
    for (int yy = ya; yy <= yb; yy++) c.px(wx + k, yy, in ? 48 : 21); }
  auto hx = [&](double tt) { return wx + (int)std::lround((tt - ed.v0) / (ed.v1 - ed.v0) * (ww - 1)); };
  for (int s = 0; s < 2; s++) { const int X = hx(s ? ed.b : ed.a); if (X < wx || X >= wx + ww) continue; for (int yy = wy; yy < wy + WH; yy++) c.px(X, yy, 4); c.rect(X - (s ? 3 : 0), wy, 4, 3, 4); }   // the handles
  if (ed.playing) { const int X = hx(std::max(0.f, eng.previewAt.load()) * dur); if (X >= wx && X < wx + ww) for (int yy = wy; yy < wy + WH; yy++) c.px(X, yy, 9); }
  c.ink = ink;
  // where you are: the view's ends, and a bar for the whole sound with the view on it (drag it to move along)
  const int ry = wy + WH + 2; c.rect(wx, ry, ww, 3, 21); const int vx0 = wx + (int)(ed.v0 / dur * ww), vx1 = wx + (int)std::ceil(ed.v1 / dur * ww); c.rect(vx0, ry, std::max(2, vx1 - vx0), 3, 9);
  c.text(tText(ed.v0), wx, ry + 5, 21); { const std::string e = tText(ed.v1); c.text(e, wx + ww - Canvas::tw(e), ry + 5, 21); }
  // START / END / LENGTH with - and +
  const int ty = ry + 14;
  c.text("START " + tText(ed.a), wx, ty, 9); c.text("-", wx + 62, ty, 9); c.text("+", wx + 70, ty, 9);
  c.text("END " + tText(ed.b), wx + 84, ty, 9); c.text("-", wx + 138, ty, 9); c.text("+", wx + 146, ty, 9);
  c.text("LENGTH " + tText(ed.b - ed.a), wx + 160, ty, 18);
  // the buttons: off ones framed, PLAYING and LOOP filled, KEEP in the highlight, UNDO / REDO dim with nothing to take back
  const int by = ty + 11;
  for (auto& b : edBtns) { b.y += by; const std::string l = lab(b.id);
    const bool on = (b.id == B_PLAY && ed.playing) || (b.id == B_LOOP && ed.loop), keep = b.id == B_KPART || b.id == B_KALL;
    const bool dim = (b.id == B_UNDO && ed.undo.empty()) || (b.id == B_REDO && ed.redo.empty());
    const int col = keep ? 4 : dim ? 21 : 9;
    if (on) { c.rect(b.x, b.y - 1, b.w, 9, 9); c.text(l, b.x + (b.w - Canvas::tw(l)) / 2, b.y + 1, MENU_BG); }
    else { c.frame(b.x, b.y - 1, b.w, 9, col); c.text(l, b.x + (b.w - Canvas::tw(l)) / 2, b.y + 1, col); } }
  { const std::string l = B_LABEL[B_CLOSE]; const int bx = x + w - cw - 2; edBtns.push_back({bx, y + 3, cw, B_CLOSE}); c.frame(bx, y + 2, cw, 9, 9); c.text(l, bx + 3, y + 4, 9); }
  edHintY = by + ry0; c.text("DRAG THE HANDLES, EVEN WHILE IT PLAYS. SCROLL TO ZOOM", wx, edHintY, 21);
}

bool Ui::editorDown(float sx, float sy) {
  const int x = edX, y = edY, w = edW, h = edH, wx = x + 4, wy = y + 12, ww = w - 8; const double dur = durOf(ed);
  if (sx < x || sx >= x + w || sy < y || sy >= y + h) return false;
  for (auto& b : edBtns) if (sx >= b.x && sx < b.x + b.w && sy >= b.y - 1 && sy < b.y + 8) { press(*this, b.id); return true; }
  auto hx = [&](double tt) { return wx + (ed.v1 - ed.v0 > 0 ? (float)((tt - ed.v0) / (ed.v1 - ed.v0) * (ww - 1)) : 0.f); };
  if (sy >= wy && sy < wy + WH) {   // grab the nearer handle (or move it here)
    const float da = std::fabs(sx - hx(ed.a)), db = std::fabs(sx - hx(ed.b)); drag.kind = D_EDIT; drag.k = da <= db ? 0 : 1; editorMove(sx, sy); return true; }
  const int ry = wy + WH + 2; if (sy >= ry - 1 && sy < ry + 5) { drag.kind = D_EDIT; drag.k = 2; drag.n0 = (float)ed.v0; return true; }   // the bar: move along
  const int ty = ry + 14; const double st = std::max(0.01, (ed.v1 - ed.v0) / 200);   // - and +: a small step at this zoom
  if (sy >= ty - 1 && sy < ty + 6) {
    if (sx >= wx + 61 && sx < wx + 67) ed.a = std::max(0.0, ed.a - st); else if (sx >= wx + 69 && sx < wx + 75) ed.a = std::min(ed.b - 0.01, ed.a + st);
    else if (sx >= wx + 137 && sx < wx + 143) ed.b = std::max(ed.a + 0.01, ed.b - st); else if (sx >= wx + 145 && sx < wx + 151) ed.b = std::min(dur, ed.b + st);
    edLive(*this); return true; }
  return true;
}

void Ui::editorMove(float sx, float) {
  const double dur = durOf(ed);
  if (drag.k == 2) { const int ww = edW - 8; const double span = ed.v1 - ed.v0; double v0 = drag.n0 + (sx - drag.sx) / std::max(1, ww) * dur; v0 = std::max(0.0, std::min(dur - span, v0)); ed.v0 = v0; ed.v1 = v0 + span; return; }
  const double t = std::max(0.0, std::min(dur, edTimeAt(sx)));
  if (drag.k == 0) ed.a = std::min(t, ed.b - 0.01); else ed.b = std::max(t, ed.a + 0.01);
  edLive(*this);   // playing: it follows the handles
}

}  // namespace tb
