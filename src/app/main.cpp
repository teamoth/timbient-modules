// Timbient Modules (native): the window (SDL3), the sound device (miniaudio), sessions and settings.
// Sound is produced on miniaudio's own real-time thread; the window, drawing and input run here,
// on the main thread, and can never make the audio wait.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "../engine/engine.h"
#include "../ui/canvas.h"
#include "../ui/session.h"
#include "../ui/ui.h"
#include "../ui/theme.h"
#include "audio_files.h"
#include "midi.h"
#include <cstdio>
#include <ctime>
#include <mutex>

using namespace tb;

static const char* VERSION = "0.6.1";
static Engine* gEngine = nullptr;

// ---------------- sound ----------------
// tiny numbers at the end of echo and reverb tails ("denormals") are very slow on Intel processors: treat them as zero
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
static inline void flushDenormals() { _mm_setcsr(_mm_getcsr() | 0x8040); }
#elif defined(__aarch64__) && !defined(_MSC_VER)
static inline void flushDenormals() { uint64_t f; __asm__ __volatile__("mrs %0, fpcr" : "=r"(f)); f |= (1ull << 24); __asm__ __volatile__("msr fpcr, %0" : : "r"(f)); }
#else
static inline void flushDenormals() {}
#endif
static void audioCallback(ma_device*, void* output, const void*, ma_uint32 frames) {
  flushDenormals();
  float* out = (float*)output; static float L[4096], R[4096];
  ma_uint32 done = 0;
  while (done < frames) {
    const ma_uint32 n = std::min<ma_uint32>(4096, frames - done);
    gEngine->render(L, R, (int)n);
    for (ma_uint32 i = 0; i < n; i++) { out[(done + i) * 2] = L[i]; out[(done + i) * 2 + 1] = R[i]; }
    done += n;
  }
}
static const int BUFFERS[5] = {128, 256, 512, 1024, 2048};
struct Sound {
  ma_device dev; bool open = false; int bufIdx = 2;
  bool start(Engine& e, unsigned rate) {
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = rate;
    cfg.dataCallback = audioCallback; cfg.periodSizeInFrames = (ma_uint32)BUFFERS[bufIdx]; cfg.periods = 3;
    cfg.performanceProfile = bufIdx <= 1 ? ma_performance_profile_low_latency : ma_performance_profile_conservative;
    if (ma_device_init(nullptr, &cfg, &dev) != MA_SUCCESS) return false;
    open = true; if (e.sr != (float)dev.sampleRate && rate == 0) e.setSampleRate((float)dev.sampleRate);
    e.outLatency = (float)(BUFFERS[bufIdx] * 3) / (float)dev.sampleRate;
    return ma_device_start(&dev) == MA_SUCCESS;
  }
  void stop() { if (open) { ma_device_uninit(&dev); open = false; } }
};

// ---------------- results from the system file dialogs (they may arrive on another thread) ----------------
struct DialogResult { bool open; std::string path; int target = 0; };   // target: the module a sound file is for
static std::mutex dlgMx; static std::vector<DialogResult> dlgResults;
static const SDL_DialogFileFilter JSON_FILTER[] = {{"Timbient session", "json;txt"}};
static void onOpenDialog(void*, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back({true, files[0]}); } }
static void onSaveDialog(void*, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back({false, files[0]}); } }
static const SDL_DialogFileFilter AUDIO_FILTER[] = {{"Sound files", "wav;wave;aif;aiff;flac;mp3"}};
static void onAudioDialog(void* ud, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back({true, files[0], (int)(intptr_t)ud}); } }
static std::string b64(const unsigned char* d, size_t n) {
  static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; o.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) { const unsigned v = (d[i] << 16) | ((i + 1 < n ? d[i + 1] : 0) << 8) | (i + 2 < n ? d[i + 2] : 0);
    o += A[(v >> 18) & 63]; o += A[(v >> 12) & 63]; o += i + 1 < n ? A[(v >> 6) & 63] : '='; o += i + 2 < n ? A[v & 63] : '='; }
  return o;
}
static std::string fileUrl(std::string p) { for (auto& c : p) if (c == '\\') c = '/'; return p.size() && p[0] == '/' ? "file://" + p : "file:///" + p; }

int main(int argc, char** argv) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1; }

  // the app's own folder for sessions, the autosave and settings
  SDL_Window* win = nullptr;
  SessionStore store; { char* pp = SDL_GetPrefPath("Timbient", "Timbient Modules"); store.dir = pp ? pp : "./"; SDL_free(pp); }
  store.load();
  Json settings; Json::parse(readFile(store.path("settings.json")), settings); if (settings.t != Json::OBJ) settings = Json::object();
  auto saveSettings = [&] { writeFile(store.path("settings.json"), settings.dump()); };

  // ---- sound first, so the engine knows the sample rate before any module is built ----
  static Engine engine; gEngine = &engine;
  static Sound sound; sound.bufIdx = std::max(0, std::min(4, (int)settings.num("buffer", 2)));
  if (!sound.start(engine, 0)) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Timbient Modules", "Could not open the sound device.", nullptr); return 1; }

  Ui ui(engine);
  static Midi midi(engine); midi.start();
  // the first time ever, a demo session is put in the list so there's something to play
  if (store.list.empty() && !settings.truthy("demoAdded")) { ui.demoPatch(); store.list.push_back({"DEMO", (double)std::time(nullptr), serializePatch(ui)}); store.save(); ui.clearPatch(); settings.set("demoAdded", Json::boolean(true)); saveSettings(); }
  // the app opens to an empty session; RESTORE LAST WORK brings back what was there
  { Json prev; if (store.readAutosave(prev) && prev.get("mods") && prev.get("mods")->a.size() > 1) writeFile(store.path("previous.json"), prev.dump()); }
  ui.add("out", 300, 30);

  bool dirty = false; float saveT = 0;
  ui.onChange = [&] { dirty = true; saveT = 1.0f; };
  // ---- sounds held by Sample and Grain Looper modules: kept as WAV files in the app's folder, named by id ----
  const std::string audioDir = store.dir + "audio/"; SDL_CreateDirectory(audioDir.c_str());
  auto giveSound = [&](UiMod* u, AudioBuf* b, bool keep) {   // hand a sound to a module (and keep a copy so sessions bring it back)
    u->peaks = peaksOf(*b); u->missing = false;
    if (keep) { char id[40]; std::snprintf(id, 40, "n%llx%04x", (unsigned long long)std::time(nullptr), (unsigned)(SDL_rand(65536)));
      if (writeWavMono16(audioDir + id + ".wav", *b)) { u->audioId = id; dirty = true; saveT = 0.5f; } else ui.status("COULD NOT KEEP THE SOUND", 4); }
    engine.setBuffer(u->m->id, b);
  };
  auto restoreSound = [&](UiMod* u) {
    if (u->audioId.empty()) return; std::string err;
    AudioBuf* b = decodeAudio(audioDir + u->audioId + ".wav", 600, err);
    if (b) giveSound(u, b, false); else { u->missing = true; ui.status(u->m->def->name + ": ITS SOUND IS NOT HERE. LOAD IT AGAIN", 6); }
  };
  auto catchSound = [&](UiMod* u, bool quiet) {   // GRAIN LOOPER: CATCH takes the last LENGTH seconds of what came in
    Module* m = u->m; if (m->ring.empty()) return; const long N = (long)m->ring.size(), w = m->ringW.load(std::memory_order_acquire);
    const long n = std::min(std::min(N, w), (long)(m->p[m->def->paramIndex("len")].load() * engine.sr)); if (n < 256) { if (!quiet) ui.status("NOTHING TO CATCH YET"); return; }
    AudioBuf* b = new AudioBuf(); b->sr = engine.sr; b->d.resize(n); float mx = 0;
    for (long i = 0; i < n; i++) { b->d[i] = m->ring[(w - n + i) % N]; mx = std::max(mx, std::fabs(b->d[i])); }
    if (mx < 0.002f) { delete b; if (!quiet) ui.status("NOTHING TO CATCH YET"); return; }
    for (auto& v : b->d) v *= 0.8f / mx;
    giveSound(u, b, true); ui.status("CAUGHT " + std::to_string((int)std::lround(n / engine.sr)) + "S");
  };
  auto loadData = [&](const Json& data, const std::string& what) {
    const std::string note = loadPatch(ui, data); ui.viewX = 0; ui.viewY = 0; dirty = true; saveT = 0.5f;
    ui.status(note.empty() ? "LOADED " + what : note, note.empty() ? 3 : 8);
    for (auto& up : ui.mods) restoreSound(up.get());
  };
  // ---- RECORD on the Output: straight to a WAV file in Music / Timbient Modules ----
  std::string recDir; { const char* mu = SDL_GetUserFolder(SDL_FOLDER_MUSIC); recDir = std::string(mu ? mu : store.dir.c_str()) + "Timbient Modules/"; SDL_CreateDirectory(recDir.c_str()); }
  Recorder rec;
  auto outMod = [&]() -> Module* { for (auto& up : ui.mods) if (up->m->def->type == "out") return up->m; return nullptr; };
  ui.onAction = [&](UiMod* u, const std::string& k) -> bool {
    const std::string& ty = u->m->def->type;
    if ((ty == "sample" || ty == "looper") && k == "load") { SDL_ShowOpenFileDialog(onAudioDialog, (void*)(intptr_t)u->m->id, win, AUDIO_FILTER, 1, nullptr, false); return true; }
    if (ty == "looper" && k == "catch") { catchSound(u, false); return true; }
    if (ty == "out" && k == "rec") {
      if (!rec.on()) { std::time_t tt = std::time(nullptr); char nm[64]; std::strftime(nm, 64, "timbient-%Y%m%d-%H%M%S.wav", std::localtime(&tt));
        if (rec.start(engine, recDir + nm)) ui.status("RECORDING"); else ui.status("COULD NOT START A RECORDING IN " + recDir, 6); }
      else { const std::string p = rec.path; const bool got = rec.stop(engine); if (Module* o = outMod()) o->disp[1].store(0);
        if (got) { std::string nm = p.substr(p.find_last_of("/\\") + 1); for (auto& ch : nm) ch = (char)std::toupper((unsigned char)ch); ui.status("SAVED " + nm + " IN MUSIC / TIMBIENT MODULES", 6); } else { std::remove(p.c_str()); ui.status("NOTHING RECORDED"); } }
      return true; }
    if (ty == "midiout" && k == "pick") {   // MIDI OUT: choose the device
      const auto names = midi.outputs(); if (names.empty()) { ui.status("NO MIDI OUTPUTS FOUND. PLUG ONE IN AND TRY AGAIN", 4); return true; }
      Dropdown d; const ModuleDef* df = u->m->def; const Ui::Geom g = ui.geom(*u); d.x = u->x - ui.viewX + 10; d.y = u->y - ui.viewY + g.py + 4;
      for (auto n : names) { for (auto& ch : n) ch = (char)std::toupper((unsigned char)ch); d.items.push_back(n.substr(0, 28)); if (names[d.items.size() - 1] == u->dev) d.cur = (int)d.items.size() - 1; }
      const int id = u->m->id; (void)df;
      d.pick = [&, names, id](int i) { UiMod* t = ui.byId(id); if (!t) return; t->dev = names[i]; t->m->devSlot.store(midi.openOutput(names[i])); dirty = true; saveT = 0.5f;
        std::string up = names[i]; for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch); ui.status("MIDI OUT TO " + up); };
      ui.openDropdown(d); return true; }
    if (ty == "out" && k == "again") { SDL_OpenURL(fileUrl(recDir).c_str()); ui.status("OPENING YOUR RECORDINGS"); return true; }
    return false;
  };

  // ---- SESSIONS menu ----
  std::function<void(int)> openSessions = [&](int x0) {
    Dropdown d; d.x = x0; d.y = 11;
    d.items = {"NEW EMPTY SESSION", "RESTORE LAST WORK", "SAVE AS NEW SESSION", "OPEN FILE...", "SAVE TO FILE..."};
    for (auto& s : store.list) d.items.push_back(s.name + "  " + stamp(s.t));
    d.pick = [&, x0](int i) {
      if (i == 0) { Dropdown q; q.x = x0; q.y = 11; q.items = {"CLEAR EVERYTHING?", "YES, START EMPTY", "NO, KEEP IT"}; q.title = 0; q.danger = 1;
        q.pick = [&](int j) { if (j == 1) { ui.clearPatch(); ui.add("out", 300, 30); dirty = true; saveT = 0.3f; ui.status("NEW EMPTY SESSION"); } }; ui.openDropdown(q); return; }
      if (i == 1) { Json prev; if (Json::parse(readFile(store.path("previous.json")), prev) && prev.get("mods")) loadData(prev, "YOUR LAST WORK"); else ui.status("NOTHING TO RESTORE"); return; }
      if (i == 2) { ui.askText("NAME THIS SESSION", "SESSION " + std::to_string(store.list.size() + 1), [&](const std::string& n) {
          store.list.push_back({n, (double)std::time(nullptr), serializePatch(ui)}); ui.status(store.save() ? "SAVED " + n : "COULD NOT SAVE"); }); return; }
      if (i == 3) { SDL_ShowOpenFileDialog(onOpenDialog, nullptr, win, JSON_FILTER, 1, nullptr, false); return; }
      if (i == 4) { SDL_ShowSaveFileDialog(onSaveDialog, nullptr, win, JSON_FILTER, 1, "timbient-session.json"); return; }
      const int k = i - 5; if (k < 0 || k >= (int)store.list.size()) return;
      const std::string nm = store.list[k].name;
      Dropdown q; q.x = x0; q.y = 11 + i * 8; q.items = {"LOAD " + nm, "SAVE OVER " + nm, "RENAME " + nm, "DELETE " + nm};
      q.pick = [&, k, nm, x0, i](int j) {
        if (k >= (int)store.list.size()) return;
        if (j == 0) loadData(store.list[k].data, nm);
        else if (j == 1) { store.list[k].data = serializePatch(ui); store.list[k].t = (double)std::time(nullptr); ui.status(store.save() ? "SAVED OVER " + nm : "COULD NOT SAVE"); }
        else if (j == 2) ui.askText("RENAME SESSION", nm, [&, k](const std::string& n) { if (k < (int)store.list.size()) { store.list[k].name = n; store.save(); ui.status("RENAMED TO " + n); } });
        else { Dropdown z; z.x = x0; z.y = 11 + i * 8; z.items = {"DELETE " + nm + "?", "YES, DELETE", "NO, KEEP IT"}; z.title = 0; z.danger = 1;
          z.pick = [&, k, nm](int q2) { if (q2 == 1 && k < (int)store.list.size()) { store.list.erase(store.list.begin() + k); store.save(); ui.status("DELETED " + nm); } }; ui.openDropdown(z); }
      };
      ui.openDropdown(q);
    };
    ui.openDropdown(d);
  };

  // ---- THEME: background, module and cable colours, remembered between launches ----
  static Canvas canvas;
  Theme theme = themeFrom(settings.get("theme") ? *settings.get("theme") : Json());
  const bool pondReady = false;   // the pond background arrives in a later update
  if (theme.bg == "pond" && !pondReady) theme.bg = "#000000";
  applyTheme(canvas, theme);
  auto setTheme = [&](const Theme& t) { theme = t; applyTheme(canvas, theme); settings.set("theme", themeJson(theme)); saveSettings(); };
  std::function<void(int)> openTheme = [&](int x0) {
    auto nameOf = [](const std::vector<Choice>& list, const std::string& v) { for (auto& c : list) if (v == c.second) return std::string(c.first); return std::string("CUSTOM"); };
    std::string preset = "CUSTOM"; for (auto& p : presets()) if (sameTheme(p.second, theme)) preset = p.first;
    const char* labels[6] = {"PRESET", "BACKGROUND", "MODULES", "AUDIO CABLES", "TRIG CABLES", "MOD CABLES"};
    Dropdown d; d.x = x0; d.y = 11;
    d.items = {std::string("PRESET  ") + preset, "BACKGROUND  " + nameOf(bgChoices(), theme.bg), "MODULES  " + nameOf(modChoices(), theme.mod), "AUDIO CABLES  " + nameOf(cableChoices(), theme.audio),
      "TRIG CABLES  " + nameOf(cableChoices(), theme.trig), "MOD CABLES  " + nameOf(cableChoices(), theme.modc), "RANDOMISE", "RESET THEME"};
    d.pick = [&, x0, labels](int i) {
      if (i == 6) { setTheme(randomTheme([](int n) { return (int)SDL_rand(n); })); ui.status("RANDOM THEME: TAP RANDOMISE AGAIN FOR ANOTHER", 3); openTheme(x0); return; }   // stays open, to roll again
      if (i == 7) { setTheme(Theme{}); ui.status("THEME RESET"); return; }
      Dropdown q; q.x = x0; q.y = 11 + (i ? i * 8 : 0); const int SW = 64;
      if (i == 0) { int k = 0; for (auto& p : presets()) { canvas.setColour(SW + k, p.second.mod.c_str()); q.items.push_back(p.first); q.sw.push_back(SW + k); if (sameTheme(p.second, theme)) q.cur = k; k++; }
        q.pick = [&](int j) { setTheme(presets()[j].second); ui.status(std::string("THEME ") + presets()[j].first); }; ui.openDropdown(q); return; }
      const std::vector<Choice>& list = i == 1 ? bgChoices() : i == 2 ? modChoices() : cableChoices();
      std::string Theme::*field = i == 1 ? &Theme::bg : i == 2 ? &Theme::mod : i == 3 ? &Theme::audio : i == 4 ? &Theme::trig : &Theme::modc;
      int k = 0; for (auto& c : list) { if (i == 1 && std::string(c.second) == "pond" && !pondReady) continue; canvas.setColour(SW + k, std::string(c.second) == "pond" ? "#1d4a52" : c.second); q.items.push_back(c.first); q.sw.push_back(SW + k); if (theme.*field == c.second) q.cur = k; k++; }
      const std::string label = labels[i];
      q.pick = [&, field, label, i](int j) { std::vector<Choice> l2; const std::vector<Choice>& list2 = i == 1 ? bgChoices() : i == 2 ? modChoices() : cableChoices();
        for (auto& c : list2) if (!(i == 1 && std::string(c.second) == "pond" && !pondReady)) l2.push_back(c);
        Theme t = theme; t.*field = l2[j].second; setTheme(t); ui.status(label + " " + l2[j].first); };
      ui.openDropdown(q);
    };
    ui.openDropdown(d);
  };

  // ---- SCREEN: how often the picture is redrawn when nothing is being touched (it always runs at full speed while you work) ----
  static const int FPS[3] = {60, 30, 20};
  int fpsIdx = std::max(0, std::min(2, (int)settings.num("fps", 1)));
  // ---- SETTINGS menu: key, scale, shared tempo, audio buffer, theme ----
  std::function<void(int)> openSettings = [&](int x0) {
    Dropdown d; d.x = x0; d.y = 11;
    char bpm[32]; std::snprintf(bpm, 32, "BPM  %d", (int)std::lround(engine.tempo.bpm.load()));
    char buf[64]; std::snprintf(buf, 64, "AUDIO BUFFER  %d (%.0f MS)", BUFFERS[sound.bufIdx], BUFFERS[sound.bufIdx] * 3 * 1000.0 / engine.sr);
    d.items = {std::string("KEY  ") + NOTE_NAMES[engine.key.root.load()], std::string("SCALE  ") + scales()[engine.key.scale.load()].name, bpm, buf, "SCREEN  " + std::to_string(FPS[fpsIdx]) + " FPS", "THEME", std::string("VERSION ") + VERSION};
    d.title = 6;
    d.pick = [&, x0](int i) {
      Dropdown q; q.x = x0; q.y = 11;
      if (i == 0) { for (int k = 0; k < 12; k++) q.items.push_back(std::string("KEY ") + NOTE_NAMES[k]); q.cur = engine.key.root.load();
        q.pick = [&](int j) { engine.key.root.store(j); ui.status(std::string("KEY ") + NOTE_NAMES[j]); dirty = true; saveT = 0.5f; }; }
      else if (i == 1) { for (auto& s : scales()) q.items.push_back(s.name); q.cur = engine.key.scale.load();
        q.pick = [&](int j) { engine.key.scale.store(j); ui.status(std::string("SCALE ") + scales()[j].name); dirty = true; saveT = 0.5f; }; }
      else if (i == 2) { ui.openTempo(x0, 11 + 2 * 8); return; }
      else if (i == 3) { for (int k = 0; k < 5; k++) { char b[48]; std::snprintf(b, 48, "%d  (%.0f MS)%s", BUFFERS[k], BUFFERS[k] * 3 * 1000.0 / engine.sr, k == 2 ? "  DEFAULT" : ""); q.items.push_back(b); } q.cur = sound.bufIdx;
        q.pick = [&](int j) { if (j == sound.bufIdx) return; sound.stop(); sound.bufIdx = j; settings.set("buffer", Json::number(j)); saveSettings();
          if (!sound.start(engine, (unsigned)engine.sr)) ui.status("COULD NOT RESTART THE SOUND DEVICE", 6); else ui.status("AUDIO BUFFER " + std::to_string(BUFFERS[j])); }; }
      else if (i == 4) { q.items = {"60 FPS  SMOOTHEST", "30 FPS  LIGHTER", "20 FPS  LIGHTEST"}; q.cur = fpsIdx;   // dragging and patching always run at full speed
        q.pick = [&](int j) { fpsIdx = j; settings.set("fps", Json::number(j)); saveSettings(); ui.status("SCREEN " + std::to_string(FPS[j]) + " FPS WHEN IDLE, FULL SPEED WHILE YOU WORK", 4); }; }
      else if (i == 5) { openTheme(x0); return; }
      else return;
      ui.openDropdown(q);
    };
    ui.openDropdown(d);
  };
  ui.bar.push_back({"SESSIONS", openSessions});
  ui.bar.push_back({"SETTINGS", openSettings});

  // ---- the window ----
  win = SDL_CreateWindow("Timbient Modules", 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
  SDL_SetRenderVSync(ren, 1);
  SDL_Texture* tex = nullptr; int texW = 0, texH = 0; float scale = 3;
  bool texting = false;
  // ZOOM: small steps of about 8%, remembered between launches. The pixels are scaled up sharply at any size.
  int zoom = std::max(-18, std::min(18, (int)settings.num("zoom", 0)));
  int pendingZoom = 0; float pinchAcc = 0, wheelAcc = 0;
  ui.onZoom = [&](int k) { pendingZoom += k; };

  bool running = true, hidden = false, held = false; float busyT = 1; Uint64 last = SDL_GetTicksNS(), lastLoop = last;
  while (running) {
    const Uint64 tl = SDL_GetTicksNS(); const float ldt = std::min(0.25f, (tl - lastLoop) / 1e9f); lastLoop = tl; busyT -= ldt;
    int pw = 0, ph = 0; SDL_GetCurrentRenderOutputSize(ren, &pw, &ph);
    float dens = SDL_GetWindowPixelDensity(win); if (dens <= 0) dens = 1;
    // apply a zoom change, keeping the middle of the view where it was
    if (pendingZoom) { const int nz = std::max(-18, std::min(18, zoom + pendingZoom)); pendingZoom = 0;
      if (nz != zoom) { const float base = (float)std::max(2, (int)std::lround(std::min(pw, ph) / 250.f));
        const float s0 = clampf(base * std::pow(2.f, zoom / 9.f), 1, 16), s1 = clampf(base * std::pow(2.f, nz / 9.f), 1, 16);
        ui.viewX += (int)std::lround(pw / s0 / 2 - pw / s1 / 2); ui.viewY += (int)std::lround(ph / s0 / 2 - ph / s1 / 2);
        zoom = nz; settings.set("zoom", Json::number(zoom)); saveSettings(); char b[32]; std::snprintf(b, 32, "ZOOM %d%%", (int)std::lround(std::pow(2.f, zoom / 9.f) * 100)); ui.status(b, 1.5f); } }
    scale = clampf(std::max(2, (int)std::lround(std::min(pw, ph) / 250.f)) * std::pow(2.f, zoom / 9.f), 1, 16);
    const int W = std::max(64, (int)(pw / scale)), H = std::max(48, (int)(ph / scale));
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) held = true; if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) held = false;
      if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_WHEEL || ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_PINCH_UPDATE || ev.type == SDL_EVENT_TEXT_INPUT) { busyT = 0.6f; hidden = false; }
      switch (ev.type) {
        case SDL_EVENT_QUIT: running = false; break;
        case SDL_EVENT_TEXT_INPUT: ui.typed(ev.text.text); break;
        case SDL_EVENT_KEY_DOWN:
          if (ui.ask) { if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) ui.keyEnter(); else if (ev.key.key == SDLK_BACKSPACE) ui.keyBackspace(); else if (ev.key.key == SDLK_ESCAPE) ui.keyEscape(); break; }
          if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_F && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)))) ui.wantFullscreen = !ui.isFullscreen;
          if (ev.key.key == SDLK_ESCAPE && ui.isFullscreen) ui.wantFullscreen = false;
          if (ev.key.key == SDLK_EQUALS || ev.key.key == SDLK_PLUS || ev.key.key == SDLK_KP_PLUS) pendingZoom++;
          if (ev.key.key == SDLK_MINUS || ev.key.key == SDLK_KP_MINUS) pendingZoom--;
          if (ev.key.key == SDLK_0 && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) pendingZoom = -zoom;
          if (ev.key.key == SDLK_SPACE) { engine.freeze.store(!engine.freeze.load()); ui.status(engine.freeze.load() ? "FROZEN" : "UNFROZEN"); }
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: if (ev.button.button == SDL_BUTTON_LEFT) ui.pointerDown(ev.button.x * dens / scale, ev.button.y * dens / scale); break;
        case SDL_EVENT_MOUSE_MOTION: ui.pointerMove(ev.motion.x * dens / scale, ev.motion.y * dens / scale); break;
        case SDL_EVENT_MOUSE_BUTTON_UP: if (ev.button.button == SDL_BUTTON_LEFT) ui.pointerUp(ev.button.x * dens / scale, ev.button.y * dens / scale); break;
        case SDL_EVENT_MOUSE_WHEEL:   // with Ctrl or Cmd held: zoom. Otherwise: move around the patch
          if (SDL_GetModState() & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) { wheelAcc += ev.wheel.y; if (std::fabs(wheelAcc) >= 1) { pendingZoom += wheelAcc > 0 ? 1 : -1; wheelAcc = 0; } }
          else { ui.viewX -= (int)std::lround(ev.wheel.x * 12); ui.viewY -= (int)std::lround(ev.wheel.y * 12); }
          break;
        case SDL_EVENT_PINCH_UPDATE:   // a trackpad pinch
          pinchAcc += std::log(std::max(0.01f, ev.pinch.scale)); if (std::fabs(pinchAcc) > 0.077f) { pendingZoom += pinchAcc > 0 ? 1 : -1; pinchAcc = 0; } break;
        case SDL_EVENT_PINCH_END: pinchAcc = 0; break;
        case SDL_EVENT_WINDOW_MINIMIZED: case SDL_EVENT_WINDOW_HIDDEN: case SDL_EVENT_WINDOW_OCCLUDED: hidden = true; break;   // nothing to see: stop drawing
        case SDL_EVENT_WINDOW_RESTORED: case SDL_EVENT_WINDOW_SHOWN: case SDL_EVENT_WINDOW_EXPOSED: case SDL_EVENT_WINDOW_MAXIMIZED: case SDL_EVENT_WINDOW_FOCUS_GAINED: case SDL_EVENT_WINDOW_RESIZED: hidden = false; break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: ui.isFullscreen = true; ui.wantFullscreen = true; break;
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: ui.isFullscreen = false; ui.wantFullscreen = false; break;
        default: break;
      }
    }
    if (ui.wantFullscreen != ui.isFullscreen) { SDL_SetWindowFullscreen(win, ui.wantFullscreen); ui.isFullscreen = ui.wantFullscreen; }
    if ((bool)ui.ask != texting) { texting = (bool)ui.ask; if (texting) SDL_StartTextInput(win); else SDL_StopTextInput(win); }
    // files chosen in the system dialogs
    { std::vector<DialogResult> rs; { std::lock_guard<std::mutex> l(dlgMx); rs.swap(dlgResults); }
      for (auto& r : rs) {
        if (r.target) { UiMod* u = ui.byId(r.target); if (!u) continue; std::string err;   // a sound for a Sample or Grain Looper
          AudioBuf* b = decodeAudio(r.path, u->m->def->type == "sample" ? 240 : 60, err); if (!b) { ui.status(err, 5); continue; }
          if (u->m->def->type == "sample") { float mx = 0; for (float v : b->d) mx = std::max(mx, std::fabs(v)); if (mx > 1e-4f) for (auto& v : b->d) v *= 0.9f / mx; }   // quiet recordings come in at full level
          const int secs = (int)std::lround(b->dur()); giveSound(u, b, true); ui.status("LOADED " + std::to_string(secs) + "S"); continue; }
        if (r.open) { Json f; if (!Json::parse(readFile(r.path), f)) { ui.status("THAT FILE COULD NOT BE READ", 5); continue; }
          if (const Json* pack = f.get("audio")) if (pack->t == Json::OBJ) for (auto& kv : pack->o) {   // a web export with its sounds packed in
            const std::string fn = audioDir + kv.first + ".wav"; if (!readFile(fn).empty()) continue;
            AudioBuf* b = fromBase64Pcm16(kv.second.str("b64"), (float)kv.second.num("sr", 48000)); if (b) { writeWavMono16(fn, *b); delete b; } }
          if (f.str("timbient") == "sessions" && f.get("sessions")) { int n = 0; for (auto& e : f.get("sessions")->a) if (const Json* dd = e.get("data")) { store.list.push_back({e.str("name", "SESSION"), e.num("t", 0) / 1000.0, *dd}); n++; }
            store.save(); ui.status("ADDED " + std::to_string(n) + " SESSIONS TO THE LIST", 4); continue; }
          const Json* data = patchIn(f); if (!data) { ui.status("THAT IS NOT A TIMBIENT SESSION", 5); continue; }
          std::string nm = r.path; const size_t sl = nm.find_last_of("/\\"); if (sl != std::string::npos) nm = nm.substr(sl + 1); loadData(*data, nm); }
        else { Json f = Json::object(); f.set("timbient", Json::string("patch")); f.set("data", serializePatch(ui)); std::string p = r.path; if (p.find('.') == std::string::npos) p += ".json";
          Json pack = Json::object(); for (auto& up : ui.mods) if (!up->audioId.empty() && !pack.get(up->audioId)) {   // the sounds go with it, as the web version packs them
            std::string err; AudioBuf* b = decodeAudio(audioDir + up->audioId + ".wav", 600, err); if (!b) continue;
            std::vector<unsigned char> raw(b->d.size() * 2); for (size_t i = 0; i < b->d.size(); i++) { const int16_t v = (int16_t)std::lround(clampf(b->d[i], -1, 1) * 32767); raw[i * 2] = (unsigned char)v; raw[i * 2 + 1] = (unsigned char)(v >> 8); }
            Json a = Json::object(); a.set("sr", Json::number(b->sr)); a.set("b64", Json::string(b64(raw.data(), raw.size()))); pack.set(up->audioId, a); delete b; }
          if (!pack.o.empty()) f.set("audio", pack);
          ui.status(writeFile(p, f.dump()) ? "SAVED TO FILE" : "COULD NOT SAVE THE FILE", 4); }
      } }
    engine.collectGarbage();
    if (rec.on()) { rec.pump(engine); if (Module* o = outMod()) { o->disp[1].store(1); o->disp[2].store(rec.frames / rec.sr); } }
    // MIDI: notice devices coming and going; give MIDI OUT modules their device
    { static float mt = 0; midi.poll(ldt); ui.midiState = midi.state(); mt -= ldt;
      if (mt <= 0) { mt = 2; std::vector<std::string> names; bool listed = false;
        for (auto& up : ui.mods) if (up->m->def->type == "midiout" && up->m->devSlot.load() < 0) { if (!listed) { names = midi.outputs(); listed = true; }
          const std::string want = up->dev.empty() ? (names.empty() ? std::string() : names[0]) : up->dev; if (!want.empty()) up->m->devSlot.store(midi.openOutput(want)); } } }
    // a Grain Looper with nothing in it catches what it hears, every couple of seconds, until it has something
    { static float catchT = 0; catchT += ldt; if (catchT > 2) { catchT = 0; for (auto& up : ui.mods) if (up->m->def->type == "looper" && up->peaks.empty() && up->audioId.empty()) catchSound(up.get(), true); } }

    if (dirty) { saveT -= ldt; if (saveT <= 0) { store.writeAutosave(serializePatch(ui)); dirty = false; } }
    // draw only when it's time: never while the window can't be seen; at the chosen rate when idle; at full speed while you work
    { const float every = hidden ? 1e9f : (busyT > 0 || held || ui.ask) ? 0.f : 1.f / FPS[fpsIdx], since = (SDL_GetTicksNS() - last) / 1e9f;
      if (since < every) { SDL_WaitEventTimeout(nullptr, (Sint32)std::max(1.f, std::min(100.f, (every - since) * 1000))); continue; } }
    const Uint64 now = SDL_GetTicksNS(); const float dt = std::min(0.1f, (now - last) / 1e9f); last = now;
    if (W != canvas.W || H != canvas.H) canvas.resize(W, H);
    ui.draw(canvas, dt);
    if (!tex || texW != W || texH != H) { if (tex) SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, W, H); SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_PIXELART); texW = W; texH = H; }
    SDL_UpdateTexture(tex, nullptr, canvas.rgba.data(), W * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
    SDL_FRect dst{0, 0, (float)(W * scale), (float)(H * scale)}; SDL_RenderTexture(ren, tex, nullptr, &dst);
    SDL_RenderPresent(ren);
  }
  store.writeAutosave(serializePatch(ui));
  if (rec.on()) rec.stop(engine);
  sound.stop();
  SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
  return 0;
}
