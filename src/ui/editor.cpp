// The sound editor: the whole sound, zoomable; drag START and END to choose a part; PLAY hears just that part;
// CROP keeps only that part as the module's sound (the file it came from is never changed, and sessions get smaller)
#include "ui.h"
#include <algorithm>
#include <cmath>

namespace tb {
static const int TOP = 10, WH = 48;   // the top bar; the waveform's height

static std::string tText(double t) { char b[24]; if (t >= 60) std::snprintf(b, 24, "%d:%05.2f", (int)(t / 60), t - 60 * (int)(t / 60)); else std::snprintf(b, 24, "%.2fS", t); return b; }

void Ui::openEditor(int targetId, int slot, const std::string& name, std::vector<float>&& data, float sr, std::vector<float>&& right) {
  dd.reset(); menuOpen = false; tp.open = false; if (br.open) closeBrowser();
  ed = Editor(); ed.open = true; ed.target = targetId; ed.slot = slot; ed.name = name; ed.d = std::move(data); ed.r = std::move(right); if (ed.r.size() != ed.d.size()) ed.r.clear(); ed.sr = sr;
  const size_t n = ed.d.size(); ed.mipLo.assign(n / 256 + 1, 0.f); ed.mipHi.assign(n / 256 + 1, 0.f); float pk = 1e-6f;
  for (size_t i = 0; i < n; i++) for (int c = 0; c < (ed.r.empty() ? 1 : 2); c++) { const float v = c ? ed.r[i] : ed.d[i]; float& lo = ed.mipLo[i / 256]; float& hi = ed.mipHi[i / 256]; lo = std::min(lo, v); hi = std::max(hi, v); pk = std::max(pk, std::fabs(v)); }
  ed.peak = pk; const double dur = n / sr; ed.a = 0; ed.b = dur; ed.v0 = 0; ed.v1 = dur;
}
void Ui::closeEditor() { if (onEditPlay) onEditPlay({}, {}, ed.sr); ed = Editor(); }   // closing (or CROP) always stops what it was playing

// the chosen part (left, and right for a stereo sound); for keeping, with its fades and normalising
static void piece(const Ui::Editor& e, bool forKeeping, std::vector<float>& L, std::vector<float>& R) {
  const size_t n = e.d.size(), i0 = (size_t)std::max(0.0, std::min((double)n, e.a * e.sr)), i1 = (size_t)std::max((double)i0, std::min((double)n, e.b * e.sr));
  L.assign(e.d.begin() + i0, e.d.begin() + i1); R.clear(); if (!e.r.empty()) R.assign(e.r.begin() + i0, e.r.begin() + i1); if (L.empty()) return;
  for (auto* p : {&L, &R}) if (!p->empty() && e.fades) { const size_t f = std::min(p->size() / 2, (size_t)(0.01 * e.sr)); for (size_t i = 0; i < f; i++) { const float g = (float)i / f; (*p)[i] *= g; (*p)[p->size() - 1 - i] *= g; } }   // 10 ms each end: no clicks at the cuts
  if (forKeeping && e.norm) { float pk = 1e-6f; for (float v : L) pk = std::max(pk, std::fabs(v)); for (float v : R) pk = std::max(pk, std::fabs(v)); const float g = 0.95f / pk; for (float& v : L) v *= g; for (float& v : R) v *= g; }
}

double Ui::edTimeAt(float sx) const { const int wx = edX + 4, ww = edW - 8; return ed.v0 + (ed.v1 - ed.v0) * clampf((sx - wx) / std::max(1, ww - 1), 0, 1); }

void Ui::editorWheel(float sx, float wheel) {
  const double dur = ed.d.size() / ed.sr, t = edTimeAt(sx), k = wheel > 0 ? 0.8 : 1.25; double span = std::max(0.01, std::min(dur, (ed.v1 - ed.v0) * k));
  double v0 = t - (t - ed.v0) * span / (ed.v1 - ed.v0); v0 = std::max(0.0, std::min(dur - span, v0)); ed.v0 = v0; ed.v1 = v0 + span;
}

void Ui::editorKey(int key) {
  if (key == 27) closeEditor();
  else if (key == ' ') { if (ed.playing) { if (onEditPlay) onEditPlay({}, {}, ed.sr); ed.playing = false; } else { std::vector<float> L, R; piece(ed, false, L, R); if (onEditPlay) onEditPlay(L, R, ed.sr); ed.playing = true; } }
}

void Ui::drawEditor(Canvas& c) {
  if (!ed.open) return;
  if (ed.playing && eng.previewAt.load() < 0) ed.playing = false;   // it finished
  edW = std::min(c.W - 8, 320); edH = 134; edX = (c.W - edW) / 2; edY = TOP + std::max(2, (c.H - TOP - edH) / 3);
  const int x = edX, y = edY, w = edW, h = edH, wx = x + 4, wy = y + 12, ww = w - 8; const double dur = ed.d.size() / ed.sr;
  c.rect(x + 2, y + 2, w, h, 26); c.rect(x, y, w, h, MENU_BG); c.frame(x, y, w, h, 21);
  UiMod* t = byId(ed.target); std::string title = "EDIT SOUND"; if (t) title += " FOR " + t->m->def->name; if (!ed.name.empty()) title += ": " + ed.name;
  const int room = (w - 16) / 4; if ((int)title.size() > room) title = title.substr(0, room - 2) + "..";
  c.text(title, x + 4, y + 3, 18); c.text("x", x + w - 6, y + 3, 9);
  // the waveform: the chosen part bright, the rest dim
  c.rect(wx, wy, ww, WH, 25);
  const double spc = (ed.v1 - ed.v0) * ed.sr / ww; const int mid = wy + WH / 2;
  for (int k = 0; k < ww; k++) { const double t0 = ed.v0 + (ed.v1 - ed.v0) * k / ww, t1 = ed.v0 + (ed.v1 - ed.v0) * (k + 1) / ww; float lo = 0, hi = 0;
    const size_t i0 = (size_t)std::max(0.0, t0 * ed.sr), i1 = std::min(ed.d.size(), (size_t)std::max(0.0, t1 * ed.sr) + 1);
    if (spc >= 512) { for (size_t q = i0 / 256; q <= std::min(ed.mipLo.size() - 1, i1 / 256); q++) { lo = std::min(lo, ed.mipLo[q]); hi = std::max(hi, ed.mipHi[q]); } }
    else for (size_t q = i0; q < i1; q++) { lo = std::min(lo, ed.d[q]); hi = std::max(hi, ed.d[q]); if (!ed.r.empty()) { lo = std::min(lo, ed.r[q]); hi = std::max(hi, ed.r[q]); } }
    const bool in = t1 > ed.a && t0 < ed.b; const int ya = mid - (int)std::lround(hi / ed.peak * (WH / 2 - 1)), yb = mid - (int)std::lround(lo / ed.peak * (WH / 2 - 1));
    if (in) c.rect(wx + k, wy, 1, WH, 26);
    for (int yy = ya; yy <= yb; yy++) c.px(wx + k, yy, in ? 48 : 21); }
  auto hx = [&](double tt) { return wx + (int)std::lround((tt - ed.v0) / (ed.v1 - ed.v0) * (ww - 1)); };
  for (int s = 0; s < 2; s++) { const int X = hx(s ? ed.b : ed.a); if (X < wx || X >= wx + ww) continue; for (int yy = wy; yy < wy + WH; yy++) c.px(X, yy, 4); c.rect(X - (s ? 3 : 0), wy, 4, 3, 4); }   // the handles
  if (ed.playing) { const int X = hx(ed.a + (ed.b - ed.a) * std::max(0.f, eng.previewAt.load())); if (X >= wx && X < wx + ww) for (int yy = wy; yy < wy + WH; yy++) c.px(X, yy, 9); }
  // where you are: the view's ends, and a bar for the whole sound with the view on it (drag it to move along)
  const int ry = wy + WH + 2; c.rect(wx, ry, ww, 3, 21); const int vx0 = wx + (int)(ed.v0 / dur * ww), vx1 = wx + (int)std::ceil(ed.v1 / dur * ww); c.rect(vx0, ry, std::max(2, vx1 - vx0), 3, 9);
  c.text(tText(ed.v0), wx, ry + 5, 21); { const std::string e = tText(ed.v1); c.text(e, wx + ww - Canvas::tw(e), ry + 5, 21); }
  // START / END / LENGTH with - and +
  const int ty = ry + 14;
  c.text("START " + tText(ed.a), wx, ty, 9); c.text("-", wx + 62, ty, 9); c.text("+", wx + 70, ty, 9);
  c.text("END " + tText(ed.b), wx + 84, ty, 9); c.text("-", wx + 138, ty, 9); c.text("+", wx + 146, ty, 9);
  c.text("LENGTH " + tText(ed.b - ed.a), wx + 160, ty, 18);
  // the buttons
  const int by = ty + 11; auto btn = [&](int bx, int bw, const std::string& l, bool on, int onCol) { if (on) c.rect(bx, by - 1, bw, 9, onCol); else c.frame(bx, by - 1, bw, 9, 9); c.text(l, bx + (bw - Canvas::tw(l)) / 2, by + 1, on ? 15 : 9); };
  btn(wx, 33, "ZOOM -", false, 0); btn(wx + 36, 33, "ZOOM +", false, 0); btn(wx + 72, 23, "ALL", false, 0); btn(wx + 98, 27, "PART", false, 0);
  btn(wx + 128, 27, ed.playing ? "STOP" : "PLAY", ed.playing, 17);
  btn(wx + 158, 35, "FADES", ed.fades, 17); btn(wx + 196, 43, "NORMALISE", ed.norm, 17);
  btn(wx + ww - 33, 33, "CROP", true, 4);
  c.text("DRAG THE HANDLES. SCROLL TO ZOOM. CROP KEEPS THE PART", wx, by + 11, 21);
}

bool Ui::editorDown(float sx, float sy) {
  const int x = edX, y = edY, w = edW, h = edH, wx = x + 4, wy = y + 12, ww = w - 8; const double dur = ed.d.size() / ed.sr;
  if (sx < x || sx >= x + w || sy < y || sy >= y + h) return false;
  if (sy < y + 10 && sx >= x + w - 9) { closeEditor(); return true; }
  auto hx = [&](double tt) { return wx + (ed.v1 - ed.v0 > 0 ? (float)((tt - ed.v0) / (ed.v1 - ed.v0) * (ww - 1)) : 0.f); };
  if (sy >= wy && sy < wy + WH) {   // grab the nearer handle (or move it here)
    const float da = std::fabs(sx - hx(ed.a)), db = std::fabs(sx - hx(ed.b)); drag.kind = D_EDIT; drag.k = da <= db ? 0 : 1; editorMove(sx, sy); return true; }
  const int ry = wy + WH + 2; if (sy >= ry - 1 && sy < ry + 5) { drag.kind = D_EDIT; drag.k = 2; drag.n0 = (float)ed.v0; return true; }   // the bar: move along
  const int ty = ry + 14, by = ty + 11; const double st = std::max(0.01, (ed.v1 - ed.v0) / 200);   // - and +: a small step at this zoom
  if (sy >= ty - 1 && sy < ty + 6) {
    if (sx >= wx + 61 && sx < wx + 67) ed.a = std::max(0.0, ed.a - st); else if (sx >= wx + 69 && sx < wx + 75) ed.a = std::min(ed.b - 0.01, ed.a + st);
    else if (sx >= wx + 137 && sx < wx + 143) ed.b = std::max(ed.a + 0.01, ed.b - st); else if (sx >= wx + 145 && sx < wx + 151) ed.b = std::min(dur, ed.b + st);
    return true; }
  if (sy >= by - 1 && sy < by + 8) {
    auto zoom = [&](double k) { const double c0 = (ed.v0 + ed.v1) / 2, span = std::max(0.01, std::min(dur, (ed.v1 - ed.v0) * k)); ed.v0 = std::max(0.0, std::min(dur - span, c0 - span / 2)); ed.v1 = ed.v0 + span; };
    if (sx >= wx && sx < wx + 33) zoom(1.6);
    else if (sx >= wx + 36 && sx < wx + 69) zoom(1 / 1.6);
    else if (sx >= wx + 72 && sx < wx + 95) { ed.v0 = 0; ed.v1 = dur; }
    else if (sx >= wx + 98 && sx < wx + 125) { const double pad = (ed.b - ed.a) * 0.1; ed.v0 = std::max(0.0, ed.a - pad); ed.v1 = std::min(dur, ed.b + pad); }
    else if (sx >= wx + 128 && sx < wx + 155) editorKey(' ');
    else if (sx >= wx + 158 && sx < wx + 193) ed.fades = !ed.fades;
    else if (sx >= wx + 196 && sx < wx + 239) ed.norm = !ed.norm;
    else if (sx >= wx + ww - 33) { std::vector<float> p, pr; piece(ed, true, p, pr);
      if (p.size() < 64) { status("CHOOSE A LONGER PART", 3); return true; }
      const int id = ed.target, sl = ed.slot; const float sr = ed.sr; const std::string nm = ed.name; closeEditor(); if (onEditCrop) onEditCrop(id, sl, std::move(p), std::move(pr), sr, nm); }
  }
  return true;
}

void Ui::editorMove(float sx, float) {
  const double dur = ed.d.size() / ed.sr;
  if (drag.k == 2) { const int ww = edW - 8; const double span = ed.v1 - ed.v0; double v0 = drag.n0 + (sx - drag.sx) / std::max(1, ww) * dur; v0 = std::max(0.0, std::min(dur - span, v0)); ed.v0 = v0; ed.v1 = v0 + span; return; }
  const double t = std::max(0.0, std::min(dur, edTimeAt(sx)));
  if (drag.k == 0) ed.a = std::min(t, ed.b - 0.01); else ed.b = std::max(t, ed.a + 0.01);
  if (ed.playing && onEditPlay) { onEditPlay({}, {}, ed.sr); ed.playing = false; }   // the part changed: stop hearing the old one
}

}  // namespace tb
