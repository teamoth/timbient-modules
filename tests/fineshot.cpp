// Headless: the demo patch with FINE LETTERS, drawn as the window would (canvas scaled by 3, then the letters), to a PPM
#include "../src/ui/ui.h"
#include "../src/ui/session.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace tb;
static void run(Engine& eng, float secs) { std::vector<float> L(256), R(256); for (int i = 0; i < 48000 * secs / 256; i++) eng.render(L.data(), R.data(), 256); }
int main(int argc, char** argv) {
  const float S = argc > 1 ? (float)std::atof(argv[1]) : 3; const int lc = argc > 2 ? std::atoi(argv[2]) : 0;
  Engine eng; eng.setSampleRate(48000); Ui ui(eng); ui.demoPatch();
  ui.bar.push_back({"SESSIONS", [](int) {}}); ui.bar.push_back({"SETTINGS", [](int) {}});
  ui.shadows = std::getenv("TB_SHADOWS") != nullptr; UiMod* tr = ui.add("tracker", 330, 330); for (int r = 0; r < 8; r++) for (int k = 0; k < 4; k++) if ((r + k) % 3 == 0) tr->m->seq[Module::si(r, k)].store(48.f + r + k * 3);
  ui.add("comp", 110, 330); ui.add("keys", 110, 420);
  UiMod* tl = ui.add("timeline", 10, 330); ui.add("qwerty", 180, 330); for (int i = 0; i < 4; i++) { tl->m->ev[i * 4].store(30.f + i * 50); tl->m->ev[i * 4 + 1].store((float)i); tl->m->ev[i * 4 + 2].store(1); } tl->m->evN.store(4); tl->tlSel = 1;
  run(eng, 1);
  Canvas c; c.fine = true; c.letterCase = lc; c.resize(460, 460); c.beginFrame(); ui.draw(c, 0.016f);
  const int W = (int)(c.W * S), H = (int)(c.H * S); std::vector<uint32_t> img((size_t)W * H);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) img[(size_t)y * W + x] = c.rgba[(size_t)(int)(y / S) * c.W + (int)(x / S)];
  int aw, ah; const std::vector<uint32_t> atlas = Canvas::fontAtlas(aw, ah); std::vector<Canvas::Quad> q; c.fineQuads(S, q);
  const int d = Canvas::fineDot(S);
  for (auto& r : q) for (int y = 0; y < r.h; y++) for (int x = 0; x < r.w; x++) {
    const int X = r.x + x, Y = r.y + y; if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
    if (r.u >= 0 && !(atlas[(size_t)(r.v + y / d) * aw + r.u + x / d] >> 24)) continue; img[(size_t)Y * W + X] = r.rgba; }
  FILE* f = std::fopen("fine.ppm", "wb"); std::fprintf(f, "P6 %d %d 255\n", W, H);
  for (auto p : img) { unsigned char rgb[3] = {(unsigned char)(p & 255), (unsigned char)((p >> 8) & 255), (unsigned char)((p >> 16) & 255)}; std::fwrite(rgb, 1, 3, f); }
  std::fclose(f); std::printf("%zu quads\n", q.size()); eng.collectGarbage(); return 0;
}
