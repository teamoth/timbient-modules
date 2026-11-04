// Timbient native: a small JSON reader and writer, enough for session files (no outside libraries)
#pragma once
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tb {

struct Json {
  enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
  bool b = false; double n = 0; std::string s;
  std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;   // objects keep their key order
  const Json* get(const std::string& k) const { for (auto& kv : o) if (kv.first == k) return &kv.second; return nullptr; }
  Json* getm(const std::string& k) { for (auto& kv : o) if (kv.first == k) return &kv.second; return nullptr; }
  double num(const std::string& k, double d = 0) const { const Json* j = get(k); return j && j->t == NUM ? j->n : (j && j->t == BOOL ? (j->b ? 1 : 0) : d); }
  std::string str(const std::string& k, const std::string& d = "") const { const Json* j = get(k); return j && j->t == STR ? j->s : d; }
  bool truthy(const std::string& k) const { const Json* j = get(k); return j && ((j->t == BOOL && j->b) || (j->t == NUM && j->n != 0)); }
  static Json number(double v) { Json j; j.t = NUM; j.n = v; return j; }
  static Json string(const std::string& v) { Json j; j.t = STR; j.s = v; return j; }
  static Json boolean(bool v) { Json j; j.t = BOOL; j.b = v; return j; }
  static Json object() { Json j; j.t = OBJ; return j; }
  static Json array() { Json j; j.t = ARR; return j; }
  Json& set(const std::string& k, Json v) { if (Json* e = getm(k)) *e = std::move(v); else o.push_back({k, std::move(v)}); return *this; }
  Json& push(Json v) { a.push_back(std::move(v)); return *this; }

  // ---- reading ----
  static bool parse(const std::string& src, Json& out) { size_t i = 0; return value(src, i, out, 0); }
  static void ws(const std::string& s, size_t& i) { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; }
  static bool value(const std::string& s, size_t& i, Json& v, int depth) {
    if (depth > 64) return false; ws(s, i); if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '{') { v.t = OBJ; i++; ws(s, i); if (i < s.size() && s[i] == '}') { i++; return true; }
      while (i < s.size()) { ws(s, i); Json k; if (!strv(s, i, k)) return false; ws(s, i); if (i >= s.size() || s[i] != ':') return false; i++;
        Json x; if (!value(s, i, x, depth + 1)) return false; v.o.push_back({k.s, std::move(x)}); ws(s, i);
        if (i < s.size() && s[i] == ',') { i++; continue; } if (i < s.size() && s[i] == '}') { i++; return true; } return false; } return false; }
    if (c == '[') { v.t = ARR; i++; ws(s, i); if (i < s.size() && s[i] == ']') { i++; return true; }
      while (i < s.size()) { Json x; if (!value(s, i, x, depth + 1)) return false; v.a.push_back(std::move(x)); ws(s, i);
        if (i < s.size() && s[i] == ',') { i++; continue; } if (i < s.size() && s[i] == ']') { i++; return true; } return false; } return false; }
    if (c == '"') return strv(s, i, v);
    if (s.compare(i, 4, "true") == 0) { v.t = BOOL; v.b = true; i += 4; return true; }
    if (s.compare(i, 5, "false") == 0) { v.t = BOOL; v.b = false; i += 5; return true; }
    if (s.compare(i, 4, "null") == 0) { v.t = NUL; i += 4; return true; }
    char* end = nullptr; const double d = std::strtod(s.c_str() + i, &end); if (end == s.c_str() + i) return false;
    v.t = NUM; v.n = d; i = end - s.c_str(); return true;
  }
  static bool strv(const std::string& s, size_t& i, Json& v) {
    if (i >= s.size() || s[i] != '"') return false; i++; v.t = STR; v.s.clear();
    while (i < s.size() && s[i] != '"') {
      if (s[i] == '\\' && i + 1 < s.size()) { const char e = s[i + 1]; i += 2;
        if (e == 'n') v.s += '\n'; else if (e == 't') v.s += '\t'; else if (e == 'r') v.s += '\r'; else if (e == 'b') v.s += '\b'; else if (e == 'f') v.s += '\f';
        else if (e == 'u' && i + 4 <= s.size()) { unsigned cp = std::stoul(s.substr(i, 4), nullptr, 16); i += 4;
          if (cp < 0x80) v.s += (char)cp; else if (cp < 0x800) { v.s += (char)(0xC0 | (cp >> 6)); v.s += (char)(0x80 | (cp & 0x3F)); }
          else { v.s += (char)(0xE0 | (cp >> 12)); v.s += (char)(0x80 | ((cp >> 6) & 0x3F)); v.s += (char)(0x80 | (cp & 0x3F)); } }
        else v.s += e; }
      else v.s += s[i++];
    }
    if (i >= s.size()) return false; i++; return true;
  }

  // ---- writing ----
  std::string dump() const { std::string o2; write(o2); return o2; }
  void write(std::string& out) const {
    switch (t) {
      case NUL: out += "null"; break;
      case BOOL: out += b ? "true" : "false"; break;
      case NUM: { char buf[40]; if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 1e15) std::snprintf(buf, 40, "%.0f", n); else std::snprintf(buf, 40, "%.9g", std::isfinite(n) ? n : 0.0); out += buf; break; }
      case STR: quote(s, out); break;
      case ARR: out += '['; for (size_t i = 0; i < a.size(); i++) { if (i) out += ','; a[i].write(out); } out += ']'; break;
      case OBJ: out += '{'; for (size_t i = 0; i < o.size(); i++) { if (i) out += ','; quote(o[i].first, out); out += ':'; o[i].second.write(out); } out += '}'; break;
    }
  }
  static void quote(const std::string& s, std::string& out) {
    out += '"'; for (unsigned char c : s) { if (c == '"' || c == '\\') { out += '\\'; out += (char)c; } else if (c == '\n') out += "\\n"; else if (c < 0x20) { char b[8]; std::snprintf(b, 8, "\\u%04x", c); out += b; } else out += (char)c; } out += '"';
  }
};

}  // namespace tb
