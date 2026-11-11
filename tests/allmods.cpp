#include <cstdlib>
// Offline test: play every module type in a small patch for a few seconds; check the sound stays finite and
// reasonably loud, and report what each one costs. Then, if a session file is given, load it and render it.
#include "../src/ui/ui.h"
#include "../src/ui/session.h"
#include <chrono>
#include <cstdio>
#include <cstring>
using namespace tb;

static const float RATE = std::getenv("TB_RATE") ? (float)std::atof(std::getenv("TB_RATE")) : 48000.f;   // TB_RATE=96000 tries another rate
struct Result { float peak = 0, rms = 0; int bad = 0; double cost = 0; };
static Result run(Engine& eng, float secs) {
  Result r; std::vector<float> L(256), R(256); const int blocks = (int)(eng.sr * secs / 256); double sq = 0; long n = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (int b = 0; b < blocks; b++) { eng.render(L.data(), R.data(), 256); for (int i = 0; i < 256; i++) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) r.bad++; r.peak = std::max(r.peak, std::max(std::fabs(L[i]), std::fabs(R[i]))); sq += L[i] * L[i]; n++; } }
  r.cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / secs * 100; r.rms = (float)std::sqrt(sq / std::max(1L, n)); eng.collectGarbage(); return r;
}

int main(int argc, char** argv) {
  Engine eng; eng.setSampleRate(RATE); Ui ui(eng);
  int fails = 0;
  for (const ModuleDef* d : allDefs()) {
    if (d->type == "out" || d->type == "keys" || d->type == "qwerty" || d->type == "midi" || d->type == "midiout") continue;   // these need a device or a hand
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
    if (d->type == "timeline") { ui.set(m, "len", 10); ui.set(m, "run", 1); for (int i = 0; i < 6; i++) { m->m->ev[i * 4].store(i * 0.5f); m->m->ev[i * 4 + 1].store(0); m->m->ev[i * 4 + 2].store(1); m->m->ev[i * 4 + 3].store(0); } m->m->evN.store(6); }
    if (d->type == "tloop") { ui.set(m, "len", 1); ui.set(m, "rec", 1); }   // record a short loop, so it plays back within the test
    if (d->type == "stretch") ui.set(m, "src", 1);  // stretch a file
    if (d->type == "sample" || d->type == "looper" || d->type == "stretch") {   // give it a sound: a few seconds of a gliding tone with some noise
      AudioBuf* b = new AudioBuf(); b->sr = 44100; b->d.resize(44100 * 3); float ph = 0; Rng rr(5);
      for (size_t i = 0; i < b->d.size(); i++) { ph += (220 + i * 0.003f) / 44100.f; b->d[i] = 0.6f * std::sin(TAU * ph) + 0.05f * rr.bi(); }
      eng.setBuffer(m->m->id, b); }
    const Result r = run(eng, 4);
    const bool silent = r.peak < 0.005f; if (r.bad || silent) fails++;
    std::printf("%-10s peak %.3f  rms %.4f  cost %5.2f%%%s%s\n", d->type.c_str(), r.peak, r.rms, r.cost, r.bad ? "  NOT FINITE" : "", silent ? "  SILENT" : "");
  }
  // every voice must follow the note it's given: its sound should repeat at the note's period (or a whole multiple of it)
  for (const ModuleDef* d : allDefs()) {
    if (d->cat != Cat::Voice || d->type == "sample" || d->type == "click" || d->type == "grains") continue;   // noise bursts and grain clouds don't repeat cleanly
    bool ok = true; char msg[160] = ""; int lags[2] = {0, 0}, li = 0;
    for (float nt : {57.f, 69.f}) { Engine e2; e2.setSampleRate(RATE); Ui u2(e2); UiMod* o = u2.add("out", 0, 0); UiMod* v = u2.add(d->type, 0, 0); u2.connect(v, "OUT", o, "A");
      if (d->paramIndex("env") >= 0) u2.set(v, "env", 1);
      std::vector<float> L(256), R(256), buf; for (int i = 0; i < 20; i++) e2.render(L.data(), R.data(), 256);
      Event ev; ev.note = nt; ev.vel = 1; e2.trigger(v->m->id, 0, ev);
      for (int i = 0; i < 60; i++) { e2.render(L.data(), R.data(), 256); if (i > 20) buf.insert(buf.end(), L.begin(), L.end()); }
      std::vector<double> ac(1400, 0.0); double bv = -1e18; for (int lag = 20; lag < 1400; lag++) { double sum = 0; for (size_t i = 0; i + lag < buf.size(); i++) sum += buf[i] * buf[i + lag]; ac[lag] = sum / (buf.size() - lag); bv = std::max(bv, ac[lag]); }
      int best = 0; for (int lag = 21; lag < 1399 && !best; lag++) if (ac[lag] >= 0.9 * bv && ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1]) best = lag;   // the shortest repeat that matches nearly as well as any
      const double per = RATE / mtof(nt), k = std::round(best / per), err = std::fabs(best / per - k);
      lags[li++] = best; if (k < 1 || err > 0.06) ok = false; std::snprintf(msg + std::strlen(msg), 80, "  note %.0f repeats every %d (expect %.0f)", nt, best, per); e2.collectGarbage(); }
    if (std::fabs((double)lags[0] / std::max(1, lags[1]) - 1) < 0.1) ok = false;   // the two notes must not come out the same
    if (!ok) fails++;
    std::printf("%-10s pitch%s%s\n", d->type.c_str(), msg, ok ? "" : "  DOES NOT FOLLOW THE NOTE");
  }
  // every voice at once, all twelve notes each, to see the worst case
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* h = ui.add("hops", 0, 0); ui.set(h, "rate", 20);
    for (const ModuleDef* d : allDefs()) if (d->cat == Cat::Voice) { UiMod* v = ui.add(d->type, 0, 0); ui.connect(h, "TRIG", v, "TRIG"); ui.connect(v, "OUT", out, "A"); }
    const Result r = run(eng, 6); std::printf("ALL VOICES, 20 NOTES/S: peak %.3f cost %.1f%%%s\n", r.peak, r.cost, r.bad ? " NOT FINITE" : ""); }
  // TIMELINE into a SWITCH socket: an OFF at 1 s mutes the clock, an ON at 2 s turns it back on; and it survives saving
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* clk = ui.add("clock", 0, 20); UiMod* tl = ui.add("timeline", 0, 100); ui.set(tl, "len", 10); ui.set(tl, "run", 1);
    UiMod* v = ui.add("pluck", 100, 20); ui.connect(clk, "PULSE", v, "TRIG"); ui.connect(v, "OUT", out, "A");
    const bool plugged = ui.connect(tl, 0, clk, SWITCH_PORT);
    const float E[2][3] = {{1, 0, 0}, {2, 0, 1}}; for (int i = 0; i < 2; i++) { for (int c = 0; c < 3; c++) tl->m->ev[i * 4 + c].store(E[i][c]); tl->m->ev[i * 4 + 3].store(0); } tl->m->evN.store(2);
    run(eng, 0.5f); const bool a = !clk->m->mute.load(); run(eng, 1.0f); const bool b = clk->m->mute.load(); run(eng, 1.0f); const bool c2 = !clk->m->mute.load();
    const std::string S = serializePatch(ui).dump(); const bool saved = S.find("\"SWITCH\"") != std::string::npos;
    Json j; Json::parse(S, j); ui.clearPatch(); loadPatch(ui, j); bool back = false; for (auto& cb : ui.cables) if (cb.toPort == SWITCH_PORT) back = true;
    int evs = 0; for (auto& um : ui.mods) if (um->m->def->type == "timeline") evs = um->m->evN.load();
    const bool ok = plugged && a && b && c2 && saved && back && evs == 2; if (!ok) fails++;
    std::printf("timeline switch: plugged %d, on %d, off at 1s %d, on at 2s %d, saved %d, loaded %d, events %d%s\n", plugged, a, b, c2, saved, back, evs, ok ? "" : "  WRONG"); }
  // TRACKER: output 2 plays only column 2; TRIG plays all four; no socket on STEP JUMP
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* tr = ui.add("tracker", 0, 20); UiMod* a = ui.add("pluck", 100, 20); UiMod* b = ui.add("pluck", 100, 120); UiMod* lfo = ui.add("lfo", 0, 200);
    ui.set(tr, "sync", 0); ui.set(tr, "bpm", 120); ui.set(tr, "len", 4);
    for (int r = 0; r < 4; r++) tr->m->seq[Module::si(r, 1)].store(60.f); tr->m->seq[Module::si(0, 0)].store(48.f);
    ui.connect(tr, tr->m->def->outIndex("2"), a, a->m->def->inIndex("TRIG")); ui.connect(tr, tr->m->def->outIndex("TRIG"), b, b->m->def->inIndex("TRIG"));
    ui.connect(a, "OUT", out, "A"); ui.connect(b, "OUT", out, "B");
    run(eng, 2.1f); const int f2 = tr->m->fires[tr->m->def->outIndex("2")].load(), ft = tr->m->fires[0].load();
    const bool nm = !ui.modulate(lfo, tr, tr->m->def->paramIndex("estep"));
    const bool ok = f2 >= 6 && ft >= f2 + 1 && nm; if (!ok) fails++;
    std::printf("tracker columns: out 2 %d, trig %d, step jump unmodulatable %d%s\n", f2, ft, nm, ok ? "" : "  WRONG"); }
  // undo / redo only change what differs: a module that stays is the very same one (so it keeps playing)
  { ui.clearPatch(); ui.demoPatch(); run(eng, 0.3f); const std::string A = serializePatch(ui).dump(); Module* first = ui.mods[0]->m;
    ui.remove(ui.mods[3].get()); ui.set(ui.mods[0].get(), "speed", 5); ui.add("tracker", 10, 10); ui.unplug(ui.cables[0]); run(eng, 0.1f);
    const std::string B = serializePatch(ui).dump(); std::vector<std::pair<UiMod*, int>> sc;
    Json ja; Json::parse(A, ja); applyPatch(ui, ja, sc); run(eng, 0.1f); const bool u1 = serializePatch(ui).dump() == A && ui.mods[0]->m == first;
    Json jb; Json::parse(B, jb); applyPatch(ui, jb, sc); run(eng, 0.1f); const bool u2 = serializePatch(ui).dump() == B;
    std::printf("undo %s, redo %s\n", u1 ? "ok" : "WRONG", u2 ? "ok" : "WRONG"); if (!u1 || !u2) fails++; }
  if (argc > 1) {
    Json f; if (!Json::parse(readFile(argv[1]), f)) { std::printf("could not read %s\n", argv[1]); return 1; }
    const Json* data = patchIn(f); const std::string note = loadPatch(ui, *data); std::printf("loaded: %s\n", note.empty() ? "everything" : note.c_str());
    const Result r = run(eng, 20); std::printf("SESSION: peak %.3f rms %.4f cost %.1f%%%s\n", r.peak, r.rms, r.cost, r.bad ? " NOT FINITE" : "");
    const std::string back = serializePatch(ui).dump(); std::printf("tracker steps kept: %s\n", back.find("\"seq\":[[93") != std::string::npos ? "yes" : "NO");
  }
  return fails ? 1 : 0;
}
