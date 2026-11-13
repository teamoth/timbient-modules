#include <ctime>
#include <thread>
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
  // KEY JOURNEY: FIFTHS UP every bar into a sequencer's KEY socket; it saves and loads; unplugged, the sequencer's own key again
  { ui.clearPatch(); ui.add("out", 300, 20); eng.tempo.bpm.store(120); eng.key.root.store(0); eng.key.scale.store(0);
    UiMod* j = ui.add("journey", 0, 20); UiMod* cl = ui.add("clump", 100, 20); ui.set(j, "path", 0); ui.set(j, "dir", 0); ui.set(j, "every", 1); ui.set(j, "soft", 1);
    const bool plugged = ui.connect(j, 0, cl, KEY_PORT); const bool refused = !ui.connect(cl, 0, j, KEY_PORT);
    run(eng, 0.2f); const int k0 = cl->m->keyIn.load(); run(eng, 2.0f); const int k1 = cl->m->keyIn.load(), soft = cl->m->softIn; run(eng, 2.0f); const int k2 = cl->m->keyIn.load(), sess = eng.key.root.load();
    const std::string S = serializePatch(ui).dump(); Json jj; Json::parse(S, jj); ui.clearPatch(); loadPatch(ui, jj); bool back = false; for (auto& cb : ui.cables) if (cb.toPort == KEY_PORT) back = true;
    UiMod* c2 = nullptr; for (auto& um : ui.mods) if (um->m->def->type == "clump") c2 = um.get(); run(eng, 0.1f); const bool follows = c2 && c2->m->keyIn.load() >= 0;
    if (c2) for (size_t i = 0; i < ui.cables.size(); i++) if (ui.cables[i].toPort == KEY_PORT) { ui.unplug(ui.cables[i]); break; } run(eng, 0.1f); const bool own = c2 && c2->m->keyIn.load() < 0;
    const bool ok = plugged && refused && k0 == 0 && k1 == 7 && k2 == 2 && soft != 0 && sess == 0 && back && follows && own; if (!ok) fails++;
    std::printf("key journey: keys %d %d %d (C G D), soft %d, session stays C %d, saved %d, follows %d, unplugged %d%s\n", k0, k1, k2, soft != 0, sess == 0, back, follows, own, ok ? "" : "  WRONG"); }
  // KEY JOURNEY: WANDER moving the session's key; MODES ping-pong between Lydian and Phrygian
  { ui.clearPatch(); ui.add("out", 300, 20); eng.tempo.bpm.store(240); eng.key.root.store(2); eng.key.scale.store(0);
    UiMod* j = ui.add("journey", 0, 20); ui.set(j, "moves", 1); ui.set(j, "every", 1); int moves = 0, last = -1;   // a bar a second
    for (int k = 0; k < 40; k++) { run(eng, 1.0f); const int now = eng.key.root.load() * 100 + eng.key.scale.load(); if (now != last) moves++; last = now; }
    ui.clearPatch(); eng.key.root.store(0); eng.key.scale.store(0); UiMod* m = ui.add("journey", 0, 20); ui.set(m, "moves", 1); ui.set(m, "every", 1); ui.set(m, "path", 1); ui.set(m, "dir", 1);
    std::string seen; for (int k = 0; k < 12; k++) { run(eng, 1.0f); seen += std::string(scales()[eng.key.scale.load()].name).substr(0, 2) + " "; }
    const bool ok = moves >= 30 && seen.find("PH") != std::string::npos && seen.find("LY") != std::string::npos && eng.key.root.load() == 0; if (!ok) fails++;
    std::printf("key journey: wander moved %d of 40 bars; modes down then back: %s%s\n", moves, seen.c_str(), ok ? "" : "  WRONG"); eng.key.root.store(2); eng.key.scale.store(0); }
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
