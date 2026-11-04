// Headless test: build the demo patch, run the engine for a moment, draw one frame of the interface to a PPM image.
#include "../src/ui/ui.h"
#include <cstdio>
#include <vector>
using namespace tb;
int main() {
  Engine eng; eng.setSampleRate(48000); Ui ui(eng); ui.demoPatch();
  std::vector<float> L(256), R(256); for (int i = 0; i < 48000 * 3 / 256; i++) eng.render(L.data(), R.data(), 256);
  Canvas c; c.resize(440, 280); ui.draw(c, 0.016f);
  FILE* f = std::fopen("shot.ppm", "wb"); std::fprintf(f, "P6 %d %d 255\n", c.W, c.H);
  for (auto p : c.rgba) { unsigned char rgb[3] = {(unsigned char)(p & 255), (unsigned char)((p >> 8) & 255), (unsigned char)((p >> 16) & 255)}; std::fwrite(rgb, 1, 3, f); }
  std::fclose(f); eng.collectGarbage(); return 0;
}
