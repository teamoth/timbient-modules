// Timbient native: themes. The background, module and cable colours, with the same choices and presets as the
// web version; module text flips light or dark so it stays readable on any module colour.
#pragma once
#include "canvas.h"
#include "json.h"
#include <string>
#include <utility>
#include <vector>

namespace tb {

struct Theme { std::string bg = "#000000", mod = "#3b3b41", audio = "#6fb3ad", trig = "#b39ddb", modc = "#86b34f"; };
using Choice = std::pair<const char*, const char*>;
inline const std::vector<Choice>& bgChoices() { static const std::vector<Choice> v = {{"POND", "pond"}, {"BLACK", "#000000"}, {"CHARCOAL", "#1a1a1d"}, {"SLATE", "#232c33"}, {"NAVY", "#111a2e"}, {"FOREST", "#13201a"}, {"PLUM", "#221624"},
  {"CLAY", "#3a2a24"}, {"MIDNIGHT", "#0b1020"}, {"EARTH", "#2a1d18"}, {"STEEL", "#5c5f66"}, {"LIGHT GREY", "#c9c9ce"}, {"PAPER", "#ebe6da"}}; return v; }
inline const std::vector<Choice>& modChoices() { static const std::vector<Choice> v = {{"GREY", "#3b3b41"}, {"GRAPHITE", "#2a2a2e"}, {"BLACK", "#111114"}, {"SLATE", "#34424a"}, {"NAVY", "#1f2a44"}, {"MOSS", "#39432f"},
  {"DARK MOSS", "#0f1a10"}, {"DARK GREY", "#2b2b2b"}, {"RUST", "#4a3328"}, {"PLUM", "#3d2d42"}, {"SILVER", "#b8bac0"}, {"BONE", "#d9d3c3"}}; return v; }
inline const std::vector<Choice>& cableChoices() { static const std::vector<Choice> v = {{"TEAL", "#6fb3ad"}, {"LILAC", "#b39ddb"}, {"GREEN", "#86b34f"}, {"AMBER", "#e0a83c"}, {"CORAL", "#e07a5f"}, {"CREAM", "#ece6cf"},
  {"SKY", "#7fb8e6"}, {"PINK", "#e6a1d8"}, {"DEEP TEAL", "#1f5f5a"}, {"DEEP PURPLE", "#5b3f8f"}, {"GREY", "#8a8a90"}, {"ICE", "#7fd1e8"}, {"LIME", "#a8e07a"}, {"GOLD", "#c9b26b"}, {"PEACH", "#e09a6b"}, {"MINT", "#9fd28b"},
  {"TAN", "#e3b07a"}, {"ORCHID", "#c8a2c8"}, {"NEON", "#4cff7a"}, {"YELLOW", "#ffd23f"}, {"AZURE", "#4cc9ff"}, {"WHITE", "#e0e0e0"}, {"SILVER", "#bdbdbd"}, {"DEEP GREEN", "#4f7a2a"}, {"OLIVE", "#3f6b1f"}}; return v; }
inline const std::vector<std::pair<const char*, Theme>>& presets() { static const std::vector<std::pair<const char*, Theme>> v = {
  {"DEFAULT", Theme{}}, {"PAPER", {"#ebe6da", "#d9d3c3", "#1f5f5a", "#5b3f8f", "#4f7a2a"}}, {"NIGHT", {"#0b1020", "#1f2a44", "#7fd1e8", "#e6a1d8", "#a8e07a"}},
  {"MOSS", {"#13201a", "#39432f", "#c9b26b", "#e09a6b", "#9fd28b"}}, {"CLAY", {"#2a1d18", "#4a3328", "#e3b07a", "#c8a2c8", "#9cc28b"}}, {"TERMINAL", {"#000000", "#0f1a10", "#4cff7a", "#ffd23f", "#4cc9ff"}},
  {"MONO", {"#111111", "#2b2b2b", "#e0e0e0", "#8a8a8a", "#bdbdbd"}}, {"STEEL", {"#5c5f66", "#b8bac0", "#1f5f5a", "#5b3f8f", "#3f6b1f"}}}; return v; }
inline bool sameTheme(const Theme& a, const Theme& b) { return a.bg == b.bg && a.mod == b.mod && a.audio == b.audio && a.trig == b.trig && a.modc == b.modc; }

inline void rgbOf(const std::string& h, float c[3]) { for (int i = 0; i < 3; i++) c[i] = h.size() >= 7 ? (float)std::strtoul(h.substr(1 + i * 2, 2).c_str(), 0, 16) : 0; }
inline std::string hexOf(const float c[3]) { char b[8]; std::snprintf(b, 8, "#%02x%02x%02x", (int)std::lround(clampf(c[0], 0, 255)), (int)std::lround(clampf(c[1], 0, 255)), (int)std::lround(clampf(c[2], 0, 255))); return b; }
inline std::string mixHex(const std::string& a, const std::string& b, float k) { float x[3], y[3], z[3]; rgbOf(a, x); rgbOf(b, y); for (int i = 0; i < 3; i++) z[i] = x[i] + (y[i] - x[i]) * k; return hexOf(z); }
inline float lum(const std::string& h) { float c[3]; rgbOf(h, c); return (0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]) / 255; }

// set the canvas palette from a theme (the same formulas as the web version)
inline void applyTheme(Canvas& cv, const Theme& t) {
  const bool pond = t.bg == "pond"; const std::string bg = pond ? "#07131d" : t.bg, md = t.mod; const bool lightMod = lum(md) > 0.5f;
  cv.setColour(25, bg.c_str()); cv.setColour(26, (lum(bg) > 0.4f ? mixHex(bg, "#000000", 0.25f) : mixHex(bg, "#000000", 0.6f)).c_str()); cv.setColour(27, md.c_str());
  cv.setColour(28, lightMod ? "#1b1b1f" : "#d2c7a6"); cv.setColour(29, (lightMod ? mixHex(md, "#000000", 0.55f) : mixHex(md, "#ffffff", 0.5f)).c_str());
  cv.setColour(30, (lightMod ? mixHex(md, "#000000", 0.3f) : mixHex(md, "#000000", 0.5f)).c_str());
  cv.setColour(43, t.audio.c_str()); cv.setColour(44, t.trig.c_str()); cv.setColour(45, t.modc.c_str()); cv.setColour(46, mixHex(t.trig, "#ffffff", 0.55f).c_str());
  cv.setColour(47, lightMod ? "#d9761c" : "#ece6cf"); cv.setColour(48, (lightMod ? mixHex(t.audio, "#000000", 0.45f) : t.audio).c_str());
  if (pond) cv.pal[25] &= 0x00ffffffu;   // the pond shows through the background
}
inline Theme themeFrom(const Json& j) { Theme t; if (j.t != Json::OBJ) return t; t.bg = j.str("bg", t.bg); t.mod = j.str("mod", t.mod); t.audio = j.str("audio", t.audio); t.trig = j.str("trig", t.trig); t.modc = j.str("modc", t.modc); return t; }
inline Json themeJson(const Theme& t) { Json j = Json::object(); j.set("bg", Json::string(t.bg)); j.set("mod", Json::string(t.mod)); j.set("audio", Json::string(t.audio)); j.set("trig", Json::string(t.trig)); j.set("modc", Json::string(t.modc)); return j; }

}  // namespace tb
