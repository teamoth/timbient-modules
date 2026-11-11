// Timbient native: the pixel canvas. Everything on screen is drawn into a small indexed-colour buffer
// (one byte per pixel, a palette of colours), then scaled up crisply by the window.
#pragma once
#include "palette_font.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

namespace tb {

struct Canvas {
  int W = 0, H = 0;
  std::vector<uint8_t> buf;                 // palette index per pixel
  std::vector<uint32_t> rgba;               // final colours, for the window
  uint32_t pal[128] = {0};      // 0-48 the web version's colours, 64+ swatches for the theme menu
  const uint8_t* ink = nullptr;             // while a module draws, some colours are swapped for theme-aware ones
  uint8_t inkMap[128];
  char glyph[128][16] = {};
  bool hasGlyph[128] = {};
  // FINE LETTERS: text isn't drawn into the canvas but noted, and the window draws it afterwards with the 5 x 7 letters at
  // two thirds of an art pixel. So that what's drawn later still covers it, every pixel keeps which text it came after
  bool fine = false; int letterCase = 0;   // 0 lowercase, 1 capitals
  struct TextRun { int x, y; uint32_t t; uint8_t c; std::string s; };
  std::vector<TextRun> runs; std::vector<uint32_t> stamp; uint32_t cur = 1;
  void beginFrame() { runs.clear(); cur = 1; if (fine) std::fill(stamp.begin(), stamp.end(), 0u); }

  Canvas() { for (int i = 0; i < 128; i++) inkMap[i] = (uint8_t)i; for (int i = 0; i < FONT_N; i++) { unsigned char c = (unsigned char)FONT[i].c; if (c < 128) { std::copy(FONT[i].bits, FONT[i].bits + 15, glyph[c]); hasGlyph[c] = true; } }
    for (int i = 0; i < PAL_N; i++) setColour(i, PAL_HEX[i]); }
  void resize(int w, int h) { W = w; H = h; buf.assign((size_t)w * h, 25); rgba.assign((size_t)w * h, 0); stamp.assign((size_t)w * h, 0); }
  static uint32_t hexRGBA(const char* h) { unsigned r = std::strtoul(std::string(h + 1, 2).c_str(), 0, 16), g = std::strtoul(std::string(h + 3, 2).c_str(), 0, 16), b = std::strtoul(std::string(h + 5, 2).c_str(), 0, 16);
    return 0xff000000u | (b << 16) | (g << 8) | r; }   // RGBA bytes in memory (little-endian)
  void setColour(int i, const char* hex) { if (i >= 0 && i < 128) pal[i] = hexRGBA(hex); }
  void setColour(int i, uint32_t c) { if (i >= 0 && i < 128) pal[i] = c; }

  inline void px(int x, int y, int c) { if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return; const size_t i = (size_t)y * W + x; buf[i] = (uint8_t)(ink ? ink[c & 127] : c); if (fine) stamp[i] = cur; }
  inline void pxf(float x, float y, int c) { px((int)std::lround(x), (int)std::lround(y), c); }
  void rect(int x, int y, int w, int h, int c) {
    const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h); if (x0 >= x1 || y0 >= y1) return;
    const uint8_t v = (uint8_t)(ink ? ink[c & 127] : c);
    for (int j = y0; j < y1; j++) std::fill(buf.begin() + (size_t)j * W + x0, buf.begin() + (size_t)j * W + x1, v);
    if (fine) for (int j = y0; j < y1; j++) std::fill(stamp.begin() + (size_t)j * W + x0, stamp.begin() + (size_t)j * W + x1, cur);
  }
  void frame(int x, int y, int w, int h, int c) { for (int i = 0; i < w; i++) { px(x + i, y, c); px(x + i, y + h - 1, c); } for (int j = 0; j < h; j++) { px(x, y + j, c); px(x + w - 1, y + j, c); } }
  void line(int x0, int y0, int x1, int y1, int c) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1; int err = dx + dy;
    for (int n = 0; n < 4000; n++) { px(x0, y0, c); if (x0 == x1 && y0 == y1) break; const int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
  }
  static int tw(const std::string& s) { return (int)s.size() * 4 - 1; }
  void text(const std::string& s, int x, int y, int c) {
    if (fine) { if (s.empty() || y >= H || y + 7 < 0 || x >= W || x + tw(s) < 0) return; runs.push_back({x, y, ++cur, (uint8_t)(ink ? ink[c & 127] : c), s}); ++cur; return; }
    for (size_t k = 0; k < s.size(); k++) {
      unsigned char ch = (unsigned char)s[k]; if (ch != 'x' && ch < 128) ch = (unsigned char)std::toupper(ch);
      if (ch >= 128 || !hasGlyph[ch]) continue;
      const char* f = glyph[ch];
      for (int j = 0; j < 5; j++) for (int i = 0; i < 3; i++) if (f[j * 3 + i] == '1') px(x + (int)k * 4 + i, y + j, c);
    }
  }
  // FINE LETTERS for a window scaled by s: rectangles in window pixels, each a whole letter (u, v: its place in fontAtlas)
  // or, where something drawn later covers part of a letter, the single dots still showing (u -1)
  struct Quad { int x, y, w, h, u, v; uint32_t rgba; };
  static constexpr int ATLAS_COLS = 16, CELL_W = 6, CELL_H = 10;   // fontAtlas: 5 x 9 letters on a 6 x 10 grid; the last cell is solid
  static int fineDot(float s) { return std::max(1, (int)std::floor(2 * s / 3 + 0.01f)); }
  static int atlasIndex(unsigned char ch) { static int idx[128]; static bool init = false;
    if (!init) { for (int i = 0; i < 128; i++) idx[i] = -1; for (int i = 0; i < FONT57_N; i++) idx[(unsigned char)FONT57[i].c] = i; init = true; }
    return ch < 128 ? idx[ch] : -1; }
  static std::vector<uint32_t> fontAtlas(int& aw, int& ah) {   // white letters on clear, for the window to tint
    aw = ATLAS_COLS * CELL_W; ah = ((FONT57_N + 1 + ATLAS_COLS - 1) / ATLAS_COLS) * CELL_H; std::vector<uint32_t> a((size_t)aw * ah, 0);
    for (int i = 0; i < FONT57_N; i++) { const int cx = (i % ATLAS_COLS) * CELL_W, cy = (i / ATLAS_COLS) * CELL_H;
      for (int j = 0; j < 9; j++) for (int k = 0; k < 5; k++) if (FONT57[i].bits[j * 5 + k] == '1') a[(size_t)(cy + j) * aw + cx + k] = 0xffffffffu; }
    const int sc = FONT57_N; for (int j = 0; j < CELL_H; j++) for (int k = 0; k < CELL_W; k++) a[(size_t)((sc / ATLAS_COLS) * CELL_H + j) * aw + (sc % ATLAS_COLS) * CELL_W + k] = 0xffffffffu;
    return a; }
  void fineQuads(float s, std::vector<Quad>& out) const {
    out.clear(); const int d = fineDot(s), xoff = std::max(0, (int)std::floor((4 * s - 6 * d) / 2)), yoff = (int)std::lround(5 * s - 7 * d);
    const int rowsDown = (int)std::ceil((yoff + 9 * d) / s);
    for (const TextRun& r : runs) { const uint32_t col = pal[r.c & 127];
      for (size_t k = 0; k < r.s.size(); k++) {
        unsigned char ch = (unsigned char)r.s[k]; if (ch < 128) ch = (unsigned char)(letterCase == 1 ? std::toupper(ch) : std::tolower(ch));
        int gi = atlasIndex(ch); if (gi < 0) gi = atlasIndex((unsigned char)std::toupper(ch)); if (gi < 0 || ch == ' ') continue;
        const int ax = r.x + 4 * (int)k, X = (int)std::lround(ax * s) + xoff, Y = (int)std::lround(r.y * s) + yoff;
        bool whole = ax >= 0 && ax + 4 <= W && r.y >= 0 && r.y + rowsDown <= H;   // nothing drawn over it since: one rectangle
        for (int j = 0; whole && j < rowsDown; j++) for (int i = 0; i < 4; i++) if (stamp[(size_t)(r.y + j) * W + ax + i] > r.t) { whole = false; break; }
        if (whole) { out.push_back({X, Y, 5 * d, 9 * d, (gi % ATLAS_COLS) * CELL_W, (gi / ATLAS_COLS) * CELL_H, col}); continue; }
        const char* b = FONT57[gi].bits;
        for (int j = 0; j < 9; j++) for (int i = 0; i < 5; i++) { if (b[j * 5 + i] != '1') continue;
          const int px = (int)std::floor((X + i * d + d * 0.5f) / s), py = (int)std::floor((Y + j * d + d * 0.5f) / s);
          if (px < 0 || py < 0 || px >= W || py >= H || stamp[(size_t)py * W + px] > r.t) continue;
          out.push_back({X + i * d, Y + j * d, d, d, -1, 0, col}); } } }
  }
  // finish a frame: palette indices to colours
  void toRGBA() { const size_t n = buf.size(); for (size_t i = 0; i < n; i++) rgba[i] = pal[buf[i]]; }
};

}  // namespace tb
