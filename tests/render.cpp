// Offline test: build a patch, render it to a WAV file, report levels and processing cost.
#include "../src/engine/engine.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
using namespace tb;

static void wav(const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
  FILE* f = std::fopen(path, "wb"); const int n = (int)L.size(), bytes = n * 4;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2); u32(sr); u32(sr * 4); u16(4); u16(16); std::fwrite("data", 1, 4, f); u32(bytes);
  for (int i = 0; i < n; i++) { int16_t a = (int16_t)(clampf(L[i], -1, 1) * 32767), b = (int16_t)(clampf(R[i], -1, 1) * 32767); std::fwrite(&a, 2, 1, f); std::fwrite(&b, 2, 1, f); }
  std::fclose(f);
}

int main(int argc, char** argv) {
  const int sr = 48000; const float secs = argc > 2 ? std::stof(argv[2]) : 20;
  Engine eng; eng.setSampleRate((float)sr);
  int nid = 1; auto add = [&](const char* type) { Module* m = eng.create(type, nid++); if (!m) { std::printf("unknown module %s\n", type); std::exit(1); } eng.add(m); return m; };
  auto wire = [&](Module* a, const char* ap, Module* b, const char* bp, PType t) { Cable c; c.from = a->id; c.fromPort = a->def->outIndex(ap); c.to = b->id; c.toPort = b->def->inIndex(bp); c.type = t; eng.connect(c); };
  auto set = [&](Module* m, const char* k, float v) { m->p[m->def->paramIndex(k)].store(v); };
  std::string which = argc > 1 ? argv[1] : "ambient";
  Module* out = add("out");
  if (which == "ambient") {   // something like the user's session
    Module *clump = add("clump"), *orb = add("orbits"), *pl = add("pluck"), *pad = add("pad"), *bell = add("bell"), *fm = add("fm2"), *tone = add("tone");
    Module *echo = add("echo"), *verb = add("reverb"), *duck = add("ducker"), *clk = add("clock"), *filt = add("filter"), *mix = add("mixer"), *lfo = add("lfo"), *amp = add("amp");
    set(clump, "oct", 5); set(clump, "mutate", 1); set(clump, "speed", 2.5f); set(pl, "tone", 0.05f); set(pad, "cut", 320); set(verb, "size", 10); set(verb, "mix", 0.54f);
    set(duck, "depth", 1); set(duck, "curve", 1); set(duck, "rbeats", 3); set(clk, "bpm", 60); set(clk, "sync", 0); set(fm, "index", 2.5f); set(fm, "ratio", 3.5f); set(fm, "isus", 0);
    wire(clump, "TRIG", pl, "TRIG", PType::Trig); wire(orb, "TRIG", pad, "TRIG", PType::Trig); wire(orb, "TRIG", bell, "TRIG", PType::Trig); wire(clump, "TRIG", fm, "TRIG", PType::Trig); wire(orb, "TRIG", tone, "TRIG", PType::Trig);
    wire(pl, "OUT", echo, "IN", PType::Audio); wire(fm, "OUT", echo, "IN", PType::Audio); wire(echo, "OUT", mix, "1", PType::Audio); wire(pad, "OUT", filt, "IN", PType::Audio); wire(filt, "OUT", mix, "2", PType::Audio);
    wire(bell, "OUT", mix, "3", PType::Audio); wire(tone, "OUT", mix, "4", PType::Audio); wire(mix, "OUT", verb, "IN", PType::Audio); wire(verb, "OUT", duck, "IN", PType::Audio); wire(duck, "OUT", amp, "IN", PType::Audio);
    wire(amp, "OUT", out, "A", PType::Audio); wire(clk, "BAR", duck, "KEY", PType::Trig);
    set(amp, "gain", 0); Cable mc; mc.from = lfo->id; mc.fromPort = 0; mc.to = filt->id; mc.toParam = filt->def->paramIndex("cut"); mc.type = PType::Mod; eng.connect(mc);
  }
  std::vector<float> L(sr * (size_t)secs), R(sr * (size_t)secs);
  const auto t0 = std::chrono::steady_clock::now();
  for (size_t i = 0; i < L.size(); i += 256) eng.render(&L[i], &R[i], (int)std::min<size_t>(256, L.size() - i));
  const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  float pk = 0; double rms = 0; int bad = 0; for (size_t i = 0; i < L.size(); i++) { if (!std::isfinite(L[i])) bad++; pk = std::max(pk, std::fabs(L[i])); rms += L[i] * L[i]; }
  std::printf("%s: %.0f s rendered in %.2f s (%.1f%% of one core), peak %.3f, rms %.4f, bad samples %d, modules %zu\n", which.c_str(), secs, el, el / secs * 100, pk, std::sqrt(rms / L.size()), bad, eng.mods.size());
  wav((which + ".wav").c_str(), L, R, sr);
  eng.collectGarbage();
  return 0;
}
