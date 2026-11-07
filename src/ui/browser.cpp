// The sound browser: a folder's sound files, heard with a tap before they go into a Sample or Grain Looper
#include "ui.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace tb {
namespace fs = std::filesystem;
static const int TOP = 10;   // the top bar, as in ui.cpp

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

void Ui::browseTo(const std::string& dir) {
  br.items.clear(); br.sel = -1; br.scroll = 0; br.err.clear();
  if (br.playing >= 0 && onPreview) onPreview(""); br.playing = -1;
  std::error_code ec; fs::path p = fs::u8path(dir); fs::path abs = fs::absolute(p, ec); if (!ec) p = abs;
  br.dir = p.u8string();
  std::vector<Browser::Entry> folders, files;
  fs::directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
  if (ec) { br.err = "THIS FOLDER CAN'T BE READ"; return; }
  for (; it != end; it.increment(ec)) {
    if (ec) break; const std::string n = it->path().filename().u8string(); if (n.empty() || n[0] == '.') continue;
    std::error_code e2; if (it->is_directory(e2)) folders.push_back({n, true}); else if (isSound(n)) files.push_back({n, false});
  }
  auto byName = [](const Browser::Entry& a, const Browser::Entry& b) { return upper(a.name) < upper(b.name); };
  std::sort(folders.begin(), folders.end(), byName); std::sort(files.begin(), files.end(), byName);
  if (p.has_parent_path() && p.parent_path() != p) br.items.push_back({"..", true});
  br.items.insert(br.items.end(), folders.begin(), folders.end()); br.items.insert(br.items.end(), files.begin(), files.end());
  if (files.empty() && folders.empty()) br.err = "NO SOUND FILES HERE";
}

void Ui::browserScroll(float rows) {
  const float most = std::max(0.f, (float)br.items.size() - std::max(1, brRows)); br.scroll = std::max(0.f, std::min(most, br.scroll + rows));
}

void Ui::browserPick(int i) {   // a tap on a row: open a folder, or hear a sound (a second tap stops it)
  if (i < 0 || i >= (int)br.items.size()) return; const auto& e = br.items[i];
  if (e.folder) { if (e.name == "..") browseTo(std::filesystem::u8path(br.dir).parent_path().u8string()); else browseTo(joinPath(br.dir, e.name)); return; }
  br.sel = i;
  if (br.playing == i) { if (onPreview) onPreview(""); br.playing = -1; return; }
  if (onPreview) onPreview(joinPath(br.dir, e.name)); br.playing = i;
}

void Ui::browserKey(int dir) {
  if (!br.open || br.items.empty()) return; int i = br.sel;
  for (int n = 0; n < (int)br.items.size(); n++) { i = std::max(0, std::min((int)br.items.size() - 1, i + dir)); if (!br.items[i].folder) break; if (i == 0 || i == (int)br.items.size() - 1) break; }
  if (br.items[i].folder) return; br.playing = -1; browserPick(i);
  if (i < br.scroll) br.scroll = (float)i; else if (i >= br.scroll + brRows) br.scroll = (float)(i - brRows + 1);
}

void Ui::drawBrowser(Canvas& c) {
  if (!br.open) return;
  if (br.playing >= 0 && eng.previewAt.load() < 0) br.playing = -1;   // it finished
  brW = std::min(c.W - 8, 190); brH = std::min(c.H - TOP - 8, 170); brX = (c.W - brW) / 2; brY = TOP + std::max(2, (c.H - TOP - brH) / 3);
  const int x = brX, y = brY, w = brW, h = brH;
  c.rect(x + 2, y + 2, w, h, 26); c.rect(x, y, w, h, 15); c.frame(x, y, w, h, 21);
  UiMod* t = byId(br.target);
  std::string title = "SOUNDS"; if (t) title += " FOR " + t->m->def->name + (br.slot >= 0 && t->m->def->type == "sample" && t->m->p[1].load() >= 2 ? " PLACE " + std::to_string(br.slot + 1) : "");
  c.text(title, x + 4, y + 3, 18); c.text("x", x + w - 6, y + 3, 9);
  std::string d = upper(br.dir); const int room = (w - 8) / 4; if ((int)d.size() > room) d = ".." + d.substr(d.size() - room + 2);
  c.text(d, x + 4, y + 11, 9);
  // the list
  const int ly = y + 19, lh = h - 19 - 22; brRows = std::max(1, lh / 8);
  c.rect(x + 3, ly - 1, w - 6, lh + 1, 21);
  const int first = (int)br.scroll;
  for (int r = 0; r < brRows && first + r < (int)br.items.size(); r++) { const int i = first + r; const auto& e = br.items[i]; const int ry = ly + r * 8;
    if (i == br.sel) c.rect(x + 3, ry - 1, w - 6, 8, 18);
    std::string n = e.name == ".." ? "< UP A FOLDER" : (e.folder ? "> " : "") + upper(e.name); const int nroom = (w - 16) / 4; if ((int)n.size() > nroom) n = n.substr(0, nroom - 2) + "..";
    c.text(n, x + 6, ry + 1, i == br.playing ? 4 : e.folder ? 7 : i == br.sel ? 15 : 9);
    if (i == br.playing) { const float at = std::max(0.f, eng.previewAt.load()); c.rect(x + 6, ry + 6, (int)std::lround(at * (w - 14)), 1, 4); } }
  if (!br.err.empty()) c.text(br.err, x + 6, ly + (br.items.size() > 1 ? (int)br.items.size() * 8 + 2 : 1), 9);
  if ((int)br.items.size() > brRows) {   // where you are in a long list
    const int th = std::max(3, lh * brRows / (int)br.items.size()), ty = ly + (int)((lh - th) * br.scroll / std::max(1.f, (float)br.items.size() - brRows));
    c.rect(x + w - 5, ty, 1, th, 9); }
  // the buttons
  const int by = y + h - 19; auto btn = [&](int bx, int bw, const std::string& l, int col) { c.frame(bx, by - 1, bw, 9, col); c.text(l, bx + (bw - Canvas::tw(l)) / 2, by + 1, col); };
  const bool can = br.sel >= 0 && br.sel < (int)br.items.size() && !br.items[br.sel].folder;
  btn(x + 4, 35, br.playing >= 0 ? "STOP" : "PLAY", can ? 9 : 21);
  btn(x + 42, 35, "LOAD IT", can ? 4 : 21);
  btn(x + w - 72, 33, "FOLDER..", 9); btn(x + w - 37, 33, "SYSTEM..", 9);
  c.text("TAP TO HEAR IT. ARROW KEYS STEP THROUGH", x + 4, y + h - 9, 21);
}

bool Ui::browserDown(float sx, float sy) {   // true if the tap was inside the browser
  const int x = brX, y = brY, w = brW, h = brH; if (sx < x || sx >= x + w || sy < y || sy >= y + h) return false;
  if (sy < y + 10 && sx >= x + w - 9) { closeBrowser(); return true; }
  const int ly = y + 19, lh = h - 19 - 22;
  if (sy >= ly && sy < ly + lh) { drag.kind = D_BROWSE; drag.n0 = br.scroll; drag.k = (int)br.scroll + (int)((sy - ly) / 8); return true; }   // a tap picks; a drag scrolls
  const int by = y + h - 19;
  if (sy >= by - 1 && sy < by + 8) {
    const bool can = br.sel >= 0 && br.sel < (int)br.items.size() && !br.items[br.sel].folder;
    if (sx >= x + 4 && sx < x + 39 && can) { if (br.playing >= 0) { if (onPreview) onPreview(""); br.playing = -1; } else { const int s = br.sel; br.playing = -1; browserPick(s); } }
    else if (sx >= x + 42 && sx < x + 77 && can) { if (onBrowseLoad) onBrowseLoad(br.target, br.slot, joinPath(br.dir, br.items[br.sel].name)); }
    else if (sx >= x + w - 72 && sx < x + w - 39) { if (onBrowseFolder) onBrowseFolder(); }
    else if (sx >= x + w - 37 && sx < x + w - 4) { const int id = br.target, sl = br.slot; closeBrowser(); if (onBrowseFile) onBrowseFile(id, sl); }
  }
  return true;
}

}  // namespace tb
