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

  Canvas() { for (int i = 0; i < 128; i++) inkMap[i] = (uint8_t)i; for (int i = 0; i < FONT_N; i++) { unsigned char c = (unsigned char)FONT[i].c; if (c < 128) { std::copy(FONT[i].bits, FONT[i].bits + 15, glyph[c]); hasGlyph[c] = true; } }
    for (int i = 0; i < PAL_N; i++) setColour(i, PAL_HEX[i]); }
  void resize(int w, int h) { W = w; H = h; buf.assign((size_t)w * h, 25); rgba.assign((size_t)w * h, 0); }
  static uint32_t hexRGBA(const char* h) { unsigned r = std::strtoul(std::string(h + 1, 2).c_str(), 0, 16), g = std::strtoul(std::string(h + 3, 2).c_str(), 0, 16), b = std::strtoul(std::string(h + 5, 2).c_str(), 0, 16);
    return 0xff000000u | (b << 16) | (g << 8) | r; }   // RGBA bytes in memory (little-endian)
  void setColour(int i, const char* hex) { if (i >= 0 && i < 128) pal[i] = hexRGBA(hex); }
  void setColour(int i, uint32_t c) { if (i >= 0 && i < 128) pal[i] = c; }

  inline void px(int x, int y, int c) { if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return; buf[(size_t)y * W + x] = (uint8_t)(ink ? ink[c & 127] : c); }
  inline void pxf(float x, float y, int c) { px((int)std::lround(x), (int)std::lround(y), c); }
  void rect(int x, int y, int w, int h, int c) {
    const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h); if (x0 >= x1 || y0 >= y1) return;
    const uint8_t v = (uint8_t)(ink ? ink[c & 127] : c);
    for (int j = y0; j < y1; j++) std::fill(buf.begin() + (size_t)j * W + x0, buf.begin() + (size_t)j * W + x1, v);
  }
  void frame(int x, int y, int w, int h, int c) { for (int i = 0; i < w; i++) { px(x + i, y, c); px(x + i, y + h - 1, c); } for (int j = 0; j < h; j++) { px(x, y + j, c); px(x + w - 1, y + j, c); } }
  void line(int x0, int y0, int x1, int y1, int c) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1; int err = dx + dy;
    for (int n = 0; n < 4000; n++) { px(x0, y0, c); if (x0 == x1 && y0 == y1) break; const int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
  }
  static int tw(const std::string& s) { return (int)s.size() * 4 - 1; }
  void text(const std::string& s, int x, int y, int c) {
    for (size_t k = 0; k < s.size(); k++) {
      unsigned char ch = (unsigned char)s[k]; if (ch != 'x' && ch < 128) ch = (unsigned char)std::toupper(ch);
      if (ch >= 128 || !hasGlyph[ch]) continue;
      const char* f = glyph[ch];
      for (int j = 0; j < 5; j++) for (int i = 0; i < 3; i++) if (f[j * 3 + i] == '1') px(x + (int)k * 4 + i, y + j, c);
    }
  }
  // finish a frame: palette indices to colours
  void toRGBA() { const size_t n = buf.size(); for (size_t i = 0; i < n; i++) rgba[i] = pal[buf[i]]; }
};

}  // namespace tb
