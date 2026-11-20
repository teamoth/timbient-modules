// Timbient Modules: the web player. The same engine and drawing as the desktop app, compiled to WebAssembly: it opens a
// session file (.tmbnt, with any sounds packed inside it) and plays it. Nothing can be changed: the patch can be looked at
// (dragged about, zoomed), the level set, and that's all. The page (web/player.js) calls these functions: it owns the file
// picker, the sound output (Web Audio) and the screen (a canvas the pixels are copied onto).
#include "../engine/params.h"
#include "../ui/ui.h"
#include "../ui/session.h"
#include "../ui/theme.h"
#include "../app/audio_files.h"
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

using namespace tb;

namespace {
Engine* eng = nullptr; Ui* ui = nullptr; Canvas* cv = nullptr;
std::vector<float> outLR;               // the sound for the page: n left, then n right
std::vector<uint32_t> img; int imgW = 0, imgH = 0;   // the screen at S times the canvas's size, with the finer letters drawn in
std::vector<uint32_t> atlas; int atlasW = 0, atlasH = 0;
std::string note;                       // what the last load had to say ("" when all was well)
std::string heavyList;

// the heavy kinds (the dropdowns' HEAVIER list) a session uses: named if the device can't keep up
void findHeavy() {
  static const std::map<std::string, std::pair<std::string, int>> FROM = {{"reverb", {"type", 8}}, {"echo", {"type", 6}}, {"pitchshift", {"mode", 3}}, {"filter", {"type", 5}}, {"amp", {"type", 6}},
    {"phaser", {"type", 4}}, {"pluck", {"type", 5}}, {"resonator", {"type", 3}}, {"pad", {"type", 2}}, {"analog", {"type", 2}}, {"formant", {"type", 1}}, {"fold", {"type", 1}}, {"comp", {"type", 4}}};
  heavyList.clear();
  for (auto& up : ui->mods) { const ModuleDef* d = up->m->def; std::string what;
    auto it = FROM.find(d->type);
    if (it != FROM.end()) { const int k = d->paramIndex(it->second.first); if (k >= 0) { const int t = (int)std::lround(up->m->p[k].load()); if (t >= it->second.second && t < (int)d->params[k].opts.size()) what = d->name + " " + d->params[k].opts[t]; } }
    if (d->type == "mi" || d->type == "stretch") what = d->name;   // these are heavy whatever they're set to
    if (!what.empty() && heavyList.find(what) == std::string::npos) heavyList += (heavyList.empty() ? "" : ", ") + what; }
}
}

extern "C" {

// start: the engine at the page's sample rate, and the look (the first preset)
EMSCRIPTEN_KEEPALIVE int tb_init(float sampleRate) {
  if (eng) return 1;
  eng = new Engine(); eng->setSampleRate(sampleRate); ui = new Ui(*eng); ui->viewOnly = true; ui->helpOn = false; ui->cableMotion = true;
  cv = new Canvas(); cv->fine = true; applyTheme(*cv, presets()[0].second, 0);
  atlas = Canvas::fontAtlas(atlasW, atlasH);
  ui->add("out", 340, 60); ui->viewTitle = "TIMBIENT MODULES: OPEN A SESSION";
  return 1;
}

// room for the page to copy a file into
EMSCRIPTEN_KEEPALIVE char* tb_alloc(int n) { return (char*)std::malloc((size_t)n); }
EMSCRIPTEN_KEEPALIVE void tb_free(char* p) { std::free(p); }

// open a session from the file's bytes, in memory from tb_alloc: they're freed here, as soon as they've been read (big
// sessions on phones need the room). Returns 1 if it opened; tb_note() says why not, or what was left out
EMSCRIPTEN_KEEPALIVE int tb_load(char* data, int len, const char* name) {
  note.clear(); Json f; { std::string text(data, (size_t)len); std::free(data); if (!Json::parse(text, f)) { note = "THAT FILE COULD NOT BE READ"; return 0; } }
  const Json* d = patchIn(f); if (!d) { note = "THAT IS NOT A TIMBIENT SESSION"; return 0; }
  std::map<std::string, AudioBuf*> pack;   // the sounds packed in the file, by name (each one's text let go once it's read)
  if (Json* p = f.getm("audio")) if (p->t == Json::OBJ) for (auto& kv : p->o) {
    AudioBuf* b = fromBase64Pcm16(kv.second.str("b64"), (float)kv.second.num("sr", 48000), (int)kv.second.num("ch", 1)); kv.second = Json::object(); if (b) pack[kv.first] = b; }
  note = loadPatch(*ui, *d); int missing = 0;
  for (auto& up : ui->mods) { UiMod* u = up.get();
    for (int k = 0; k < UiMod::SLOTS; k++) { UiMod::Snd& sd = u->snd[k]; if (sd.id.empty()) continue;
      auto it = pack.find(sd.id); if (it == pack.end() || !it->second) { if (it == pack.end()) { sd.missing = true; missing++; continue; } }
      AudioBuf* b = it->second; bool usedAgain = false;   // the engine takes it; a second place using the same sound gets a copy
      for (auto& up2 : ui->mods) for (int k2 = 0; k2 < UiMod::SLOTS; k2++) if ((up2.get() != u || k2 > k) && up2->snd[k2].id == sd.id && (up2.get() != u || k2 > k)) usedAgain = true;
      if (usedAgain) b = new AudioBuf(*it->second); else it->second = nullptr;
      if (!b) { sd.missing = true; missing++; continue; }
      sd.peaks = peaksOf(*b); sd.missing = false; sd.secs = b->d.size() / b->sr; u->m->roots[k].store(sd.root); eng->setBuffer(u->m->id, b, k); } }
  for (auto& kv : pack) delete kv.second;
  if (missing) note += std::string(note.empty() ? "" : ". ") + std::to_string(missing) + (missing > 1 ? " SOUNDS ARE" : " SOUND IS") + " NOT IN THE FILE (SAVE IT FROM THE APP WITH SAVE AS)";
  std::string t = name ? name : ""; const size_t sl = t.find_last_of("/\\"); if (sl != std::string::npos) t = t.substr(sl + 1); const size_t dot = t.find_last_of('.'); if (dot != std::string::npos) t = t.substr(0, dot); for (auto& ch : t) ch = (char)std::toupper((unsigned char)ch);
  ui->viewTitle = t.empty() ? "SESSION" : t; ui->centreView(); findHeavy();
  return 1;
}
EMSCRIPTEN_KEEPALIVE const char* tb_note() { return note.c_str(); }
EMSCRIPTEN_KEEPALIVE const char* tb_heavy() { return heavyList.c_str(); }

// n samples of sound: n left then n right, in a buffer the page reads
EMSCRIPTEN_KEEPALIVE float* tb_render(int n) {
  if ((int)outLR.size() < n * 2) outLR.assign((size_t)n * 2, 0.f);
  eng->render(outLR.data(), outLR.data() + n, n); eng->collectGarbage();
  return outLR.data();
}
EMSCRIPTEN_KEEPALIVE float tb_cpu() { return eng ? eng->cpuLoad.load() : 0.f; }

// OUTPUT's level (0..1.5, as its knob)
EMSCRIPTEN_KEEPALIVE void tb_level(float v) { for (auto& up : ui->mods) if (up->m->def->type == "out") { const int k = up->m->def->paramIndex("level"); if (k >= 0) up->m->p[k].store(v); } }
EMSCRIPTEN_KEEPALIVE float tb_getLevel() { for (auto& up : ui->mods) if (up->m->def->type == "out") { const int k = up->m->def->paramIndex("level"); if (k >= 0) return up->m->p[k].load(); } return 0.85f; }

// looking around: drag the view (in the canvas's own pixels)
EMSCRIPTEN_KEEPALIVE void tb_pan(int dx, int dy) { ui->viewX -= dx; ui->viewY -= dy; }

// draw: the canvas is w by h of the app's pixels; the picture comes back S times larger (the finer letters need the room)
EMSCRIPTEN_KEEPALIVE uint32_t* tb_frame(int w, int h, int S, float dt) {
  w = std::max(80, std::min(1600, w)); h = std::max(60, std::min(1200, h)); S = std::max(1, std::min(4, S));
  if (cv->W != w || cv->H != h) cv->resize(w, h);
  cv->beginFrame(); ui->draw(*cv, dt); cv->toRGBA();
  const int W = w * S, H = h * S; imgW = W; imgH = H; img.resize((size_t)W * H);
  for (int y = 0; y < H; y++) { const uint32_t* row = &cv->rgba[(size_t)(y / S) * cv->W]; uint32_t* o = &img[(size_t)y * W]; for (int x = 0; x < W; x++) o[x] = row[x / S] | 0xFF000000u; }   // (solid: the page shows these bytes as they are)
  std::vector<Canvas::Quad> q; cv->fineQuads((float)S, q); const int dd = Canvas::fineDot((float)S);
  for (auto& r : q) for (int y = 0; y < r.h; y++) for (int x = 0; x < r.w; x++) { const int X = r.x + x, Y = r.y + y; if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
    if (r.u >= 0 && !(atlas[(size_t)(r.v + y / dd) * atlasW + r.u + x / dd] >> 24)) continue; img[(size_t)Y * W + X] = r.rgba | 0xFF000000u; }
  return img.data();
}
EMSCRIPTEN_KEEPALIVE int tb_imgW() { return imgW; }
EMSCRIPTEN_KEEPALIVE int tb_imgH() { return imgH; }

}  // extern "C"
