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
#include "../engine/analyse.h"
#include "midi.h"
#include <cstdio>
#include <fstream>
#include <ctime>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>

using namespace tb;

static const char* VERSION = "0.58.1";
static std::atomic<Engine*> gEngine{nullptr};   // the engine the sound card renders: the session on the screen (while blending, it plays the last one too)

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
// DROPOUTS: a callback that took longer than the sound it made (the computer couldn't keep up), or one that came far later than
// it should have (it was held up). Counted here, told on screen. gDropArm: none counted until the sound has settled (just started)
static std::atomic<unsigned> gDropouts{0}; static std::atomic<bool> gDropArm{false}; static std::atomic<float> gDropRate{48000};
static void audioCallback(ma_device*, void* output, const void*, ma_uint32 frames) {
  flushDenormals();
  using clk = std::chrono::steady_clock; static clk::time_point lastStart; static bool had = false; const clk::time_point t0 = clk::now();
  float* out = (float*)output; static float L[4096], R[4096];
  ma_uint32 done = 0;
  while (done < frames) {
    const ma_uint32 n = std::min<ma_uint32>(4096, frames - done);
    gEngine.load(std::memory_order_acquire)->render(L, R, (int)n);
    for (ma_uint32 i = 0; i < n; i++) { out[(done + i) * 2] = L[i]; out[(done + i) * 2 + 1] = R[i]; }
    done += n;
  }
  const double period = frames / (double)gDropRate.load(std::memory_order_relaxed), took = std::chrono::duration<double>(clk::now() - t0).count();
  const double gap = had ? std::chrono::duration<double>(t0 - lastStart).count() : 0; lastStart = t0; had = true;
  if (gDropArm.load(std::memory_order_relaxed) && (took > period * 0.95 || gap > period * 3 + 0.004)) gDropouts.fetch_add(1, std::memory_order_relaxed);
}
static const int BUFFERS[5] = {128, 256, 512, 1024, 2048};
// QUALITY: samples per second (0: the sound output's own rate). Lower rates cost less and lose the very highest treble
// 96K and 88.2K (about twice the work of 48K) only in the 64-bit versions; the 32-bit one is for older, slower computers
static const unsigned RATES[7] = {0, 48000, 44100, 32000, 24000, 96000, 88200};
static const int NRATES = sizeof(void*) == 8 ? 7 : 5;
// the sound devices: one context to list them (by name: what SETTINGS keeps), the output it plays to, the input AUDIO IN hears
static ma_context gCtx; static bool gCtxOk = false;
static void initContext() { if (!gCtxOk) gCtxOk = ma_context_init(nullptr, 0, nullptr, &gCtx) == MA_SUCCESS; }
static std::vector<std::pair<std::string, ma_device_id>> listDevices(bool capture) {
  std::vector<std::pair<std::string, ma_device_id>> v; initContext(); if (!gCtxOk) return v;
  ma_device_info *pl = nullptr, *cp = nullptr; ma_uint32 np = 0, nc = 0; if (ma_context_get_devices(&gCtx, &pl, &np, &cp, &nc) != MA_SUCCESS) return v;
  const ma_device_info* in = capture ? cp : pl; const ma_uint32 n = capture ? nc : np; for (ma_uint32 i = 0; i < n; i++) v.push_back({in[i].name, in[i].id}); return v; }
static bool findDevice(bool capture, const std::string& name, ma_device_id& id) { if (name.empty()) return false; for (auto& d : listDevices(capture)) if (d.first == name) { id = d.second; return true; } return false; }
static void inputCallback(ma_device*, void*, const void* input, ma_uint32 frames) { Engine* e = gEngine.load(std::memory_order_acquire); if (e && input && e->input.live.load(std::memory_order_relaxed) && e->input.frames) e->input.write((const float*)input, frames); }
struct Capture {
  ma_device dev; bool open = false; std::string name;
  bool start(Engine& e, const std::string& want, int period) {
    stop(); initContext(); ma_device_config cfg = ma_device_config_init(ma_device_type_capture); ma_device_id id; const bool named = findDevice(true, want, id);
    cfg.capture.format = ma_format_f32; cfg.capture.channels = 0; cfg.capture.pDeviceID = named ? &id : nullptr; cfg.sampleRate = (ma_uint32)e.sr;
    cfg.dataCallback = inputCallback; cfg.periodSizeInFrames = (ma_uint32)period; cfg.periods = 3;
    if (ma_device_init(gCtxOk ? &gCtx : nullptr, &cfg, &dev) != MA_SUCCESS) return false;
    const int ch = std::max(1, std::min(8, (int)dev.capture.channels)); AudioInput& I = e.input; I.live.store(false); I.ch.store(ch); I.frames = 65536; I.buf.assign(I.frames * (size_t)ch, 0.f); I.w.store(0);
    I.lag.store(period * 2); name = named ? want : std::string(dev.capture.name); open = true; I.live.store(true);
    if (ma_device_start(&dev) != MA_SUCCESS) { stop(); return false; } return true; }
  void stop() { if (open) { if (Engine* e = gEngine.load()) e->input.live.store(false); ma_device_uninit(&dev); open = false; } }
};
struct Sound {
  ma_device dev; bool open = false; int bufIdx = 2; std::string want, name;   // want: the output chosen in SETTINGS (empty: the system's)
  bool start(Engine& e, unsigned rate) {
    initContext(); ma_device_config cfg = ma_device_config_init(ma_device_type_playback); ma_device_id id; const bool named = findDevice(false, want, id);
    cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = rate; cfg.playback.pDeviceID = named ? &id : nullptr;
    cfg.dataCallback = audioCallback; cfg.periodSizeInFrames = (ma_uint32)BUFFERS[bufIdx]; cfg.periods = 3;
    cfg.performanceProfile = bufIdx <= 1 ? ma_performance_profile_low_latency : ma_performance_profile_conservative;
    if (ma_device_init(gCtxOk ? &gCtx : nullptr, &cfg, &dev) != MA_SUCCESS) return false;
    gDropArm.store(false); gDropRate.store((float)dev.sampleRate);   // (dropouts counted again once it has settled)
    name = named ? want : std::string(dev.playback.name); open = true; if (e.sr != (float)dev.sampleRate) e.setSampleRate((float)dev.sampleRate);   // the engine runs at whatever rate the sound runs at
    e.outLatency = (float)(BUFFERS[bufIdx] * 3) / (float)dev.sampleRate;
    return ma_device_start(&dev) == MA_SUCCESS;
  }
  void stop() { if (open) { ma_device_uninit(&dev); open = false; } }
};

// ---------------- results from the system file dialogs (they may arrive on another thread) ----------------
struct DialogResult { bool open; std::string path; int target = 0, slot = 0; bool folder = false; std::string recFrom; bool recording = false; bool exporting = false, temp = false; };   // exporting: EXPORT WAV (recFrom the sound; temp: made just for this)   // recording: where a just-stopped recording should go (path empty: cancelled)   // target: the module a sound file is for
static std::mutex dlgMx; static std::vector<DialogResult> dlgResults;
static const SDL_DialogFileFilter JSON_FILTER[] = {{"Timbient session", "tmbnt;json;txt"}};
static void onOpenDialog(void*, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back({true, files[0]}); } }
// "calm example" in D flat YO: "calm example (db_yo)". A key-and-scale bracket already at the end (from an earlier save) is
// replaced, not added to. Lower case, flats as b, F# as #; the scale's name without spaces
std::string withKeyTag(std::string n, int root, int scale) {
  auto isTag = [](const std::string& in) { const size_t u = in.find('_'); return u != std::string::npos && u > 0 && u + 1 < in.size() && in.find(' ') == std::string::npos && in.find('_', u + 1) == std::string::npos; };
  while (!n.empty() && n.back() == ' ') n.pop_back();
  if (!n.empty() && n.back() == ')') { const size_t o = n.rfind('('); if (o != std::string::npos && isTag(n.substr(o + 1, n.size() - o - 2))) { n.resize(o); while (!n.empty() && n.back() == ' ') n.pop_back(); } }
  std::string k; for (const char* p = KEY_NAMES[((root % 12) + 12) % 12]; *p; p++) k += *p == '\x02' ? 'b' : (char)std::tolower((unsigned char)*p);
  std::string sc; const auto& all = scales(); if (scale >= 0 && scale < (int)all.size()) for (char ch : std::string(all[scale].name)) if (ch != ' ') sc += (char)std::tolower((unsigned char)ch);
  return (n.empty() ? n : n + " ") + "(" + k + "_" + sc + ")";
}
static void onBlendDialog(void*, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); DialogResult r{true, files[0]}; r.target = -2; dlgResults.push_back(r); } }   // BLEND TO
static void onSaveDialog(void*, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back({false, files[0]}); } }
static void onFolderDialog(void* ud, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); DialogResult r{true, files[0]}; r.folder = true; r.target = ud ? -1 : 0; dlgResults.push_back(r); } }
static const SDL_DialogFileFilter AUDIO_FILTER[] = {{"Sound files", "wav;wave;aif;aiff;flac;mp3"}};
static const SDL_DialogFileFilter WAV_FILTER[] = {{"WAV sound file", "wav"}};
static const SDL_DialogFileFilter GROUP_FILTER[] = {{"Timbient group", "tmgroup"}};
static void onGroupDialog(void*, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); DialogResult r{true, files[0]}; r.target = -3; dlgResults.push_back(r); } }   // ADD > GROUPS > OPEN A GROUP FILE
// EXPORT WAV: where it goes (ud: the sound's file, and whether that was made just for this; a heap pair, freed here)
static void onExportDialog(void* ud, const char* const* files, int) { auto* from = (std::pair<std::string, bool>*)ud; DialogResult r{false, files && files[0] ? files[0] : ""}; r.exporting = true; r.recFrom = from->first; r.temp = from->second; delete from;
  std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back(r); }
// where to save a recording that has just stopped: ud is the recording as it was made (a heap string, freed here)
static void onRecDialog(void* ud, const char* const* files, int) { std::string* from = (std::string*)ud; DialogResult r{false, files && files[0] ? files[0] : ""}; r.recording = true; r.recFrom = *from; delete from;
  std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back(r); }
// a sound file chosen for a Sample or Grain Looper: ud carries the module id and the place (-1: fill the empty places, one per file)
static void onAudioDialog(void* ud, const char* const* files, int) {
  if (!files) return; const intptr_t v = (intptr_t)ud; const int id = (int)(v >> 8), slot = (int)(v & 0xff) - 1; std::lock_guard<std::mutex> l(dlgMx);
  for (int i = 0; files[i]; i++) dlgResults.push_back({true, files[i], id, slot});
}
// a note named in a file name ("cello_C3.wav", "Pad F#2", "bass-Bb1"): its MIDI number, or -1
static int noteInName(std::string n) {
  const size_t sl = n.find_last_of("/\\"); if (sl != std::string::npos) n = n.substr(sl + 1); const size_t dot = n.rfind('.'); if (dot != std::string::npos) n = n.substr(0, dot);
  static const int base[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
  for (int i = (int)n.size() - 1; i >= 0; i--) { const char c = (char)std::toupper((unsigned char)n[i]); if (c < 'A' || c > 'G') continue;
    if (i > 0 && std::isalpha((unsigned char)n[i - 1])) continue;   // must start a word
    size_t j = i + 1; int acc = 0; if (j < n.size() && (n[j] == '#' || n[j] == 's')) { acc = 1; j++; } else if (j < n.size() && n[j] == 'b') { acc = -1; j++; }
    bool neg = false; if (j < n.size() && n[j] == '-') { neg = true; j++; } if (j >= n.size() || !std::isdigit((unsigned char)n[j])) continue;
    const int oct = (n[j] - '0') * (neg ? -1 : 1); if (j + 1 < n.size() && std::isalnum((unsigned char)n[j + 1])) continue;
    return std::max(0, std::min(127, 12 * (oct + 1) + base[c - 'A'] + acc)); }
  return -1;
}
static std::string b64(const unsigned char* d, size_t n) {
  static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; o.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) { const unsigned v = (d[i] << 16) | ((i + 1 < n ? d[i + 1] : 0) << 8) | (i + 2 < n ? d[i + 2] : 0);
    o += A[(v >> 18) & 63]; o += A[(v >> 12) & 63]; o += i + 1 < n ? A[(v >> 6) & 63] : '='; o += i + 2 < n ? A[v & 63] : '='; }
  return o;
}
static std::string fileUrl(std::string p) { for (auto& c : p) if (c == '\\') c = '/'; return p.size() && p[0] == '/' ? "file://" + p : "file:///" + p; }

// every sound a patch (or a group) uses, by id (each once)
static std::vector<std::string> soundIds(const Json& data) {
  std::vector<std::string> ids; auto want = [&](const std::string& id) { if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id); };
  if (const Json* ms = data.get("mods")) if (ms->t == Json::ARR) for (auto& m : ms->a) if (const Json* p = m.get("p")) { want(p->str("audio"));
    if (const Json* sn = p->get("sounds")) if (sn->t == Json::ARR) for (auto& e : sn->a) if (e.t == Json::ARR && !e.a.empty() && e.a[0].t == Json::STR) want(e.a[0].s); }
  return ids; }
int main(int argc, char** argv) {
  // Mac: fullscreen simply covers the screen (not a Space of its own), so the menu bar never drops down over the top bar
  SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0"); SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "0");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1; }

  // the app's own folder for sessions, the autosave and settings
  SDL_Window* win = nullptr;
  SessionStore store; { char* pp = SDL_GetPrefPath("Timbient", "Timbient Modules"); store.dir = pp ? pp : "./"; SDL_free(pp); }
  store.load();
  Json settings; Json::parse(readFile(store.path("settings.json")), settings); if (settings.t != Json::OBJ) settings = Json::object();
  auto saveSettings = [&] { writeFile(store.path("settings.json"), settings.dump()); };

  // ---- sound first, so the engine knows the sample rate before any module is built ----
  // two engines: the session on the screen plays in one; BLEND TO loads the next session into the other and fades across
  static Engine engA, engB; Engine* curEng = &engA; gEngine.store(&engA);
  static Sound sound; sound.bufIdx = std::max(0, std::min(4, (int)settings.num("buffer", 2))); sound.want = settings.str("outputDevice");
  static Capture capture; float captureT = 0, captureIdle = 0;   // AUDIO IN's input: opened while there's an AUDIO IN (so nothing asks for the microphone until then)
  int rateIdx = std::max(0, std::min(NRATES - 1, (int)settings.num("rate", 0)));
  if (!sound.start(*curEng, RATES[rateIdx]) && !sound.want.empty()) sound.want.clear();   // the chosen output isn't there: the system's
  if (!sound.open && !sound.start(*curEng, RATES[rateIdx]) && !(rateIdx != 0 && sound.start(*curEng, 0))) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Timbient Modules", "Could not open the sound device.", nullptr); return 1; }

  Ui ui(*curEng);
  static Midi midi(*curEng); midi.alsoSendFrom(engB); midi.start();
  // the app opens to an empty session; RESTORE LAST WORK brings back what was there
  { Json prev; if (store.readAutosave(prev) && prev.get("mods") && prev.get("mods")->a.size() > 1) writeFile(store.path("previous.json"), prev.dump()); }
  ui.add("out", 300, 30);

  bool dirty = false; float saveT = 0;
  // UNDO / REDO: the patch as it was after each change (settled for a moment), up to 60 steps back
  std::vector<std::string> hist; int hpos = -1; bool restoring = false, histDirty = false; float histT = 0;
  std::string savedSnap; bool markSaved = true, neverSaved = true;   // the session as last opened or saved (so the top bar can show a • for changes since)
  auto snapshot = [&] { std::string sn = serializePatch(ui).dump(); if (markSaved) { savedSnap = sn; markSaved = false; } if (hpos >= 0 && hist[hpos] == sn) return;
    hist.resize(hpos + 1); hist.push_back(sn); if (hist.size() > 60) hist.erase(hist.begin()); hpos = (int)hist.size() - 1; };
  ui.onChange = [&] { dirty = true; saveT = 1.0f; if (!restoring) { histDirty = true; histT = 0.4f; } };
  snapshot();   // the empty session is the first step
  // ---- sounds held by Sample and Grain Looper modules: kept as WAV files in the app's folder, named by id ----
  const std::string audioDir = store.dir + "audio/"; SDL_CreateDirectory(audioDir.c_str());
  auto giveSound = [&](UiMod* u, AudioBuf* b, bool keep, int slot = 0, const std::string& name = "", std::vector<float>* pk = nullptr) {   // hand a sound to a module (and keep a copy so sessions bring it back); pk: its waveform, if worked out already
    UiMod::Snd& sd = u->snd[slot]; if (pk) sd.peaks = std::move(*pk); else sd.peaks = peaksOf(*b); sd.missing = false; sd.secs = b->d.size() / b->sr; if (keep) sd.name = name;
    if (keep) { char id[40]; std::snprintf(id, 40, "n%llx%04x", (unsigned long long)std::time(nullptr), (unsigned)(SDL_rand(65536)));
      if (writeWavMono16(audioDir + id + ".wav", *b)) { sd.id = id; dirty = true; saveT = 0.5f; } else ui.status("COULD NOT KEEP THE SOUND", 4); }
    u->m->roots[slot].store(sd.root); (*curEng).setBuffer(u->m->id, b, slot);
    if (keep && u->m->def->type == "looper") u->m->p[0].store(1);   // GRAIN LOOPER: a sound caught or loaded is HELD
  };
  // sounds already read (with their waveforms) away from the screen, ready to hand over: a blend's, while it starts
  std::map<std::string, std::pair<AudioBuf*, std::vector<float>>>* readySounds = nullptr;
  auto restoreSound = [&](UiMod* u) {
    for (int k = 0; k < UiMod::SLOTS; k++) { UiMod::Snd& sd = u->snd[k]; if (sd.id.empty()) continue; std::string err;
      if (readySounds) { auto it = readySounds->find(sd.id); if (it != readySounds->end() && it->second.first) { AudioBuf* b = it->second.first; it->second.first = nullptr; giveSound(u, b, false, k, "", &it->second.second); continue; } }
      AudioBuf* b = decodeAudio(audioDir + sd.id + ".wav", 600, err, true);
      if (b) giveSound(u, b, false, k); else { sd.missing = true; ui.status(u->m->def->name + ": A SOUND IS NOT HERE. LOAD IT AGAIN", 6); } }
  };
  auto catchSound = [&](UiMod* u, bool quiet) {   // GRAIN LOOPER: CATCH takes the last LENGTH seconds of what came in
    Module* m = u->m; if (m->ring.empty()) return; const long N = (long)m->ring.size(), w = m->ringW.load(std::memory_order_acquire);
    const long n = std::min(std::min(N, w), (long)(m->p[m->def->paramIndex("len")].load() * (*curEng).sr)); if (n < 256) { if (!quiet) ui.status("NOTHING TO CATCH YET"); return; }
    AudioBuf* b = new AudioBuf(); b->sr = (*curEng).sr; b->d.resize(n); float mx = 0; const int ci = m->def->paramIndex("chan"); const bool st = ci >= 0 && m->p[ci].load() > 0.5f && m->ringR.size() == m->ring.size();
    if (st) b->r.resize(n);   // CHANNELS STEREO: left and right
    for (long i = 0; i < n; i++) { const long k = (w - n + i) % N; b->d[i] = st ? 2 * m->ring[k] - m->ringR[k] : m->ring[k]; mx = std::max(mx, std::fabs(b->d[i])); if (st) { b->r[i] = m->ringR[k]; mx = std::max(mx, std::fabs(b->r[i])); } }
    if (mx < 0.002f) { delete b; if (!quiet) ui.status("NOTHING TO CATCH YET"); return; }
    for (auto& v : b->d) v *= 0.8f / mx; for (auto& v : b->r) v *= 0.8f / mx;
    giveSound(u, b, true); ui.status("CAUGHT " + std::to_string((int)std::lround(n / (*curEng).sr)) + "S");
  };
  auto savedNow = [&] { savedSnap = serializePatch(ui).dump(); markSaved = false; neverSaved = false; dirty = true; saveT = 0.5f; };
  auto loadData = [&](const Json& data, const std::string& what) {
    const std::string note = loadPatch(ui, data); ui.centrePending = true; dirty = true; saveT = 0.5f;   // the view: to the middle of the session
    ui.status(note.empty() ? "LOADED " + what : note, note.empty() ? 3 : 8);
    for (auto& up : ui.mods) restoreSound(up.get());
    histDirty = true; histT = 0.1f; markSaved = true;   // (opened as it was saved: no changes yet)
  };
  auto restoreStep = [&](int to) {   // go back (or forward) to a remembered step, keeping the view where it is
    if (curEng->blending()) { ui.status(to < hpos ? "UNDO WAITS FOR THE BLEND TO FINISH" : "REDO WAITS FOR THE BLEND TO FINISH", 2); return; }   // (a blend is two sessions: undo leaves it alone)
    if (histDirty) { snapshot(); histDirty = false; }
    if (to < 0 || to >= (int)hist.size() || to == hpos) { ui.status(to < hpos ? "NOTHING TO UNDO" : "NOTHING TO REDO"); return; }
    const bool back = to < hpos; hpos = to; Json j; if (!Json::parse(hist[hpos], j)) return;
    const int vx = ui.viewX, vy = ui.viewY; restoring = true; std::vector<std::pair<UiMod*, int>> sc; applyPatch(ui, j, sc);   // only what changed: sequencers keep going
    for (auto& [u, k] : sc) { UiMod::Snd& sd = u->snd[k];
      if (sd.id.empty()) { (*curEng).setBuffer(u->m->id, nullptr, k); sd.peaks.clear(); continue; }
      std::string err; AudioBuf* b = decodeAudio(audioDir + sd.id + ".wav", 600, err, true); if (b) giveSound(u, b, false, k); else sd.missing = true; }
    restoring = false;
    ui.viewX = vx; ui.viewY = vy; dirty = true; saveT = 0.5f; ui.status(back ? "UNDONE" : "REDONE", 1.2f);
  };
  // DUPLICATE: the module again, a little down and to the right, with all its settings and what it holds (steps, notes,
  // events, sounds) but no cables. Done as a change to the patch, like undo, so nothing else stops or reloads
  bool ctxClick = false;
  ui.onDuplicate = [&](UiMod* src) {
    if (histDirty) { snapshot(); histDirty = false; }
    Json data = serializePatch(ui); Json* mods = data.getm("mods"); if (!mods || mods->t != Json::ARR) return;
    int maxId = 0; const Json* mine = nullptr; for (auto& m : mods->a) { maxId = std::max(maxId, (int)m.num("id")); if ((int)m.num("id") == src->m->id) mine = &m; }
    if (!mine) return; Json copy = *mine; const int nid = maxId + 1;
    copy.set("id", Json::number(nid)); copy.set("x", Json::number(src->x + 12)); copy.set("y", Json::number(src->y + 12)); mods->push(copy);
    std::vector<std::pair<UiMod*, int>> sc; applyPatch(ui, data, sc);
    for (auto& [u, k] : sc) { UiMod::Snd& sd = u->snd[k]; if (sd.id.empty()) continue;
      std::string err; AudioBuf* b = decodeAudio(audioDir + sd.id + ".wav", 600, err, true); if (b) giveSound(u, b, false, k); else sd.missing = true; }
    ui.lastTouched = nid; ui.status("DUPLICATED " + src->m->def->name, 2); dirty = true; saveT = 0.5f; histDirty = true; histT = 0.1f;
  };
  // ---- RECORD on the Output: straight to a WAV file in Music / Timbient Modules ----
  std::string recDir; { const char* mu = SDL_GetUserFolder(SDL_FOLDER_MUSIC); recDir = std::string(mu ? mu : store.dir.c_str()) + "Timbient Modules/"; SDL_CreateDirectory(recDir.c_str()); }
  Recorder rec;
  auto outMod = [&]() -> Module* { for (auto& up : ui.mods) if (up->m->def->type == "out") return up->m; return nullptr; };
  // ---- the sound browser: hear a file before it goes in ----
  auto browseDir = [&]() -> std::string { const std::string d = settings.str("browseDir"); if (!d.empty()) return d; const char* mu = SDL_GetUserFolder(SDL_FOLDER_MUSIC); return mu ? mu : store.dir; };
  ui.onPreview = [&](const std::string& path) {
    if (path.empty()) { (*curEng).preview(nullptr); return; }
    std::string err; AudioBuf* b = decodeAudio(path, 1200, err, true); if (!b) { (*curEng).preview(nullptr); ui.status(err, 4); return; }   // heard as it is, stereo or not
    float mx = 0; for (float v : b->d) mx = std::max(mx, std::fabs(v)); for (float v : b->r) mx = std::max(mx, std::fabs(v)); if (mx > 1e-4f && mx < 0.5f) { for (auto& v : b->d) v *= 0.5f / mx; for (auto& v : b->r) v *= 0.5f / mx; }   // quiet files a little louder, so you can hear them
    (*curEng).preview(b); };
  // the sound browser's details: worked out on a thread of their own, so the screen never waits (only the newest ask matters)
  struct InfoJob { std::mutex mx; std::condition_variable cv; std::string want; bool stop = false; bool ready = false; std::string path; SoundInfo info; int ch = 0; float rate = 0; double secs = 0; bool failed = false; };
  static InfoJob job;
  static std::thread infoThread([] {
    for (;;) { std::string p; { std::unique_lock<std::mutex> l(job.mx); job.cv.wait(l, [] { return job.stop || !job.want.empty(); }); if (job.stop) return; p = job.want; job.want.clear(); }
      int ch = 0; float rate = 0; double secs = 0; std::string err; const bool ok = soundFileInfo(p, ch, rate, secs); SoundInfo si; bool failed = !ok;
      if (ok) { AudioBuf* b = decodeAudio(p, 90, err); if (b) { si = analyseSound(*b); if (secs <= 0) secs = b->d.size() / b->sr; delete b; } else failed = true; }
      std::lock_guard<std::mutex> l(job.mx); job.ready = true; job.path = p; job.info = si; job.ch = ch; job.rate = rate; job.secs = secs; job.failed = failed; } });
  ui.onInfo = [](const std::string& p) { std::lock_guard<std::mutex> l(job.mx); job.want = p; job.cv.notify_one(); };
  auto pumpInfo = [&] { std::lock_guard<std::mutex> l(job.mx); if (!job.ready) return; job.ready = false;
    if (job.path == ui.br.infoPath) { ui.br.info = job.info; ui.br.channels = job.ch; ui.br.fileRate = job.rate; ui.br.secs = job.secs; ui.br.infoFailed = job.failed; ui.br.infoReady = true; } };
  ui.onBrowseLoad = [&](int id, int slot, const std::string& path) {
    settings.set("browseDir", Json::string(ui.br.dir)); saveSettings();
    std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back({true, path, id, slot}); };
  ui.onList = [](const std::string& dir, std::vector<Ui::Browser::Entry>& out) {
    struct Ctx { std::string dir; std::vector<Ui::Browser::Entry>* out; } ctx{dir, &out};
    return SDL_EnumerateDirectory(dir.c_str(), [](void* ud, const char* d, const char* name) -> SDL_EnumerationResult {
      Ctx* c = (Ctx*)ud; SDL_PathInfo info; std::string full = d; if (!full.empty() && full.back() != '/' && full.back() != '\\') full += "/"; full += name;
      const bool folder = SDL_GetPathInfo(full.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY; c->out->push_back({name, folder}); return SDL_ENUM_CONTINUE; }, &ctx); };
  ui.br.autoPreview = settings.num("autoPreview", 1) > 0.5; ui.br.wide = settings.num("browseWide", 0) > 0.5;
  ui.onEditPlay = [&](const std::vector<float>& d, const std::vector<float>& r, float sr, double a, double e, bool loop) {
    if (d.empty()) { (*curEng).preview(nullptr); return; } AudioBuf* b = new AudioBuf(); b->sr = sr; b->d = d; b->r = r; (*curEng).preview(b, a, e, loop); };
  ui.onEditCrop = [&](int id, int slot, std::vector<float>&& d, std::vector<float>&& rr, float sr, const std::string& name) {   // KEEP: this becomes the module's sound (kept, so sessions bring it back)
    UiMod* u = ui.byId(id); if (!u) { ui.status("ITS MODULE HAS GONE", 3); return; } AudioBuf* b = new AudioBuf(); b->sr = sr; b->d = std::move(d); b->r = std::move(rr); const double secs = b->d.size() / sr;
    giveSound(u, b, true, slot, name.empty() ? "EDITED" : name);
    if (u->m->def->type == "sample") { const int ks = u->m->def->paramIndex("start"), kl = u->m->def->paramIndex("len"); if (ks >= 0) u->m->p[ks].store(0); if (kl >= 0) u->m->p[kl].store(1); }   // the whole of the new sound
    char t[48]; std::snprintf(t, 48, "KEPT: %.2fS", secs); ui.status(t, 4); histDirty = true; histT = 0.1f; dirty = true; saveT = 0.3f; };
  ui.onBrowseChanged = [&] { settings.set("autoPreview", Json::number(ui.br.autoPreview ? 1 : 0)); settings.set("browseWide", Json::number(ui.br.wide ? 1 : 0)); saveSettings(); };
  ui.onBrowseFolder = [&]() { const std::string d = ui.br.dir; SDL_ShowOpenFolderDialog(onFolderDialog, nullptr, win, d.empty() ? nullptr : d.c_str(), false); };
  ui.onBrowseFile = [&](int id, int slot) { const UiMod* u = ui.byId(id); const bool many = slot < 0 && u && u->m->def->type == "sample";
    SDL_ShowOpenFileDialog(onAudioDialog, (void*)(((intptr_t)id << 8) | (intptr_t)(slot + 1)), win, AUDIO_FILTER, 1, nullptr, many); };
  // QWERTY MIDI: which computer key plays each of its 36 notes (C-0 to B-2), kept in the settings by name.
  // The default is SunVox's: Z S X D C V G B H N J M, then Q 2 W 3 E R 5 T 6 Y 7 U, then I 9 O 0 P [ = ]
  static const char* QW_DEFAULT[36] = {"Z", "S", "X", "D", "C", "V", "G", "B", "H", "N", "J", "M",
                                       "Q", "2", "W", "3", "E", "R", "5", "T", "6", "Y", "7", "U",
                                       "I", "9", "O", "0", "P", "[", "=", "]", "", "", "", ""};
  SDL_Keycode qwMap[36];
  auto qwNames = [&] { for (int i = 0; i < 36; i++) { std::string s = qwMap[i] ? SDL_GetKeyName(qwMap[i]) : ""; for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch); ui.qwKeys[i] = s; } };
  // a second ready-made layout: three rows of the keyboard, the black notes on the row above where they fall (bottom octave first)
  static const char* QW_ROWS[36] = {"Y", "1", "U", "2", "I", "O", "3", "P", "4", "A", "5", "S",
                                    "D", "6", "F", "7", "G", "H", "8", "J", "9", "K", "0", "L",
                                    "Z", "Q", "X", "W", "C", "V", "E", "B", "R", "N", "T", "M"};
  auto qwSet = [&](const char* const* L) { for (int i = 0; i < 36; i++) qwMap[i] = L[i][0] ? SDL_GetKeyFromName(L[i]) : 0; };
  auto qwDefault = [&] { qwSet(QW_DEFAULT); };
  auto qwSave = [&] { Json a = Json::array(); for (int i = 0; i < 36; i++) a.push(Json::string(qwMap[i] ? SDL_GetKeyName(qwMap[i]) : "")); settings.set("qwertyMap", a); saveSettings(); qwNames(); };
  qwDefault();
  if (const Json* a = settings.get("qwertyMap")) if (a->t == Json::ARR && a->a.size() == 36) for (int i = 0; i < 36; i++) qwMap[i] = a->a[i].t == Json::STR && !a->a[i].s.empty() ? SDL_GetKeyFromName(a->a[i].s.c_str()) : 0;
  qwNames();
  // QWERTY MIDI: the layouts you've saved (a name, and the key for each of the 36 notes), kept with the settings
  std::vector<std::pair<std::string, std::vector<std::string>>> qwMine;
  if (const Json* a = settings.get("qwertyLayouts")) if (a->t == Json::ARR) for (auto& x : a->a) {
    const Json* nm = x.get("name"); const Json* ks = x.get("keys"); if (!nm || nm->t != Json::STR || !ks || ks->t != Json::ARR || ks->a.size() != 36) continue;
    std::vector<std::string> v; for (auto& k : ks->a) v.push_back(k.t == Json::STR ? k.s : ""); qwMine.push_back({nm->s, v}); }
  auto qwMineSave = [&] { Json a = Json::array(); for (auto& [nm, ks] : qwMine) { Json o = Json::object(); o.set("name", Json::string(nm)); Json k = Json::array(); for (auto& x : ks) k.push(Json::string(x)); o.set("keys", k); a.push(o); }
    settings.set("qwertyLayouts", a); saveSettings(); };
  // ---- EXPORT WAV: a module's sound (or the editor's, edits and all) to a WAV file, wherever you choose ----
  auto exportWav = [&](const std::string& from, std::string name, bool temp) {
    for (auto& ch : name) ch = (ch == '/' || ch == '\\' || ch == ':') ? ' ' : (char)std::tolower((unsigned char)ch);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".wav") name.resize(name.size() - 4); if (name.empty()) name = "sound";
    std::string dir = settings.str("exportDir"); if (dir.empty()) dir = recDir; if (dir.back() != '/' && dir.back() != '\\') dir += "/";
    const std::string start = dir + name + ".wav";
    SDL_ShowSaveFileDialog(onExportDialog, new std::pair<std::string, bool>(from, temp), win, WAV_FILTER, 1, start.c_str()); };
  ui.onAction = [&](UiMod* u, const std::string& k) -> bool {
    const std::string& ty = u->m->def->type;
    if (ty == "qwerty" && k == "reset") {   // KEY LAYOUTS: the ready-made ones to choose from (your own changes are kept until you pick one)
      // the two ready-made layouts, then yours, then SAVE THIS LAYOUT (and REMOVE, once there's one to remove)
      Dropdown l; l.items = {"PIANO LAYOUT (Z S X D ...)", "TIMBIENT LAYOUT (Y 1 U 2 ...)"}; for (auto& m : qwMine) l.items.push_back(m.first);
      const int nMine = (int)qwMine.size(); l.items.push_back("SAVE THIS LAYOUT..."); if (nMine) l.items.push_back("REMOVE A SAVED LAYOUT...");
      l.x = u->x - ui.viewX + 12; l.y = u->y - ui.viewY + 74;
      l.pick = [&, u, nMine](int i) {
        if (i < 2) { qwSet(i == 0 ? QW_DEFAULT : QW_ROWS); qwSave(); ui.qwLearn = -1; ui.status(std::string("QWERTY MIDI: ") + (i == 0 ? "THE PIANO LAYOUT" : "THE TIMBIENT LAYOUT"), 3); return; }
        if (i < 2 + nMine) { const auto& m = qwMine[i - 2]; for (int k = 0; k < 36; k++) qwMap[k] = m.second[k].empty() ? 0 : SDL_GetKeyFromName(m.second[k].c_str()); qwSave(); ui.qwLearn = -1; ui.status("QWERTY MIDI: " + m.first, 3); return; }
        if (i == 2 + nMine) {   // SAVE THIS LAYOUT: a name for the keys as they are now (the same name again replaces it)
          ui.askText("NAME THIS LAYOUT", "MY LAYOUT", [&](const std::string& nm0) { std::string nm = nm0; for (auto& ch : nm) ch = (char)std::toupper((unsigned char)ch); if (nm.empty()) return;
            std::vector<std::string> ks; for (int k = 0; k < 36; k++) ks.push_back(qwMap[k] ? SDL_GetKeyName(qwMap[k]) : "");
            bool replaced = false; for (auto& m : qwMine) if (m.first == nm) { m.second = ks; replaced = true; } if (!replaced) qwMine.push_back({nm, ks});
            qwMineSave(); ui.status((replaced ? "QWERTY MIDI: UPDATED " : "QWERTY MIDI: SAVED ") + nm + " (IT'S IN KEY LAYOUTS)", 4); }); return; }
        // REMOVE A SAVED LAYOUT: which one, then sure?
        Dropdown r; for (auto& m : qwMine) r.items.push_back(m.first); r.x = u->x - ui.viewX + 12; r.y = u->y - ui.viewY + 74;
        r.pick = [&, u](int j) { if (j < 0 || j >= (int)qwMine.size()) return; const std::string nm = qwMine[j].first;
          Dropdown c; c.items = {"REMOVE " + nm, "KEEP IT"}; c.x = u->x - ui.viewX + 12; c.y = u->y - ui.viewY + 74;
          c.pick = [&, nm](int q) { if (q != 0) return; for (size_t z = 0; z < qwMine.size(); z++) if (qwMine[z].first == nm) { qwMine.erase(qwMine.begin() + z); break; } qwMineSave(); ui.status("QWERTY MIDI: REMOVED " + nm, 3); };
          ui.openDropdown(c); };
        ui.openDropdown(r); };
      ui.openDropdown(l); return true; }
    if ((ty == "sample" || ty == "looper" || ty == "stretch") && k == "edit") {   // EDIT SOUND: the editor, with this module's sound (for SAMPLE, the place on show)
      const int slot = ty == "sample" ? std::max(0, std::min(UiMod::SLOTS - 1, u->shown)) : 0; const UiMod::Snd& sd = u->snd[slot];
      if (sd.id.empty()) { ui.status("THERE'S NO SOUND TO EDIT YET: LOAD ONE FIRST", 4); return true; }
      std::string err; AudioBuf* b = decodeAudio(audioDir + sd.id + ".wav", 600, err, true); if (!b) { ui.status(err, 4); return true; }
      ui.openEditor(u->m->id, slot, sd.name, std::move(b->d), b->sr, std::move(b->r)); delete b; return true; }
    if ((ty == "sample" || ty == "looper" || ty == "stretch") && (k == "load" || k.rfind("load:", 0) == 0)) {   // LOAD FILE: KIT and MULTI take several at once, into the empty places
      const bool many = ty == "sample" && u->m->p[1].load() >= 2 && k == "load"; const int slot = k == "load" ? (many ? -1 : 0) : std::atoi(k.c_str() + 5);
      ui.openBrowser(u->m->id, slot, browseDir()); return true; }
    if (ty == "looper" && k == "catch") { catchSound(u, false); return true; }
    if (ty == "audioin" && k == "pick") {   // DEVICE: the app's input (every AUDIO IN hears the same one)
      const auto devs = listDevices(true); Dropdown q; q.x = u->x - ui.viewX + 8; q.y = u->y - ui.viewY + 20; q.items.push_back("SYSTEM DEFAULT"); q.cur = 0; const std::string cur = settings.str("inputDevice");
      for (size_t k2 = 0; k2 < devs.size(); k2++) { q.items.push_back(devs[k2].first); if (devs[k2].first == cur) q.cur = (int)k2 + 1; }
      q.pick = [&, devs](int j) { if (j > (int)devs.size()) return; const std::string nm = j == 0 ? std::string() : devs[j - 1].first; settings.set("inputDevice", Json::string(nm)); saveSettings();
        if (!capture.start(*curEng, nm, BUFFERS[sound.bufIdx])) ui.status("COULD NOT OPEN THAT INPUT", 5); else ui.status("INPUT: " + capture.name + " (FOR EVERY AUDIO IN)", 3); };
      ui.openDropdown(q); return true; }
    if ((ty == "sample" || ty == "looper" || ty == "stretch") && k == "export") {   // EXPORT WAV: the sound (for SAMPLE, the place on show) to a file
      const UiMod::Snd& sd = u->snd[ty == "sample" ? std::max(0, std::min(UiMod::SLOTS - 1, u->shown)) : 0];
      if (sd.id.empty()) { ui.status("THERE'S NO SOUND TO EXPORT YET", 3); return true; }
      std::string nm = sd.name; if (nm.empty()) { char ts[32]; const std::time_t tt = std::time(nullptr); std::strftime(ts, 32, "take %Y-%m-%d %H%M", std::localtime(&tt)); nm = ts; }
      exportWav(audioDir + sd.id + ".wav", nm, false); return true; }
    if (ty == "out" && k == "rec") {
      if (!rec.on()) { std::time_t tt = std::time(nullptr); char nm[64]; std::strftime(nm, 64, "timbient-%Y%m%d-%H%M%S.wav", std::localtime(&tt));
        if (rec.start(*curEng, recDir + nm)) ui.status("RECORDING"); else ui.status("COULD NOT START A RECORDING IN " + recDir, 6); }
      else { const std::string p = rec.path; if (Engine* src = curEng->blending()) rec.pump(*src); const bool got = rec.stop(*curEng); if (Module* o = outMod()) o->disp[1].store(0);
        if (got) { ui.status("RECORDING STOPPED: CHOOSE WHERE TO SAVE IT", 6);   // ask where it goes (it's safe in Music / Timbient Modules meanwhile)
          SDL_ShowSaveFileDialog(onRecDialog, new std::string(p), win, WAV_FILTER, 1, p.c_str()); }
        else { std::remove(p.c_str()); ui.status("NOTHING RECORDED"); } }
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

  // ---- sessions are files: a .tmbnt holds the patch and every sound it uses (inside it's the web version's
  // session format, so a .json session renamed to .tmbnt opens too, and the web version can open a .tmbnt) ----
  std::string curFile;   // the file this session was opened from or last saved to: SAVE writes there
  // SESSIONS: the app's own picker opens in the folder sessions last came from (or went to), else beside the current one, else Documents
  auto sessionsDir = [&]() -> std::string { std::string d = settings.str("sessionDir");
    if (d.empty() && !curFile.empty()) { const size_t sl = curFile.find_last_of("/\\"); if (sl != std::string::npos) d = curFile.substr(0, sl + 1); }
    if (d.empty()) { const char* dc = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS); d = dc ? dc : store.dir; } return d; };
  auto pickSession = [&](bool blend) { ui.openSessions(sessionsDir(), blend); };
  ui.onSessionPick = [&](const std::string& path, bool blend) {   // as the system's window would have answered (the same way in, below)
    const size_t sl = path.find_last_of("/\\"); if (sl != std::string::npos) { settings.set("sessionDir", Json::string(path.substr(0, sl + 1))); saveSettings(); }
    std::lock_guard<std::mutex> l(dlgMx); DialogResult r{true, path}; if (blend) r.target = -2; dlgResults.push_back(r); };
  ui.onSessionOther = [&](bool blend) { const std::string d = sessionsDir(); SDL_ShowOpenFileDialog(blend ? onBlendDialog : onOpenDialog, nullptr, win, JSON_FILTER, 1, d.c_str(), false); };
  auto sessionChanged = [&] { return !markSaved && (savedSnap.empty() || (hpos >= 0 && hist[hpos] != savedSnap)); };   // (changed since it was opened, saved or started)
  auto autosaveData = [&] { Json j = serializePatch(ui); j.set("file", Json::string(curFile)); j.set("changed", Json::number(sessionChanged() ? 1 : 0)); return j; };
  auto soundPack = [&](const Json& data) {   // every sound a patch (or a group) uses, packed in base64 (to go inside its file)
    const std::vector<std::string> ids = soundIds(data);
    Json pack = Json::object(); for (auto& id : ids) {   // the sounds go inside it
      std::string err; AudioBuf* b = decodeAudio(audioDir + id + ".wav", 3600, err, true); if (!b) continue; const int ch = b->stereo() ? 2 : 1;   // a stereo sound: left and right, interleaved
      std::vector<unsigned char> raw(b->d.size() * 2 * ch); for (size_t i = 0; i < b->d.size() * ch; i++) { const float x = ch == 2 ? (i & 1 ? b->r[i / 2] : b->d[i / 2]) : b->d[i]; const int16_t v = (int16_t)std::lround(clampf(x, -1, 1) * 32767); raw[i * 2] = (unsigned char)v; raw[i * 2 + 1] = (unsigned char)(v >> 8); }
      Json a = Json::object(); a.set("sr", Json::number(b->sr)); if (ch == 2) a.set("ch", Json::number(2)); a.set("b64", Json::string(b64(raw.data(), raw.size()))); pack.set(id, a); delete b; }
    return pack; };
  auto unpackSounds = [&](const Json& f) {   // a file's packed sounds into the app's folder (any already here are left as they are)
    if (const Json* pack = f.get("audio")) if (pack->t == Json::OBJ) for (auto& kv : pack->o) { const std::string fn = audioDir + kv.first + ".wav"; if (!readFile(fn).empty()) continue;
      AudioBuf* b = fromBase64Pcm16(kv.second.str("b64"), (float)kv.second.num("sr", 48000), (int)kv.second.num("ch", 1)); if (b) { writeWavMono16(fn, *b); delete b; } } };
  auto sessionFile = [&](const Json& data) {
    Json f = Json::object(); f.set("timbient", Json::string("patch")); f.set("data", data);
    Json pack = soundPack(data); if (!pack.o.empty()) f.set("audio", pack);
    return f.dump();
  };
  auto fileName = [](std::string p) { const size_t sl = p.find_last_of("/\\"); if (sl != std::string::npos) p = p.substr(sl + 1); const size_t dt = p.rfind('.'); if (dt != std::string::npos && dt > 0) p = p.substr(0, dt);
    for (auto& ch : p) ch = (char)std::toupper((unsigned char)ch); return p; };
  static std::string saveName;   // SAVE AS: a name first (the dice suggests some), then where it goes
  auto saveAs = [&] { ui.askName("NAME THIS SESSION", [&](const std::string& n) { std::string fn; for (char ch : n) fn += (ch == '/' ? '-' : (char)std::tolower((unsigned char)ch));
      const bool tag = ui.askChecked > 0; settings.set("keyInName", Json::number(tag ? 1 : 0)); saveSettings();
      if (tag) fn = withKeyTag(fn, curEng->key.root.load(), curEng->key.scale.load());   // "calm example (db_yo)"
      if (fn.empty()) fn = "session"; saveName = fn + ".tmbnt";
      std::string start = saveName; if (!curFile.empty()) { const size_t sl = curFile.find_last_of("/\\"); if (sl != std::string::npos) start = curFile.substr(0, sl + 1) + saveName; }
      SDL_ShowSaveFileDialog(onSaveDialog, nullptr, win, JSON_FILTER, 1, start.c_str()); });
    if (ui.ask) { ui.ask->check = settings.num("keyInName", 0) > 0.5 ? 1 : 0; ui.ask->checkLabel = "ADD KEY AND SCALE";
      if (!curFile.empty()) { std::string n = fileName(curFile); const size_t dot = n.rfind('.'); if (dot != std::string::npos && dot > 0) n.resize(dot); ui.ask->text = n; } } };   // a saved session: its own name to start from (NEW NAME: a random one)
  // ---- GROUPS: several modules together, with the cables between them. COPY puts them on the clipboard (as text), PASTE adds
  // them to whatever session is open, at the pointer; SAVE AS A GROUP keeps them as a file (with their sounds) in the groups folder,
  // and ADD > GROUPS lists those. OUTPUT is never copied, but cables into it are: pasted, they go into this session's OUTPUT ----
  static const bool isMac = std::string(SDL_GetPlatform()) == "macOS"; const std::string cmdKey = isMac ? "CMD" : "CTRL";
  const std::string groupsDir = recDir + "groups/";
  // the modules (positions from the group's top left corner) and the cables between them; x0, y0: where that corner was
  auto groupData = [&](const std::vector<int>& ids, int& x0, int& y0) -> Json { return groupOf(ui, ids, x0, y0); };
  // add a group's modules to the patch on screen, their top left corner at (wx, wy), as one change; they become the selection
  auto pasteGroup = [&](const Json& g, int wx, int wy) -> int {
    if (histDirty) { snapshot(); histDirty = false; }
    std::vector<std::pair<UiMod*, int>> sc; bool unknown = false; const std::vector<int> made = addGroup(ui, g, wx, wy, sc, unknown);
    if (made.empty()) { ui.status(unknown ? "THOSE MODULES AREN'T IN THIS VERSION" : "NOTHING TO PASTE", 3); return 0; }
    for (auto& [u, k] : sc) { UiMod::Snd& sd = u->snd[k]; if (sd.id.empty()) continue;
      std::string err; AudioBuf* b = decodeAudio(audioDir + sd.id + ".wav", 600, err, true); if (b) giveSound(u, b, false, k); else sd.missing = true; }
    dirty = true; saveT = 0.5f; histDirty = true; histT = 0.1f;
    return (int)made.size(); };
  auto here = [&](int& wx, int& wy) { wx = (int)ui.lastPX + ui.viewX; wy = std::max(ui.viewY + 12, (int)ui.lastPY + ui.viewY); };   // the pointer, on the patch (below the top bar)
  auto nMods = [](int n) { return std::to_string(n) + (n == 1 ? " MODULE" : " MODULES"); };
  ui.onDuplicateMany = [&](const std::vector<int>& ids) {
    int x0, y0; const Json g = groupData(ids, x0, y0); if (g.t != Json::OBJ) return;
    const int n = pasteGroup(g, x0 + 12, y0 + 12); if (n) ui.status("DUPLICATED " + nMods(n) + " (THE COPIES ARE SELECTED)", 3); };
  ui.onCopy = [&](const std::vector<int>& ids) {
    int x0, y0; const Json g = groupData(ids, x0, y0); if (g.t != Json::OBJ) { ui.status("NOTHING TO COPY (OUTPUT STAYS WITH ITS SESSION)", 3); return; }
    Json f = Json::object(); f.set("timbient", Json::string("group")); f.set("data", g); SDL_SetClipboardText(f.dump().c_str());
    ui.status("COPIED " + nMods((int)g.get("mods")->a.size()) + ": " + cmdKey + "+V PASTES THEM HERE OR IN ANOTHER SESSION", 4); };
  ui.onPaste = [&] {
    char* t = SDL_GetClipboardText(); Json f; const bool ok = t && Json::parse(t, f); if (t) SDL_free(t);
    if (!ok || f.str("timbient") != "group" || !f.get("data")) { ui.status("NOTHING TO PASTE: COPY SOME MODULES FIRST (" + cmdKey + "+C)", 3); return; }
    unpackSounds(f); int wx, wy; here(wx, wy); const int n = pasteGroup(*f.get("data"), wx, wy); if (n) ui.status("PASTED " + nMods(n), 2); };
  auto addGroupFile = [&](const std::string& path) {   // ADD > GROUPS: a group file, added near the top left of the view
    Json f; if (!Json::parse(readFile(path), f) || f.str("timbient") != "group" || !f.get("data")) { ui.status("THAT IS NOT A TIMBIENT GROUP", 4); return; }
    unpackSounds(f); const int n = pasteGroup(*f.get("data"), ui.viewX + 40, ui.viewY + 30);
    std::string nm = path.substr(path.find_last_of("/\\") + 1); const size_t dot = nm.rfind('.'); if (dot != std::string::npos && dot > 0) nm.resize(dot);
    for (auto& ch : nm) ch = (char)std::toupper((unsigned char)ch); if (n) ui.status("ADDED THE GROUP " + nm + " (" + nMods(n) + ", SELECTED: DRAG A TITLE TO MOVE THEM)", 4); };
  ui.onSaveGroup = [&](const std::vector<int>& ids) {
    int x0, y0; const Json g = groupData(ids, x0, y0); if (g.t != Json::OBJ) { ui.status("NOTHING TO SAVE (OUTPUT STAYS WITH ITS SESSION)", 3); return; }
    ui.askName("NAME THIS GROUP", [&, g](const std::string& n) { std::string fn; for (char ch : n) fn += (ch == '/' || ch == '\\' || ch == ':') ? '-' : (char)std::tolower((unsigned char)ch);
      while (!fn.empty() && fn.back() == ' ') fn.pop_back(); if (fn.empty()) fn = "group";
      SDL_CreateDirectory(groupsDir.c_str()); std::string path = groupsDir + fn + ".tmgroup"; for (int k = 2; !readFile(path).empty(); k++) path = groupsDir + fn + " " + std::to_string(k) + ".tmgroup";
      Json f = Json::object(); f.set("timbient", Json::string("group")); f.set("data", g); Json pack = soundPack(g); if (!pack.o.empty()) f.set("audio", pack);
      std::string shown = path.substr(path.find_last_of("/\\") + 1); shown.resize(shown.size() - 8); for (auto& ch : shown) ch = (char)std::toupper((unsigned char)ch);
      ui.status(writeFile(path, f.dump()) ? "SAVED THE GROUP " + shown + ": IT'S IN ADD > GROUPS" : std::string("COULD NOT SAVE THE GROUP"), 5); }); };
  ui.onGroups = [&](int x, int y) {   // the saved groups, A to Z; then a file from anywhere, and the folder itself
    std::vector<std::string> names; SDL_EnumerateDirectory(groupsDir.c_str(), [](void* ud, const char*, const char* name) -> SDL_EnumerationResult {
      const std::string n = name; if (n.size() > 8 && n.substr(n.size() - 8) == ".tmgroup") ((std::vector<std::string>*)ud)->push_back(n.substr(0, n.size() - 8)); return SDL_ENUM_CONTINUE; }, &names);
    std::sort(names.begin(), names.end());
    Dropdown q; q.x = x; q.y = y; q.title = 0; q.items = {names.empty() ? "NO GROUPS SAVED YET (RIGHT-CLICK MODULES: SAVE AS A GROUP)" : "GROUPS"};
    for (auto& n : names) { std::string s2 = n; for (auto& ch : s2) ch = (char)std::toupper((unsigned char)ch); q.items.push_back(s2); }
    const int nn = (int)names.size(); q.items.push_back("OPEN A GROUP FILE..."); q.items.push_back("SHOW THE GROUPS FOLDER");
    q.pick = [&, names, nn](int i) { if (i >= 1 && i <= nn) { addGroupFile(groupsDir + names[i - 1] + ".tmgroup"); return; }
      if (i == nn + 1) { SDL_ShowOpenFileDialog(onGroupDialog, nullptr, win, GROUP_FILTER, 1, groupsDir.c_str(), false); return; }
      if (i == nn + 2) { SDL_CreateDirectory(groupsDir.c_str()); SDL_OpenURL(fileUrl(groupsDir).c_str()); ui.status("OPENING THE GROUPS FOLDER"); } };
    ui.openDropdown(q); };
  ui.onEditExport = [&](const std::string& name, const std::vector<float>& d, const std::vector<float>& r, float sr) {
    AudioBuf b; b.sr = sr; b.d = d; b.r = r; const std::string tmp = store.dir + "export-" + std::to_string(SDL_GetTicks()) + ".wav";
    if (!writeWavMono16(tmp, b)) { ui.status("COULD NOT EXPORT THE SOUND", 4); return; }
    exportWav(tmp, name.empty() ? "edited" : name, true); };
  // ---- BLEND TO: the next session loads into the other engine, then fades in as this one fades out (equal loudness) ----
  // The session being left keeps playing in its own engine (the new one plays it inside itself), and its screen is set aside,
  // so CANCEL can fade back to it just as it was. The new one's tempo glides from the old one's to its own over the blend
  static const int BLEND_SECS[6] = {10, 30, 60, 120, 240, 8};   // the last: MANUAL (it moves only by hand; 8 s is how its NOW and CANCEL are timed)
  int blendIdx = std::max(0, std::min(5, (int)settings.num("blendLength", 1))); const int MANUAL = 5; float blendRest = 0;
  struct BlendLoad { std::thread th; std::atomic<int> state{0}; std::string path, err; Json data;   // state: 0 none, 1 reading, 2 ready, 3 failed
    std::map<std::string, std::pair<AudioBuf*, std::vector<float>>> sounds;   // its sounds, read and drawn already (so starting it is quick)
    void dropSounds() { for (auto& kv : sounds) delete kv.second.first; sounds.clear(); } };
  static BlendLoad bl; Ui::Stash blendStash; std::string blendOldFile, blendOldSnap; bool blendOldNever = true; std::vector<std::string> blendOldHist; int blendOldHpos = -1; float blendBpm0 = 0, blendBpm1 = 0, blendClearT = -1; bool blendGlide = false;
  auto blendLabel = [&](int k) { if (k == MANUAL) return std::string("MANUAL"); const int v = BLEND_SECS[k]; return v < 60 ? std::to_string(v) + " S" : std::to_string(v / 60) + " MIN"; };
  auto otherEng = [&]() -> Engine& { return curEng == &engA ? engB : engA; };
  auto blendBusy = [&](bool say) { const bool b = curEng->blending() != nullptr || bl.state.load() == 1 || bl.state.load() == 2;
    if (b && say) ui.status("WAIT FOR THE BLEND TO FINISH (OR NOW, OR CANCEL)", 4); return b; };
  auto handRecording = [&](Engine& from, Engine& to) {   // a recording carries straight on in the engine taking over
    if (!rec.on()) return; if (to.recBuf.size() != from.recBuf.size()) to.recBuf.assign(from.recBuf.size(), 0.f); to.recR = to.recW.load(); to.recOn.store(true, std::memory_order_release); };
  auto blendLoad = [&](const std::string& path) {   // read, unpack and check the file away from the screen
    if (blendBusy(true)) return; if (bl.th.joinable()) bl.th.join();
    bl.path = path; bl.err.clear(); bl.state.store(1); ui.status("GETTING " + fileName(path) + " READY TO BLEND...", 30);
    bl.th = std::thread([&, path] { Json f; if (!Json::parse(readFile(path), f)) { bl.err = "THAT FILE COULD NOT BE READ"; bl.state.store(3); return; }
      if (const Json* pack = f.get("audio")) if (pack->t == Json::OBJ) for (auto& kv : pack->o) { const std::string fn = audioDir + kv.first + ".wav"; if (!readFile(fn).empty()) continue;
        AudioBuf* b = fromBase64Pcm16(kv.second.str("b64"), (float)kv.second.num("sr", 48000), (int)kv.second.num("ch", 1)); if (b) { writeWavMono16(fn, *b); delete b; } }
      const Json* data = patchIn(f); if (!data) { bl.err = "THAT IS NOT A TIMBIENT SESSION"; bl.state.store(3); return; }
      bl.dropSounds(); for (auto& id : soundIds(*data)) { std::string err; AudioBuf* b = decodeAudio(audioDir + id + ".wav", 600, err, true); if (b) bl.sounds[id] = {b, peaksOf(*b)}; }   // every sound, ready
      bl.data = *data; bl.state.store(2); }); };
  auto blendStart = [&] {   // (on the screen's thread, once the file is ready)
    Engine& from = *curEng; Engine& to = otherEng(); to.collectGarbage();
    to.setSampleRate(from.sr); to.outLatency = from.outLatency; to.noteLimit.store(from.noteLimit.load()); to.freeze.store(false); to.input.live.store(false);
    to.key.root.store(from.key.root.load()); to.key.scale.store(from.key.scale.load()); to.tempo.bpm.store(from.tempo.bpm.load());   // (a file without them carries on with these)
    blendOldFile = curFile; blendBpm0 = from.tempo.bpm.load(); ui.stashPatch(blendStash);
    handRecording(from, to); to.blendFrom(&from, BLEND_SECS[blendIdx]); curEng = &to; ui.setEngine(to); gEngine.store(&to, std::memory_order_release); midi.setTarget(to);
    if (capture.open) capture.start(to, settings.str("inputDevice"), BUFFERS[sound.bufIdx]);
    blendOldSnap = savedSnap; blendOldNever = neverSaved; blendOldHist.swap(hist); blendOldHpos = hpos; hist.clear(); hpos = -1;   // the new session's undo starts with it
    readySounds = &bl.sounds; loadData(bl.data, fileName(bl.path)); readySounds = nullptr; bl.dropSounds(); curFile = bl.path; neverSaved = false; bl.data = Json();
    blendBpm1 = to.tempo.bpm.load(); to.tempo.bpm.store(blendBpm0); blendGlide = std::fabs(blendBpm1 - blendBpm0) > 0.01f;
    if (blendIdx != MANUAL) to.blendGo(); ui.blendAt = 0; ui.blendName = fileName(bl.path); blendClearT = -1; blendRest = 0;
    ui.status(blendIdx == MANUAL ? "BLEND BY HAND TO " + fileName(bl.path) + ": DRAG THE BAR (OR A MIDI KNOB)" : "BLENDING TO " + fileName(bl.path) + " OVER " + blendLabel(blendIdx), 5); };
  ui.onBlendSet = [&](float u) { if (!curEng->blending() || blendClearT >= 0) return; curEng->blendPause(); curEng->blendSet(u); };
  ui.onBlendPause = [&] { if (!curEng->blending() || blendClearT >= 0) return; if (curEng->blendRunning()) { curEng->blendPause(); ui.status("BLEND PAUSED", 1.5f); } else { curEng->blendGo(); ui.status("BLEND GOING ON", 1.5f); } };
  ui.onBlendNow = [&] { if (curEng->blending() && ui.blendAt >= 0 && ui.blendAt < 1) { curEng->blendFinish(3); curEng->blendGo(); ui.status("FINISHING THE BLEND", 2); } };
  ui.onBlendCancel = [&] {   // back the way it came: the session being left fades back in, as it was; the new one fades out (as long as it took to get here, at most 4 s)
    Engine* back = curEng->blending(); if (!back || ui.blendAt < 0 || ui.blendAt >= 1 || !blendStash.held) return;
    Engine& now = *curEng; const float p = std::max(0.02f, now.blendAt.load()); const double rest = std::min(4.0, (double)p * BLEND_SECS[blendIdx]);
    Ui::Stash gone; ui.stashPatch(gone); gone = Ui::Stash();   // (its modules stay in the engine being left, which is cleared when the fade ends)
    ui.unstashPatch(blendStash); handRecording(now, *back);
    back->blendFrom(&now, rest / p, 1 - p); back->blendGo(); curEng = back; gEngine.store(back, std::memory_order_release); now.blendRelease(); midi.setTarget(*back);
    if (capture.open) capture.start(*back, settings.str("inputDevice"), BUFFERS[sound.bufIdx]);
    curFile = blendOldFile; savedSnap = blendOldSnap; neverSaved = blendOldNever; markSaved = false; blendGlide = false; hist.swap(blendOldHist); hpos = blendOldHpos; blendOldHist.clear(); /* its own undo, as it was */ ui.blendName = fileName(blendOldFile); dirty = true; saveT = 0.5f; histDirty = true; histT = 0.1f; ui.status("BACK TO " + (blendOldFile.empty() ? std::string("THE LAST SESSION") : fileName(blendOldFile)), 3); };
  auto blendFrame = [&](float dt) {   // each frame (dt: the time since the last, in seconds): start a blend that's ready, follow one that's going, and tidy up after one that's done
    const int st = bl.state.load(); if (st == 3) { bl.state.store(0); bl.dropSounds(); ui.status(bl.err, 5); } else if (st == 2) { bl.state.store(0); blendStart(); }
    Engine* src = curEng->blending(); if (!src) { ui.blendAt = -1; return; }
    const float p = curEng->blendAt.load();
    if (blendClearT < 0) {
      ui.blendAt = std::max(0.f, p); if (blendGlide) curEng->tempo.bpm.store(blendBpm0 + (blendBpm1 - blendBpm0) * std::max(0.f, p));
      if (curEng->cpuLoad.load() > 0.92f) ui.status("THE TWO SESSIONS TOGETHER ARE A LOT FOR THIS COMPUTER", 2);
      ui.blendPaused = !curEng->blendRunning();
      blendRest = p >= 0.999f && !curEng->blendRunning() ? blendRest + dt : 0;   // by hand at the far end: it finishes once it has rested there 2 s
      if ((p >= 1 && curEng->blendRunning()) || blendRest >= 2) { if (blendGlide) curEng->tempo.bpm.store(blendBpm1); src->clear(); blendClearT = 0.4f; blendStash = Ui::Stash(); ui.blendAt = -1; ui.status("BLENDED: " + ui.blendName, 3); } }
    else if ((blendClearT -= dt) <= 0) { curEng->blendRelease(); blendClearT = -1; } };

  std::function<void(int)> openSessions = [&](int x0) {
    Dropdown d; d.x = x0; d.y = 11;
    d.items = {"NEW EMPTY SESSION", "RESTORE LAST WORK", "OPEN...", "BLEND TO...", "BLEND LENGTH  " + blendLabel(blendIdx), curFile.empty() ? "SAVE..." : "SAVE  " + fileName(curFile), "SAVE AS..."};
    if (!store.list.empty()) { d.items.push_back("SAVE THE " + std::to_string(store.list.size()) + " OLD IN-APP SESSIONS AS FILES..."); d.items.push_back("REMOVE THE OLD IN-APP SESSIONS"); }
    d.pick = [&, x0](int i) {
      if (i <= 2 && blendBusy(true)) return;   // (a new session can't come in halfway through a blend)
      if (i == 3) { pickSession(true); return; }
      if (i == 4) { Dropdown q; q.x = x0; q.y = 11 + 4 * 8; q.title = 0; q.items = {"BLEND LENGTH"}; for (int k = 0; k < 6; k++) q.items.push_back(blendLabel(k)); q.cur = blendIdx + 1;
        q.pick = [&](int j) { if (j < 1) return; blendIdx = j - 1; settings.set("blendLength", Json::number(blendIdx)); saveSettings(); ui.status(blendIdx == MANUAL ? std::string("BLENDS MOVE ONLY BY HAND") : "BLENDS TAKE " + blendLabel(blendIdx), 2); }; ui.openDropdown(q); return; }
      if (i >= 5) i -= 2;
      if (i == 0) { Dropdown q; q.x = x0; q.y = 11; q.items = {"CLEAR EVERYTHING?", "YES, START EMPTY", "NO, KEEP IT"}; q.title = 0; q.danger = 1;
        q.pick = [&](int j) { if (j == 1) { ui.clearPatch(); ui.add("out", 300, 30); curFile.clear(); neverSaved = true; markSaved = true; dirty = true; saveT = 0.3f; histDirty = true; histT = 0.1f; ui.status("NEW EMPTY SESSION"); } }; ui.openDropdown(q); return; }
      if (i == 1) { Json prev; if (Json::parse(readFile(store.path("previous.json")), prev) && prev.get("mods")) { loadData(prev, "YOUR LAST WORK"); curFile = prev.str("file"); neverSaved = curFile.empty(); if (prev.num("changed", 1) > 0.5) { markSaved = false; savedSnap.clear(); } } else ui.status("NOTHING TO RESTORE"); return; }
      if (i == 2) { pickSession(false); return; }
      if (i == 3) { if (curFile.empty()) saveAs(); else if (writeFile(curFile, sessionFile(serializePatch(ui)))) { savedNow(); ui.status("SAVED " + fileName(curFile), 4); } else ui.status("COULD NOT SAVE THE FILE", 4); return; }
      if (i == 4) { saveAs(); return; }
      if (i == 5) { SDL_ShowOpenFolderDialog(onFolderDialog, (void*)1, win, nullptr, false); ui.status("PICK A FOLDER FOR THEM", 4); return; }
      if (i == 6) { Dropdown z; z.x = x0; z.y = 11 + (i + 2) * 8; z.items = {"REMOVE THEM FROM THE APP?", "YES (I SAVED THEM AS FILES)", "NO, KEEP THEM"}; z.title = 0; z.danger = 1;
        z.pick = [&](int q2) { if (q2 == 1) { store.list.clear(); store.save(); ui.status("THE OLD IN-APP SESSIONS ARE GONE"); } }; ui.openDropdown(z); }
    };
    ui.openDropdown(d);
  };

  // ---- THEME: background, module and cable colours, remembered between launches ----
  static Canvas canvas;
  Theme theme = themeFrom(settings.get("theme") ? *settings.get("theme") : Json());
  const bool pondReady = false;   // the pond background arrives in a later update
  if (theme.bg == "pond" && !pondReady) theme.bg = "#000000";
  int menus = std::max(0, std::min(2, (int)settings.num("menus", 0)));   // THEME > MENUS: 0 MATCH, 1 BLACK, 2 DARK
  static const char* MENUS[3] = {"MATCH", "BLACK", "DARK"};
  applyTheme(canvas, theme, menus);
  std::vector<Theme> recent;   // RANDOMISE: the last few it made (and the theme from before them), for PREVIOUS; kept between launches
  if (const Json* h = settings.get("themeRecent")) if (h->t == Json::ARR) for (auto& x : h->a) recent.push_back(themeFrom(x));
  auto saveRecent = [&] { Json a = Json::array(); for (auto& x : recent) a.push(themeJson(x)); settings.set("themeRecent", a); };
  auto setTheme = [&](const Theme& t) { theme = t; applyTheme(canvas, theme, menus); settings.set("theme", themeJson(theme)); saveSettings(); };
  int letters = std::max(0, std::min(2, (int)settings.num("letters", 0)));   // 0 fine lowercase, 1 fine capitals, 2 the classic small capitals
  static const char* LETTERS[3] = {"LOWERCASE", "CAPITALS", "CLASSIC"};
  std::function<std::string()> letterReport = [] { return std::string(); };   // what LETTERS is doing on this screen (set once the window is made)
  ui.shadows = settings.num("shadows", 1) > 0.5; ui.borders = settings.num("borders", 0) > 0.5;
  std::function<void(int)> openTheme = [&](int x0) {
    auto nameOf = [](const std::vector<Choice>& list, const std::string& v) { for (auto& c : list) if (v == c.second) return std::string(c.first); return std::string("CUSTOM"); };
    std::string preset = "CUSTOM"; for (auto& p : presets()) if (sameTheme(p.second, theme)) preset = p.first;
    const char* labels[6] = {"PRESET", "BACKGROUND", "MODULES", "AUDIO CABLES", "TRIG CABLES", "MOD CABLES"};
    Dropdown d; d.x = x0; d.y = 11;
    d.items = {std::string("PRESET  ") + preset, "BACKGROUND  " + nameOf(bgChoices(), theme.bg), "MODULES  " + nameOf(modChoices(), theme.mod), "AUDIO CABLES  " + nameOf(cableChoices(), theme.audio),
      "TRIG CABLES  " + nameOf(cableChoices(), theme.trig), "MOD CABLES  " + nameOf(cableChoices(), theme.modc), std::string("LETTERS  ") + LETTERS[letters], std::string("MENUS  ") + MENUS[menus], std::string("SHADOWS  ") + (ui.shadows ? "ON" : "OFF"), std::string("BORDERS  ") + (ui.borders ? "ON" : "OFF"), "RANDOMISE", recent.empty() ? std::string("PREVIOUS  (NONE YET)") : "PREVIOUS  (" + std::to_string(recent.size()) + " KEPT)", "RESET THEME"};
    d.pick = [&, x0, labels](int i) {
      if (i == 6) { Dropdown q; q.x = x0; q.y = 11 + i * 8; q.items = {"LOWERCASE  (FINER LETTERS)", "CAPITALS  (FINER LETTERS)", "CLASSIC  (THE ORIGINAL SMALL CAPITALS)"}; q.cur = letters;
        q.pick = [&](int j) { letters = j; settings.set("letters", Json::number(j)); saveSettings();
          ui.status(letterReport(), 6); }; ui.openDropdown(q); return; }
      if (i == 7) { Dropdown q; q.x = x0; q.y = 11 + i * 8; q.items = {"MATCH  (A SHADE OF THE BACKGROUND)", "BLACK", "DARK  (A DARK SHADE OF THE BACKGROUND)"}; q.cur = menus;
        q.pick = [&](int j) { menus = j; settings.set("menus", Json::number(j)); saveSettings(); applyTheme(canvas, theme, menus); ui.status(std::string("MENUS ") + MENUS[j], 2); }; ui.openDropdown(q); return; }
      if (i == 8) { ui.shadows = !ui.shadows; settings.set("shadows", Json::number(ui.shadows ? 1 : 0)); saveSettings(); ui.status(ui.shadows ? "SHADOWS UNDER THE MODULES" : "NO SHADOWS", 2); openTheme(x0); return; }
      if (i == 9) { ui.borders = !ui.borders; settings.set("borders", Json::number(ui.borders ? 1 : 0)); saveSettings(); ui.status(ui.borders ? "A LINE ROUND EACH MODULE" : "NO BORDERS", 2); openTheme(x0); return; }
      if (i >= 10) i -= 4;   // RANDOMISE, PREVIOUS and RESET come after LETTERS, MENUS, SHADOWS and BORDERS
      if (i == 6) {   // a new theme from one colour, pale or dark (or now and then mixed); the one it replaces is kept (the last 12)
        recent.push_back(theme); if (recent.size() > 12) recent.erase(recent.begin()); saveRecent();
        setTheme(randomTheme([](int n) { return (int)SDL_rand(n); }));
        ui.status("A RANDOM THEME: TAP RANDOMISE FOR ANOTHER, PREVIOUS TO GO BACK", 3); openTheme(x0); return; }   // stays open, to roll again
      if (i == 7) { if (recent.empty()) { ui.status("NO RANDOM THEMES YET", 2); openTheme(x0); return; }
        const Theme t = recent.back(); recent.pop_back(); saveRecent(); setTheme(t); ui.status("THE THEME BEFORE (" + std::to_string(recent.size()) + " MORE KEPT)", 2); openTheme(x0); return; }
      if (i == 8) { setTheme(Theme{}); ui.status("THEME RESET"); return; }
      Dropdown q; q.x = x0; q.y = 11 + (i ? i * 8 : 0); const int SW = 64;
      if (i == 0) { int k = 0; for (auto& p : presets()) { canvas.setColour(SW + k, p.second.mod.c_str()); q.items.push_back(p.first); q.sw.push_back(SW + k); if (sameTheme(p.second, theme)) q.cur = k; k++; }
        q.pick = [&](int j) { setTheme(presets()[j].second); ui.status(std::string("THEME ") + presets()[j].first + "  (TAP AWAY WHEN DONE)"); }; q.stay = true; ui.openDropdown(q); return; }
      const std::vector<Choice>& list = i == 1 ? bgChoices() : i == 2 ? modChoices() : cableChoices();
      std::string Theme::*field = i == 1 ? &Theme::bg : i == 2 ? &Theme::mod : i == 3 ? &Theme::audio : i == 4 ? &Theme::trig : &Theme::modc;
      int k = 0; for (auto& c : list) { if (i == 1 && std::string(c.second) == "pond" && !pondReady) continue; canvas.setColour(SW + k, std::string(c.second) == "pond" ? "#1d4a52" : c.second); q.items.push_back(c.first); q.sw.push_back(SW + k); if (theme.*field == c.second) q.cur = k; k++; }
      const std::string label = labels[i];
      q.pick = [&, field, label, i](int j) { std::vector<Choice> l2; const std::vector<Choice>& list2 = i == 1 ? bgChoices() : i == 2 ? modChoices() : cableChoices();
        for (auto& c : list2) if (!(i == 1 && std::string(c.second) == "pond" && !pondReady)) l2.push_back(c);
        Theme t = theme; t.*field = l2[j].second; setTheme(t); ui.status(label + " " + l2[j].first + "  (TAP AWAY WHEN DONE)"); };
      q.stay = true; ui.openDropdown(q);
    };
    ui.openDropdown(d);
  };

  // ---- SCREEN: how often the picture is redrawn when nothing is being touched (it always runs at full speed while you work) ----
  static const int FPS[3] = {60, 30, 20};
  int fpsIdx = std::max(0, std::min(2, (int)settings.num("fps", 1)));
  ui.cableMotion = settings.num("cableMotion", 1) > 0.5;
  // MIDI LEARN: values go back to the devices (on unless turned off); the EVERY SESSION mappings live in the settings
  ui.midiFeedback = settings.num("midiFeedback", 1) > 0.5; if (const Json* g = settings.get("midiGlobal")) ui.midiMapsLoad(*g, true, nullptr);
  ui.onMidiSend = [&](const std::string& dev, const unsigned char* b, int n) { return midi.sendTo(dev, b, n); };
  // inputs switched off in SETTINGS > MIDI > DEVICES (kept by name in the settings)
  { std::vector<std::string> off; if (const Json* o = settings.get("midiOff")) if (o->t == Json::ARR) for (auto& e : o->a) if (e.t == Json::STR) off.push_back(e.s); midi.setOff(off); }
  ui.onMidiDevToggle = [&](const std::string& name) { std::vector<std::string> off = midi.offList(); auto it = std::find(off.begin(), off.end(), name); const bool nowOff = it == off.end();
    if (nowOff) off.push_back(name); else off.erase(it); midi.setOff(off); Json a = Json::array(); for (auto& n : off) a.push(Json::string(n)); settings.set("midiOff", a); saveSettings();
    std::string up = name; for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch); ui.status("MIDI: " + up + (nowOff ? " SWITCHED OFF" : " SWITCHED ON"), 2); };
  // NOTEPAD: the app's own note, kept with the settings (saved a moment after it changes, and on quitting)
  if (const Json* np = settings.get("notepad")) if (UiMod* p = ui.padMod()) {
    p->text = np->str("text"); p->noteW = std::max(60, (int)np->num("w", 150)); p->noteH = std::max(16, (int)np->num("h", 110));
    ui.padX = (int)np->num("x", -1); ui.padY = (int)np->num("y", -1); ui.padOpen = np->num("open", 0) > 0.5; p->fold = np->num("fold", 0) > 0.5; }
  float padSaveT = -1;
  auto savePad = [&] { if (!ui.pad) return; Json j = Json::object(); j.set("text", Json::string(ui.pad->text)); j.set("w", Json::number(ui.pad->noteW)); j.set("h", Json::number(ui.pad->noteH));
    j.set("x", Json::number(ui.padX)); j.set("y", Json::number(ui.padY)); j.set("open", Json::number(ui.padOpen ? 1 : 0)); j.set("fold", Json::number(ui.pad->fold ? 1 : 0)); settings.set("notepad", j); saveSettings(); };
  ui.onGlobalMaps = [&] { settings.set("midiGlobal", ui.midiMapsJson(true)); settings.set("midiFeedback", Json::number(ui.midiFeedback ? 1 : 0)); saveSettings(); };
  ui.helpOn = settings.num("help", 1) > 0.5;   // HELP starts on; switched off, it stays off
  ui.onHelpToggle = [&](bool on) { settings.set("help", Json::number(on ? 1 : 0)); saveSettings(); };
  // ---- SETTINGS menu: key, scale, shared tempo, audio buffer, theme ----
  std::function<void(int)> openSettings = [&](int x0) {
    Dropdown d; d.x = x0; d.y = 11;
    const std::string bpm = "BPM  " + bpmText((*curEng).tempo.bpm.load());
    char buf[64]; std::snprintf(buf, 64, "AUDIO BUFFER  %d (%.0f MS)", BUFFERS[sound.bufIdx], BUFFERS[sound.bufIdx] * 3 * 1000.0 / (*curEng).sr);
    d.items = {std::string("KEY  ") + KEY_NAMES[(*curEng).key.root.load()], std::string("SCALE  ") + scales()[(*curEng).key.scale.load()].name, bpm, buf, "OUTPUT  " + (sound.want.empty() ? std::string("SYSTEM DEFAULT") : sound.want), "INPUT  " + (settings.str("inputDevice").empty() ? std::string("SYSTEM DEFAULT") : settings.str("inputDevice")), std::string("QUALITY  ") + (rateIdx == 0 ? "AUTO " : "") + std::to_string((int)std::lround((*curEng).sr / 100) / 10) + "." + std::to_string((int)std::lround((*curEng).sr / 100) % 10) + "K", "SCREEN  " + std::to_string(FPS[fpsIdx]) + " FPS", std::string("CABLE MOTION  ") + (ui.cableMotion ? "ON" : "OFF"), "THEME", "ABOUT AND CREDITS", "MIDI", "SEND FEEDBACK"};
    d.title = -1;
    d.pick = [&, x0](int i) {
      Dropdown q; q.x = x0; q.y = 11;
      if (i == 0) { for (int k = 0; k < 12; k++) q.items.push_back(std::string("KEY ") + KEY_NAMES[k]); q.cur = (*curEng).key.root.load();
        q.pick = [&](int j) { (*curEng).key.root.store(j); ui.status(std::string("KEY ") + KEY_NAMES[j]); dirty = true; saveT = 0.5f; }; }
      else if (i == 1) { for (auto& s : scales()) q.items.push_back(s.name); q.cur = (*curEng).key.scale.load();
        q.pick = [&](int j) { (*curEng).key.scale.store(j); ui.status(std::string("SCALE ") + scales()[j].name); dirty = true; saveT = 0.5f; }; }
      else if (i == 2) { ui.openTempo(x0, 11 + 2 * 8); return; }
      else if ((i == 3 || i == 4 || i == 6) && blendBusy(true)) return;   // (the sound restarting, or a new rate, halfway through a blend)
      else if (i == 3) { for (int k = 0; k < 5; k++) { char b[48]; std::snprintf(b, 48, "%d  (%.0f MS)%s", BUFFERS[k], BUFFERS[k] * 3 * 1000.0 / (*curEng).sr, k == 2 ? "  DEFAULT" : ""); q.items.push_back(b); } q.cur = sound.bufIdx;
        q.pick = [&](int j) { if (j == sound.bufIdx) return; sound.stop(); sound.bufIdx = j; settings.set("buffer", Json::number(j)); saveSettings();
          if (!sound.start(*curEng, (unsigned)(*curEng).sr)) ui.status("COULD NOT RESTART THE SOUND DEVICE", 6); else ui.status("AUDIO BUFFER " + std::to_string(BUFFERS[j]));
          if (capture.open) capture.start(*curEng, settings.str("inputDevice"), BUFFERS[j]); }; }
      else if (i == 4 || i == 5) {   // OUTPUT / INPUT: which device (by name; SYSTEM DEFAULT follows the computer's own choice)
        const bool cap = i == 5; const auto devs = listDevices(cap); const std::string cur = cap ? settings.str("inputDevice") : sound.want;
        q.items.push_back("SYSTEM DEFAULT"); q.cur = 0; for (size_t k = 0; k < devs.size(); k++) { q.items.push_back(devs[k].first); if (devs[k].first == cur) q.cur = (int)k + 1; }
        if (devs.empty()) q.items.push_back(cap ? "(NO INPUTS FOUND)" : "(NO OUTPUTS FOUND)");
        q.pick = [&, cap, devs](int j) { if (j > (int)devs.size()) return; const std::string nm = j == 0 ? std::string() : devs[j - 1].first;
          if (cap) { settings.set("inputDevice", Json::string(nm)); saveSettings(); if (capture.open) { if (!capture.start(*curEng, nm, BUFFERS[sound.bufIdx])) ui.status("COULD NOT OPEN THAT INPUT", 5); } ui.status("INPUT: " + (nm.empty() ? std::string("SYSTEM DEFAULT") : nm), 3); return; }
          sound.stop(); sound.want = nm; settings.set("outputDevice", Json::string(nm)); saveSettings();
          if (!sound.start(*curEng, (unsigned)(*curEng).sr) && !sound.start(*curEng, RATES[rateIdx])) { sound.want.clear(); sound.start(*curEng, RATES[rateIdx]); ui.status("COULD NOT OPEN THAT OUTPUT: BACK TO THE SYSTEM'S", 5); }
          else ui.status("OUTPUT: " + sound.name, 3);
          if (capture.open) capture.start(*curEng, settings.str("inputDevice"), BUFFERS[sound.bufIdx]); }; }
      else if (i == 6) { q.items = {"AUTO  (YOUR SOUND OUTPUT'S OWN RATE)", "48K", "44.1K", "32K  LIGHTER", "24K  LIGHTEST, LESS TREBLE"};
        if (NRATES > 5) { q.items.push_back("96K  HEAVIER: ABOUT TWICE THE WORK"); q.items.push_back("88.2K  HEAVIER"); } q.cur = rateIdx;
        q.pick = [&](int j) { if (j == rateIdx) return;
          if (rec.on()) { rec.stop(*curEng); if (Module* o = outMod()) o->disp[1].store(0); }   // a recording can't change rate halfway
          const std::string snap = serializePatch(ui).dump(); const int vx = ui.viewX, vy = ui.viewY;
          sound.stop(); rateIdx = j; settings.set("rate", Json::number(j)); saveSettings();
          if (!sound.start(*curEng, RATES[j])) { rateIdx = 0; sound.start(*curEng, 0); ui.status("THAT RATE ISN'T AVAILABLE: BACK TO AUTO", 5); }
          if (capture.open) capture.start(*curEng, settings.str("inputDevice"), BUFFERS[sound.bufIdx]);   // the input at the new rate too
          Json js; if (Json::parse(snap, js)) { restoring = true; loadPatch(ui, js); for (auto& up : ui.mods) restoreSound(up.get()); restoring = false; }   // rebuilt at the new rate
          ui.viewX = vx; ui.viewY = vy; char b[64]; std::snprintf(b, 64, "QUALITY %.1fK SAMPLES A SECOND", (*curEng).sr / 1000); ui.status(b, 4); }; }
      else if (i == 7) { q.items = {"60 FPS  SMOOTHEST", "30 FPS  LIGHTER", "20 FPS  LIGHTEST"}; q.cur = fpsIdx;   // dragging and patching always run at full speed
        q.pick = [&](int j) { fpsIdx = j; settings.set("fps", Json::number(j)); saveSettings(); ui.status("SCREEN " + std::to_string(FPS[j]) + " FPS WHEN IDLE, FULL SPEED WHILE YOU WORK", 4); }; }
      else if (i == 8) { ui.cableMotion = !ui.cableMotion; settings.set("cableMotion", Json::number(ui.cableMotion ? 1 : 0)); saveSettings();
        ui.status(ui.cableMotion ? "CABLE MOTION ON: PULSES AND FLOW ALONG THE CABLES" : "CABLE MOTION OFF: A LITTLE LIGHTER ON OLDER COMPUTERS", 4); return; }
      else if (i == 9) { openTheme(x0); return; }
      else if (i == 11) { ui.openMidiMenu(x0, 11); return; }   // MIDI: DEVICES, MONITOR, MIDI MAP
      else if (i == 12) {   // SEND FEEDBACK: the form on the website, with this copy's version and set-up filled in (nothing personal)
        auto enc = [](const std::string& v) { std::string o; char b[4]; for (unsigned char ch : v) { if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_') o += (char)ch; else { std::snprintf(b, sizeof b, "%%%02X", ch); o += b; } } return o; };
        int midiIns = 0; for (auto& md : ui.midiDevs) if (md.input && md.open) midiIns++;
        const std::string os = std::string(SDL_GetPlatform()) + (sizeof(void*) == 4 ? " 32-bit" : " 64-bit");
        const std::string url = "https://teamoth.github.io/timbient-modules/#feedback?v=" + enc(VERSION) + "&os=" + enc(os) + "&audio=" + enc(sound.name) + "&buf=" + std::to_string(BUFFERS[sound.bufIdx])
          + "&rate=" + std::to_string((int)std::lround((*curEng).sr)) + "&midi=" + std::to_string(midiIns);
        SDL_OpenURL(url.c_str()); ui.status("OPENING THE FEEDBACK FORM IN YOUR BROWSER", 3); return; }
      else if (i == 10) { q.items = {std::string("TIMBIENT MODULES ") + VERSION, "DESIGNED BY TIM MEDLICOTT,", "  BUILT WITH CLAUDE, ANTHROPIC'S AI", "BUILT ON THESE OPEN-SOURCE PROJECTS,", "  EACH USED AS ITS LICENCE ALLOWS:",
          "MI VOICES: MUTABLE INSTRUMENTS' PLAITS,", "RINGS (PLUCK, RESONATOR): MUTABLE INSTRUMENTS' RINGS,", "  BY EMILIE GILLET (MIT LICENCE)", "  NOT MADE BY OR AFFILIATED WITH MUTABLE INSTRUMENTS",
          "SDL 3: WINDOW AND DRAWING", "  BY SAM LANTINGA (ZLIB LICENCE)", "MINIAUDIO: SOUND IN AND OUT", "  BY DAVID REID (PUBLIC DOMAIN / MIT-0)",
          "RTMIDI: MIDI DEVICES", "  BY GARY P. SCAVONE (MIT LICENCE)", "EVERYTHING ELSE WAS WRITTEN FOR TIMBIENT MODULES",
          "THE FULL NOTICES ARE IN CREDITS.MD,", "  WHICH COMES WITH THE APP", "WEB PLAYER (PLAYS SAVED SESSIONS):", "  TEAMOTH.GITHUB.IO/TIMBIENT-MODULES"}; q.title = 0; q.pick = [](int) {}; }
      else return;
      ui.openDropdown(q);
    };
    ui.openDropdown(d);
  };
  ui.bar.push_back({"SESSIONS", openSessions});
  ui.bar.push_back({"SETTINGS", openSettings});
  ui.bar.push_back({"UNDO", [&](int) { restoreStep(hpos - 1); }});
  ui.bar.push_back({"REDO", [&](int) { restoreStep(hpos + 1); }});

  // ---- the window ----
  // as big as 1280 x 800, but never bigger than the screen it opens on (a 1024 x 600 netbook, say): on a small screen it opens maximised
  int winW = 1280, winH = 800; bool small = false;
  { SDL_Rect ub; const SDL_DisplayID dsp = SDL_GetPrimaryDisplay();
    if (dsp && SDL_GetDisplayUsableBounds(dsp, &ub) && ub.w > 0 && ub.h > 0) {
      small = ub.w < 1340 || ub.h < 860;
      winW = std::min(winW, std::max(480, ub.w - 40)); winH = std::min(winH, std::max(320, ub.h - 60)); } }
  win = SDL_CreateWindow("Timbient Modules", winW, winH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (small ? SDL_WINDOW_MAXIMIZED : 0));
  SDL_SetWindowMinimumSize(win, 480, 320);
  SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
  SDL_SetRenderVSync(ren, 1);
  SDL_Texture* tex = nullptr; int texW = 0, texH = 0; float scale = 3;
  // FINER LETTERS: a texture of the 5 x 7 letters, drawn over the scaled canvas, tinted, at two thirds of an art pixel
  int atlasW = 0, atlasH = 0; SDL_Texture* atlas = nullptr; static std::string atlasErr;
  { const std::vector<uint32_t> a = Canvas::fontAtlas(atlasW, atlasH); atlas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, atlasW, atlasH);
    if (atlas && (!SDL_UpdateTexture(atlas, nullptr, a.data(), atlasW * 4) || !SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND))) { SDL_DestroyTexture(atlas); atlas = nullptr; }
    if (atlas) SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_NEAREST); else atlasErr = SDL_GetError(); }   // without it, the letters go dot by dot
  std::vector<Canvas::Quad> quads; std::vector<SDL_Vertex> verts; std::vector<int> vidx;
  letterReport = [&]() -> std::string { char sc[16]; std::snprintf(sc, 16, "%.1fX", scale);
    if (letters == 2) return "THE CLASSIC LETTERS";
    if (scale < 1.95f) return std::string("THE WINDOW IS DRAWN AT ") + sc + ": TOO SMALL FOR THE FINER LETTERS, SO THE CLASSIC ONES (ZOOM IN WITH CTRL +)";
    std::string e = atlasErr; for (auto& ch : e) ch = (char)std::toupper((unsigned char)ch); if (e.size() > 60) e = e.substr(0, 60);
    return std::string(letters == 1 ? "FINER CAPITALS" : "FINER LOWERCASE") + " AT " + sc + (atlas ? "" : "  (DRAWN DOT BY DOT: " + (e.empty() ? std::string("NO LETTER TEXTURE") : e) + ")"); };
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
          if (ui.noteEdit) {   // NOTES and the NOTEPAD, as in a text editor (letters come as text). Shift extends the selection; by word with
            // Option (Mac) or Ctrl (Windows), to the line's or note's ends with Cmd (Mac); Cmd/Ctrl+A, C, X, V, Z (Shift+Z or Ctrl+Y: redo)
            const SDL_Keycode k = ev.key.key; const SDL_Keymod km = ev.key.mod; static const bool mac = std::string(SDL_GetPlatform()) == "macOS";
            const bool cmd = km & (SDL_KMOD_GUI | SDL_KMOD_CTRL);
            if (cmd && k == SDLK_A) { ui.noteSelectAll(); break; }
            if (cmd && k == SDLK_C) { const std::string t = ui.noteText(); if (!t.empty()) SDL_SetClipboardText(t.c_str()); break; }
            if (cmd && k == SDLK_X) { const std::string t = ui.noteText(); if (!t.empty()) { SDL_SetClipboardText(t.c_str()); ui.noteCut(); } break; }
            if (cmd && k == SDLK_V) { if (char* t = SDL_GetClipboardText()) { ui.notePaste(t); SDL_free(t); } break; }
            if (cmd && k == SDLK_Z) { ui.noteUndo(km & SDL_KMOD_SHIFT); break; }
            if ((km & SDL_KMOD_CTRL) && k == SDLK_Y) { ui.noteUndo(true); break; }
            const int nk = k == SDLK_UP ? 1 : k == SDLK_DOWN ? 2 : k == SDLK_LEFT ? 3 : k == SDLK_RIGHT ? 4 : k == SDLK_BACKSPACE ? 5 : k == SDLK_DELETE ? 6 : (k == SDLK_RETURN || k == SDLK_KP_ENTER) ? 7 : k == SDLK_HOME ? 8 : k == SDLK_END ? 9 : k == SDLK_ESCAPE ? 27 : 0;
            if (nk) { int m = (km & SDL_KMOD_SHIFT) ? Ui::NK_SHIFT : 0;
              if (mac) { if (km & SDL_KMOD_ALT) m |= Ui::NK_WORD; if (km & SDL_KMOD_GUI) m |= Ui::NK_LINE; } else if (km & SDL_KMOD_CTRL) m |= Ui::NK_WORD;
              ui.noteKey(nk, m); break; }
            if (!cmd) break; }   // (anything else: typed text, which comes as its own event; other Cmd/Ctrl keys still reach the app)
          if (ui.qwLearn >= 0) {   // QWERTY MIDI: the key for the note picked
            const int i = ui.qwLearn; ui.qwLearn = -1; static const char* NM[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
            const std::string nn = std::string(NM[i % 12]) + std::to_string(i / 12);
            if (ev.key.key == SDLK_ESCAPE) { ui.status("NO CHANGE", 1); break; }
            if (ev.key.key == SDLK_DELETE || ev.key.key == SDLK_BACKSPACE) { qwMap[i] = 0; qwSave(); ui.status(nn + " HAS NO KEY NOW", 2); break; }
            std::string was; for (int j = 0; j < 36; j++) if (j != i && qwMap[j] == ev.key.key) { qwMap[j] = 0; was = std::string(NM[j % 12]) + std::to_string(j / 12); }   // a key plays one note
            qwMap[i] = ev.key.key; qwSave(); ui.status(ui.qwKeys[i] + " PLAYS " + nn + (was.empty() ? "" : "  (IT WAS " + was + ")"), 3); break; }
          if (!(ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_ALT)) && !ui.br.open && !ui.ed.open && ui.hasQwerty()) {   // QWERTY MIDI: its keys play notes (before any shortcut)
            int i = -1; for (int j = 0; j < 36; j++) if (qwMap[j] && qwMap[j] == ev.key.key) i = j;
            if (i >= 0) { if (!ev.key.repeat) ui.qwertyPlay(i); break; } }
          if ((ev.key.key == SDLK_DELETE || ev.key.key == SDLK_BACKSPACE) && !ui.ask && ui.unplugSelected()) break;   // a picked cable
          if ((ev.key.key == SDLK_DELETE || ev.key.key == SDLK_BACKSPACE) && !ui.ask && !ui.br.open && !ui.ed.open && !ui.picked.empty()) { ui.deletePicked(); break; }   // the selected modules (asks first)
          if (ev.key.key == SDLK_ESCAPE && ui.sixLearning()) { ui.cancelSixLearn(); ui.status("LEARN 64 STEPS CANCELLED", 2); break; }
          if (ev.key.key == SDLK_ESCAPE && !ui.picked.empty() && !ui.br.open && !ui.ed.open && !ui.learning()) { ui.picked.clear(); ui.status("NOTHING SELECTED", 1); break; }
          if (!ui.ask && !ui.br.open && !ui.ed.open) {   // the Tracker last touched: arrows move its cursor, Delete empties the step
            const int tk = ev.key.key == SDLK_UP ? 1 : ev.key.key == SDLK_DOWN ? 2 : ev.key.key == SDLK_LEFT ? 3 : ev.key.key == SDLK_RIGHT ? 4 : (ev.key.key == SDLK_DELETE || ev.key.key == SDLK_BACKSPACE) ? 5 : 0;
            if (tk && ui.trackerKey(tk)) break; }
          if (ui.learning() && ev.key.key == SDLK_ESCAPE) { ui.cancelLearn(); ui.status("MIDI LEARN CANCELLED", 2); break; }
          if (ui.ed.open && (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_SPACE)) { ui.editorKey(ev.key.key == SDLK_ESCAPE ? 27 : ' '); break; }
          if (ui.br.open) {   // the sound browser: arrows step through, Enter loads, Escape closes
            if (ev.key.key == SDLK_DOWN) { ui.browserKey(1); break; } if (ev.key.key == SDLK_UP) { ui.browserKey(-1); break; }
            if (ev.key.key == SDLK_ESCAPE) { ui.closeBrowser(); break; }
            if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) { ui.browserLoad(); break; }
            if (ev.key.key == SDLK_SPACE) { ui.browserPreview(); break; } }
          if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_F && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)))) ui.wantFullscreen = !ui.isFullscreen;
          if (ev.key.key == SDLK_ESCAPE && ui.isFullscreen) ui.wantFullscreen = false;
          if (ev.key.key == SDLK_S && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) && !ui.ask) {   // Cmd/Ctrl+S saves to the session's file (Shift: SAVE AS)
            if (curFile.empty() || (ev.key.mod & SDL_KMOD_SHIFT)) saveAs(); else if (writeFile(curFile, sessionFile(serializePatch(ui)))) { savedNow(); ui.status("SAVED " + fileName(curFile), 4); } else ui.status("COULD NOT SAVE THE FILE", 4); break; }
          { const bool cmd = ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL); const bool free = !ui.ask && !ui.br.open && !ui.ed.open;
            if (cmd && free && ev.key.key == SDLK_C) { const auto ids = ui.pickedOrTouched(); if (!ids.empty()) ui.onCopy(ids); else ui.status("SELECT SOME MODULES TO COPY (SHIFT-DRAG A BOX ROUND THEM)", 3); break; }
            if (cmd && free && ev.key.key == SDLK_V) { ui.onPaste(); break; }
            if (cmd && free && ev.key.key == SDLK_A) { ui.pickAll(); break; }
            if (cmd && free && ev.key.key == SDLK_D && ui.picked.size() > 1) { ui.onDuplicateMany(ui.picked); break; }
            if (cmd && free && ev.key.key == SDLK_O) { if (!blendBusy(true)) pickSession(false); break; }   // Cmd/Ctrl+O: OPEN
            if (cmd && free && ev.key.key == SDLK_B) {   // Cmd/Ctrl+B: BLEND TO (while a blend goes on: pause it, or let it carry on)
              if (curEng->blending() && blendClearT < 0 && ui.blendAt >= 0) ui.onBlendPause(); else if (!blendBusy(true)) pickSession(true); break; } }
          if (ev.key.key == SDLK_D && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) && !ui.ask) { if (UiMod* u = ui.byId(ui.lastTouched)) if (!u->m->def->fixed) ui.onDuplicate(u); break; }   // Cmd/Ctrl+D: duplicate
          if (ui.ed.open && ((ev.key.key == SDLK_Z && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) || (ev.key.key == SDLK_Y && (ev.key.mod & SDL_KMOD_CTRL)))) {   // the editor open: its own undo
            ui.editorUndo(ev.key.key == SDLK_Y || (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : -1); break; }
          if (ev.key.key == SDLK_Z && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) { restoreStep((ev.key.mod & SDL_KMOD_SHIFT) ? hpos + 1 : hpos - 1); break; }   // Cmd/Ctrl+Z, and with Shift to redo
          if (ev.key.key == SDLK_Y && (ev.key.mod & SDL_KMOD_CTRL)) { restoreStep(hpos + 1); break; }
          if (ev.key.key == SDLK_EQUALS || ev.key.key == SDLK_PLUS || ev.key.key == SDLK_KP_PLUS) pendingZoom++;
          if (ev.key.key == SDLK_MINUS || ev.key.key == SDLK_KP_MINUS) pendingZoom--;
          if (ev.key.key == SDLK_0 && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) pendingZoom = -zoom;
          if (ev.key.key == SDLK_SPACE) { (*curEng).freeze.store(!(*curEng).freeze.load()); ui.status((*curEng).freeze.load() ? "FROZEN" : "UNFROZEN"); }
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:   // right click, or Ctrl-click (a Mac trackpad): the module's menu
          if (ev.button.button == SDL_BUTTON_RIGHT || (ev.button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & SDL_KMOD_CTRL))) { ui.contextAt(ev.button.x * dens / scale, ev.button.y * dens / scale); ctxClick = true; break; }
          if (ev.button.button == SDL_BUTTON_LEFT) { ui.shiftHeld = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0; ui.pointerDown(ev.button.x * dens / scale, ev.button.y * dens / scale); } break;
        case SDL_EVENT_MOUSE_MOTION: ui.pointerMove(ev.motion.x * dens / scale, ev.motion.y * dens / scale); break;
        case SDL_EVENT_MOUSE_BUTTON_UP: if (ctxClick) { ctxClick = false; break; } if (ev.button.button == SDL_BUTTON_LEFT) ui.pointerUp(ev.button.x * dens / scale, ev.button.y * dens / scale); break;
        case SDL_EVENT_MOUSE_WHEEL:   // with Ctrl or Cmd held: zoom. Otherwise: move around the patch
          if (SDL_GetModState() & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) { wheelAcc += ev.wheel.y; if (std::fabs(wheelAcc) >= 1) { pendingZoom += wheelAcc > 0 ? 1 : -1; wheelAcc = 0; } }
          else if (ui.ed.open) ui.editorWheel(ev.wheel.mouse_x * dens / scale, ev.wheel.y);   // the sound editor: zoom
          else if (ui.br.open) ui.browserScroll(-ev.wheel.y * 2);   // over the sound browser: scroll its list
          else if (ui.noteWheel(ev.wheel.mouse_x * dens / scale, ev.wheel.mouse_y * dens / scale, ev.wheel.y)) {}   // over a note: scroll it
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
    if (ui.typing() != texting) { texting = ui.typing(); if (texting) SDL_StartTextInput(win); else SDL_StopTextInput(win); }
    // files chosen in the system dialogs
    { std::vector<DialogResult> rs; { std::lock_guard<std::mutex> l(dlgMx); rs.swap(dlgResults); }
      for (auto& r : rs) {
        if (r.recording) {   // a recording: moved where it was asked to go, or left in Music / Timbient Modules
          std::string nm = r.recFrom.substr(r.recFrom.find_last_of("/\\") + 1); for (auto& ch : nm) ch = (char)std::toupper((unsigned char)ch);
          if (r.path.empty() || r.path == r.recFrom) { ui.status("KEPT " + nm + " IN MUSIC / TIMBIENT MODULES", 6); continue; }
          std::string to = r.path; if (to.size() < 4 || (to.substr(to.size() - 4) != ".wav" && to.substr(to.size() - 4) != ".WAV")) to += ".wav";
          bool ok = std::rename(r.recFrom.c_str(), to.c_str()) == 0;
          if (!ok) { std::ifstream a(r.recFrom, std::ios::binary); std::ofstream b(to, std::ios::binary); b << a.rdbuf(); ok = a.good() && b.good(); b.close(); if (ok) std::remove(r.recFrom.c_str()); }   // another drive: copied across
          std::string shown = to.substr(to.find_last_of("/\\") + 1); for (auto& ch : shown) ch = (char)std::toupper((unsigned char)ch);
          ui.status(ok ? "SAVED " + shown : "COULDN'T SAVE THERE: KEPT " + nm + " IN MUSIC / TIMBIENT MODULES", 6); continue; }
        if (r.exporting) {   // EXPORT WAV: copied where it was asked to go
          if (r.path.empty()) { if (r.temp) std::remove(r.recFrom.c_str()); continue; }
          std::string to = r.path; if (to.size() < 4 || (to.substr(to.size() - 4) != ".wav" && to.substr(to.size() - 4) != ".WAV")) to += ".wav";
          const bool ok = SDL_CopyFile(r.recFrom.c_str(), to.c_str()); if (r.temp) std::remove(r.recFrom.c_str());
          const size_t sl = to.find_last_of("/\\"); if (ok && sl != std::string::npos) { settings.set("exportDir", Json::string(to.substr(0, sl + 1))); saveSettings(); }
          std::string shown = to.substr(sl == std::string::npos ? 0 : sl + 1); for (auto& ch : shown) ch = (char)std::toupper((unsigned char)ch);
          ui.status(ok ? "EXPORTED " + shown : std::string("COULD NOT EXPORT THERE"), 5); continue; }
        if (r.target == -3) { addGroupFile(r.path); continue; }   // a group file
        if (r.folder && r.target == -1) {   // the old in-app sessions, each as its own .tmbnt in this folder
          int n = 0; for (auto& ss : store.list) { std::string nm; for (char ch : ss.name) nm += (std::isalnum((unsigned char)ch) || ch == ' ' || ch == '-' || ch == '_') ? ch : '_';
            if (nm.empty()) nm = "SESSION"; std::string path = r.path + "/" + nm + ".tmbnt"; for (int k = 2; !readFile(path).empty(); k++) path = r.path + "/" + nm + " " + std::to_string(k) + ".tmbnt";
            if (writeFile(path, sessionFile(ss.data))) n++; }
          ui.status(n == (int)store.list.size() ? "SAVED " + std::to_string(n) + " SESSIONS. YOU CAN NOW REMOVE THEM FROM THE APP" : "SAVED " + std::to_string(n) + " OF " + std::to_string(store.list.size()), 6); continue; }
        if (r.folder) { if (ui.br.open) ui.browseTo(r.path); settings.set("browseDir", Json::string(r.path)); saveSettings(); continue; }
        if (r.target == -2) { blendLoad(r.path); continue; }   // BLEND TO
        if (r.target) { UiMod* u = ui.byId(r.target); if (!u) continue; std::string err;   // a sound for a Sample or Grain Looper
          int slot = r.slot; if (slot < 0) { for (int k = 0; k < UiMod::SLOTS; k++) if (u->snd[k].id.empty()) { slot = k; break; } if (slot < 0) { ui.status("ALL 8 PLACES ARE FULL: TAP A NUMBER TO REPLACE ONE", 5); continue; } }
          const int ci = u->m->def->paramIndex("chan"); const bool st = ci >= 0 && u->m->p[ci].load() > 0.5f;   // CHANNELS: STEREO keeps a file's left and right
          AudioBuf* b = decodeAudio(r.path, u->m->def->type == "looper" ? 60 : 600, err, st); if (!b) { ui.status(err, 5); continue; }
          if (u->m->def->type == "sample" || u->m->def->type == "stretch") { float mx = 0; for (float v : b->d) mx = std::max(mx, std::fabs(v)); for (float v : b->r) mx = std::max(mx, std::fabs(v)); if (mx > 1e-4f) { for (auto& v : b->d) v *= 0.9f / mx; for (auto& v : b->r) v *= 0.9f / mx; } }   // quiet recordings come in at full level
          std::string nm = r.path.substr(r.path.find_last_of("/\\") + 1); for (auto& ch : nm) ch = (char)std::toupper((unsigned char)ch);
          const int rn = noteInName(r.path); u->snd[slot].root = rn >= 0 ? (float)rn : 60.f; u->shown = slot;   // MULTI: the ROOT note from the file name, if it names one
          const int secs = (int)std::lround(b->dur()); giveSound(u, b, true, slot, nm);
          ui.status("LOADED " + std::to_string(secs) + "S" + (u->m->def->type == "sample" && u->m->p[1].load() >= 2 ? " INTO PLACE " + std::to_string(slot + 1) + (rn >= 0 ? ", ROOT " + noteName(rn) : "") : "")); continue; }
        if (r.open) { Json f; if (!Json::parse(readFile(r.path), f)) { ui.status("THAT FILE COULD NOT BE READ", 5); continue; }
          unpackSounds(f);   // its sounds, packed in
          const Json* data = patchIn(f); if (!data) { ui.status("THAT IS NOT A TIMBIENT SESSION", 5); continue; }
          loadData(*data, fileName(r.path)); curFile = r.path; neverSaved = false;
          if (f.str("timbient") == "sessions") ui.status("THAT FILE HOLDS " + std::to_string(f.get("sessions")->a.size()) + " SESSIONS: THIS IS THE FIRST", 6); }
        else { std::string p = r.path; const size_t sl = p.find_last_of("/\\"); if (p.find('.', sl == std::string::npos ? 0 : sl) == std::string::npos) p += ".tmbnt";
          if (writeFile(p, sessionFile(serializePatch(ui)))) { curFile = p; savedNow(); ui.status("SAVED " + fileName(p), 4); } else ui.status("COULD NOT SAVE THE FILE", 5); }
      } }
    engA.collectGarbage(); engB.collectGarbage(); blendFrame(ldt);
    if (ui.padDirty) { ui.padDirty = false; padSaveT = 1.0f; } if (padSaveT > 0 && (padSaveT -= ldt) <= 0) savePad();
    { std::string t = curFile.empty() ? std::string("NEW SESSION") : fileName(curFile); const size_t dot = t.rfind('.'); if (!curFile.empty() && dot != std::string::npos && dot > 0) t = t.substr(0, dot);
      ui.sessionTitle = t; ui.sessionChanged = sessionChanged(); }
    pumpInfo();
    if (rec.on()) { if (Engine* src = curEng->blending()) rec.pump(*src); rec.pump(*curEng); if (Module* o = outMod()) { o->disp[1].store(1); o->disp[2].store(rec.frames / rec.sr); } }
    // MIDI: notice devices coming and going; give MIDI OUT modules their device
    { int dv; MidiMsg mm; ui.midiDevNames = midi.inputNames(); while (midi.popUi(dv, mm)) ui.midiIn(dv, mm.st, mm.d1, mm.d2); ui.midiFrame(ldt); }   // MIDI LEARN
    { // SETTINGS > MIDI > DEVICES: every input and output, which are open, which are sending (a light) and which are off
      static float outT = 0; static std::vector<std::string> outsNow; static std::map<std::string, std::pair<uint32_t, float>> seen; static std::map<std::string, std::pair<uint32_t, float>> clk;
      if ((outT -= ldt) <= 0) { outT = 2; outsNow = midi.outputs(); }
      const auto& open = midi.inputNames(); const auto& off = midi.offList(); const auto inUse = midi.outputsInUse(); std::vector<Ui::MidiDev> v;
      for (auto& n : midi.allInputs()) { Ui::MidiDev d; d.name = n; d.input = true; d.on = std::find(off.begin(), off.end(), n) == off.end(); d.open = std::find(open.begin(), open.end(), n) != open.end();
        const uint32_t m = midi.messagesFrom(n), c = midi.clockFrom(n); auto& s1 = seen[n]; if (m != s1.first) { s1.first = m; s1.second = 0; } else s1.second += ldt; d.seen = s1.second;
        auto& s2 = clk[n]; if (c != s2.first) { s2.first = c; s2.second = 0; } else s2.second += ldt; d.clock = s2.second < 0.5f; v.push_back(d); }
      for (auto& n : outsNow) { if (n.find("Timbient Modules") != std::string::npos) continue; Ui::MidiDev d; d.name = n; d.input = false; d.inUse = std::find(inUse.begin(), inUse.end(), n) != inUse.end(); v.push_back(d); }
      ui.midiDevs = std::move(v); }
    { static float mt = 0; midi.poll(ldt); ui.midiState = midi.state(); mt -= ldt;
      if (mt <= 0) { mt = 2; std::vector<std::string> names; bool listed = false;
        for (auto& up : ui.mods) if (up->m->def->type == "midiout" && up->m->devSlot.load() < 0) { if (!listed) { names = midi.outputs(); listed = true; }
          const std::string want = up->dev.empty() ? (names.empty() ? std::string() : names[0]) : up->dev; if (!want.empty()) up->m->devSlot.store(midi.openOutput(want)); } } }
    { static float armT = 0, quietT = 0; static unsigned seen = 0; quietT -= ldt;   // DROPOUTS: said on screen (at most every few seconds)
      if (!gDropArm.load()) { armT += ldt; if (armT > 2) { armT = 0; gDropArm.store(true); seen = gDropouts.load(); } }
      const unsigned n = gDropouts.load(); if (n != seen) { const unsigned k = n - seen; seen = n;
        if (quietT <= 0) { quietT = 4; ui.status(std::string(k > 1 ? "SOUND DROPOUTS" : "A SOUND DROPOUT") + (curEng->blending() ? " AS THE BLEND WENT ON" : "") + ": THE COMPUTER COULDN'T KEEP UP. A BIGGER BUFFER (SETTINGS) GIVES IT MORE TIME", 5); } } }
    // a Grain Looper with nothing in it catches what it hears, every couple of seconds, until it has something
    { bool want = false; for (auto& up : ui.mods) if (up->m->def->type == "audioin") want = true;   // AUDIO IN: the input open while there's one (closed a little after the last goes)
      captureT -= ldt; if (want) captureIdle = 0; else captureIdle += ldt;
      if (want && !capture.open && captureT <= 0) { captureT = 5; if (!capture.start(*curEng, settings.str("inputDevice"), BUFFERS[sound.bufIdx]))
          ui.status("COULD NOT OPEN THE INPUT. ON A MAC, ALLOW THE MICROPHONE FOR TIMBIENT MODULES IN SYSTEM SETTINGS > PRIVACY", 8); }
      if (!want && capture.open && captureIdle > 2) capture.stop();
      ui.inputName = capture.open ? capture.name : std::string(); }
    for (auto& up : ui.mods) if (up->m->def->type == "sample") {   // SAMPLER: somewhere to record (once it could), each take kept as its sound, and the feedback guard
      UiMod* u = up.get(); Module* m = u->m; const ModuleDef* d = m->def; bool inUse = m->p[d->paramIndex("rec")].load() > 0.5f;
      for (auto& c : ui.cables) if (c.to == m->id && (c.toPort == 2 || c.toPort == 3)) inUse = true;
      u->recGiveT -= ldt;
      if (inUse && !m->recBuf.load() && u->recGiveT <= 0) { u->recGiveT = 1; AudioBuf* b = new AudioBuf(); b->sr = (*curEng).sr; const size_t cap = (size_t)(60.0 * (*curEng).sr); b->d.assign(cap, 0.f); b->r.assign(cap, 0.f); (*curEng).setBuffer(m->id, b, 99); }
      const int seq = (int)m->disp[42].load(); AudioBuf* rb = m->recBuf.load();
      if (seq != u->lastTake && rb) { u->lastTake = seq; const size_t n = std::min(rb->d.size(), (size_t)m->disp[43].load());
        if (n > 64) { AudioBuf* b = new AudioBuf(); b->sr = rb->sr; b->d.assign(rb->d.begin(), rb->d.begin() + n); const int ci = d->paramIndex("chan");
          if (ci >= 0 && m->p[ci].load() > 0.5f) { bool same = true; for (size_t i = 0; i < n && same; i++) if (std::fabs(rb->d[i] - rb->r[i]) > 1e-6f) same = false; if (!same) b->r.assign(rb->r.begin(), rb->r.begin() + n); }   // STEREO: both sides, unless they were the same
          char nm[32]; const std::time_t tt = std::time(nullptr); std::strftime(nm, 32, "TAKE %H.%M.%S", std::localtime(&tt));
          giveSound(u, b, true, 0, nm); const int ks = d->paramIndex("start"), kl = d->paramIndex("len"); if (ks >= 0) m->p[ks].store(0); if (kl >= 0) m->p[kl].store(1);
          char t[48]; std::snprintf(t, 48, "RECORDED %.1fS", n / rb->sr); ui.status(t, 3); histDirty = true; histT = 0.2f; }
        (*curEng).action(m->id, "taken"); }
      // the feedback guard: its own sound finding its way back into its IN
      const bool loop = ui.feedsBack(m->id, 2);
      if ((int)loop != u->fbSent) { u->fbSent = loop; (*curEng).action(m->id, loop ? "fb:1" : "fb:0"); if (loop) ui.status("SAMPLER: ITS OWN OUT LEADS BACK INTO ITS IN (THROUGH THE CABLES), SO IT WON'T MONITOR. IT STILL RECORDS", 6); } }
    { static float catchT = 0; catchT += ldt; if (catchT > 2) { catchT = 0; for (auto& up : ui.mods) if (up->m->def->type == "looper" && up->m->p[0].load() > 0.5f && up->snd[0].peaks.empty() && up->snd[0].id.empty()) catchSound(up.get(), true); } }   // HELD with nothing held yet: what came in

    if (dirty) { saveT -= ldt; if (saveT <= 0) { store.writeAutosave(autosaveData()); dirty = false; } }
    if (histDirty) { histT -= ldt; if (histT <= 0) { snapshot(); histDirty = false; } }
    // draw only when it's time: never while the window can't be seen; at the chosen rate when idle; at full speed while you work
    { const float every = hidden ? 1e9f : (busyT > 0 || held || ui.typing()) ? 0.f : 1.f / FPS[fpsIdx], since = (SDL_GetTicksNS() - last) / 1e9f;
      if (since < every) { SDL_WaitEventTimeout(nullptr, (Sint32)std::max(1.f, std::min(100.f, (every - since) * 1000))); continue; } }
    const Uint64 now = SDL_GetTicksNS(); const float dt = std::min(0.1f, (now - last) / 1e9f); last = now;
    if (W != canvas.W || H != canvas.H) canvas.resize(W, H);
    canvas.fine = letters < 2 && scale >= 1.95f; canvas.letterCase = letters;   // below 2x: the classic ones (they'd be too small to read)
    ui.shiftHeld = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    canvas.beginFrame();
    ui.draw(canvas, dt);
    if (!tex || texW != W || texH != H) { if (tex) SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, W, H); SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_PIXELART); texW = W; texH = H; }
    SDL_UpdateTexture(tex, nullptr, canvas.rgba.data(), W * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
    SDL_FRect dst{0, 0, (float)(W * scale), (float)(H * scale)}; SDL_RenderTexture(ren, tex, nullptr, &dst);
    if (canvas.fine) { canvas.fineQuads(scale, quads, !atlas); verts.clear(); vidx.clear();
      const float iu = 1.f / atlasW, iv = 1.f / atlasH; const int sc = FONT57_N, su = (sc % Canvas::ATLAS_COLS) * Canvas::CELL_W, sv = (sc / Canvas::ATLAS_COLS) * Canvas::CELL_H;
      for (auto& q : quads) { const SDL_FColor col{(q.rgba & 255) / 255.f, ((q.rgba >> 8) & 255) / 255.f, ((q.rgba >> 16) & 255) / 255.f, 1.f};
        float u0, v0, u1, v1; if (q.u >= 0) { u0 = q.u * iu; v0 = q.v * iv; u1 = (q.u + 5) * iu; v1 = (q.v + 9) * iv; } else { u0 = (su + 1) * iu; v0 = (sv + 1) * iv; u1 = (su + 2) * iu; v1 = (sv + 2) * iv; }
        const int b = (int)verts.size(); const float x0 = (float)q.x, y0 = (float)q.y, x1 = (float)(q.x + q.w), y1 = (float)(q.y + q.h);
        verts.push_back({{x0, y0}, col, {u0, v0}}); verts.push_back({{x1, y0}, col, {u1, v0}}); verts.push_back({{x1, y1}, col, {u1, v1}}); verts.push_back({{x0, y1}, col, {u0, v1}});
        for (int k : {0, 1, 2, 0, 2, 3}) vidx.push_back(b + k); }
      if (!verts.empty() && !SDL_RenderGeometry(ren, atlas, verts.data(), (int)verts.size(), vidx.data(), (int)vidx.size()) && atlas) { SDL_DestroyTexture(atlas); atlas = nullptr; atlasErr = SDL_GetError(); } }   // it failed: dot by dot from now on
    SDL_RenderPresent(ren);
  }
  store.writeAutosave(autosaveData());
  if (rec.on()) rec.stop(*curEng);
  if (ui.pad) savePad();
  if (bl.th.joinable()) bl.th.join();
  capture.stop(); sound.stop(); if (gCtxOk) ma_context_uninit(&gCtx);
  { std::lock_guard<std::mutex> l(job.mx); job.stop = true; job.cv.notify_one(); } if (infoThread.joinable()) infoThread.join();
  SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
  return 0;
}
