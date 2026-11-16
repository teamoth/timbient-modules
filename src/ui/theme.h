// Timbient native: themes. The background, module and cable colours, with the same choices and presets as the
// web version; module text flips light or dark so it stays readable on any module colour.
#pragma once
#include "canvas.h"
#include "json.h"
#include <string>
#include <deque>
#include <utility>
#include <vector>

namespace tb {

struct Theme { std::string bg = "#e4e6ea", mod = "#cdd1d8", audio = "#6a8caf", trig = "#c77d8e", modc = "#6fa58f"; };   // FOG by default: pale grey, soft blue, rose, sage
using Choice = std::pair<const char*, const char*>;
inline const std::vector<Choice>& bgBase() { static const std::vector<Choice> v = {{"POND", "pond"}, {"BLACK", "#000000"}, {"CHARCOAL", "#1a1a1d"}, {"SLATE", "#232c33"}, {"NAVY", "#111a2e"}, {"FOREST", "#13201a"}, {"PLUM", "#221624"},
  {"CLAY", "#3a2a24"}, {"MIDNIGHT", "#0b1020"}, {"EARTH", "#2a1d18"}, {"STEEL", "#5c5f66"}, {"LIGHT GREY", "#c9c9ce"}, {"PAPER", "#ebe6da"},
  {"INK", "#0d0d14"}, {"DEEP SEA", "#06222a"}, {"PINE", "#0f2a22"}, {"OXBLOOD", "#2a0f12"}, {"AUBERGINE", "#1e1028"}, {"COCOA", "#24170f"}, {"OLIVE DRAB", "#262a16"}, {"STORM", "#2c3440"},
  {"DUSK", "#2a2238"}, {"TEAL DARK", "#0f2f33"}, {"RUST", "#3a1d12"}, {"MOSS", "#2c3a22"}, {"SAND", "#d9c9a3"}, {"SAGE", "#b7c4ad"}, {"SKY", "#c4d6e3"}, {"BLUSH", "#e8d3cf"}, {"LAVENDER", "#d6cfe6"}, {"BONE", "#efe9dc"}}; return v; }
inline const std::vector<Choice>& modBase() { static const std::vector<Choice> v = {{"GREY", "#3b3b41"}, {"GRAPHITE", "#2a2a2e"}, {"BLACK", "#111114"}, {"SLATE", "#34424a"}, {"NAVY", "#1f2a44"}, {"MOSS", "#39432f"},
  {"DARK MOSS", "#0f1a10"}, {"DARK GREY", "#2b2b2b"}, {"RUST", "#4a3328"}, {"PLUM", "#3d2d42"}, {"SILVER", "#b8bac0"}, {"BONE", "#d9d3c3"},
  {"TEAL", "#1f4a4c"}, {"PINE", "#23392c"}, {"WINE", "#4a2230"}, {"INDIGO", "#2b2a55"}, {"BROWN", "#4b3a2a"}, {"OLIVE", "#4a4a2a"}, {"STORM", "#3e4a5a"}, {"MAUVE", "#5a4560"},
  {"CLAY", "#7a5040"}, {"STONE", "#8f8b82"}, {"SAGE", "#9eae95"}, {"SAND", "#c9b98f"}, {"MIST", "#c6ced6"}, {"ROSE", "#d8b7b4"}, {"LILAC", "#c5bad9"}, {"CREAM", "#e9e1cc"}}; return v; }
inline const std::vector<Choice>& cableBase() { static const std::vector<Choice> v = {   // earthy, muted colours (nothing neon)
  {"MOSS", "#8e9b6b"}, {"OCHRE", "#c39a69"}, {"CLAY", "#a8786c"}, {"TERRACOTTA", "#b86f50"}, {"SAND", "#d4bf91"}, {"SAGE", "#9aab8c"}, {"OLIVE", "#7d7f4a"}, {"RUST", "#9c5a3c"},
  {"STONE", "#a39e92"}, {"LICHEN", "#b7b58a"}, {"HEATHER", "#9c8aa0"}, {"SLATE BLUE", "#7d8ea0"}, {"DUSK BLUE", "#6f7f96"}, {"PEAT", "#6d5a46"}, {"BRACKEN", "#a4703f"}, {"FERN", "#6f8a5a"},
  {"SOFT TEAL", "#5f8f8a"}, {"DUSTY LILAC", "#8f84a8"}, {"LINEN", "#e3d9c2"}, {"BONE", "#d8cfbb"}, {"PEWTER", "#8a8a84"}, {"HONEY", "#c9a24f"}, {"BRICK", "#9a4a3a"}, {"PINE", "#4f6b52"},
  {"TEAL", "#6fb3ad"}, {"LILAC", "#b39ddb"}, {"CREAM", "#ece6cf"}, {"GOLD", "#c9b26b"}, {"PEACH", "#e09a6b"}, {"TAN", "#e3b07a"}, {"BROWN", "#8a5a3a"}, {"FOREST", "#2f5a3a"}, {"PLUM", "#6a3a6a"}, {"CHARCOAL", "#3a3a40"},
  {"PALE SAGE", "#b9c9a8"}, {"POWDER BLUE", "#a9bfd1"}, {"BLUSH", "#e0b4ac"}, {"BUTTER", "#e6d394"}, {"PALE LILAC", "#c8bde0"}, {"SEAFOAM", "#a8cfc4"}, {"APRICOT", "#efc39a"}}; return v; }
inline const std::vector<std::pair<const char*, Theme>>& presets() { static const std::vector<std::pair<const char*, Theme>> v = {
  // pale
  {"FOG", {"#e4e6ea", "#cdd1d8", "#6a8caf", "#c77d8e", "#6fa58f"}}, {"ICE", {"#e6edf2", "#cfdbe4", "#5b8fb9", "#d08c60", "#8a7fc0"}},
  {"MINT", {"#e3f1ea", "#c8e2d5", "#3f9c86", "#e08a6b", "#7b86c9"}}, {"PEACH", {"#f6e9e1", "#ecd3c4", "#d9775b", "#5a9bb0", "#a07cc0"}},
  {"LAVENDER", {"#ece8f4", "#d8d0ea", "#8a6fc4", "#4fa39a", "#d98a9e"}}, {"SKY", {"#e5f0f7", "#c9dfed", "#3d8fc4", "#e3a03f", "#c4677c"}},
  {"PORCELAIN", {"#f2f3f4", "#dfe2e6", "#4878c8", "#e0694e", "#3fa77a"}}, {"PISTACHIO", {"#eef2df", "#d9e2bb", "#7a9a3a", "#c26a8a", "#4f8fb0"}},
  // dark
  {"NAVY", {"#0f1a2e", "#1f2d47", "#6fb0e0", "#f0b45a", "#c28fd9"}}, {"INK TEAL", {"#0c2226", "#183a3f", "#5fd1c0", "#f08a6a", "#b6a2f0"}},
  {"PLUM NIGHT", {"#1d1426", "#33263f", "#d98ad0", "#7fc7d9", "#f0c070"}}, {"CHARCOAL", {"#18191c", "#2b2d32", "#59c3e8", "#f2a541", "#e86f8a"}},
  {"SLATE", {"#141414", "#262626", "#e2563b", "#e8e2d0", "#8fa6b3"}}, {"FOREST NIGHT", {"#0f1d17", "#1d3329", "#8fd17a", "#e0a86a", "#7fb0e0"}},
  {"MIDNIGHT ROSE", {"#1a1420", "#2e2236", "#ef8fa8", "#8fd3c2", "#e8c27a"}}, {"DEEP SEA", {"#08202e", "#12384a", "#4fc3d9", "#f4d35e", "#ee8a8a"}}}; return v; }
// a colour's name from its hue (for the presets' colours in the lists): GREY, RED, CORAL, AMBER ... ROSE
inline std::string hueName(const std::string& hex) {
  if (hex.size() < 7 || hex[0] != '#') return "COLOUR"; const int v = (int)std::strtol(hex.c_str() + 1, nullptr, 16);
  const float r = ((v >> 16) & 255) / 255.f, g = ((v >> 8) & 255) / 255.f, b = (v & 255) / 255.f, mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  if (mx < 0.2f) return "INK"; if (d < 0.08f || (mx > 0 && d / mx < 0.12f)) return mx > 0.75f ? "PALE GREY" : mx > 0.4f ? "GREY" : "DARK GREY";
  float h = mx == r ? 60 * std::fmod((g - b) / d, 6.f) : mx == g ? 60 * ((b - r) / d + 2) : 60 * ((r - g) / d + 4); if (h < 0) h += 360;
  static const struct { float to; const char* n; } H[] = {{15, "RED"}, {35, "CORAL"}, {48, "AMBER"}, {62, "GOLD"}, {90, "OLIVE"}, {150, "GREEN"}, {185, "TEAL"}, {210, "SKY"}, {250, "BLUE"}, {285, "VIOLET"}, {330, "ORCHID"}, {361, "ROSE"}};
  for (auto& x : H) if (h < x.to) return (mx > 0.8f && d / mx < 0.35f ? std::string("PALE ") : mx < 0.35f ? std::string("DEEP ") : std::string("")) + x.n;
  return "COLOUR";
}
// the colour lists for the THEME menu: the named colours, then each preset's own (by preset and hue: "ICE BLUE"), so a preset's colours show by name
inline std::vector<Choice> withPresets(const std::vector<Choice>& base, int role) {
  static std::deque<std::string> pool;   // the names and colours made here, kept for good (the lists hold pointers to them)
  std::vector<Choice> v = base; auto has = [&](const std::string& c) { for (auto& x : v) if (c == x.second) return true; return false; };
  auto add = [&](const std::string& n, const std::string& c) { if (has(c)) return; pool.push_back(n); const char* pn = pool.back().c_str(); pool.push_back(c); v.push_back({pn, pool.back().c_str()}); };
  for (auto& p : presets()) { const Theme& t = p.second;
    if (role == 0) add(p.first, t.bg); else if (role == 1) add(p.first, t.mod);
    else for (const std::string* c : {&t.audio, &t.trig, &t.modc}) { const std::string h = hueName(*c), n = p.first; add(n.find(h) != std::string::npos ? n : n + " " + h, *c); } }
  return v; }
inline const std::vector<Choice>& bgChoices() { static const std::vector<Choice> v = withPresets(bgBase(), 0); return v; }
inline const std::vector<Choice>& modChoices() { static const std::vector<Choice> v = withPresets(modBase(), 1); return v; }
inline const std::vector<Choice>& cableChoices() { static const std::vector<Choice> v = withPresets(cableBase(), 2); return v; }
inline bool sameTheme(const Theme& a, const Theme& b) { return a.bg == b.bg && a.mod == b.mod && a.audio == b.audio && a.trig == b.trig && a.modc == b.modc; }
inline void rgbOf(const std::string& h, float c[3]) { for (int i = 0; i < 3; i++) c[i] = h.size() >= 7 ? (float)std::strtoul(h.substr(1 + i * 2, 2).c_str(), 0, 16) : 0; }
inline std::string hexOf(const float c[3]) { char b[8]; std::snprintf(b, 8, "#%02x%02x%02x", (int)std::lround(clampf(c[0], 0, 255)), (int)std::lround(clampf(c[1], 0, 255)), (int)std::lround(clampf(c[2], 0, 255))); return b; }
inline std::string mixHex(const std::string& a, const std::string& b, float k) { float x[3], y[3], z[3]; rgbOf(a, x); rgbOf(b, y); for (int i = 0; i < 3; i++) z[i] = x[i] + (y[i] - x[i]) * k; return hexOf(z); }
inline float lum(const std::string& h) { float c[3]; rgbOf(h, c); return (0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]) / 255; }

inline float colDist(const std::string& a, const std::string& b) { float x[3], y[3]; rgbOf(a, x); rgbOf(b, y); return std::sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2])) / 441.7f; }
// RANDOMISE: a new theme from one colour. A random hue for the background and modules (pale or dark, as the theme on
// now is), and three cable colours placed round the colour wheel from it by one of the usual harmonies (side by side,
// opposite, split, a triangle or a square). Each cable has to stand out clearly from the background, the modules and the
// other two; earthy browns (dark oranges and yellows) are left out. Tries again until all of that holds.
inline std::string hslHex(float h, float s, float l) {
  h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f); const float c = (1 - std::fabs(2 * l - 1)) * s, x = c * (1 - std::fabs(std::fmod(h / 60.f, 2.f) - 1)), m = l - c / 2;
  float r = 0, g = 0, b = 0; const int k = (int)(h / 60) % 6;
  if (k == 0) { r = c; g = x; } else if (k == 1) { r = x; g = c; } else if (k == 2) { g = c; b = x; } else if (k == 3) { g = x; b = c; } else if (k == 4) { r = x; b = c; } else { r = c; b = x; }
  const float v[3] = {(r + m) * 255, (g + m) * 255, (b + m) * 255}; return hexOf(v); }
inline float contrast(const std::string& a, const std::string& b) { auto L = [](const std::string& h) { float c[3]; rgbOf(h, c); float o[3];   // WCAG's contrast ratio: 1 (the same) to 21 (black on white)
    for (int i = 0; i < 3; i++) { const float u = c[i] / 255; o[i] = u <= 0.04045f ? u / 12.92f : std::pow((u + 0.055f) / 1.055f, 2.4f); } return 0.2126f * o[0] + 0.7152f * o[1] + 0.0722f * o[2]; };
  float x = L(a), y = L(b); if (x < y) std::swap(x, y); return (x + 0.05f) / (y + 0.05f); }
inline float hueGap(float a, float b) { float d = std::fabs(std::fmod(a - b + 720.f, 360.f)); return std::min(d, 360 - d); }
inline bool brownish(float h, float l) { h = std::fmod(h + 360.f, 360.f); return h >= 12 && h <= 66 && l < 0.55f; }   // a dark orange or yellow reads as brown (or olive)
// style: 0 pale, 1 dark, 2 a dark background with pale modules, 3 a pale background with deep, rich modules
enum { TH_PALE, TH_DARK, TH_DARK_BG, TH_PALE_BG };
inline float cableNeedBg(int st) { return st == TH_PALE ? 2.4f : st == TH_DARK ? 3.5f : 2.8f; }
inline float cableNeedMod(int st) { return st == TH_PALE ? 1.8f : st == TH_DARK ? 2.6f : 1.9f; }
template <class R> Theme harmonyTheme(int style, R&& rnd) {   // rnd(n) gives 0..n-1
  auto u = [&] { return rnd(10000) / 10000.f; };
  const bool paleBg = style == TH_PALE || style == TH_PALE_BG, mixed = style >= TH_DARK_BG;
  Theme t;
  for (int tries = 0; tries < 600; tries++) {
    const float H = u() * 360;
    if (!paleBg && brownish(H, 0.2f) && tries < 590) continue;   // no brown backgrounds
    const float bs = paleBg ? 0.12f + u() * 0.25f : 0.2f + u() * 0.3f, bl = paleBg ? 0.87f + u() * 0.06f : 0.07f + u() * 0.05f;
    t.bg = hslHex(H, bs, bl);
    const float mh = H + (mixed ? (u() - 0.5f) * 60 : (u() - 0.5f) * 20);
    if (style == TH_DARK_BG) t.mod = hslHex(mh, 0.15f + u() * 0.2f, 0.82f + u() * 0.06f);   // pale modules floating on the dark
    else if (style == TH_PALE_BG) { { const float hh = std::fmod(mh + 720.f, 360.f); if (hh >= 12 && hh <= 95) continue; }   /* deep oranges, yellows and olives read as brown or khaki */ t.mod = hslHex(mh, 0.3f + u() * 0.25f, 0.16f + u() * 0.08f); }   // deep, rich modules on the pale
    else t.mod = hslHex(mh, std::min(1.f, bs + 0.05f), paleBg ? bl - 0.08f - u() * 0.06f : bl + 0.08f + u() * 0.06f);
    static const float SCHEMES[5][3] = {{0, 150, 210}, {0, 120, 240}, {0, 45, 180}, {90, 180, 270}, {0, 50, 200}};   // split, triangle, opposite, square, side by side (and one across)
    const int sc = rnd(5); float hs[3]; const float off = (u() - 0.5f) * 30;
    for (int i = 0; i < 3; i++) hs[i] = H + SCHEMES[sc][i] + off;
    for (int i = 2; i > 0; i--) std::swap(hs[i], hs[rnd(i + 1)]);   // which cable gets which
    std::string* slot[3] = {&t.audio, &t.trig, &t.modc}; bool ok = true;
    for (int i = 0; i < 3 && ok; i++) {
      float s = paleBg && !mixed ? 0.45f + u() * 0.25f : 0.5f + u() * 0.3f, l = style == TH_PALE ? 0.5f + u() * 0.1f : style == TH_DARK ? 0.6f + u() * 0.12f : 0.42f + u() * 0.16f;
      if (brownish(hs[i], 0)) { s = std::max(s, 0.72f); l = std::max(l, 0.58f); }   // an orange or a yellow: kept bright and clear, never tan or brown
      // stepped away from whichever it's too close to until it stands out from the background and the modules
      auto clear = [&](const std::string& c) { return contrast(c, t.bg) >= cableNeedBg(style) && contrast(c, t.mod) >= cableNeedMod(style); };
      for (int k = 0; k < 16 && !clear(hslHex(hs[i], s, l)); k++) {   // away from whichever it's too close to
        const std::string c = hslHex(hs[i], s, l); const bool nearBg = contrast(c, t.bg) < cableNeedBg(style);
        const float from = lum(nearBg ? t.bg : t.mod); l += (from > lum(c) ? -0.02f : 0.02f) * (style == TH_PALE ? 1.25f : 1.f); }
      *slot[i] = hslHex(hs[i], s, l);
      if (!clear(*slot[i]) || (brownish(hs[i], l + 0.06f) && l < 0.5f)) ok = false;   // still not clear, or darkened into brown
      for (int j = 0; j < i; j++) if (hueGap(hs[i], hs[j]) < 35 || colDist(*slot[i], *slot[j]) < 0.2f) ok = false; }
    if (ok && contrast(t.bg, t.mod) > (mixed ? 3.f : 1.12f)) return t;
  }
  return t;
}
// RANDOMISE: pale or dark at random (a coin toss each press), and now and then one of the mixed styles
template <class R> Theme randomTheme(R&& rnd) { const int r = rnd(100); return harmonyTheme(r < 40 ? TH_PALE : r < 80 ? TH_DARK : r < 90 ? TH_DARK_BG : TH_PALE_BG, rnd); }
inline std::string headerOf(const std::string& c) { std::string h = c; for (int k = 1; k <= 10 && lum(h) < 0.42f; k++) h = mixHex(c, "#ffffff", k * 0.08f); return h; }
// set the canvas palette from a theme (the same formulas as the web version)
// MENUS: the top bar, menus, dropdowns and panels. 0 MATCH (a shade of the background, with text to suit), 1 BLACK, 2 DARK (a dark shade of the background)
inline void applyTheme(Canvas& cv, const Theme& t, int menus = 0) {
  const bool pond = t.bg == "pond"; const std::string bg = pond ? "#07131d" : t.bg, md = t.mod; const bool lightMod = lum(md) > 0.5f;
  cv.setColour(25, bg.c_str()); cv.setColour(26, (lum(bg) > 0.4f ? mixHex(bg, "#000000", 0.25f) : mixHex(bg, "#000000", 0.6f)).c_str()); cv.setColour(27, md.c_str());
  cv.setColour(28, lightMod ? "#1b1b1f" : "#d2c7a6"); cv.setColour(29, (lightMod ? mixHex(md, "#000000", 0.55f) : mixHex(md, "#ffffff", 0.5f)).c_str());
  cv.setColour(30, (lightMod ? mixHex(md, "#000000", 0.3f) : mixHex(md, "#000000", 0.5f)).c_str());   // quiet lines and fills
  cv.setColour(31, (lightMod ? mixHex(md, "#000000", 0.45f) : mixHex(md, "#ffffff", 0.38f)).c_str());   // quiet text: away from the module colour either way (it used 30, darker even on dark modules, so it was hard to read)
  cv.setColour(43, t.audio.c_str()); cv.setColour(44, t.trig.c_str()); cv.setColour(45, t.modc.c_str());
  // the module headers (and highlights) follow the cable colours, so a theme hangs together; a dark one is lightened for them
  cv.setColour(17, headerOf(t.audio).c_str()); cv.setColour(24, headerOf(t.trig).c_str()); cv.setColour(11, headerOf(t.modc).c_str());
  cv.setColour(46, mixHex(t.trig, "#ffffff", 0.55f).c_str());
  cv.setColour(7, headerOf(mixHex(t.audio, lum(bg) > 0.45f ? "#000000" : "#ffffff", 0.18f)).c_str());   // sound makers' headers and the slider fills: from the theme too (they were a fixed brown)
  cv.setColour(47, lightMod ? "#a8560c" : "#ece6cf");   /* what's active: a deeper orange, readable on pale modules */ cv.setColour(48, (lightMod ? mixHex(t.audio, "#000000", 0.45f) : t.audio).c_str());
  { const bool paleBg = lum(bg) > 0.45f;
    cv.pal[MENU_HI] = cv.pal[4]; cv.pal[MENU_TEXT] = cv.pal[9]; cv.pal[MENU_MID] = cv.pal[18]; cv.pal[MENU_LINE] = cv.pal[21];   // light text, as on black
    if (menus == 1) cv.pal[MENU_BG] = cv.pal[15];
    else if (menus == 2) cv.setColour(MENU_BG, mixHex(bg, "#000000", paleBg ? 0.75f : 0.4f).c_str());
    else if (!paleBg) cv.setColour(MENU_BG, mixHex(bg, "#000000", 0.35f).c_str());
    else { cv.setColour(MENU_BG, mixHex(bg, "#000000", 0.06f).c_str()); cv.setColour(MENU_HI, "#1b1b1f"); cv.setColour(MENU_TEXT, mixHex(bg, "#000000", 0.68f).c_str());   // pale: dark text
      cv.setColour(MENU_MID, mixHex(bg, "#000000", 0.45f).c_str()); cv.setColour(MENU_LINE, mixHex(bg, "#000000", 0.2f).c_str()); } }
  if (pond) cv.pal[25] &= 0x00ffffffu;   // the pond shows through the background
}
inline Theme themeFrom(const Json& j) { Theme t; if (j.t != Json::OBJ) return t; t.bg = j.str("bg", t.bg); t.mod = j.str("mod", t.mod); t.audio = j.str("audio", t.audio); t.trig = j.str("trig", t.trig); t.modc = j.str("modc", t.modc); return t; }
inline Json themeJson(const Theme& t) { Json j = Json::object(); j.set("bg", Json::string(t.bg)); j.set("mod", Json::string(t.mod)); j.set("audio", Json::string(t.audio)); j.set("trig", Json::string(t.trig)); j.set("modc", Json::string(t.modc)); return j; }

}  // namespace tb
