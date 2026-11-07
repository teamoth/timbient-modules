// Timbient native: shorthand for module settings, matching the web version's helpers one for one
#pragma once
#include "engine.h"
#include <cstdio>

namespace tb {

inline ParamDef pr(const char* k, const char* l, float mn, float mx, float v) { ParamDef p; p.key = k; p.label = l; p.min = mn; p.max = mx; p.def = v; return p; }
inline ParamDef prExp(const char* k, const char* l, float mn, float mx, float v) { auto p = pr(k, l, mn, mx, v); p.exp = true; return p; }
inline ParamDef prHz(const char* k, const char* l, float mn, float mx, float v) { auto p = prExp(k, l, mn, mx, v); p.hz = true; return p; }
inline ParamDef T(const char* k, const char* l, float mn, float mx, float v) { auto p = prExp(k, l, mn, mx, v); p.time = true; return p; }
inline ParamDef pct(const char* k, const char* l, float v) { return pr(k, l, 0, 1, v); }
inline ParamDef pctX(const char* k, const char* l, float mx, float v) { auto p = pr(k, l, 0, mx, v); p.pctFmt = true; return p; }
inline ParamDef ST(const char* k, const char* l, float mn, float mx, float v) { auto p = pr(k, l, mn, mx, v); p.step = 1; return p; }
inline ParamDef OP(const char* k, const char* l, std::vector<std::string> opts, int v) { auto p = pr(k, l, 0, (float)opts.size() - 1, (float)v); p.step = 1; p.opts = opts; return p; }
inline ParamDef ACT(const char* k, const char* l) { ParamDef p; p.key = k; p.label = l; p.act = true; return p; }
inline ParamDef rev(ParamDef p) { p.rev = true; return p; }
inline ParamDef only(ParamDef p, const char* o) { p.only = o; return p; }
inline ParamDef fmt(ParamDef p, std::function<std::string(float)> f) { p.fmt = f; return p; }
inline ParamDef playBtn(ParamDef p) { p.play = true; return p; }
inline ParamDef tog(ParamDef p) { p.tog = true; return p; }

inline ParamDef LEVEL() { return pct("level", "LEVEL", 0.7f); }
inline ParamDef PITCH() { return ST("pitch", "PITCH", -24, 24, 0); }
inline std::vector<ParamDef> envP(float a, float d, float su, float h, float r) {
  return {T("atk", "ATTACK", 0.002f, 10, a), T("dec", "DECAY", 0.01f, 10, d), pct("sus", "SUSTAIN", su), T("hold", "HOLD", 0.01f, 30, h), T("rel", "RELEASE", 0.01f, 20, r)};
}
inline ParamDef OCTP(const char* k, float mn, float mx, float v) { auto p = pr(k, "OCTAVE", mn, mx, v); p.step = 1; p.fmt = [](float x) { char b[8]; std::snprintf(b, 8, "%d", (int)x); return std::string("@") + b; }; return p; }   // '@' becomes the key's root note on screen
inline std::string fmtSecs(float v) { char b[16]; if (v < 1) std::snprintf(b, 16, "%dMS", (int)std::round(v * 1000)); else std::snprintf(b, 16, "%.1fS", v); return b; }

// format a setting's value for the screen
inline std::string fmtParam(const ParamDef& q, float v, int root) {
  char b[32];
  if (q.act) return "TAP";
  if (!q.opts.empty()) { int i = (int)std::round(v); return q.opts[std::max(0, std::min((int)q.opts.size() - 1, i))]; }
  if (q.fmt) { std::string s = q.fmt(v); if (!s.empty() && s[0] == '@') s = std::string(NOTE_NAMES[root]) + s.substr(1); return s; }
  if (q.pctFmt) { std::snprintf(b, 32, "%d%%", (int)std::round(v * 100)); return b; }
  if (q.time) return fmtSecs(v);
  if (q.hz) { if (v < 1000) std::snprintf(b, 32, "%dHZ", (int)std::round(v)); else std::snprintf(b, 32, "%.1fK", v / 1000); return b; }
  if (q.step > 0) { int i = (int)std::round(v); std::snprintf(b, 32, (q.min < 0 && i > 0) ? "+%d" : "%d", i); return b; }
  if (q.min == 0 && q.max == 1) { std::snprintf(b, 32, "%d%%", (int)std::round(v * 100)); return b; }
  std::snprintf(b, 32, "%.2f", v); return b;
}

// a short way to register a module type
struct Reg { Reg(ModuleDef* d) { registerDef(d); } };

}  // namespace tb
