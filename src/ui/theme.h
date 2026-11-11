// Timbient native: themes. The background, module and cable colours, with the same choices and presets as the
// web version; module text flips light or dark so it stays readable on any module colour.
#pragma once
#include "canvas.h"
#include "json.h"
#include <string>
#include <utility>
#include <vector>

namespace tb {

struct Theme { std::string bg = "#1c1915", mod = "#3b352d", audio = "#8e9b6b", trig = "#c39a69", modc = "#a8786c"; };   // earthy by default: umber, warm stone, moss, ochre, clay
using Choice = std::pair<const char*, const char*>;
inline const std::vector<Choice>& bgChoices() { static const std::vector<Choice> v = {{"POND", "pond"}, {"BLACK", "#000000"}, {"CHARCOAL", "#1a1a1d"}, {"SLATE", "#232c33"}, {"NAVY", "#111a2e"}, {"FOREST", "#13201a"}, {"PLUM", "#221624"},
  {"CLAY", "#3a2a24"}, {"MIDNIGHT", "#0b1020"}, {"EARTH", "#2a1d18"}, {"STEEL", "#5c5f66"}, {"LIGHT GREY", "#c9c9ce"}, {"PAPER", "#ebe6da"},
  {"INK", "#0d0d14"}, {"DEEP SEA", "#06222a"}, {"PINE", "#0f2a22"}, {"OXBLOOD", "#2a0f12"}, {"AUBERGINE", "#1e1028"}, {"COCOA", "#24170f"}, {"OLIVE DRAB", "#262a16"}, {"STORM", "#2c3440"},
  {"DUSK", "#2a2238"}, {"TEAL DARK", "#0f2f33"}, {"RUST", "#3a1d12"}, {"MOSS", "#2c3a22"}, {"SAND", "#d9c9a3"}, {"SAGE", "#b7c4ad"}, {"SKY", "#c4d6e3"}, {"BLUSH", "#e8d3cf"}, {"LAVENDER", "#d6cfe6"}, {"BONE", "#efe9dc"}}; return v; }
inline const std::vector<Choice>& modChoices() { static const std::vector<Choice> v = {{"GREY", "#3b3b41"}, {"GRAPHITE", "#2a2a2e"}, {"BLACK", "#111114"}, {"SLATE", "#34424a"}, {"NAVY", "#1f2a44"}, {"MOSS", "#39432f"},
  {"DARK MOSS", "#0f1a10"}, {"DARK GREY", "#2b2b2b"}, {"RUST", "#4a3328"}, {"PLUM", "#3d2d42"}, {"SILVER", "#b8bac0"}, {"BONE", "#d9d3c3"},
  {"TEAL", "#1f4a4c"}, {"PINE", "#23392c"}, {"WINE", "#4a2230"}, {"INDIGO", "#2b2a55"}, {"BROWN", "#4b3a2a"}, {"OLIVE", "#4a4a2a"}, {"STORM", "#3e4a5a"}, {"MAUVE", "#5a4560"},
  {"CLAY", "#7a5040"}, {"STONE", "#8f8b82"}, {"SAGE", "#9eae95"}, {"SAND", "#c9b98f"}, {"MIST", "#c6ced6"}, {"ROSE", "#d8b7b4"}, {"LILAC", "#c5bad9"}, {"CREAM", "#e9e1cc"}}; return v; }
inline const std::vector<Choice>& cableChoices() { static const std::vector<Choice> v = {   // earthy, muted colours (nothing neon)
  {"MOSS", "#8e9b6b"}, {"OCHRE", "#c39a69"}, {"CLAY", "#a8786c"}, {"TERRACOTTA", "#b86f50"}, {"SAND", "#d4bf91"}, {"SAGE", "#9aab8c"}, {"OLIVE", "#7d7f4a"}, {"RUST", "#9c5a3c"},
  {"STONE", "#a39e92"}, {"LICHEN", "#b7b58a"}, {"HEATHER", "#9c8aa0"}, {"SLATE BLUE", "#7d8ea0"}, {"DUSK BLUE", "#6f7f96"}, {"PEAT", "#6d5a46"}, {"BRACKEN", "#a4703f"}, {"FERN", "#6f8a5a"},
  {"SOFT TEAL", "#5f8f8a"}, {"DUSTY LILAC", "#8f84a8"}, {"LINEN", "#e3d9c2"}, {"BONE", "#d8cfbb"}, {"PEWTER", "#8a8a84"}, {"HONEY", "#c9a24f"}, {"BRICK", "#9a4a3a"}, {"PINE", "#4f6b52"},
  {"TEAL", "#6fb3ad"}, {"LILAC", "#b39ddb"}, {"CREAM", "#ece6cf"}, {"GOLD", "#c9b26b"}, {"PEACH", "#e09a6b"}, {"TAN", "#e3b07a"}, {"BROWN", "#8a5a3a"}, {"FOREST", "#2f5a3a"}, {"PLUM", "#6a3a6a"}, {"CHARCOAL", "#3a3a40"},
  {"PALE SAGE", "#b9c9a8"}, {"POWDER BLUE", "#a9bfd1"}, {"BLUSH", "#e0b4ac"}, {"BUTTER", "#e6d394"}, {"PALE LILAC", "#c8bde0"}, {"SEAFOAM", "#a8cfc4"}, {"APRICOT", "#efc39a"}}; return v; }
inline const std::vector<std::pair<const char*, Theme>>& presets() { static const std::vector<std::pair<const char*, Theme>> v = {
  {"EARTH", Theme{}},
  {"MOSS", {"#161c14", "#323a2a", "#b7b58a", "#c39a69", "#8e9b6b"}},
  {"CLAY", {"#24180f", "#4a3328", "#d4bf91", "#b86f50", "#9aab8c"}},
  {"PEAT", {"#1a1612", "#2f2924", "#a4703f", "#9c8aa0", "#7d7f4a"}},
  {"SLATE", {"#1b1f22", "#363d42", "#9aab8c", "#c39a69", "#7d8ea0"}},
  {"HEATH", {"#1e1820", "#3a3038", "#b7b58a", "#9c8aa0", "#a8786c"}},
  {"SANDSTONE", {"#d8ccb2", "#bcae90", "#4f6b52", "#9c5a3c", "#6f7f96"}},
  {"PAPER", {"#ebe6da", "#d9d3c3", "#4f6b52", "#8f84a8", "#9a4a3a"}},
  {"NIGHT", {"#11141b", "#272c36", "#7d8ea0", "#c39a69", "#9aab8c"}},
  {"MONO", {"#141414", "#2e2e2e", "#d8cfbb", "#8a8a84", "#a39e92"}},
  // light and pastel, still earthy
  {"CHALK", {"#ece6da", "#d9cfbc", "#6f7f4a", "#a4703f", "#9a4a3a"}},
  {"MEADOW", {"#e1e6d6", "#c8d1b9", "#4f6b52", "#b86f50", "#7d6f96"}},
  {"SHELL", {"#f0e4da", "#e2ccbe", "#6f7f96", "#a8786c", "#6f8a5a"}},
  {"DUNE", {"#e8d8b6", "#d2bd93", "#6d5a46", "#9c5a3c", "#4f6b52"}},
  {"MIST", {"#e0e4e3", "#c6cdcc", "#5f8f8a", "#a4703f", "#8f84a8"}},
  {"BLOSSOM", {"#efe2e4", "#dcc6cb", "#6f8a5a", "#9c8aa0", "#b86f50"}}}; return v; }
inline bool sameTheme(const Theme& a, const Theme& b) { return a.bg == b.bg && a.mod == b.mod && a.audio == b.audio && a.trig == b.trig && a.modc == b.modc; }

inline void rgbOf(const std::string& h, float c[3]) { for (int i = 0; i < 3; i++) c[i] = h.size() >= 7 ? (float)std::strtoul(h.substr(1 + i * 2, 2).c_str(), 0, 16) : 0; }
inline std::string hexOf(const float c[3]) { char b[8]; std::snprintf(b, 8, "#%02x%02x%02x", (int)std::lround(clampf(c[0], 0, 255)), (int)std::lround(clampf(c[1], 0, 255)), (int)std::lround(clampf(c[2], 0, 255))); return b; }
inline std::string mixHex(const std::string& a, const std::string& b, float k) { float x[3], y[3], z[3]; rgbOf(a, x); rgbOf(b, y); for (int i = 0; i < 3; i++) z[i] = x[i] + (y[i] - x[i]) * k; return hexOf(z); }
inline float lum(const std::string& h) { float c[3]; rgbOf(h, c); return (0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]) / 255; }

// RANDOMISE: any background and module colour that sit apart, and three cable colours that stand out from both and from each other
inline float colDist(const std::string& a, const std::string& b) { float x[3], y[3]; rgbOf(a, x); rgbOf(b, y); return std::sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2])) / 441.7f; }
template <class R> Theme randomTheme(R&& rnd) {   // rnd(n) gives 0..n-1
  Theme t; const auto& bgs = bgChoices(); const auto& mods = modChoices(); const auto& cs = cableChoices();
  const bool pale = rnd(4) != 0;   // three times in four a pale background, with a light-to-mid module colour
  for (int k = 0; k < 200; k++) { t.bg = bgs[1 + rnd((int)bgs.size() - 1)].second; if (!pale || lum(t.bg) > 0.45f) break; }
  for (int k = 0; k < 300; k++) { t.mod = mods[rnd((int)mods.size())].second; if (std::fabs(lum(t.mod) - lum(t.bg)) > 0.08f && colDist(t.mod, t.bg) > 0.12f && (!pale || k > 200 || lum(t.mod) > 0.35f)) break; }
  std::string* slot[3] = {&t.audio, &t.trig, &t.modc};
  for (int i = 0; i < 3; i++) for (int k = 0; k < 800; k++) {   // light-to-mid (they colour the headers too); asking less after 400 tries
      const std::string c = cs[rnd((int)cs.size())].second; const float r = k < 400 ? 1.f : 0.6f;
      bool ok = lum(c) > 0.42f && colDist(c, t.bg) > 0.3f * r && colDist(c, t.mod) > 0.25f * r;
      for (int j = 0; j < i; j++) if (colDist(c, *slot[j]) < 0.22f * r) ok = false; if (ok || k == 799) { *slot[i] = c; break; } }
  return t;
}
inline std::string headerOf(const std::string& c) { std::string h = c; for (int k = 1; k <= 10 && lum(h) < 0.42f; k++) h = mixHex(c, "#ffffff", k * 0.08f); return h; }
// set the canvas palette from a theme (the same formulas as the web version)
inline void applyTheme(Canvas& cv, const Theme& t) {
  const bool pond = t.bg == "pond"; const std::string bg = pond ? "#07131d" : t.bg, md = t.mod; const bool lightMod = lum(md) > 0.5f;
  cv.setColour(25, bg.c_str()); cv.setColour(26, (lum(bg) > 0.4f ? mixHex(bg, "#000000", 0.25f) : mixHex(bg, "#000000", 0.6f)).c_str()); cv.setColour(27, md.c_str());
  cv.setColour(28, lightMod ? "#1b1b1f" : "#d2c7a6"); cv.setColour(29, (lightMod ? mixHex(md, "#000000", 0.55f) : mixHex(md, "#ffffff", 0.5f)).c_str());
  cv.setColour(30, (lightMod ? mixHex(md, "#000000", 0.3f) : mixHex(md, "#000000", 0.5f)).c_str());
  cv.setColour(43, t.audio.c_str()); cv.setColour(44, t.trig.c_str()); cv.setColour(45, t.modc.c_str());
  // the module headers (and highlights) follow the cable colours, so a theme hangs together; a dark one is lightened for them
  cv.setColour(17, headerOf(t.audio).c_str()); cv.setColour(24, headerOf(t.trig).c_str()); cv.setColour(11, headerOf(t.modc).c_str());
  cv.setColour(46, mixHex(t.trig, "#ffffff", 0.55f).c_str());
  cv.setColour(47, lightMod ? "#d9761c" : "#ece6cf"); cv.setColour(48, (lightMod ? mixHex(t.audio, "#000000", 0.45f) : t.audio).c_str());
  if (pond) cv.pal[25] &= 0x00ffffffu;   // the pond shows through the background
}
inline Theme themeFrom(const Json& j) { Theme t; if (j.t != Json::OBJ) return t; t.bg = j.str("bg", t.bg); t.mod = j.str("mod", t.mod); t.audio = j.str("audio", t.audio); t.trig = j.str("trig", t.trig); t.modc = j.str("modc", t.modc); return t; }
inline Json themeJson(const Theme& t) { Json j = Json::object(); j.set("bg", Json::string(t.bg)); j.set("mod", Json::string(t.mod)); j.set("audio", Json::string(t.audio)); j.set("trig", Json::string(t.trig)); j.set("modc", Json::string(t.modc)); return j; }

}  // namespace tb
