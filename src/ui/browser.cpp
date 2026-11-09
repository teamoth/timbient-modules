// The sound browser: a folder's sound files, heard with a tap before they go into a Sample or Grain Looper
#include "ui.h"
#include <algorithm>
#include <cctype>

namespace tb {
static const int TOP = 10;   // the top bar, as in ui.cpp
static const int INFO_H = 33;  // about the picked sound, under the list

static std::string upper(std::string s) { for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch); return s; }
static bool isSound(const std::string& n) {
  const size_t d = n.rfind('.'); if (d == std::string::npos) return false; const std::string e = upper(n.substr(d + 1));
  return e == "WAV" || e == "WAVE" || e == "AIF" || e == "AIFF" || e == "FLAC" || e == "MP3";
}
static std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty()) return name; const char last = dir.back(); return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

void Ui::openBrowser(int targetId, int slot, const std::string& dir) {
  dd.reset(); menuOpen = false; tp.open = false; br.open = true; br.target = targetId; br.slot = slot; browseTo(dir);
}
void Ui::closeBrowser() { if (br.playing >= 0 && onPreview) onPreview(""); br.open = false; br.playing = -1; }

static std::string parentOf(std::string d) {   // the folder above ("/a/b/" -> "/a", "C:\\x" -> "C:\\"), or "" at the top
  while (d.size() > 1 && (d.back() == '/' || d.back() == '\\') && !(d.size() == 3 && d[1] == ':')) d.pop_back();
  const size_t s = d.find_last_of("/\\"); if (s == std::string::npos) return "";
  if (s + 1 == d.size()) return "";   // already at the top: "/" or "C:\\"
  if (s == 0) return d.substr(0, 1); if (s == 2 && d[1] == ':') return d.substr(0, 3); return d.substr(0, s);
}

void Ui::browseTo(const std::string& dir) {
  br.items.clear(); br.sel = -1; br.scroll = 0; br.err.clear();
  if (br.playing >= 0 && onPreview) onPreview(""); br.playing = -1;
  br.dir = dir;
  std::vector<Browser::Entry> all, folders, files;
  if (!onList || !onList(dir, all)) { br.err = "THIS FOLDER CAN'T BE READ"; if (!parentOf(dir).empty()) br.items.push_back({"..", true}); return; }
  for (auto& e : all) { if (e.name.empty() || e.name[0] == '.') continue; if (e.folder) folders.push_back(e); else if (isSound(e.name)) files.push_back(e); }
  auto byName = [](const Browser::Entry& a, const Browser::Entry& b) { return upper(a.name) < upper(b.name); };
  std::sort(folders.begin(), folders.end(), byName); std::sort(files.begin(), files.end(), byName);
  if (!parentOf(dir).empty()) br.items.push_back({"..", true});
  br.items.insert(br.items.end(), folders.begin(), folders.end()); br.items.insert(br.items.end(), files.begin(), files.end());
  if (files.empty() && folders.empty()) br.err = "NO SOUND FILES HERE";
}

void Ui::browserScroll(float rows) {
  const float most = std::max(0.f, (float)br.items.size() - std::max(1, brRows)); br.scroll = std::max(0.f, std::min(most, br.scroll + rows));
}

void Ui::browserPick(int i) {   // a tap on a row: open a folder, or hear a sound (a second tap stops it)
  if (i < 0 || i >= (int)br.items.size()) return; const auto& e = br.items[i];
  if (e.folder) { if (e.name == "..") browseTo(parentOf(br.dir)); else browseTo(joinPath(br.dir, e.name)); return; }
  br.sel = i;   // a tap picks it: whatever was playing stops, and with PREVIEW on, this one plays
  if (br.playing >= 0 && onPreview) onPreview(""); br.playing = -1;
  if (br.autoPreview && onPreview) { onPreview(joinPath(br.dir, e.name)); br.playing = i; }
  const std::string p = joinPath(br.dir, e.name); if (p != br.infoPath) { br.infoPath = p; br.infoReady = br.infoFailed = false; if (onInfo) onInfo(p); }
}
void Ui::browserLoad() {   // LOAD IT: stop the preview, and put the sound in the module
  if (br.sel < 0 || br.sel >= (int)br.items.size() || br.items[br.sel].folder) return;
  if (br.playing >= 0 && onPreview) onPreview(""); br.playing = -1;
  const int t = br.target, sl = br.slot; const std::string p = joinPath(br.dir, br.items[br.sel].name); closeBrowser();   // loaded: the browser closes
  if (onBrowseLoad) onBrowseLoad(t, sl, p);
}
void Ui::browserPreview() {   // the PREVIEW switch: whether tapping a sound plays it
  br.autoPreview = !br.autoPreview; if (!br.autoPreview && br.playing >= 0 && onPreview) { onPreview(""); br.playing = -1; }
  else if (br.autoPreview && br.sel >= 0 && br.sel < (int)br.items.size() && !br.items[br.sel].folder && onPreview) { onPreview(joinPath(br.dir, br.items[br.sel].name)); br.playing = br.sel; }
  if (onBrowseChanged) onBrowseChanged();
}

void Ui::browserKey(int dir) {
  if (!br.open || br.items.empty()) return; int i = br.sel;
  for (int n = 0; n < (int)br.items.size(); n++) { i = std::max(0, std::min((int)br.items.size() - 1, i + dir)); if (!br.items[i].folder) break; if (i == 0 || i == (int)br.items.size() - 1) break; }
  if (br.items[i].folder) return; browserPick(i);
  if (i < br.scroll) br.scroll = (float)i; else if (i >= br.scroll + brRows) br.scroll = (float)(i - brRows + 1);
}

void Ui::drawBrowser(Canvas& c) {
  if (!br.open) return;
  if (br.playing >= 0 && eng.previewAt.load() < 0) br.playing = -1;   // it finished
  brW = br.wide ? c.W - 8 : std::min(c.W - 8, 190); brH = std::min(c.H - TOP - 8, 204); brX = (c.W - brW) / 2; brY = TOP + std::max(2, (c.H - TOP - brH) / 3);
  const int x = brX, y = brY, w = brW, h = brH;
  c.rect(x + 2, y + 2, w, h, 26); c.rect(x, y, w, h, 15); c.frame(x, y, w, h, 21);
  UiMod* t = byId(br.target);
  std::string title = "SOUNDS"; if (t) title += " FOR " + t->m->def->name + (br.slot >= 0 && t->m->def->type == "sample" && t->m->p[1].load() >= 2 ? " PLACE " + std::to_string(br.slot + 1) : "");
  c.text(title, x + 4, y + 3, 18); c.text("x", x + w - 6, y + 3, 9); c.text(br.wide ? "><" : "<>", x + w - 18, y + 3, 9);   // <>: wider, for long names
  std::string d = upper(br.dir); const int room = (w - 8) / 4; if ((int)d.size() > room) d = ".." + d.substr(d.size() - room + 2);
  c.text(d, x + 4, y + 11, 9);
  // the list
  const int ly = y + 19, lh = h - 19 - 22 - INFO_H; brRows = std::max(1, lh / 8);
  c.rect(x + 3, ly - 1, w - 6, lh + 1, 21);
  const int first = (int)br.scroll;
  for (int r = 0; r < brRows && first + r < (int)br.items.size(); r++) { const int i = first + r; const auto& e = br.items[i]; const int ry = ly + r * 8;
    if (i == br.sel) c.rect(x + 3, ry - 1, w - 6, 8, 18);
    std::string n = e.name == ".." ? "< UP A FOLDER" : (e.folder ? "> " : "") + upper(e.name); const int nroom = (w - 16) / 4;
    if ((int)n.size() > nroom) { const int tail = std::max(8, nroom * 2 / 5); n = n.substr(0, nroom - tail - 2) + ".." + n.substr(n.size() - tail); }   // long names keep their end, where they differ
    c.text(n, x + 6, ry + 1, i == br.playing ? 4 : e.folder ? 7 : i == br.sel ? 15 : 9);
    if (i == br.playing) { const float at = std::max(0.f, eng.previewAt.load()); c.rect(x + 6, ry + 6, (int)std::lround(at * (w - 14)), 1, 4); } }
  if (!br.err.empty()) c.text(br.err, x + 6, ly + (int)br.items.size() * 8 + 3, 9);   // under the rows (and clear of UP A FOLDER)
  if ((int)br.items.size() > brRows) {   // where you are in a long list
    const int th = std::max(3, lh * brRows / (int)br.items.size()), ty = ly + (int)((lh - th) * br.scroll / std::max(1.f, (float)br.items.size() - brRows));
    c.rect(x + w - 5, ty, 1, th, 9); }
  // about the picked sound: its shape, then length / rate / channels / loudness, then key, note and tempo
  { const int iy = ly + lh + 3, iw = w - 8, ix = x + 4; const bool file = br.sel >= 0 && br.sel < (int)br.items.size() && !br.items[br.sel].folder;
    if (!file) c.text("TAP A SOUND TO SEE WHAT'S IN IT", ix, iy + 10, 21);
    else if (br.infoFailed) c.text("COULDN'T READ THIS ONE", ix, iy + 10, 14);
    else if (!br.infoReady) c.text("LISTENING TO IT...", ix, iy + 10, 18);
    else { const SoundInfo& f = br.info;
      for (int k = 0; k < iw; k++) { const float v = f.peaks.empty() ? 0 : f.peaks[std::min((int)f.peaks.size() - 1, k * (int)f.peaks.size() / iw)]; const int hh = std::max(0, (int)std::lround(v * 4));
        for (int j = -hh; j <= hh; j++) c.px(ix + k, iy + 4 + j, 48); }
      char b[96]; const int mins = (int)(br.secs / 60); const double sec = br.secs - mins * 60;
      std::string len = mins > 0 ? std::to_string(mins) + "M " + std::to_string((int)sec) + "S" : (br.secs < 10 ? (std::snprintf(b, 16, "%.1fS", br.secs), std::string(b)) : std::to_string((int)br.secs) + "S");
      std::snprintf(b, sizeof b, "%s  %.1fK %s  LOUDNESS %.0fDB", len.c_str(), br.fileRate / 1000, br.channels == 1 ? "MONO" : br.channels == 2 ? "STEREO" : (std::to_string(br.channels) + " CH").c_str(), f.rmsDb);
      c.text(b, ix, iy + 11, 9);
      std::string l3;
      if (f.noteKnown) { const int nn = (int)std::lround(f.note), cents = (int)std::lround((f.note - nn) * 100); l3 += "NOTE " + noteName(nn) + (std::abs(cents) >= 8 ? (cents > 0 ? " +" : " ") + std::to_string(cents) : ""); }
      if (f.keyKnown) { if (!l3.empty()) l3 += "  "; l3 += std::string("KEY ") + NOTE_NAMES[f.root] + " " + scales()[f.scale].name;
        const bool fits = [&] { const auto& st = scales()[eng.key.scale.load()].steps; const int r0 = eng.key.root.load();
          for (int dd : scales()[f.scale].steps) { const int pc = (f.root + dd) % 12; bool in = false; for (int q : st) if ((r0 + q) % 12 == pc) in = true; if (!in) return false; } return true; }();
        if (fits) l3 += " *"; }
      if (f.tempoKnown) { if (!l3.empty()) l3 += "  "; l3 += "ABOUT " + std::to_string((int)std::lround(f.bpm)) + " BPM"; }
      if (l3.empty()) l3 = f.peakDb < -40 ? "VERY QUIET" : "NO CLEAR NOTE, KEY OR BEAT";
      c.text(l3, ix, iy + 18, 18);
      if (f.keyKnown && f.altClose) c.text(std::string("(OR ") + NOTE_NAMES[f.altRoot] + " " + scales()[f.altScale].name + ")", ix, iy + 25, 21);
      else if (f.keyKnown) { bool fits = l3.find(" *") != std::string::npos; if (fits) c.text("* FITS THE SESSION'S KEY", ix, iy + 25, 21); } } }
  // the buttons
  const int by = y + h - 19; auto btn = [&](int bx, int bw, const std::string& l, int col) { c.frame(bx, by - 1, bw, 9, col); c.text(l, bx + (bw - Canvas::tw(l)) / 2, by + 1, col); };
  const bool can = br.sel >= 0 && br.sel < (int)br.items.size() && !br.items[br.sel].folder;
  if (br.autoPreview) { c.rect(x + 4, by - 1, 51, 9, 17); c.text("PREVIEW ON", x + 10, by + 1, 15); } else btn(x + 4, 51, "PREVIEW OFF", 9);   // a switch
  btn(x + 58, 35, "LOAD IT", can ? 4 : 21);
  c.text(br.autoPreview ? "TAP A SOUND TO HEAR IT. ENTER LOADS" : "TAP A SOUND TO PICK IT. ENTER LOADS", x + 4, y + h - 9, 21);
}

bool Ui::browserDown(float sx, float sy) {   // true if the tap was inside the browser
  const int x = brX, y = brY, w = brW, h = brH; if (sx < x || sx >= x + w || sy < y || sy >= y + h) return false;
  if (sy < y + 10 && sx >= x + w - 9) { closeBrowser(); return true; }
  if (sy < y + 10 && sx >= x + w - 21 && sx < x + w - 10) { br.wide = !br.wide; if (onBrowseChanged) onBrowseChanged(); return true; }
  const int ly = y + 19, lh = h - 19 - 22 - INFO_H;
  if (sy >= ly && sy < ly + lh) { drag.kind = D_BROWSE; drag.n0 = br.scroll; drag.k = (int)br.scroll + (int)((sy - ly) / 8); return true; }   // a tap picks; a drag scrolls
  const int by = y + h - 19;
  if (sy >= by - 1 && sy < by + 8) {
    const bool can = br.sel >= 0 && br.sel < (int)br.items.size() && !br.items[br.sel].folder;
    if (sx >= x + 4 && sx < x + 55) browserPreview();
    else if (sx >= x + 58 && sx < x + 93 && can) { browserLoad(); }
  }
  return true;
}

}  // namespace tb
