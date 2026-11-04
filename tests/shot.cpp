// Headless test: build the demo patch, run the engine for a moment, check that a session survives
// saving and loading, and draw one frame of the interface to a PPM image.
#include "../src/ui/ui.h"
#include "../src/ui/session.h"
#include <cstdio>
#include <vector>
using namespace tb;
static void run(Engine& eng, float secs) { std::vector<float> L(256), R(256); for (int i = 0; i < 48000 * secs / 256; i++) eng.render(L.data(), R.data(), 256); }
int main() {
  Engine eng; eng.setSampleRate(48000); Ui ui(eng); ui.demoPatch();
  ui.bar.push_back({"SESSIONS", [](int) {}}); ui.bar.push_back({"SETTINGS", [](int) {}});
  run(eng, 1);
  const std::string a = serializePatch(ui).dump();
  ui.clearPatch(); run(eng, 0.1f);
  Json j; Json::parse(a, j); const std::string note = loadPatch(ui, j); run(eng, 2);
  const std::string b = serializePatch(ui).dump();
  std::printf("round trip: %s%s\n", a == b ? "same" : "DIFFERENT", note.empty() ? "" : (" / " + note).c_str());
  if (a != b) std::printf("A %s\nB %s\n", a.c_str(), b.c_str());
  Canvas c; c.resize(440, 280); ui.draw(c, 0.016f);
  FILE* f = std::fopen("shot.ppm", "wb"); std::fprintf(f, "P6 %d %d 255\n", c.W, c.H);
  for (auto p : c.rgba) { unsigned char rgb[3] = {(unsigned char)(p & 255), (unsigned char)((p >> 8) & 255), (unsigned char)((p >> 16) & 255)}; std::fwrite(rgb, 1, 3, f); }
  std::fclose(f); eng.collectGarbage(); return 0;
}
