// Headless: a few modules on a pale theme with shadows, with and without the border round each module (bordershot <0|1>)
#include "../src/ui/ui.h"
#include "../src/ui/session.h"
#include "../src/ui/theme.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace tb;
int main(int argc, char** argv) {
  const bool border = argc > 1 && std::atoi(argv[1]); const float S = 3;
  Engine eng; eng.setSampleRate(48000); Ui ui(eng); ui.shadows = true; ui.borders = border;
  ui.bar.push_back({"SESSIONS", [](int) {}}); ui.bar.push_back({"SETTINGS", [](int) {}}); ui.add("clump", 10, 14); ui.add("mutes", 130, 14); UiMod* nl = ui.add("nloop", 250, 14); { Module* m = nl->m; for (int i = 0; i < 14; i++) { m->ev[i * 4].store(i * 0.55f); m->ev[i * 4 + 1].store(55.f + (i * 5) % 12); m->ev[i * 4 + 2].store(0.8f); m->ev[i * 4 + 3].store(0); } m->evN.store(14); nl->m->p[nl->m->def->paramIndex("start")].store(1); nl->m->p[nl->m->def->paramIndex("lenb")].store(5); }
  Canvas c; c.fine = true; c.resize(360, 260); applyTheme(c, Theme{}, argc > 2 ? std::atoi(argv[2]) : 0);
  { std::vector<float> L(256), R(256); for (int i = 0; i < 100; i++) eng.render(L.data(), R.data(), 256); } c.beginFrame(); ui.draw(c, 0.016f);
  const int W = (int)(c.W * S), H = (int)(c.H * S); std::vector<uint32_t> img((size_t)W * H);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) img[(size_t)y * W + x] = c.rgba[(size_t)(int)(y / S) * c.W + (int)(x / S)];
  int aw, ah; const std::vector<uint32_t> atlas = Canvas::fontAtlas(aw, ah); std::vector<Canvas::Quad> q; c.fineQuads(S, q); const int d = Canvas::fineDot(S);
  for (auto& r : q) for (int y = 0; y < r.h; y++) for (int x = 0; x < r.w; x++) { const int X = r.x + x, Y = r.y + y; if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
    if (r.u >= 0 && !(atlas[(size_t)(r.v + y / d) * aw + r.u + x / d] >> 24)) continue; img[(size_t)Y * W + X] = r.rgba; }
  FILE* f = std::fopen(border ? "border1.ppm" : "border0.ppm", "wb"); std::fprintf(f, "P6 %d %d 255\n", W, H);
  for (auto p : img) { unsigned char rgb[3] = {(unsigned char)(p & 255), (unsigned char)((p >> 8) & 255), (unsigned char)((p >> 16) & 255)}; std::fwrite(rgb, 1, 3, f); }
  std::fclose(f); eng.collectGarbage(); return 0;
}
