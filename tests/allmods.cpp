// Offline test: play every module type in a small patch for a few seconds; check the sound stays finite and
// reasonably loud, and report what each one costs. Then, if a session file is given, load it and render it.
#include "../src/ui/ui.h"
#include "../src/ui/session.h"
#include <chrono>
#include <cstdio>
using namespace tb;

struct Result { float peak = 0, rms = 0; int bad = 0; double cost = 0; };
static Result run(Engine& eng, float secs) {
  Result r; std::vector<float> L(256), R(256); const int blocks = (int)(eng.sr * secs / 256); double sq = 0; long n = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (int b = 0; b < blocks; b++) { eng.render(L.data(), R.data(), 256); for (int i = 0; i < 256; i++) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) r.bad++; r.peak = std::max(r.peak, std::max(std::fabs(L[i]), std::fabs(R[i]))); sq += L[i] * L[i]; n++; } }
  r.cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / secs * 100; r.rms = (float)std::sqrt(sq / std::max(1L, n)); eng.collectGarbage(); return r;
}

int main(int argc, char** argv) {
  Engine eng; eng.setSampleRate(48000); Ui ui(eng);
  int fails = 0;
  for (const ModuleDef* d : allDefs()) {
    if (d->type == "out" || d->type == "keys") continue;
    ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* clk = ui.add("clock", 0, 20); ui.set(clk, "sync", 0); ui.set(clk, "bpm", 100);
    UiMod* m = ui.add(d->type, 100, 20);
    if (d->cat == Cat::Voice) { ui.connect(clk, "PULSE", m, "TRIG"); ui.connect(m, "OUT", out, "A"); }
    else if (d->cat == Cat::Proc) { UiMod* v = ui.add("pluck", 50, 20); ui.connect(clk, "PULSE", v, "TRIG"); ui.connect(v, "OUT", m, d->ins[0].name); ui.connect(m, "OUT", out, "A");
      if (d->inIndex("TRIG") >= 0) ui.connect(clk, "PULSE", m, "TRIG"); if (d->inIndex("KEY") >= 0) ui.connect(clk, "BAR", m, "KEY"); }
    else if (d->cat == Cat::Mod) { UiMod* v = ui.add("pluck", 50, 20); UiMod* f = ui.add("filter", 150, 20); ui.connect(clk, "PULSE", v, "TRIG"); ui.connect(v, "OUT", f, "IN"); ui.connect(f, "OUT", out, "A");
      if (d->ins.size() && d->ins[0].type == PType::Trig) ui.connect(clk, "PULSE", m, d->ins[0].name); if (d->ins.size() && d->ins[0].type == PType::Audio) ui.connect(v, "OUT", m, "IN");
      ui.modulate(m, f, f->m->def->paramIndex("cut")); }
    else { UiMod* v = ui.add("pluck", 150, 20); ui.connect(m, d->outs[0].name, v, "TRIG"); ui.connect(v, "OUT", out, "A");
      if (d->ins.size()) ui.connect(clk, "PULSE", m, d->ins[0].name);
      if (d->type == "tracker") { for (int i = 0; i < 16; i += 2) { m->m->seq[i * 2].store(60.f + i); m->m->seq[i * 2 + 1].store(0.8f); } } }
    if (d->type == "sample" || d->type == "looper") {   // give it a sound: a few seconds of a gliding tone with some noise
      AudioBuf* b = new AudioBuf(); b->sr = 44100; b->d.resize(44100 * 3); float ph = 0; Rng rr(5);
      for (size_t i = 0; i < b->d.size(); i++) { ph += (220 + i * 0.003f) / 44100.f; b->d[i] = 0.6f * std::sin(TAU * ph) + 0.05f * rr.bi(); }
      eng.setBuffer(m->m->id, b); }
    const Result r = run(eng, 4);
    const bool silent = r.peak < 0.005f; if (r.bad || silent) fails++;
    std::printf("%-10s peak %.3f  rms %.4f  cost %5.2f%%%s%s\n", d->type.c_str(), r.peak, r.rms, r.cost, r.bad ? "  NOT FINITE" : "", silent ? "  SILENT" : "");
  }
  // every voice at once, all twelve notes each, to see the worst case
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* h = ui.add("hops", 0, 0); ui.set(h, "rate", 20);
    for (const ModuleDef* d : allDefs()) if (d->cat == Cat::Voice) { UiMod* v = ui.add(d->type, 0, 0); ui.connect(h, "TRIG", v, "TRIG"); ui.connect(v, "OUT", out, "A"); }
    const Result r = run(eng, 6); std::printf("ALL VOICES, 20 NOTES/S: peak %.3f cost %.1f%%%s\n", r.peak, r.cost, r.bad ? " NOT FINITE" : ""); }
  if (argc > 1) {
    Json f; if (!Json::parse(readFile(argv[1]), f)) { std::printf("could not read %s\n", argv[1]); return 1; }
    const Json* data = patchIn(f); const std::string note = loadPatch(ui, *data); std::printf("loaded: %s\n", note.empty() ? "everything" : note.c_str());
    const Result r = run(eng, 20); std::printf("SESSION: peak %.3f rms %.4f cost %.1f%%%s\n", r.peak, r.rms, r.cost, r.bad ? " NOT FINITE" : "");
    const std::string back = serializePatch(ui).dump(); std::printf("tracker steps kept: %s\n", back.find("\"seq\":[[93") != std::string::npos ? "yes" : "NO");
  }
  return fails ? 1 : 0;
}
