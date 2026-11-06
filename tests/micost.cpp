// CI test: each MI VOICES engine playing six notes at once; checks the sound is finite and not silent,
// and reports what it costs as a share of one processor core (on the build machine).
#include "../src/ui/ui.h"
#include "../src/engine_mi/mi_labels.h"
#include <chrono>
#include <cstdio>
using namespace tb;
int main() {
  int bad = 0;
  for (int env = 0; env < 2; env++) for (int k = 0; k < MI_ENGINES; k++) {
    Engine eng; eng.setSampleRate(48000); Ui ui(eng); UiMod* out = ui.add("out", 0, 0); UiMod* mi = ui.add("mi", 0, 0); UiMod* h = ui.add("hops", 0, 0);
    ui.set(mi, "voice", (float)k); ui.set(mi, "env", (float)env); ui.set(h, "rate", 8); ui.connect(h, "TRIG", mi, "TRIG"); ui.connect(mi, "OUT", out, "A");
    std::vector<float> L(256), R(256); float pk = 0; int nan = 0; const float secs = 4;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < (int)(48000 * secs / 256); b++) { eng.render(L.data(), R.data(), 256); for (float v : L) { if (!std::isfinite(v)) nan++; pk = std::max(pk, std::fabs(v)); } }
    const double cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / secs * 100;
    const bool silent = pk < 0.003f; if (nan || silent) bad++;
    std::printf("%-8s %-11s peak %.3f  cost %5.1f%% of a core%s%s\n", env ? "ADSR" : "NATURAL", miEngines()[k].name, pk, cost, nan ? "  NOT FINITE" : "", silent ? "  SILENT" : "");
    eng.collectGarbage();
  }
  return bad ? 1 : 0;
}
