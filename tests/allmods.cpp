#include <set>
#include <ctime>
#include <thread>
#include <cstdlib>
// Offline test: play every module type in a small patch for a few seconds; check the sound stays finite and
// reasonably loud, and report what each one costs. Then, if a session file is given, load it and render it.
#include "../src/ui/ui.h"
#include "../src/ui/session.h"
#include "../src/ui/help.h"
#include "../src/engine/fft.h"
namespace tb { bool euclidHit(int i, int steps, int hits, int rot); }
#include "../src/ui/theme.h"
#include <random>
#include <map>
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
    if (d->type == "stretch") ui.set(m, "src", 1);  // stretch a file
    if (d->type == "sample" || d->type == "looper" || d->type == "stretch") {   // give it a sound: a few seconds of a gliding tone with some noise
      AudioBuf* b = new AudioBuf(); b->sr = 44100; b->d.resize(44100 * 3); float ph = 0; Rng rr(5);
      for (size_t i = 0; i < b->d.size(); i++) { ph += (220 + i * 0.003f) / 44100.f; b->d[i] = 0.6f * std::sin(TAU * ph) + 0.05f * rr.bi(); }
      eng.setBuffer(m->m->id, b); }
    const Result r = run(eng, 4);
    const bool silent = r.peak < 0.005f && d->type != "audioin";   /* AUDIO IN: silent with nothing coming in */ if (r.bad || silent) fails++;
    std::printf("%-10s peak %.3f  rms %.4f  cost %5.2f%%%s%s\n", d->type.c_str(), r.peak, r.rms, r.cost, r.bad ? "  NOT FINITE" : "", silent ? "  SILENT" : "");
  }
  // every voice must follow the note it's given: its sound should repeat at the note's period (or a whole multiple of it)
  for (const ModuleDef* d : allDefs()) {
    if (d->cat != Cat::Voice || d->type == "sample" || d->type == "click" || d->type == "grains" || d->type == "euclid") continue;   // noise bursts, grain clouds and drums don't follow a note
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
    ui.connect(tr, tr->m->def->outIndex("CHN 2"), a, a->m->def->inIndex("TRIG")); ui.connect(tr, tr->m->def->outIndex("TRIG"), b, b->m->def->inIndex("TRIG"));
    ui.connect(a, "OUT", out, "A"); ui.connect(b, "OUT", out, "B");
    run(eng, 2.1f); const int f2 = tr->m->fires[tr->m->def->outIndex("CHN 2")].load(), ft = tr->m->fires[0].load();
    const bool nm = !ui.modulate(lfo, tr, tr->m->def->paramIndex("estep"));
    const bool ok = f2 >= 6 && ft >= f2 + 1 && nm; if (!ok) fails++;
    std::printf("tracker columns: out 2 %d, trig %d, step jump unmodulatable %d%s\n", f2, ft, nm, ok ? "" : "  WRONG"); }
  // DUPLICATE (as the app does it): a Tracker copy keeps its steps and settings, gets no cables
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* tr = ui.add("tracker", 0, 20); UiMod* v = ui.add("pluck", 100, 20); ui.connect(tr, 0, v, v->m->def->inIndex("TRIG"));
    ui.set(tr, "len", 7); tr->m->seq[Module::si(3, 2)].store(65); tr->m->mute.store(true);
    Json data = serializePatch(ui); Json* mods = data.getm("mods"); int maxId = 0; Json copy; for (auto& m : mods->a) { maxId = std::max(maxId, (int)m.num("id")); if ((int)m.num("id") == tr->m->id) copy = m; }
    copy.set("id", Json::number(maxId + 1)); copy.set("x", Json::number(12)); mods->push(copy); std::vector<std::pair<UiMod*, int>> sc; applyPatch(ui, data, sc);
    UiMod* d = ui.byId(maxId + 1); int cab = 0; for (auto& c : ui.cables) if (c.from == maxId + 1 || c.to == maxId + 1) cab++;
    const bool ok = d && d->m->def->type == "tracker" && d->m->seq[Module::si(3, 2)].load() == 65 && (int)d->m->p[d->m->def->paramIndex("len")].load() == 7 && d->m->mute.load() && cab == 0 && ui.cables.size() == 1;
    if (!ok) fails++; std::printf("duplicate: %s%s\n", d ? "made" : "none", ok ? "" : "  WRONG"); }
  // NOTE LOOPER: CHANCE 0 plays nothing; RANDOM RHYTHM keeps the notes, moves them onto the grid, first on the downbeat; UNDO RANDOM puts them back
  { ui.clearPatch(); ui.add("out", 300, 20); UiMod* nl = ui.add("nloop", 0, 20); UiMod* v = ui.add("pluck", 100, 20); ui.connect(nl, 0, v, v->m->def->inIndex("TRIG"));
    Module* m = nl->m; const float T[6] = {0.1f, 1.3f, 1.3f, 2.7f, 5.2f, 6.9f}; for (int i = 0; i < 6; i++) { m->ev[i * 4].store(T[i]); m->ev[i * 4 + 1].store(60.f + i); m->ev[i * 4 + 2].store(0.8f); m->ev[i * 4 + 3].store(0); } m->evN.store(6);
    ui.set(nl, "lenb", 8); ui.set(nl, "chance", 0); run(eng, 3); const int f0 = m->fires[0].load(); ui.set(nl, "chance", 1); run(eng, 3); const int f1 = m->fires[0].load() - f0;
    eng.action(m->id, "rrhythm"); run(eng, 0.05f); bool grid = true; float first = 99; int chordSame = m->ev[1 * 4].load() == m->ev[2 * 4].load();
    for (int i = 0; i < 6; i++) { const float t = m->ev[i * 4].load(); first = std::min(first, t); if (std::fabs(t * 4 - std::round(t * 4)) > 1e-3f) grid = false; }
    eng.action(m->id, "urand"); run(eng, 0.05f); bool back = true; for (int i = 0; i < 6; i++) if (std::fabs(m->ev[i * 4].load() - T[i]) > 1e-4f) back = false;
    const bool ok = f0 == 0 && f1 > 0 && grid && first == 0 && chordSame && back; if (!ok) fails++;
    std::printf("note looper: chance 0 -> %d, 1 -> %d, random rhythm on grid %d, first %.2f, chord kept %d, undo %d%s\n", f0, f1, grid, first, chordSame, back, ok ? "" : "  WRONG"); }
  // NOTE LOOPER: RANDOM RHYTHM moves dense, already-even notes too (20 notes on the 1/16s of 4 beats)
  { ui.clearPatch(); ui.add("out", 300, 20); UiMod* nl = ui.add("nloop", 0, 20); Module* m = nl->m; ui.set(nl, "lenb", 4); ui.set(nl, "quant", 1);
    for (int i = 0; i < 20; i++) { m->ev[i * 4].store(i * 0.2f); m->ev[i * 4 + 1].store(60.f + i % 7); m->ev[i * 4 + 2].store(0.8f); m->ev[i * 4 + 3].store(0); } m->evN.store(20);
    int moved = 0; for (int r = 0; r < 3; r++) { float before[20]; for (int i = 0; i < 20; i++) before[i] = m->ev[i * 4].load(); eng.action(m->id, "rrhythm"); run(eng, 0.02f);
      int ch = 0; for (int i = 0; i < 20; i++) if (std::fabs(m->ev[i * 4].load() - before[i]) > 1e-4f) ch++; if (ch > 0) moved++; }
    const bool ok = moved == 3; if (!ok) fails++; std::printf("random rhythm, dense: changed %d of 3 presses%s\n", moved, ok ? "" : "  WRONG"); }
  // MUTE SEQUENCER: lane 1 (five on, three off) switches a module; HOLD TO PLAY writes lane 2 from the button
  { ui.clearPatch(); ui.add("out", 300, 20); UiMod* ms = ui.add("mutes", 0, 20); UiMod* v = ui.add("pluck", 200, 20); ui.set(ms, "sync", 0); ui.set(ms, "bpm", 120); ui.set(ms, "div", 1);   // 1/8 at 120: a step each 0.25 s
    ui.connect(ms, 0, v, SWITCH_PORT); int onSteps = 0, offSteps = 0;
    for (int k = 0; k < 16; k++) { run(eng, 0.25f); if (v->m->mute.load()) offSteps++; else onSteps++; }
    ui.set(ms, "rlane", 1); ui.set(ms, "len2", 8); run(eng, 0.01f); eng.action(ms->m->id, "hold:1"); run(eng, 0.5f); eng.action(ms->m->id, "hold:0"); run(eng, 2.2f);
    int lit = 0; for (int k = 0; k < 8; k++) if (ms->m->seq[Module::si(k, 1)].load() >= 0) lit++;
    const bool ok = onSteps >= 9 && onSteps <= 11 && offSteps >= 5 && lit >= 2 && lit <= 3; if (!ok) fails++;
    std::printf("mute sequencer: on %d off %d of 16 steps, held for 2 steps -> %d lit%s\n", onSteps, offSteps, lit, ok ? "" : "  WRONG"); }
  // TOMBOLA: its balls' notes go into the session and come back
  { ui.clearPatch(); ui.add("out", 300, 20); UiMod* tb = ui.add("tombola", 0, 20); for (float nt : {60.f, 64.f, 67.f}) { Event ev; ev.note = nt; eng.trigger(tb->m->id, tb->m->def->inIndex("ADD"), ev); }
    run(eng, 0.2f); const std::string S = serializePatch(ui).dump(); Json j; Json::parse(S, j); ui.clearPatch(); loadPatch(ui, j); run(eng, 0.2f);
    int balls = 0; for (auto& um : ui.mods) if (um->m->def->type == "tombola") balls = (int)um->m->disp[2].load();
    const bool ok = balls == 3; if (!ok) fails++; std::printf("tombola balls kept: %d%s\n", balls, ok ? "" : "  WRONG"); }
  // DUCKER: a loud sound into SIDE pushes IN down
  { float lv[2]; for (int k = 0; k < 2; k++) { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* tn = ui.add("tone", 0, 20); UiMod* dk = ui.add("ducker", 100, 20); UiMod* clk = ui.add("clock", 0, 120); ui.set(clk, "sync", 0);
      ui.connect(clk, "PULSE", tn, "TRIG"); ui.connect(tn, "OUT", dk, "IN"); ui.connect(dk, "OUT", out, "A"); ui.set(dk, "depth", 1);
      if (k) { UiMod* ns = ui.add("pluck", 0, 200); UiMod* c2 = ui.add("clock", 0, 260); ui.set(c2, "sync", 0); ui.set(c2, "bpm", 240); ui.connect(c2, "PULSE", ns, "TRIG"); ui.connect(ns, "OUT", dk, "SIDE"); }
      run(eng, 1); lv[k] = dk->m->outLev[0].load(); Result r = run(eng, 3); lv[k] = r.rms; }
    const bool ok = lv[1] < lv[0] * 0.8f; if (!ok) fails++; std::printf("ducker side: without %.4f, with %.4f%s\n", lv[0], lv[1], ok ? "" : "  WRONG"); }
  // VOICES: MONO plays one note at a time (POLY overlaps them); GLIDE slides between notes
  { float rm[2]; for (int k = 0; k < 2; k++) { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* v = ui.add("pad", 100, 20); ui.connect(v, "OUT", out, "A"); ui.set(v, "poly", (float)k);
      for (int i = 0; i < 6; i++) { Event ev; ev.note = 48.f + i * 4; eng.trigger(v->m->id, 0, ev); run(eng, 0.05f); } rm[k] = run(eng, 0.3f).rms; }
    const bool ok = rm[1] < rm[0] * 0.7f; if (!ok) fails++; std::printf("voices: poly rms %.4f, mono rms %.4f%s\n", rm[0], rm[1], ok ? "" : "  WRONG"); }
  // FOLLOWER: CLOCK fires on each hit of a beat
  { ui.clearPatch(); ui.add("out", 300, 20); UiMod* clk = ui.add("clock", 0, 20); ui.set(clk, "sync", 0); ui.set(clk, "bpm", 120); UiMod* v = ui.add("pluck", 100, 20); UiMod* fo = ui.add("follow", 200, 20);
    ui.connect(clk, "PULSE", v, "TRIG"); ui.connect(v, "OUT", fo, "IN"); run(eng, 4); const int hits = fo->m->fires[1].load(), beats = clk->m->fires[0].load();
    const bool ok = hits >= beats - 2 && hits <= beats + 2; if (!ok) fails++; std::printf("follower clock: %d hits for %d notes%s\n", hits, beats, ok ? "" : "  WRONG"); }
  // LISTEN: hears the tempo of a beat
  { ui.clearPatch(); ui.add("out", 300, 20); UiMod* clk = ui.add("clock", 0, 20); ui.set(clk, "sync", 0); ui.set(clk, "bpm", 100); UiMod* v = ui.add("click", 100, 20); UiMod* li = ui.add("listen", 200, 20);
    ui.connect(clk, "PULSE", v, "TRIG"); ui.connect(v, "OUT", li, "IN"); run(eng, 10); const float bp = li->m->disp[19].load();
    const bool ok = std::fabs(bp - 100) < 3 || std::fabs(bp - 50) < 2 || std::fabs(bp - 200) < 4; if (!ok) fails++; std::printf("listen tempo: %.0f BPM (clock 100), sure %.0f%%%s\n", bp, li->m->disp[20].load() * 100, ok ? "" : "  WRONG"); }
  // CHANNELS STEREO: a sound with only a left side plays on the left
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* sm = ui.add("sample", 0, 20); ui.connect(sm, "OUT", out, "A");
    AudioBuf* b = new AudioBuf(); b->sr = 48000; b->d.resize(48000); b->r.assign(48000, 0.f); for (int i = 0; i < 48000; i++) b->d[i] = 0.5f * std::sin(i * 0.05f);
    eng.setBuffer(sm->m->id, b, 0); run(eng, 0.3f); float l = 0, r = 0; std::vector<float> L(256), R(256); for (int k = 0; k < 40; k++) { eng.render(L.data(), R.data(), 256); for (int i = 0; i < 256; i++) { l = std::max(l, std::fabs(L[i])); r = std::max(r, std::fabs(R[i])); } }
    const bool ok = l > 0.05f && r < l * 0.05f; if (!ok) fails++; std::printf("stereo sample: left %.3f right %.4f%s\n", l, r, ok ? "" : "  WRONG"); }
  // KEY JOURNEY: a sequence of keys into a sequencer's KEY socket, one a bar; saved and loaded with the steps; unplugged, the sequencer's own
  { ui.clearPatch(); ui.add("out", 300, 20); eng.tempo.bpm.store(120); eng.key.root.store(0); eng.key.scale.store(0);
    UiMod* j = ui.add("journey", 0, 20); UiMod* cl = ui.add("clump", 100, 20); ui.set(j, "every", 1); ui.set(j, "steps", 3);
    const int keysWanted[3] = {2, 7, 9}; for (int i = 0; i < 8; i++) { j->m->seq[Module::si(i, 0)].store(i < 3 ? (float)keysWanted[i] : -1.f); j->m->seq[Module::si(i, 0) + 1].store(i == 2 ? 10.f + 4 : 0.8f); }   // D, G, A (step 3 in scale 4)
    const bool plugged = ui.connect(j, 0, cl, KEY_PORT);
    run(eng, 0.2f); const int k0 = cl->m->keyIn.load(); run(eng, 2.0f); const int k1 = cl->m->keyIn.load(); run(eng, 2.0f); const int k2 = cl->m->keyIn.load(), s2 = cl->m->scaleIn.load(); run(eng, 2.0f); const int k3 = cl->m->keyIn.load();
    const std::string S = serializePatch(ui).dump(); Json jj; Json::parse(S, jj); ui.clearPatch(); loadPatch(ui, jj); UiMod* j2 = nullptr; for (auto& um : ui.mods) if (um->m->def->type == "journey") j2 = um.get();
    const bool steps = j2 && std::lround(j2->m->seq[Module::si(1, 0)].load()) == 7 && std::lround(j2->m->seq[Module::si(2, 0) + 1].load()) == 14;
    const bool ok = plugged && k0 == 2 && k1 == 7 && k2 == 9 && s2 == 4 && k3 == 2 && steps; if (!ok) fails++;
    std::printf("key journey: %d %d %d (scale %d) %d (D G A, D again), steps saved %d%s\n", k0, k1, k2, s2, k3, steps, ok ? "" : "  WRONG"); }
  // KEY JOURNEY: SUGGEST fills smooth steps (each a fifth from the last); SHUFFLE plays each once before repeating; an old session gets steps
  { ui.clearPatch(); eng.key.root.store(2); eng.key.scale.store(0); UiMod* j = ui.add("journey", 0, 20); ui.set(j, "steps", 8); run(eng, 0.05f); eng.action(j->m->id, "suggest"); run(eng, 0.05f);
    bool smooth = true; for (int i = 0; i < 8; i++) { const int a = (int)std::lround(j->m->seq[Module::si(i, 0)].load()), b2 = (int)std::lround(j->m->seq[Module::si((i + 1) % 8, 0)].load()); const int dk = ((b2 - a) % 12 + 12) % 12; if (!(dk == 0 || dk == 5 || dk == 7)) smooth = false; }   /* each a fifth from the last, round the end too */
    ui.set(j, "order", 3); ui.set(j, "moves", 1); ui.set(j, "every", 1); eng.tempo.bpm.store(240); std::set<int> seen; for (int k = 0; k < 48; k++) { run(eng, 0.25f); seen.insert((int)j->m->disp[8].load()); }   /* twelve bars: SHUFFLE has played every step by then */
    const char* old = R"({"v":2,"w":2,"mods":[{"id":1,"type":"journey","x":0,"y":20,"p":{"path":2,"dir":0,"every":3}}],"cables":[]})";
    Json jj; Json::parse(old, jj); ui.clearPatch(); loadPatch(ui, jj); run(eng, 0.1f); int filled = 0; for (auto& um : ui.mods) if (um->m->def->type == "journey") for (int i = 0; i < 4; i++) if (um->m->seq[Module::si(i, 0)].load() >= 0) filled++;
    const bool ok = smooth && seen.size() == 8 && filled == 4; if (!ok) fails++; eng.key.root.store(2); eng.key.scale.store(0);
    std::printf("key journey: suggest smooth %d, shuffle visited %zu of 8 steps in 12 bars, an old one gets %d steps%s\n", smooth, seen.size(), filled, ok ? "" : "  WRONG"); }
  // SOUND EDITOR: FADE OUT twice is steeper, DELETE shortens with a smooth join, NORMALISE, its UNDO; app UNDO leaves it open
  { ui.clearPatch(); UiMod* sm = ui.add("sample", 0, 20); std::vector<float> L(48000, 0.25f); ui.openEditor(sm->m->id, 0, "TEST", std::move(L), 48000);
    Canvas cv; cv.resize(400, 300); auto press = [&](int id) { cv.beginFrame(); ui.draw(cv, 0.016f); for (auto& b : ui.edBtns) if (b.id == id) { ui.pointerDown(b.x + 2, b.y + 2); ui.pointerUp(b.x + 2, b.y + 2); return true; } return false; };
    ui.ed.a = 0.5; ui.ed.b = 1.0; press(11); const float f1 = ui.ed.d[36000]; press(11); const float f2 = ui.ed.d[36000];   // halfway through the fade: 1/2, then 1/4
    press(6); press(6); ui.ed.a = 0.25; ui.ed.b = 0.5; press(13); const size_t n1 = ui.ed.d.size(); float jump = 0; for (size_t i = 1; i < n1; i++) jump = std::max(jump, std::fabs(ui.ed.d[i] - ui.ed.d[i - 1]));
    ui.ed.a = 0; ui.ed.b = 0.2; press(9); const float nm = ui.ed.d[4800]; press(6); const float un = ui.ed.d[4800];
    ui.pointerDown(5, 3); const bool stillOpen = ui.ed.open; press(16); const bool asks = ui.ed.open; press(16); const bool shut = !ui.ed.open;
    const bool ok = std::fabs(f1 - 0.125f) < 0.01f && std::fabs(f2 - 0.0625f) < 0.01f && std::abs((long)n1 - 36000) < 400 && jump < 0.01f && std::fabs(nm - 0.95f) < 0.01f && std::fabs(un - 0.25f) < 0.01f && stillOpen && asks && shut;
    if (!ok) fails++;
    std::printf("sound editor: fades %.3f %.3f, delete -> %zu (biggest step %.4f), normalise %.2f, undo %.2f, open after bar tap %d, close asks %d then closes %d%s\n", f1, f2, n1, jump, nm, un, stillOpen, asks, shut, ok ? "" : "  WRONG"); }
  // taking away a playing SAMPLE: it's gone in about a second (not left ringing for 20)
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* sm = ui.add("sample", 0, 20); ui.connect(sm, "OUT", out, "A"); AudioBuf* b = new AudioBuf(); b->d.assign(48000 * 30, 0.3f); eng.setBuffer(sm->m->id, b);
    ui.set(sm, "trig", 1); eng.trigger(sm->m->id, 0, Event{}); const float pk0 = run(eng, 0.5f).peak; const int id = sm->m->id; ui.removeGently(sm); float gone = -1;
    for (int k = 0; k < 40; k++) { run(eng, 0.1f); if (!eng.byId(id)) { gone = (k + 1) * 0.1f; break; } }
    const bool ok = gone > 0 && gone < 1.6f; if (!ok) fails++; std::printf("playing sample taken away: gone after %.1fs (was playing at %.2f)%s\n", gone, pk0, ok ? "" : "  WRONG"); }
  // every slider: its ends and default map to and from the slider without NaN (an exponential slider can't start at 0)
  { int bad = 0; std::string which; for (auto* d : allDefs()) for (auto& q : d->params) { if (!q.opts.empty() || q.act) continue;
      for (float v : {q.min, q.max, q.def}) { const float n = q.toNorm(v), b = q.fromNorm(n); if (!std::isfinite(n) || !std::isfinite(b) || std::fabs(b - v) > 1e-3f * std::max(1.f, std::fabs(v))) { bad++; which += " " + d->type + "." + q.key; break; } } }
    if (bad) fails++; std::printf("sliders map cleanly: %s%s\n", bad ? "NO:" : "all", which.c_str()); }
  // SAMPLER: REPLACE records what comes in; 1 BAR stops by itself; OVERDUB layers on (louder each pass); MONITOR WHILE REC
  { ui.clearPatch(); eng.tempo.bpm.store(120); UiMod* out = ui.add("out", 300, 20); UiMod* src = ui.add("tone", 0, 20); UiMod* sm = ui.add("sample", 100, 20);
    ui.connect(src, "OUT", sm, "IN"); ui.connect(sm, "OUT", out, "A");
    { const int at = src->m->def->paramIndex("atk"); src->m->p[at + 2].store(1); src->m->p[at + 3].store(60); }
    Event nt; nt.note = 60; nt.vel = 1; eng.trigger(src->m->id, 0, nt); run(eng, 0.3f);
    AudioBuf* rb = new AudioBuf(); rb->sr = 48000; rb->d.assign(48000 * 60, 0.f); rb->r.assign(48000 * 60, 0.f); eng.setBuffer(sm->m->id, rb, 99); run(eng, 0.05f);
    ui.set(sm, "rec", 1); const float hearRec = run(eng, 1.0f).rms; ui.set(sm, "rec", 0); run(eng, 0.05f);
    const int take1 = (int)sm->m->disp[42].load(); const float n1 = sm->m->disp[43].load(); double e1 = 0; for (long i = 0; i < (long)n1; i++) e1 += rb->d[i] * rb->d[i]; const float rms1 = (float)std::sqrt(e1 / std::max(1.f, n1));
    const float quietAfter = run(eng, 0.3f).rms;   // MONITOR WHILE REC: quiet again (no sound kept here, in the test)
    eng.action(sm->m->id, "taken"); ui.set(sm, "reclen", 1); ui.set(sm, "rec", 1); run(eng, 2.3f); const float n2 = sm->m->disp[43].load(); const bool stopped = sm->m->p[sm->m->def->paramIndex("rec")].load() < 0.5f;
    eng.action(sm->m->id, "taken"); ui.set(sm, "reclen", 0);
    AudioBuf* one = new AudioBuf(); one->sr = 48000; one->d.assign(48000, 0.f); for (int i = 0; i < 48000; i++) one->d[i] = 0.3f * std::sin(i * 0.01f); eng.setBuffer(sm->m->id, one, 0);
    ui.set(sm, "recmode", 1); ui.set(sm, "keep", 0.8f); run(eng, 0.1f); ui.set(sm, "rec", 1); run(eng, 2.5f); ui.set(sm, "rec", 0); run(eng, 0.1f);
    const float n3 = sm->m->disp[43].load(); double e3 = 0; for (long i = 0; i < 48000; i++) e3 += rb->d[i] * rb->d[i]; const float rms3 = (float)std::sqrt(e3 / 48000);
    const bool ok = take1 == 1 && std::fabs(n1 - 48000) < 600 && rms1 > 0.05f && hearRec > 0.02f && quietAfter < 0.005f && std::fabs(n2 - 96000) < 300 && stopped && std::fabs(n3 - 48000) < 2 && rms3 > rms1 * 0.9f;
    if (!ok) fails++;
    std::printf("sampler: take %d of %.0f samples (rms %.3f), heard while recording %.3f, quiet after %.4f; 1 bar -> %.0f, stopped %d; overdub %.0f (rms %.3f)%s\n", take1, n1, rms1, hearRec, quietAfter, n2, stopped, n3, rms3, ok ? "" : "  WRONG"); }
  // an old session's TAPE LOOP comes back as a SAMPLER set to OVERDUB, its cables kept
  { const char* old = R"({"v":2,"w":2,"mods":[{"id":1,"type":"out","x":300,"y":20,"p":{}},{"id":2,"type":"tloop","x":0,"y":20,"p":{"keep":0.6,"len":8,"level":0.5}},{"id":3,"type":"tone","x":0,"y":120,"p":{}}],
      "cables":[{"f":2,"fp":"OUT","t":1,"tp":"A","ty":"audio"},{"f":3,"fp":"OUT","t":2,"tp":"IN","ty":"audio"}]})";
    Json j; Json::parse(old, j); ui.clearPatch(); loadPatch(ui, j); UiMod* sm = nullptr; for (auto& um : ui.mods) if (um->m->def->type == "sample") sm = um.get();
    int cab = 0; for (auto& c : ui.cables) if (sm && (c.to == sm->m->id || c.from == sm->m->id)) cab++;
    const bool ok = sm && std::fabs(sm->m->p[sm->m->def->paramIndex("keep")].load() - 0.6f) < 1e-4f && sm->m->p[sm->m->def->paramIndex("recmode")].load() > 0.5f && sm->m->p[sm->m->def->paramIndex("len")].load() > 0.99f && cab == 2;
    if (!ok) fails++; std::printf("old tape loop -> sampler %d, keep and overdub kept, %d cables%s\n", sm != nullptr, cab, ok ? "" : "  WRONG"); }
  // GRAIN LOOPER LIVE: grains straight from what comes in; FROZEN it keeps playing the window after the input stops
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* src = ui.add("tone", 0, 20); UiMod* gl = ui.add("looper", 100, 20); ui.connect(src, "OUT", gl, "IN"); ui.connect(gl, "OUT", out, "A");
    ui.set(gl, "len", 4); const int at = src->m->def->paramIndex("atk"); src->m->p[at + 2].store(1); src->m->p[at + 3].store(3); src->m->p[at + 4].store(0.05f);
    Event nt; nt.note = 64; nt.vel = 1; eng.trigger(src->m->id, 0, nt); run(eng, 2.5f); const float liveR = run(eng, 0.5f).rms;
    ui.set(gl, "freeze", 1); run(eng, 3.0f); const float srcGone = src->m->disp[0].load(); (void)srcGone; const float frozenR = run(eng, 1.0f).rms;
    ui.set(gl, "freeze", 0); run(eng, 5.0f); const float afterR = run(eng, 1.0f).rms;   // listening again: the input is silent now, so the cloud dies away
    const bool def = gl->m->p[0].load() < 0.5f;
    const char* old = R"({"v":2,"w":2,"mods":[{"id":1,"type":"out","x":300,"y":20,"p":{}},{"id":2,"type":"looper","x":0,"y":20,"p":{"len":6}}],"cables":[]})";
    Json j; Json::parse(old, j); ui.clearPatch(); loadPatch(ui, j); bool held = false; for (auto& um : ui.mods) if (um->m->def->type == "looper") held = um->m->p[0].load() > 0.5f;
    const bool ok = def && liveR > 0.01f && frozenR > 0.01f && afterR < frozenR * 0.2f && held; if (!ok) fails++;
    std::printf("grain looper live: %.3f, frozen after the input stops %.3f, let go %.4f, new ones LIVE %d, old sessions HELD %d%s\n", liveR, frozenR, afterR, def, held, ok ? "" : "  WRONG"); }
  // STRETCH: the 2.7S window sounds, and its big windows (worked out on their own thread) never hold an audio block up
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* src = ui.add("pad", 0, 20); UiMod* st = ui.add("stretch", 100, 20); ui.connect(src, "OUT", st, "IN"); ui.connect(st, "OUT", out, "A");
    ui.set(st, "win", std::getenv("TB_WIN") ? std::atoi(std::getenv("TB_WIN")) : 6); ui.set(st, "amt", 8); const int at = src->m->def->paramIndex("atk"); src->m->p[at].store(0.01f); src->m->p[at + 2].store(1); src->m->p[at + 3].store(20);
    Event nt; nt.note = 57; nt.vel = 1; eng.trigger(src->m->id, 0, nt);
    std::vector<float> L(64), R(64); double worst = 0, worst2 = 0; double e2 = 0; long n = 0;
    const auto tStart = std::chrono::steady_clock::now();   // at four times real speed (the worker needs some real time, as it gets when the app plays)
    for (int k = 0; k < 48000 * 10 / 64; k++) { while (std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count() < k * 64 / 48000.0 / 4) std::this_thread::sleep_for(std::chrono::microseconds(200));
      timespec c0, c1; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c0); eng.render(L.data(), R.data(), 64); clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c1);   // this thread's own time (not the other's)
      const double ms = (c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6;
      if (k > 100) { if (ms > worst) { worst2 = worst; worst = ms; } else worst2 = std::max(worst2, ms); } /* the second slowest: one stray hiccup of the machine isn't the module's */ if (k > 48000 * 6 / 64) for (int i = 0; i < 64; i++) { e2 += L[i] * L[i]; n++; } }
    const float rms = (float)std::sqrt(e2 / std::max(1L, n)); const bool whole = std::fabs(st->m->def->params[st->m->def->paramIndex("amt")].fromNorm(0.37f) - std::round(st->m->def->params[st->m->def->paramIndex("amt")].fromNorm(0.37f))) < 1e-4f;
    const bool ok = rms > 0.002f && worst2 < 1.3 && whole; if (!ok) fails++;
    std::printf("stretch 2.7s window: rms %.4f, slowest 64-sample blocks %.2f, %.2f ms of work (a block lasts 1.33), whole-number STRETCH %d%s\n", rms, worst, worst2, whole, ok ? "" : "  WRONG"); }
  // ROW ORDER: VOICES first, GLIDE under PITCH, LEVEL last (PAN and WIDTH just above), on every module; nothing cut off
  { ui.clearPatch(); std::string bad;
    for (auto* d : allDefs()) { if (d->type == "out" || d->fixed) continue; UiMod* u = ui.add(d->type, 0, 0); const auto o = ui.shownRows(*u); auto at = [&](const char* k) { const int j = d->paramIndex(k); for (size_t r = 0; r < o.size(); r++) if ((int)o[r] == j) return (int)r; return -1; };
      const int lv = at("level"), pv = at("poly"), pi_ = at("pitch"), gl = d->type == "formant" ? at("pglide") : at("glide"), pn = at("pan");   /* FORMANT's "glide" is its MOVE TO */
      if (lv >= 0 && lv != (int)o.size() - 1) bad += " " + d->type + "(level)";
      if (pv > 0) bad += " " + d->type + "(voices)";
      if (gl >= 0 && pi_ >= 0 && d->type != "tone" && gl != pi_ + 1) bad += " " + d->type + "(glide)";
      if (pn >= 0 && lv >= 0 && pn < lv - 2) bad += " " + d->type + "(pan)"; ui.remove(u); }
    const auto cut = ui.cutOff(); std::string cs; for (auto& c : cut) cs += "  [" + c + "]";
    const bool ok = bad.empty() && cut.empty(); if (!ok) fails++; std::printf("row order:%s; cut off:%s%s\n", bad.empty() ? " all as they should be" : bad.c_str(), cut.empty() ? " nothing" : cs.c_str(), ok ? "" : "  WRONG"); }
  // AUDIO IN: what the input brings, from the channels chosen, at GAIN
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* ai = ui.add("audioin", 0, 20); ui.connect(ai, "OUT", out, "A");
    AudioInput& I = eng.input; I.live.store(false); I.ch.store(4); I.frames = 65536; I.buf.assign(I.frames * 4, 0.f); I.w.store(0); I.lag.store(512); I.live.store(true);
    std::vector<float> blk(256 * 4); float got[4] = {}; const float want[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    auto feed = [&](float secs) { for (int k = 0; k < (int)(secs * 48000 / 256); k++) { for (int i = 0; i < 256; i++) for (int c = 0; c < 4; c++) blk[i * 4 + c] = want[c]; I.write(blk.data(), 256); run(eng, 256 / 48000.f); } };
    feed(0.2f); { std::vector<float> L(64), R(64); eng.render(L.data(), R.data(), 64); got[0] = ai->m->disp[0].load(); got[1] = ai->m->disp[1].load(); }
    ui.set(ai, "input", 4); feed(0.2f); got[2] = ai->m->disp[0].load(); ui.set(ai, "input", 6); feed(0.1f); const float missing = ai->m->disp[2].load();
    ui.set(ai, "input", 3); ui.set(ai, "gain", 6); feed(0.3f); got[3] = ai->m->disp[0].load(); I.live.store(false);
    const bool ok = std::fabs(got[0] - 0.1f) < 0.01f && std::fabs(got[1] - 0.2f) < 0.01f && std::fabs(got[2] - 0.3f) < 0.01f && missing > 1.5f && std::fabs(got[3] - 0.3f * 1.995f) < 0.03f; if (!ok) fails++;
    std::printf("audio in: 1+2 %.2f %.2f, 3 %.2f, 5 on a 4-in device -> %s, +6 dB %.3f%s\n", got[0], got[1], got[2], missing > 1.5f ? "not there" : "?", got[3], ok ? "" : "  WRONG"); }
  // ORBITS MUTATE: at 0 the notes stay put; turned up they move (never out of RANGE)
  { ui.clearPatch(); UiMod* ob = ui.add("orbits", 0, 20); ui.set(ob, "speed", 1); run(eng, 10); bool moved0 = false; for (int r = 0; r < 8; r++) if (std::fabs(ob->m->disp[17 + r].load()) > 0.5f) moved0 = true;
    ui.set(ob, "mutate", 1); run(eng, 20); int moved = 0; for (int r = 0; r < 5; r++) if (std::fabs(ob->m->disp[17 + r].load()) > 0.5f) moved++;
    const bool ok = !moved0 && moved >= 2; if (!ok) fails++; std::printf("orbits mutate: still at 0 %d, rings moved at full %d of 5%s\n", !moved0, moved, ok ? "" : "  WRONG"); }
  // HELP: every module, setting and socket has a note; the pointer resting on things finds them
  { std::string missing; int n = 0;
    for (auto* d : allDefs()) { if (helpModule(d->type).empty()) missing += " " + d->type; n++;
      for (auto& q : d->params) { if (helpParam(d, q).empty()) missing += " " + d->type + "." + q.key; n++; }
      for (int k = 0; k < (int)d->ins.size(); k++) { if (helpPort(d, false, k).empty()) missing += " " + d->type + "<" + d->ins[k].name; n++; }
      for (int k = 0; k < (int)d->outs.size(); k++) { if (helpPort(d, true, k).empty()) missing += " " + d->type + ">" + d->outs[k].name; n++; } }
    for (const char* o : {"M", "S", "B", "ON", "FOLD", "SWITCH", "KEYSOCKET", "CLOSE", "MODRING", "CABLE", "+ ADD", "SESSIONS", "SETTINGS", "UNDO", "REDO", "FOLD ALL", "OPEN ALL", "?", "FULLSCREEN", "WINDOW", "ZOOM", "AUDIO"})
      { if (helpOther(o).empty()) missing += std::string(" ") + o; n++; }
    ui.clearPatch(); UiMod* pd = ui.add("pad", 100, 40); UiMod* lf = ui.add("lfo", 10, 40); ui.connect(lf, 0, pd, pd->m->def->paramIndex("cut"));
    Canvas cv; cv.resize(400, 300); cv.beginFrame(); ui.draw(cv, 0.016f);
    const std::string onTitle = ui.helpAt(110 - ui.viewX, 43 - ui.viewY); std::string onRow; for (int yy = 20; yy < 90 && onRow.find("JUMP STRAIGHT TO -24") == std::string::npos; yy++) onRow = ui.helpAt(140 - ui.viewX + 0.f, (float)(pd->y + yy - ui.viewY));   /* PITCH, a row or two down */
    std::string stepNote; { UiMod* kj = ui.add("journey", 200, 150); ui.set(kj, "steps", 2); kj->m->seq[Module::si(0, 0)].store(2); kj->m->seq[Module::si(1, 0)].store(7); cv.beginFrame(); ui.draw(cv, 0.016f);
      for (int yy = 0; yy < 120 && stepNote.find("SMOOTH") == std::string::npos; yy++) stepNote = ui.helpAt((float)(kj->x - ui.viewX + 3 + 20), (float)(kj->y - ui.viewY + yy)); }   /* step 2 (G after D): one note changes */
    ui.setAddMenu(true); cv.beginFrame(); ui.draw(cv, 0.016f); std::string inMenu; { int mx, my; if (ui.menuItemAt("reverb", mx, my)) inMenu = ui.helpAt((float)mx + 2, (float)my + 2); } ui.setAddMenu(false);
    const bool ok = stepNote.find("SMOOTH: ONE NOTE") != std::string::npos && inMenu.rfind("Reverb|", 0) == 0 && missing.empty() && onTitle.rfind("Pad|", 0) == 0 && onRow.find("JUMP STRAIGHT TO -24") != std::string::npos;   /* the row under it is PITCH: its note tells of the octave jumps */ if (!ok) fails++;
    std::printf("help: %d notes, missing:%s; title %s; a row %s%s\n", n, missing.empty() ? " none" : missing.c_str(), onTitle.substr(0, 12).c_str(), onRow.substr(0, 20).c_str(), ok ? "" : "  WRONG"); }
  // MI VOICES: the chosen model's own names on its three rows (and help), and the LPG rows only where Plaits uses its gate
  { ui.clearPatch(); UiMod* mi = ui.add("mi", 0, 20); Canvas cv; cv.resize(300, 300);
    auto rows = [&] { return ui.shownRows(*mi).size(); }; ui.set(mi, "voice", 0); const size_t nA = rows(); ui.set(mi, "voice", 18); const size_t nK = rows();
    cv.beginFrame(); ui.draw(cv, 0.016f); std::string note; const auto o = ui.shownRows(*mi);
    for (size_t r = 0; r < o.size(); r++) if (mi->m->def->params[o[r]].key == "harm") { for (int y = 0; y < 200 && note.empty(); y++) { const std::string h = ui.helpAt((float)(mi->x - ui.viewX + 30), (float)(mi->y - ui.viewY + y)); if (h.rfind("DRIVE|", 0) == 0) note = h; } }
    const bool ok = nA == nK + 2 && !note.empty(); if (!ok) fails++; std::printf("mi voices: ANALOG %zu rows, KICK %zu (no LPG), KICK's first control: %s%s\n", nA, nK, note.substr(0, 40).c_str(), ok ? "" : "  WRONG"); }
  // SAMPLER LOOP keeps time: a pass every LENGTH exactly, however long FADE is (it used to come round FADE early)
  { ui.clearPatch(); UiMod* out = ui.add("out", 300, 20); UiMod* sm = ui.add("sample", 0, 20); ui.connect(sm, "OUT", out, "A");
    AudioBuf* b = new AudioBuf(); b->sr = 48000; b->d.assign(48000, 0.f); for (int i = 0; i < 2400; i++) b->d[i] = 0.8f; eng.setBuffer(sm->m->id, b, 0); ui.set(sm, "fade", 0.2f);
    std::vector<float> L(64), R(64); std::vector<long> starts; bool above = false; long n = 0;
    for (int k = 0; k < 48000 * 6 / 64; k++) { eng.render(L.data(), R.data(), 64); for (int i = 0; i < 64; i++, n++) { const bool on = std::fabs(L[i]) > 0.05f; if (on && !above) starts.push_back(n); above = on; } }
    double per = 0; int cnt = 0; for (size_t i = 2; i < starts.size(); i++) { per += starts[i] - starts[i - 1]; cnt++; } per = cnt ? per / cnt / 48000.0 : 0;
    const bool ok = std::fabs(per - 1.0) < 0.003; if (!ok) fails++; std::printf("sampler loop: a pass every %.4fs (its length 1s, FADE 0.2s)%s\n", per, ok ? "" : "  WRONG"); }
  // undo / redo only change what differs: a module that stays is the very same one (so it keeps playing)
  { ui.clearPatch(); ui.demoPatch(); run(eng, 0.3f); const std::string A = serializePatch(ui).dump(); Module* first = ui.mods[0]->m;
    ui.remove(ui.mods[3].get()); ui.set(ui.mods[0].get(), "speed", 5); ui.add("tracker", 10, 10); ui.unplug(ui.cables[0]); run(eng, 0.1f);
    const std::string B = serializePatch(ui).dump(); std::vector<std::pair<UiMod*, int>> sc;
    Json ja; Json::parse(A, ja); applyPatch(ui, ja, sc); run(eng, 0.1f); const bool u1 = serializePatch(ui).dump() == A && ui.mods[0]->m == first;
    Json jb; Json::parse(B, jb); applyPatch(ui, jb, sc); run(eng, 0.1f); const bool u2 = serializePatch(ui).dump() == B;
    std::printf("undo %s, redo %s\n", u1 ? "ok" : "WRONG", u2 ? "ok" : "WRONG"); if (!u1 || !u2) fails++; }
  // REVERB: each TYPE rings for about its DECAY, all at about the same loudness; INFINITE holds; old REVERB and INFINITE HALL sessions open as types
  { const float DEC[13] = {0.9f, 2.5f, 6, 10, 10, 8, 8, 1.5f, 12, 5, 10, 14, 14}; std::string res; bool ok = true;
    for (int ty = 0; ty < 13; ty++) {
      for (int infm = 0; infm < 2; infm++) {
        Module* m = eng.create("reverb", 900 + ty); const ModuleDef* d = m->def; auto set = [&](const char* k, float v) { m->e[d->paramIndex(k)] = v; m->p[d->paramIndex(k)].store(v); };
        set("type", (float)ty); set("decay", infm ? 100.f : DEC[ty]); set("mix", 1); m->prepare(); Rng r(5); std::vector<double> w;   /* 50ms windows */ double acc = 0; int cnt = 0;
        const int blocks = (int)((infm ? 22 : 16) * 48000 / 64);
        for (int k = 0; k < blocks; k++) { for (int i = 0; i < 64; i++) { const float x = k * 64 + i < 4800 ? (r.uni() * 2 - 1) * 0.5f : 0; m->in[0][0][i] = x; m->in[0][1][i] = x; }
          m->render(0, 64); for (int i = 0; i < 64; i++) { const float y = m->out[0][0][i]; if (!std::isfinite(y)) ok = false; acc += y * y; } if (++cnt == 37) { w.push_back(acc / (37 * 64)); acc = 0; cnt = 0; } }
        delete m;
        int pk = 0; for (size_t i = 0; i < w.size(); i++) if (w[i] > w[pk]) pk = (int)i;
        const double peakDb = 10 * std::log10(w[pk] / 0.0833);
        if (!infm) { double t5 = -1, t35 = -1; for (size_t i = pk; i < w.size(); i++) { const double db = 10 * std::log10(w[i] / w[pk] + 1e-30); if (t5 < 0 && db < -5) t5 = i * 0.05; if (t35 < 0 && db < -35) { t35 = i * 0.05; break; } }
          const double rt = (t35 - t5) * 2; char b[64]; std::snprintf(b, 64, " %.1f/%.1fs %.0fdB", rt, DEC[ty], peakDb); res += b;
          if (ty != 7 && ty != 4 && ty != 12 && (rt < DEC[ty] * 0.75 || rt > DEC[ty] * 1.3)) ok = false;   /* FROZEN holds then stops; SHIMMER's pitched part rings on */
          if (std::fabs(peakDb + 10.3) > 2.5) ok = false; }
        else { double a = 0, b2 = 0; for (int i = 100; i < 200; i++) a += w[i]; for (int i = 340; i < 440; i++) b2 += w[i]; const double drop = 10 * std::log10(a / b2);
          if (drop > 8 || drop < -3) ok = false; char b[32]; std::snprintf(b, 32, " inf-%.1fdB", drop); res += b; } } }
    const char* old = R"({"v":2,"w":2,"mods":[{"id":1,"type":"hall","x":0,"y":20,"p":{"size":1.2,"decay":100,"damp":0.35,"mix":0.6}},{"id":2,"type":"reverb","x":100,"y":20,"p":{"size":1.2,"damp":0.4}},{"id":3,"type":"lfo","x":200,"y":20,"p":{}}],"cables":[{"f":3,"fp":"OUT","t":1,"tq":"damp","ty":"mod"},{"f":3,"fp":"OUT","t":2,"tq":"size","ty":"mod"}]})";
    Json jj; Json::parse(old, jj); ui.clearPatch(); loadPatch(ui, jj); std::vector<UiMod*> vs; for (auto& um : ui.mods) if (um->m->def->type == "reverb") vs.push_back(um.get());
    const ModuleDef* d = vs.empty() ? nullptr : vs[0]->m->def; std::set<std::string> modded; for (auto& cb : ui.cables) if (cb.type == PType::Mod && d) modded.insert(d->params[cb.toParam].key);
    const bool mig = vs.size() == 2 && std::lround(vs[0]->m->p[0].load()) == 2 && vs[0]->m->p[d->paramIndex("decay")].load() >= 99 && std::fabs(vs[0]->m->p[d->paramIndex("size")].load() - 0.444f) < 0.01f
      && std::lround(vs[1]->m->p[0].load()) == 0 && std::fabs(vs[1]->m->p[d->paramIndex("decay")].load() - 1.2f) < 0.01f && modded.count("tone") && modded.count("decay");
    UiMod* v = vs.empty() ? nullptr : vs[0]; size_t nH = 0, nS = 0, nN = 0;
    if (v) { ui.set(v, "type", 2); nH = ui.shownRows(*v).size(); ui.set(v, "type", 4); nS = ui.shownRows(*v).size(); ui.set(v, "type", 6); nN = ui.shownRows(*v).size(); }
    const bool rows = nS == nH + 2 && nN == nH + 1 && helpPort(findDef("reverb"), false, 0).find("STEREO") != std::string::npos;
    if (!ok || !mig || !rows) fails++;
    std::printf("reverb:%s; old sessions %s; rows HALL %zu SHIMMER %zu NEBULA %zu%s\n", res.c_str(), mig ? "ok" : "WRONG", nH, nS, nN, ok && mig && rows ? "" : "  WRONG"); }
  // TRACKER RANDOM NOTE: never further than its steps from the note entered, never off the scale, never off the keyboard. Every scale, every key,
  // every range, every note 0..127 (the very edges too), many times over: the once-in-a-blue-moon wild note must not happen
  { long bad = 0, plays = 0; unsigned rr = 12345; int spread[15] = {};
    for (int sc = 0; sc < (int)scales().size(); sc++) for (int root = 0; root < 12; root++) for (int n = 1; n <= 7; n++) for (int nt = 0; nt <= 127; nt++) for (int k = 0; k < 12; k++) {
      rr = rr * 1664525u + 1013904223u; const float m = wanderNote((float)nt, n, root, sc, rr >> 3); plays++;
      if (m < 0 || m > 127 || !std::isfinite(m)) { bad++; continue; }
      const int d0 = nearDeg((float)nt, root, sc), d1 = nearDeg(m, root, sc);
      if (m != (float)nt && (std::abs(d1 - d0) > n || std::fabs(midiOf(d1, root, sc) - m) > 0.01f)) bad++;   // too far, or not a note of the scale
      if (std::fabs(m - nt) > 12.f * n + 12) bad++;   // and plainly never a wild leap
      if (n == 7 && sc == 0 && nt > 30 && nt < 100 && m != (float)nt) spread[std::max(0, std::min(14, d1 - d0 + 7))]++; }
    int lo = 1 << 30, hi = 0; for (int i = 0; i < 15; i++) if (i != 7) { lo = std::min(lo, spread[i]); hi = std::max(hi, spread[i]); }
    // and in the module itself: column 1 at 3 steps, column 2 off, run for a long time
    UiMod* tr = ui.add("tracker", 10, 10); Module* m = tr->m; const ModuleDef* d = m->def;
    m->seq[Module::si(0, 0)].store(60); m->seq[Module::si(0, 1)].store(64); ui.set(tr, "len", 1); ui.set(tr, "w1", 3);
    ui.set(tr, "sync", 0); ui.set(tr, "bpm", 240); ui.set(tr, "div", 2);   // 16 steps a second
    int got = 0, wild = 0; std::set<int> heard; float seen = m->disp[12].load(); std::vector<float> L(64), R(64);
    for (int b = 0; b < 48000 * 200 / 64 && got < 3000; b++) { eng.render(L.data(), R.data(), 64); const float c = m->disp[12].load();
      if (c != seen) { seen = c; got++; const float a = m->disp[8].load(), b2 = m->disp[9].load(); heard.insert((int)a); if (std::abs(nearDeg(a, m->root(), m->scale()) - nearDeg(60, m->root(), m->scale())) > 3) wild++; if (b2 != 64) wild++; } }
    ui.remove(tr);
    const bool ok = bad == 0 && lo > 0 && hi < lo * 1.3 && wild == 0 && got >= 3000 && heard.size() == 7 && d->paramIndex("w4") >= 0;
    if (!ok) fails++;
    std::printf("tracker random note: %ld plays, %ld out of range; spread even %s; in the module %d notes, %zu different, %d wild%s\n", plays, bad, hi < lo * 1.3 ? "yes" : "NO", got, heard.size(), wild, ok ? "" : "  WRONG"); }
  // EVERY TYPE: each kind of each module with a TYPE (or MODE) list, played for 2 s (notes for a voice, a saw with noise bursts for an
  // effect): nothing that isn't a number, nothing wildly loud, and not silent
  { std::string res; bool ok = true;
    for (const char* ty : {"filter", "amp", "phaser", "pluck", "resonator", "pad"}) { Module* m0 = eng.create(ty, 980); const int tj = m0->def->paramIndex("type"); const int nt = (int)m0->def->params[tj].opts.size(); const bool voice = m0->def->cat == Cat::Voice; delete m0;
      for (int t = 0; t < nt; t++) { Module* m = eng.create(ty, 981 + t); const ModuleDef* d = m->def; m->e[tj] = t; m->p[tj].store(t); if (d->paramIndex("mix") >= 0) { m->e[d->paramIndex("mix")] = 1; m->p[d->paramIndex("mix")].store(1); } m->prepare();
        double s2 = 0, pk = 0; int bad = 0; double ph = 0; unsigned r = 7; const int B = 2 * 48000 / 64;
        for (int b = 0; b < B; b++) { if (voice && b % 150 == 0) { Event ev; ev.note = 55 + (b / 150) % 5 * 3; ev.vel = 0.9f; m->trig(0, ev); }
          for (int i = 0; i < 64; i++) { r = r * 1664525u + 1013904223u; const float nz = (r >> 8) / 8388608.f - 1; const float x = voice ? 0.f : 0.2f * (2 * (float)ph - 1) + ((b % 200) < 4 ? 0.4f * nz : 0.f); ph += 110.0 / 48000; if (ph >= 1) ph -= 1; m->in[0][0][i] = m->in[0][1][i] = x; }
          m->blockStart(64); m->render(0, 64); m->t += 64.0 / 48000;
          for (int i = 0; i < 64; i++) for (int c = 0; c < 2; c++) { const float y = m->out[0][c][i]; if (!std::isfinite(y)) bad++; else { s2 += y * y; pk = std::max(pk, (double)std::fabs(y)); } } }
        const double db = 10 * std::log10(s2 / (B * 128.0) + 1e-20); if (bad || pk > 3 || db < -60) { ok = false; char b2[96]; std::snprintf(b2, 96, " [%s %s: %d bad, peak %.1f, %.0f dB]", ty, d->params[tj].opts[t].c_str(), bad, pk, db); res += b2; }
        delete m; } }
    if (!ok) fails++; std::printf("every type plays:%s%s\n", ok ? " all fine" : res.c_str(), ok ? "" : "  WRONG"); }
  // NO ZIPPER: controls swept fast end to end (as an LFO cable would, 4 Hz) must glide, not jump once a block. A steady tone in; next to
  // nothing may appear away from its own family of frequencies
  { std::string res; bool ok = true;
    struct ZC { std::string ty, key; int typ; float hold = -1; };   // typ: which TYPE (-1: as it starts); hold: not swept, held there (the
    // drive amounts: sweeping how hard something distorts changes its harmonics fast by its nature, so those are checked held still, for aliasing)
    const std::vector<ZC> cases = {{"filter", "cut", -1}, {"filter", "res", -1}, {"comp", "thr", -1}, {"comp", "ratio", -1}, {"master", "tilt", -1}, {"master", "warmth", -1}, {"master", "glue", -1}, {"tape", "tone", -1}, {"tape", "sat", -1}, {"phaser", "fb", -1},
      {"filter", "res", 3}, {"filter", "vowel", 4}, {"filter", "cut", 4}, {"filter", "cut", 5}, {"filter", "res", 5}, {"filter", "drive", 5, 1}, {"filter", "drive", 6, 1}, {"filter", "cut", 6}, {"filter", "cut", 7}, {"filter", "shape", 7},
      {"amp", "amt", 1, 1}, {"amp", "amt", 2, 0.3f}, {"amp", "amt", 3, 1}, {"amp", "amt", 4, 1}, {"amp", "amt", 6, 1}, {"amp", "mix", 2}, {"amp", "mix", 6}, {"phaser", "fb", 2}};
    for (auto& [ty, key, typ, hold] : cases) { Module* m = eng.create(ty, 970); const ModuleDef* d = m->def; const int k = d->paramIndex(key); const float lo = d->params[k].min, hi = d->params[k].max;
      if (typ >= 0) { const int tj = d->paramIndex("type"); m->e[tj] = typ; m->p[tj].store(typ); }
      m->e[k] = lo; m->p[k].store(lo); if (ty == "tape") for (const char* z : {"wow", "flutter", "hiss"}) { m->e[d->paramIndex(z)] = 0; m->p[d->paramIndex(z)].store(0); }   /* its own wobble and hiss would cloud it */
      m->prepare(); const int N = 65536; std::vector<float> rec; double ph = 0, ph2 = 0;
      for (int b = 0; b < (int)(5 * 48000 / 64); b++) { const float t = hold >= 0 ? hold : 0.5f + 0.5f * std::sin(TAU * 4.0 * b * 64 / 48000); const float v = d->params[k].exp && lo > 0 ? lo * std::pow(hi / lo, t) : lo + (hi - lo) * t; m->e[k] = v; m->p[k].store(v);
        m->blockStart(64); for (int i = 0; i < 64; i++) { const float x = 0.2f * std::sin(ph) + 0.12f * std::sin(ph2); ph += TAU * 220 / 48000; ph2 += TAU * 330 / 48000; m->in[0][0][i] = m->in[0][1][i] = x; }
        m->render(0, 64); for (int i = 0; i < 64; i++) rec.push_back(m->out[0][0][i]); }
      delete m; std::vector<float> re(N), im(N, 0); for (int i = 0; i < N; i++) re[i] = rec[rec.size() - N + i] * (0.5f - 0.5f * std::cos(TAU * i / N));
      FFT f(N); f.run(re.data(), im.data(), false); double tot = 1e-30, g = 0;
      for (int q = 1; q < N / 2; q++) { const double p = re[q] * re[q] + im[q] * im[q]; const float hz = q * 48000.f / N; tot += p; const float u = hz / 55; if (hz > 1000 && std::fabs(u - std::round(u)) * 55 > 12) g += p; }
      const double db = 10 * std::log10(g / tot + 1e-30); char b[64]; if (typ >= 0) std::snprintf(b, 64, " %s%d.%s %.0f", ty.c_str(), typ, key.c_str(), db); else std::snprintf(b, 64, " %s.%s %.0f", ty.c_str(), key.c_str(), db); res += b; if (db > -58) ok = false; }
    if (!ok) fails++; std::printf("no zipper (dB):%s%s\n", res.c_str(), ok ? "" : "  WRONG"); }
  // REVERB: no grit from movement. A steady note through every type with MOTION all the way up: next to nothing above 1 kHz that isn't near the
  // note's own family (the kind of roughness a wobble moving in steps once a block makes). The pitch-shifting types are left out
  { std::string res; bool ok = true;
    for (int ty : {0, 1, 2, 3, 5, 6, 7, 8, 9, 11}) { Module* m = eng.create("reverb", 960); const ModuleDef* d = m->def; auto set = [&](const char* k, float v) { m->e[d->paramIndex(k)] = v; m->p[d->paramIndex(k)].store(v); };
      set("type", (float)ty); set("mix", 1); set("decay", 4); set("motion", 1); if (ty == 9) set("wow", 1); m->prepare();
      const int N = 65536; std::vector<float> rec; double ph = 0;
      for (int b = 0; b < (int)(6 * 48000 / 64); b++) { for (int i = 0; i < 64; i++) { const float x = 0.25f * std::sin(ph); ph += TAU * 220 / 48000; m->in[0][0][i] = m->in[0][1][i] = x; } m->render(0, 64); for (int i = 0; i < 64; i++) rec.push_back(m->out[0][0][i]); }
      delete m; std::vector<float> re(N), im(N, 0); for (int i = 0; i < N; i++) re[i] = rec[rec.size() - N + i] * (0.5f - 0.5f * std::cos(TAU * i / N));
      FFT f(N); f.run(re.data(), im.data(), false); double tot = 1e-30, g = 0;
      for (int k = 1; k < N / 2; k++) { const double p = re[k] * re[k] + im[k] * im[k]; const float hz = k * 48000.f / N; tot += p; const float q = hz / 55; if (hz > 1000 && std::fabs(q - std::round(q)) * 55 > 12) g += p; }
      const double db = 10 * std::log10(g / tot + 1e-30); char b[24]; std::snprintf(b, 24, " %.0f", db); res += b; if (db > (ty == 7 ? -58 : -68)) ok = false; }   /* the old once-a-block wobble on PLATE and HALL came out about -63 */
    if (!ok) fails++; std::printf("reverb grit with MOTION full (dB):%s%s\n", res.c_str(), ok ? "" : "  WRONG"); }
  // REVERB TAPE SPACE: WOW only bends the pitch smoothly (no grit), SATURATE is what adds harmonics
  { auto measure = [&](float w, float dr, double& harm, double& grit) { Module* m = eng.create("reverb", 950); const ModuleDef* d = m->def; auto set = [&](const char* k, float v) { m->e[d->paramIndex(k)] = v; m->p[d->paramIndex(k)].store(v); };
      set("type", 9); set("mix", 1); set("decay", 5); set("wow", w); set("drive", dr); set("motion", 0); m->prepare();
      const int N = 65536; std::vector<float> rec; double ph = 0;
      for (int b = 0; b < (int)(9 * 48000 / 64); b++) { for (int i = 0; i < 64; i++) { const float x = 0.3f * std::sin(ph); ph += TAU * 220 / 48000; m->in[0][0][i] = m->in[0][1][i] = x; } m->render(0, 64); for (int i = 0; i < 64; i++) rec.push_back(m->out[0][0][i]); }
      delete m; std::vector<float> re(N), im(N, 0); for (int i = 0; i < N; i++) re[i] = rec[rec.size() - N + i] * (0.5f - 0.5f * std::cos(TAU * i / N));
      FFT f(N); f.run(re.data(), im.data(), false); double fund = 0, h = 0, g = 0;
      for (int k = 1; k < N / 2; k++) { const double p = re[k] * re[k] + im[k] * im[k]; const float hz = k * 48000.f / N; const int hn = (int)std::lround(hz / 220); const bool atH = std::fabs(hz - hn * 220) < 6;
        if (atH && hn == 1) fund += p; else if (atH && hn > 1) h += p; else if (hz > 1000) g += p; }
      harm = 10 * std::log10(h / fund); grit = 10 * std::log10(g / fund); };
    double wh, wg, sh, sg; measure(1, 0, wh, wg); measure(0, 1, sh, sg);
    const bool ok = wg < -60 && sh > -26 && sg < -60 && sh > wh + 15; if (!ok) fails++;
    std::printf("tape space: WOW grit %.0f dB, harmonics %.0f dB; SATURATE harmonics %.0f dB, grit %.0f dB%s\n", wg, wh, sh, sg, ok ? "" : "  WRONG"); }
  // THEMES: every module's text readable on every preset (what's quiet, too); dark text on lit fills is by design and left out
  { int poor = 0; std::string ex;
    auto lumOf = [](uint32_t p) { float o[3]; for (int i = 0; i < 3; i++) { const float u = ((p >> (8 * i)) & 255) / 255.f; o[i] = u <= 0.04045f ? u / 12.92f : std::pow((u + 0.055f) / 1.055f, 2.4f); } return 0.2126f * o[0] + 0.7152f * o[1] + 0.0722f * o[2]; };
    for (auto& pr : presets()) {
      Ui u2(eng); int x = 6, y = 14; std::vector<UiMod*> made;
      for (const ModuleDef* d : allDefs()) { made.push_back(u2.add(d->type, x, y)); x += d->width + 8; if (x > 1400) { x = 6; y += 260; } }
      Canvas c; c.fine = true; c.resize(1500, y + 300); applyTheme(c, pr.second, 0); c.beginFrame(); u2.draw(c, 0.016f);
      for (auto& r : c.runs) { if ((r.c & 127) == 15) continue; std::map<uint8_t, int> cnt; const int tw = Canvas::tw(r.s);
        for (int yy = r.y; yy < r.y + 6; yy++) for (int xx = r.x; xx < r.x + tw; xx++) if (xx >= 0 && yy >= 0 && xx < c.W && yy < c.H) cnt[c.buf[(size_t)yy * c.W + xx]]++;
        uint8_t bg = 0; int best = -1; for (auto& kv : cnt) if (kv.second > best) { best = kv.second; bg = kv.first; }
        float a = lumOf(c.pal[r.c & 127]), b = lumOf(c.pal[bg]); if (a < b) std::swap(a, b);
        if ((a + 0.05f) / (b + 0.05f) < 2.5f) { poor++; if (ex.size() < 120) ex += std::string(" [") + pr.first + ": " + r.s.substr(0, 16) + "]"; } }
      for (UiMod* m : made) u2.remove(m); }
    if (poor) fails++; std::printf("theme text: %d hard to read%s%s\n", poor, ex.c_str(), poor ? "  WRONG" : ""); }
  // ECHO: every TYPE repeats (a burst comes back), fades with FEEDBACK at 0.6, and stays in bounds at 0.95 with sound pouring in
  { std::string res; bool ok = true;
    for (int ty = 0; ty < 13; ty++) { double w[12] = {}; float mx = 0;
      for (int mode = 0; mode < 2; mode++) { Module* m = eng.create("echo", 980); const ModuleDef* d = m->def; auto set = [&](const char* k, float v) { m->e[d->paramIndex(k)] = v; m->p[d->paramIndex(k)].store(v); };
        set("type", (float)ty); set("mix", 1); set("time", 0.3f); set("fb", mode ? 0.95f : 0.6f); m->prepare(); Rng r(4);
        for (int k = 0; k < (int)(10 * 48000 / 64); k++) { for (int i = 0; i < 64; i++) { const int n = k * 64 + i; const float x = (mode || n < 4800) ? (r.uni() * 2 - 1) * 0.4f : 0; m->in[0][0][i] = m->in[0][1][i] = x; }
          m->render(0, 64); for (int i = 0; i < 64; i++) { const float y = m->out[0][0][i], z = m->out[0][1][i]; if (!std::isfinite(y) || !std::isfinite(z)) ok = false; if (mode) mx = std::max(mx, std::max(std::fabs(y), std::fabs(z))); else w[k * 64 / 48000] += y * y + z * z; } }
        delete m; }
      const double a = 10 * std::log10(w[0] / 96000 + 1e-30), b = 10 * std::log10(w[6] / 96000 + 1e-30);
      if (a < -40 || b > a - 20 || mx > 1.0f) ok = false; char bb[40]; std::snprintf(bb, 40, " %d:%.0f/%.0f/%.2f", ty, a, b, mx); res += bb; }
    if (!ok) fails++; std::printf("echo types (first second dB / at 6 s / peak at FB .95):%s%s\n", res.c_str(), ok ? "" : "  WRONG"); }
  // NO-INPUT MIXER: quiet with GAIN low, sings near FREQ with GAIN up, never past its ceiling
  { bool ok = true; double lv[2] = {}; float pkHz = 0, mx = 0;
    for (int g = 0; g < 2; g++) { Module* m = eng.create("noinput", 990); const ModuleDef* d = m->def; auto set = [&](const char* k, float v) { m->e[d->paramIndex(k)] = v; m->p[d->paramIndex(k)].store(v); };
      set("g1", g ? 0.6f : 0.2f); set("f1", 440); set("pulse", 0); m->prepare(); std::vector<float> rec;
      for (int k = 0; k < 4 * 48000 / 64; k++) { for (int i = 0; i < 64; i++) m->in[0][0][i] = m->in[0][1][i] = 0; m->render(0, 64); for (int i = 0; i < 64; i++) { rec.push_back(m->out[0][0][i]); mx = std::max(mx, std::fabs(m->out[0][0][i])); } }
      delete m; const int N = 65536; double s2 = 0; for (size_t i = rec.size() - N; i < rec.size(); i++) s2 += rec[i] * rec[i]; lv[g] = 10 * std::log10(s2 / N + 1e-30);
      if (g) { std::vector<float> re(N), im(N, 0); for (int i = 0; i < N; i++) re[i] = rec[rec.size() - N + i]; FFT f(N); f.run(re.data(), im.data(), false); double bp = 0; for (int k = 1; k < N / 2; k++) { const double p = re[k] * re[k] + im[k] * im[k]; if (p > bp) { bp = p; pkHz = k * 48000.f / N; } } } }
    if (lv[0] > -80 || lv[1] < -30 || std::fabs(pkHz - 440) > 30 || mx > 0.71f) ok = false; if (!ok) fails++;
    std::printf("no-input mixer: gain low %.0f dB, gain up %.0f dB singing at %.0f Hz (FREQ 440), peak %.2f%s\n", lv[0], lv[1], pkHz, mx, ok ? "" : "  WRONG"); }
  // NO-INPUT MIXER, PULSE: left alone, it chokes and builds again by itself, about SPEED times a second
  { bool ok = true; std::string res;
    for (float spd : {1.f, 3.f}) { Module* m = eng.create("noinput", 991); const ModuleDef* d = m->def; auto set = [&](const char* k, float v) { m->e[d->paramIndex(k)] = v; m->p[d->paramIndex(k)].store(v); };
      set("g1", 0.6f); set("pulse", 0.5f); set("prate", spd); m->prepare(); double acc = 0; int c = 0, rises = 0; bool up = false;
      for (int k = 0; k < 12 * 48000 / 64; k++) { for (int i = 0; i < 64; i++) m->in[0][0][i] = m->in[0][1][i] = 0; m->render(0, 64);
        for (int i = 0; i < 64; i++) { acc += m->out[0][0][i] * m->out[0][0][i]; if (++c == 480) { const bool u = 10 * std::log10(acc / 480 + 1e-30) > -30; if (k > 2 * 48000 / 64 && u && !up) rises++; up = u; acc = 0; c = 0; } } }
      delete m; if (rises < spd * 10 * 0.6f || rises > spd * 10 * 1.6f) ok = false; char b[32]; std::snprintf(b, 32, " %.0f Hz: %d", spd, rises); res += b; }
    if (!ok) fails++; std::printf("no-input pulse, pulses in 10 s at SPEED%s%s\n", res.c_str(), ok ? "" : "  WRONG"); }
  // EUCLID DRUMS: HITS spread evenly (3 in 8 is the tresillo), each lane plays its count every cycle, its trigger output agrees
  { bool ok = true; std::string pat; for (int k = 0; k < 8; k++) pat += euclidHit(k, 8, 3, 0) ? 'x' : '.'; if (pat != "x..x..x.") ok = false;
    std::string p2; for (int k = 0; k < 8; k++) p2 += euclidHit(k, 8, 5, 0) ? 'x' : '.';
    Module* m = eng.create("euclid", 995); const ModuleDef* d = m->def; auto set = [&](const char* k, float v) { m->e[d->paramIndex(k)] = v; m->p[d->paramIndex(k)].store(v); };
    set("sync", 0); set("bpm", 120); set("div", 1); set("chance", 1); set("mutate", 0); m->prepare(); float mx = 0; double e2 = 0;
    for (int k = 0; k < 4 * 48000 / 64; k++) { m->blockStart(64); m->render(0, 64); m->t += 64.0 / 48000; for (int i = 0; i < 64; i++) { mx = std::max(mx, std::fabs(m->out[0][0][i])); e2 += m->out[0][0][i] * m->out[0][0][i]; } }
    const float steps = m->disp[8].load(); delete m;
    if (steps < 30 || steps > 34 || mx > 1 || e2 < 1) ok = false;   // 120 BPM in sixteenths: 8 steps a second, 32 in 4 s
    if (!ok) fails++; std::printf("euclid drums: 3 in 8 %s, 5 in 8 %s, %d steps in 4 s, peak %.2f%s\n", pat.c_str(), p2.c_str(), (int)steps, mx, ok ? "" : "  WRONG"); }
  // TAP: three taps in rhythm set a speed row (and a drag doesn't count as a tap)
  { UiMod* ck = ui.add("clock", 10, 10); UiMod* ec = ui.add("echo", 100, 10); bool ok = true;
    const int bi = ck->m->def->paramIndex("bpm"), ti = ec->m->def->paramIndex("time");
    ck->m->p[ck->m->def->paramIndex("sync")].store(0);
    auto tapAt = [&](UiMod* u, int k, double gap, int n) { for (int i = 0; i < n; i++) { ui.tapRow(u, k); const auto t0 = std::chrono::steady_clock::now(); while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < gap) {} } };
    tapAt(ck, bi, 0.5, 4); const float b1 = ck->m->p[bi].load(); if (std::fabs(b1 - 120) > 2) ok = false;
    tapAt(ec, ti, 0.3, 3); const float t1 = ec->m->p[ti].load(); if (std::fabs(t1 - 0.3f) > 0.03f) ok = false;
    ui.remove(ck); ui.remove(ec); if (!ok) fails++;
    std::printf("tap tempo: 4 taps 0.5 s apart -> %.0f BPM; 3 taps 0.3 s apart -> echo %.2f s%s\n", b1, t1, ok ? "" : "  WRONG"); }
  // RANDOMISE: themes from one hue, every cable clear against the background, the modules and each other
  { std::mt19937 r(3); int bad = 0, kinds[4] = {}; for (int st = 0; st < 4; st++) for (int k = 0; k < 500; k++) { const Theme t = harmonyTheme(st, [&](int m) { return (int)(r() % (unsigned)m); });
      for (const std::string* c : {&t.audio, &t.trig, &t.modc}) if (contrast(*c, t.bg) < cableNeedBg(st) || contrast(*c, t.mod) < cableNeedMod(st)) bad++;
      const bool pb = lum(t.bg) > 0.45f, pm = lum(t.mod) > 0.45f; if (pb != (st == TH_PALE || st == TH_PALE_BG) || pm != (st == TH_PALE || st == TH_DARK_BG)) bad++; }
    for (int k = 0; k < 1000; k++) { const Theme t = randomTheme([&](int m) { return (int)(r() % (unsigned)m); }); kinds[(lum(t.bg) > 0.45f ? 0 : 2) + (lum(t.mod) > 0.45f ? 0 : 1)]++; }
    if (kinds[0] < 300 || kinds[3] < 300 || kinds[1] < 50 || kinds[2] < 50) bad++;   /* pale, dark and both mixed styles all turn up */
    if (bad) fails++; std::printf("random themes: %d of 2000 not clear enough (styles over 1000 presses: %d pale, %d dark, %d dark with pale modules, %d pale with deep modules)%s\n", bad, kinds[0], kinds[3], kinds[2], kinds[1], bad ? "  WRONG" : ""); }
  if (argc > 1) {
    Json f; if (!Json::parse(readFile(argv[1]), f)) { std::printf("could not read %s\n", argv[1]); return 1; }
    const Json* data = patchIn(f); const std::string note = loadPatch(ui, *data); std::printf("loaded: %s\n", note.empty() ? "everything" : note.c_str());
    const Result r = run(eng, 20); std::printf("SESSION: peak %.3f rms %.4f cost %.1f%%%s\n", r.peak, r.rms, r.cost, r.bad ? " NOT FINITE" : "");
    const std::string back = serializePatch(ui).dump(); std::printf("tracker steps kept: %s\n", back.find("\"seq\":[[93") != std::string::npos ? "yes" : "NO");
  }
  return fails ? 1 : 0;
}
