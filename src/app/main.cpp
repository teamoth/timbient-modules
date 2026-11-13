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
#include <ctime>
#include <mutex>
#include <condition_variable>
#include <thread>

using namespace tb;

static const char* VERSION = "0.32.0";
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
// QUALITY: samples per second (0: the sound output's own rate). Lower rates cost less and lose the very highest treble
// 96K and 88.2K (about twice the work of 48K) only in the 64-bit versions; the 32-bit one is for older, slower computers
static const unsigned RATES[7] = {0, 48000, 44100, 32000, 24000, 96000, 88200};
static const int NRATES = sizeof(void*) == 8 ? 7 : 5;
struct Sound {
  ma_device dev; bool open = false; int bufIdx = 2;
  bool start(Engine& e, unsigned rate) {
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = rate;
    cfg.dataCallback = audioCallback; cfg.periodSizeInFrames = (ma_uint32)BUFFERS[bufIdx]; cfg.periods = 3;
    cfg.performanceProfile = bufIdx <= 1 ? ma_performance_profile_low_latency : ma_performance_profile_conservative;
    if (ma_device_init(nullptr, &cfg, &dev) != MA_SUCCESS) return false;
    open = true; if (e.sr != (float)dev.sampleRate) e.setSampleRate((float)dev.sampleRate);   // the engine runs at whatever rate the sound runs at
    e.outLatency = (float)(BUFFERS[bufIdx] * 3) / (float)dev.sampleRate;
    return ma_device_start(&dev) == MA_SUCCESS;
  }
  void stop() { if (open) { ma_device_uninit(&dev); open = false; } }
};

// ---------------- results from the system file dialogs (they may arrive on another thread) ----------------
struct DialogResult { bool open; std::string path; int target = 0, slot = 0; bool folder = false; };   // target: the module a sound file is for
static std::mutex dlgMx; static std::vector<DialogResult> dlgResults;
static const SDL_DialogFileFilter JSON_FILTER[] = {{"Timbient session", "tmbnt;json;txt"}};
static void onOpenDialog(void*, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back({true, files[0]}); } }
static void onSaveDialog(void*, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); dlgResults.push_back({false, files[0]}); } }
static void onFolderDialog(void* ud, const char* const* files, int) { if (files && files[0]) { std::lock_guard<std::mutex> l(dlgMx); DialogResult r{true, files[0]}; r.folder = true; r.target = ud ? -1 : 0; dlgResults.push_back(r); } }
static const SDL_DialogFileFilter AUDIO_FILTER[] = {{"Sound files", "wav;wave;aif;aiff;flac;mp3"}};
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
  int rateIdx = std::max(0, std::min(NRATES - 1, (int)settings.num("rate", 0)));
  if (!sound.start(engine, RATES[rateIdx]) && !(rateIdx != 0 && sound.start(engine, 0))) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Timbient Modules", "Could not open the sound device.", nullptr); return 1; }

  Ui ui(engine);
  static Midi midi(engine); midi.start();
  // the app opens to an empty session; RESTORE LAST WORK brings back what was there
  { Json prev; if (store.readAutosave(prev) && prev.get("mods") && prev.get("mods")->a.size() > 1) writeFile(store.path("previous.json"), prev.dump()); }
  ui.add("out", 300, 30);

  bool dirty = false; float saveT = 0;
  // UNDO / REDO: the patch as it was after each change (settled for a moment), up to 60 steps back
  std::vector<std::string> hist; int hpos = -1; bool restoring = false, histDirty = false; float histT = 0;
  auto snapshot = [&] { std::string sn = serializePatch(ui).dump(); if (hpos >= 0 && hist[hpos] == sn) return;
    hist.resize(hpos + 1); hist.push_back(sn); if (hist.size() > 60) hist.erase(hist.begin()); hpos = (int)hist.size() - 1; };
  ui.onChange = [&] { dirty = true; saveT = 1.0f; if (!restoring) { histDirty = true; histT = 0.4f; } };
  snapshot();   // the empty session is the first step
  // ---- sounds held by Sample and Grain Looper modules: kept as WAV files in the app's folder, named by id ----
  const std::string audioDir = store.dir + "audio/"; SDL_CreateDirectory(audioDir.c_str());
  auto giveSound = [&](UiMod* u, AudioBuf* b, bool keep, int slot = 0, const std::string& name = "") {   // hand a sound to a module (and keep a copy so sessions bring it back)
    UiMod::Snd& sd = u->snd[slot]; sd.peaks = peaksOf(*b); sd.missing = false; sd.secs = b->d.size() / b->sr; if (keep) sd.name = name;
    if (keep) { char id[40]; std::snprintf(id, 40, "n%llx%04x", (unsigned long long)std::time(nullptr), (unsigned)(SDL_rand(65536)));
      if (writeWavMono16(audioDir + id + ".wav", *b)) { sd.id = id; dirty = true; saveT = 0.5f; } else ui.status("COULD NOT KEEP THE SOUND", 4); }
    u->m->roots[slot].store(sd.root); engine.setBuffer(u->m->id, b, slot);
  };
  auto restoreSound = [&](UiMod* u) {
    for (int k = 0; k < UiMod::SLOTS; k++) { UiMod::Snd& sd = u->snd[k]; if (sd.id.empty()) continue; std::string err;
      AudioBuf* b = decodeAudio(audioDir + sd.id + ".wav", 600, err, true);
      if (b) giveSound(u, b, false, k); else { sd.missing = true; ui.status(u->m->def->name + ": A SOUND IS NOT HERE. LOAD IT AGAIN", 6); } }
  };
  auto catchSound = [&](UiMod* u, bool quiet) {   // GRAIN LOOPER: CATCH takes the last LENGTH seconds of what came in
    Module* m = u->m; if (m->ring.empty()) return; const long N = (long)m->ring.size(), w = m->ringW.load(std::memory_order_acquire);
    const long n = std::min(std::min(N, w), (long)(m->p[m->def->paramIndex("len")].load() * engine.sr)); if (n < 256) { if (!quiet) ui.status("NOTHING TO CATCH YET"); return; }
    AudioBuf* b = new AudioBuf(); b->sr = engine.sr; b->d.resize(n); float mx = 0; const int ci = m->def->paramIndex("chan"); const bool st = ci >= 0 && m->p[ci].load() > 0.5f && m->ringR.size() == m->ring.size();
    if (st) b->r.resize(n);   // CHANNELS STEREO: left and right
    for (long i = 0; i < n; i++) { const long k = (w - n + i) % N; b->d[i] = st ? 2 * m->ring[k] - m->ringR[k] : m->ring[k]; mx = std::max(mx, std::fabs(b->d[i])); if (st) { b->r[i] = m->ringR[k]; mx = std::max(mx, std::fabs(b->r[i])); } }
    if (mx < 0.002f) { delete b; if (!quiet) ui.status("NOTHING TO CATCH YET"); return; }
    for (auto& v : b->d) v *= 0.8f / mx; for (auto& v : b->r) v *= 0.8f / mx;
    giveSound(u, b, true); ui.status("CAUGHT " + std::to_string((int)std::lround(n / engine.sr)) + "S");
  };
  auto loadData = [&](const Json& data, const std::string& what) {
    const std::string note = loadPatch(ui, data); ui.centrePending = true; dirty = true; saveT = 0.5f;   // the view: to the middle of the session
    ui.status(note.empty() ? "LOADED " + what : note, note.empty() ? 3 : 8);
    for (auto& up : ui.mods) restoreSound(up.get());
    histDirty = true; histT = 0.1f;
  };
  auto restoreStep = [&](int to) {   // go back (or forward) to a remembered step, keeping the view where it is
    if (histDirty) { snapshot(); histDirty = false; }
    if (to < 0 || to >= (int)hist.size() || to == hpos) { ui.status(to < hpos ? "NOTHING TO UNDO" : "NOTHING TO REDO"); return; }
    const bool back = to < hpos; hpos = to; Json j; if (!Json::parse(hist[hpos], j)) return;
    const int vx = ui.viewX, vy = ui.viewY; restoring = true; std::vector<std::pair<UiMod*, int>> sc; applyPatch(ui, j, sc);   // only what changed: sequencers keep going
    for (auto& [u, k] : sc) { UiMod::Snd& sd = u->snd[k];
      if (sd.id.empty()) { engine.setBuffer(u->m->id, nullptr, k); sd.peaks.clear(); continue; }
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
    if (path.empty()) { engine.preview(nullptr); return; }
    std::string err; AudioBuf* b = decodeAudio(path, 1200, err, true); if (!b) { engine.preview(nullptr); ui.status(err, 4); return; }   // heard as it is, stereo or not
    float mx = 0; for (float v : b->d) mx = std::max(mx, std::fabs(v)); for (float v : b->r) mx = std::max(mx, std::fabs(v)); if (mx > 1e-4f && mx < 0.5f) { for (auto& v : b->d) v *= 0.5f / mx; for (auto& v : b->r) v *= 0.5f / mx; }   // quiet files a little louder, so you can hear them
    engine.preview(b); };
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
    if (d.empty()) { engine.preview(nullptr); return; } AudioBuf* b = new AudioBuf(); b->sr = sr; b->d = d; b->r = r; engine.preview(b, a, e, loop); };
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
  auto qwDefault = [&] { for (int i = 0; i < 36; i++) qwMap[i] = QW_DEFAULT[i][0] ? SDL_GetKeyFromName(QW_DEFAULT[i]) : 0; };
  auto qwSave = [&] { Json a = Json::array(); for (int i = 0; i < 36; i++) a.push(Json::string(qwMap[i] ? SDL_GetKeyName(qwMap[i]) : "")); settings.set("qwertyMap", a); saveSettings(); qwNames(); };
  qwDefault();
  if (const Json* a = settings.get("qwertyMap")) if (a->t == Json::ARR && a->a.size() == 36) for (int i = 0; i < 36; i++) qwMap[i] = a->a[i].t == Json::STR && !a->a[i].s.empty() ? SDL_GetKeyFromName(a->a[i].s.c_str()) : 0;
  qwNames();
  ui.onAction = [&](UiMod* u, const std::string& k) -> bool {
    const std::string& ty = u->m->def->type;
    if (ty == "qwerty" && k == "reset") { qwDefault(); qwSave(); ui.qwLearn = -1; ui.status("QWERTY MIDI: THE DEFAULT KEYS (AS SUNVOX)", 3); return true; }
    if ((ty == "sample" || ty == "looper" || ty == "stretch") && k == "edit") {   // EDIT SOUND: the editor, with this module's sound (for SAMPLE, the place on show)
      const int slot = ty == "sample" ? std::max(0, std::min(UiMod::SLOTS - 1, u->shown)) : 0; const UiMod::Snd& sd = u->snd[slot];
      if (sd.id.empty()) { ui.status("THERE'S NO SOUND TO EDIT YET: LOAD ONE FIRST", 4); return true; }
      std::string err; AudioBuf* b = decodeAudio(audioDir + sd.id + ".wav", 600, err, true); if (!b) { ui.status(err, 4); return true; }
      ui.openEditor(u->m->id, slot, sd.name, std::move(b->d), b->sr, std::move(b->r)); delete b; return true; }
    if ((ty == "sample" || ty == "looper" || ty == "stretch") && (k == "load" || k.rfind("load:", 0) == 0)) {   // LOAD FILE: KIT and MULTI take several at once, into the empty places
      const bool many = ty == "sample" && u->m->p[1].load() >= 2 && k == "load"; const int slot = k == "load" ? (many ? -1 : 0) : std::atoi(k.c_str() + 5);
      ui.openBrowser(u->m->id, slot, browseDir()); return true; }
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

  // ---- sessions are files: a .tmbnt holds the patch and every sound it uses (inside it's the web version's
  // session format, so a .json session renamed to .tmbnt opens too, and the web version can open a .tmbnt) ----
  std::string curFile;   // the file this session was opened from or last saved to: SAVE writes there
  auto sessionFile = [&](const Json& data) {
    Json f = Json::object(); f.set("timbient", Json::string("patch")); f.set("data", data);
    std::vector<std::string> ids; auto want = [&](const std::string& id) { if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id); };
    if (const Json* ms = data.get("mods")) if (ms->t == Json::ARR) for (auto& m : ms->a) if (const Json* p = m.get("p")) { want(p->str("audio"));
      if (const Json* sn = p->get("sounds")) if (sn->t == Json::ARR) for (auto& e : sn->a) if (e.t == Json::ARR && !e.a.empty() && e.a[0].t == Json::STR) want(e.a[0].s); }
    Json pack = Json::object(); for (auto& id : ids) {   // the sounds go inside it
      std::string err; AudioBuf* b = decodeAudio(audioDir + id + ".wav", 3600, err, true); if (!b) continue; const int ch = b->stereo() ? 2 : 1;   // a stereo sound: left and right, interleaved
      std::vector<unsigned char> raw(b->d.size() * 2 * ch); for (size_t i = 0; i < b->d.size() * ch; i++) { const float x = ch == 2 ? (i & 1 ? b->r[i / 2] : b->d[i / 2]) : b->d[i]; const int16_t v = (int16_t)std::lround(clampf(x, -1, 1) * 32767); raw[i * 2] = (unsigned char)v; raw[i * 2 + 1] = (unsigned char)(v >> 8); }
      Json a = Json::object(); a.set("sr", Json::number(b->sr)); if (ch == 2) a.set("ch", Json::number(2)); a.set("b64", Json::string(b64(raw.data(), raw.size()))); pack.set(id, a); delete b; }
    if (!pack.o.empty()) f.set("audio", pack);
    return f.dump();
  };
  auto fileName = [](std::string p) { const size_t sl = p.find_last_of("/\\"); if (sl != std::string::npos) p = p.substr(sl + 1); const size_t dt = p.rfind('.'); if (dt != std::string::npos && dt > 0) p = p.substr(0, dt);
    for (auto& ch : p) ch = (char)std::toupper((unsigned char)ch); return p; };
  static std::string saveName;   // SAVE AS: a name first (the dice suggests some), then where it goes
  auto saveAs = [&] { ui.askName("NAME THIS SESSION", [&](const std::string& n) { std::string fn; for (char ch : n) fn += (ch == '/' ? '-' : (char)std::tolower((unsigned char)ch));
      if (fn.empty()) fn = "session"; saveName = fn + ".tmbnt";
      std::string start = saveName; if (!curFile.empty()) { const size_t sl = curFile.find_last_of("/\\"); if (sl != std::string::npos) start = curFile.substr(0, sl + 1) + saveName; }
      SDL_ShowSaveFileDialog(onSaveDialog, nullptr, win, JSON_FILTER, 1, start.c_str()); }); };
  std::function<void(int)> openSessions = [&](int x0) {
    Dropdown d; d.x = x0; d.y = 11;
    d.items = {"NEW EMPTY SESSION", "RESTORE LAST WORK", "OPEN...", curFile.empty() ? "SAVE..." : "SAVE  " + fileName(curFile), "SAVE AS..."};
    if (!store.list.empty()) { d.items.push_back("SAVE THE " + std::to_string(store.list.size()) + " OLD IN-APP SESSIONS AS FILES..."); d.items.push_back("REMOVE THE OLD IN-APP SESSIONS"); }
    d.pick = [&, x0](int i) {
      if (i == 0) { Dropdown q; q.x = x0; q.y = 11; q.items = {"CLEAR EVERYTHING?", "YES, START EMPTY", "NO, KEEP IT"}; q.title = 0; q.danger = 1;
        q.pick = [&](int j) { if (j == 1) { ui.clearPatch(); ui.add("out", 300, 30); curFile.clear(); dirty = true; saveT = 0.3f; histDirty = true; histT = 0.1f; ui.status("NEW EMPTY SESSION"); } }; ui.openDropdown(q); return; }
      if (i == 1) { Json prev; if (Json::parse(readFile(store.path("previous.json")), prev) && prev.get("mods")) loadData(prev, "YOUR LAST WORK"); else ui.status("NOTHING TO RESTORE"); return; }
      if (i == 2) { SDL_ShowOpenFileDialog(onOpenDialog, nullptr, win, JSON_FILTER, 1, nullptr, false); return; }
      if (i == 3) { if (curFile.empty()) saveAs(); else ui.status(writeFile(curFile, sessionFile(serializePatch(ui))) ? "SAVED " + fileName(curFile) : "COULD NOT SAVE THE FILE", 4); return; }
      if (i == 4) { saveAs(); return; }
      if (i == 5) { SDL_ShowOpenFolderDialog(onFolderDialog, (void*)1, win, nullptr, false); ui.status("PICK A FOLDER FOR THEM", 4); return; }
      if (i == 6) { Dropdown z; z.x = x0; z.y = 11 + i * 8; z.items = {"REMOVE THEM FROM THE APP?", "YES (I SAVED THEM AS FILES)", "NO, KEEP THEM"}; z.title = 0; z.danger = 1;
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
      "TRIG CABLES  " + nameOf(cableChoices(), theme.trig), "MOD CABLES  " + nameOf(cableChoices(), theme.modc), std::string("LETTERS  ") + LETTERS[letters], std::string("MENUS  ") + MENUS[menus], std::string("SHADOWS  ") + (ui.shadows ? "ON" : "OFF"), std::string("BORDERS  ") + (ui.borders ? "ON" : "OFF"), "RANDOMISE", "RESET THEME"};
    d.pick = [&, x0, labels](int i) {
      if (i == 6) { Dropdown q; q.x = x0; q.y = 11 + i * 8; q.items = {"LOWERCASE  (FINER LETTERS)", "CAPITALS  (FINER LETTERS)", "CLASSIC  (THE ORIGINAL SMALL CAPITALS)"}; q.cur = letters;
        q.pick = [&](int j) { letters = j; settings.set("letters", Json::number(j)); saveSettings();
          ui.status(letterReport(), 6); }; ui.openDropdown(q); return; }
      if (i == 7) { Dropdown q; q.x = x0; q.y = 11 + i * 8; q.items = {"MATCH  (A SHADE OF THE BACKGROUND)", "BLACK", "DARK  (A DARK SHADE OF THE BACKGROUND)"}; q.cur = menus;
        q.pick = [&](int j) { menus = j; settings.set("menus", Json::number(j)); saveSettings(); applyTheme(canvas, theme, menus); ui.status(std::string("MENUS ") + MENUS[j], 2); }; ui.openDropdown(q); return; }
      if (i == 8) { ui.shadows = !ui.shadows; settings.set("shadows", Json::number(ui.shadows ? 1 : 0)); saveSettings(); ui.status(ui.shadows ? "SHADOWS UNDER THE MODULES" : "NO SHADOWS", 2); openTheme(x0); return; }
      if (i == 9) { ui.borders = !ui.borders; settings.set("borders", Json::number(ui.borders ? 1 : 0)); saveSettings(); ui.status(ui.borders ? "A LINE ROUND EACH MODULE" : "NO BORDERS", 2); openTheme(x0); return; }
      if (i >= 10) i -= 4;   // RANDOMISE and RESET come after LETTERS, MENUS, SHADOWS and BORDERS
      if (i == 6) { setTheme(randomPreset(theme, [](int n) { return (int)SDL_rand(n); })); { std::string nm = "?"; for (auto& pr : presets()) if (sameTheme(pr.second, theme)) nm = pr.first; ui.status("THEME " + nm + ": TAP RANDOMISE AGAIN FOR ANOTHER", 3); } openTheme(x0); return; }   // stays open, to roll again
      if (i == 7) { setTheme(Theme{}); ui.status("THEME RESET"); return; }
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
  // ---- SETTINGS menu: key, scale, shared tempo, audio buffer, theme ----
  std::function<void(int)> openSettings = [&](int x0) {
    Dropdown d; d.x = x0; d.y = 11;
    char bpm[32]; std::snprintf(bpm, 32, "BPM  %d", (int)std::lround(engine.tempo.bpm.load()));
    char buf[64]; std::snprintf(buf, 64, "AUDIO BUFFER  %d (%.0f MS)", BUFFERS[sound.bufIdx], BUFFERS[sound.bufIdx] * 3 * 1000.0 / engine.sr);
    d.items = {std::string("KEY  ") + NOTE_NAMES[engine.key.root.load()], std::string("SCALE  ") + scales()[engine.key.scale.load()].name, bpm, buf, std::string("QUALITY  ") + (rateIdx == 0 ? "AUTO " : "") + std::to_string((int)std::lround(engine.sr / 100) / 10) + "." + std::to_string((int)std::lround(engine.sr / 100) % 10) + "K", "SCREEN  " + std::to_string(FPS[fpsIdx]) + " FPS", std::string("CABLE MOTION  ") + (ui.cableMotion ? "ON" : "OFF"), "THEME", "ABOUT AND CREDITS"};
    d.title = -1;
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
      else if (i == 4) { q.items = {"AUTO  (YOUR SOUND OUTPUT'S OWN RATE)", "48K", "44.1K", "32K  LIGHTER", "24K  LIGHTEST, LESS TREBLE"};
        if (NRATES > 5) { q.items.push_back("96K  HEAVIER: ABOUT TWICE THE WORK"); q.items.push_back("88.2K  HEAVIER"); } q.cur = rateIdx;
        q.pick = [&](int j) { if (j == rateIdx) return;
          if (rec.on()) { rec.stop(engine); if (Module* o = outMod()) o->disp[1].store(0); }   // a recording can't change rate halfway
          const std::string snap = serializePatch(ui).dump(); const int vx = ui.viewX, vy = ui.viewY;
          sound.stop(); rateIdx = j; settings.set("rate", Json::number(j)); saveSettings();
          if (!sound.start(engine, RATES[j])) { rateIdx = 0; sound.start(engine, 0); ui.status("THAT RATE ISN'T AVAILABLE: BACK TO AUTO", 5); }
          Json js; if (Json::parse(snap, js)) { restoring = true; loadPatch(ui, js); for (auto& up : ui.mods) restoreSound(up.get()); restoring = false; }   // rebuilt at the new rate
          ui.viewX = vx; ui.viewY = vy; char b[64]; std::snprintf(b, 64, "QUALITY %.1fK SAMPLES A SECOND", engine.sr / 1000); ui.status(b, 4); }; }
      else if (i == 5) { q.items = {"60 FPS  SMOOTHEST", "30 FPS  LIGHTER", "20 FPS  LIGHTEST"}; q.cur = fpsIdx;   // dragging and patching always run at full speed
        q.pick = [&](int j) { fpsIdx = j; settings.set("fps", Json::number(j)); saveSettings(); ui.status("SCREEN " + std::to_string(FPS[j]) + " FPS WHEN IDLE, FULL SPEED WHILE YOU WORK", 4); }; }
      else if (i == 6) { ui.cableMotion = !ui.cableMotion; settings.set("cableMotion", Json::number(ui.cableMotion ? 1 : 0)); saveSettings();
        ui.status(ui.cableMotion ? "CABLE MOTION ON: PULSES AND FLOW ALONG THE CABLES" : "CABLE MOTION OFF: A LITTLE LIGHTER ON OLDER COMPUTERS", 4); return; }
      else if (i == 7) { openTheme(x0); return; }
      else if (i == 8) { q.items = {std::string("TIMBIENT MODULES ") + VERSION, "MI VOICES: MUTABLE INSTRUMENTS' PLAITS,", "  BY EMILIE GILLET (MIT LICENCE)", "  NOT MADE BY OR AFFILIATED WITH MUTABLE INSTRUMENTS",
          "ALSO SDL 3, MINIAUDIO AND RTMIDI", "THE FULL NOTICES ARE IN CREDITS.MD,", "  WHICH COMES WITH THE APP"}; q.title = 0; q.pick = [](int) {}; }
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
          if (!ui.ask && !ui.br.open && !ui.ed.open) {   // the Tracker last touched: arrows move its cursor, Delete empties the step
            const int tk = ev.key.key == SDLK_UP ? 1 : ev.key.key == SDLK_DOWN ? 2 : ev.key.key == SDLK_LEFT ? 3 : ev.key.key == SDLK_RIGHT ? 4 : (ev.key.key == SDLK_DELETE || ev.key.key == SDLK_BACKSPACE) ? 5 : 0;
            if (tk && ui.trackerKey(tk)) break; }
          if (ui.ed.open && (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_SPACE)) { ui.editorKey(ev.key.key == SDLK_ESCAPE ? 27 : ' '); break; }
          if (ui.br.open) {   // the sound browser: arrows step through, Enter loads, Escape closes
            if (ev.key.key == SDLK_DOWN) { ui.browserKey(1); break; } if (ev.key.key == SDLK_UP) { ui.browserKey(-1); break; }
            if (ev.key.key == SDLK_ESCAPE) { ui.closeBrowser(); break; }
            if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) { ui.browserLoad(); break; }
            if (ev.key.key == SDLK_SPACE) { ui.browserPreview(); break; } }
          if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_F && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)))) ui.wantFullscreen = !ui.isFullscreen;
          if (ev.key.key == SDLK_ESCAPE && ui.isFullscreen) ui.wantFullscreen = false;
          if (ev.key.key == SDLK_S && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) && !ui.ask) {   // Cmd/Ctrl+S saves to the session's file (Shift: SAVE AS)
            if (curFile.empty() || (ev.key.mod & SDL_KMOD_SHIFT)) saveAs(); else ui.status(writeFile(curFile, sessionFile(serializePatch(ui))) ? "SAVED " + fileName(curFile) : "COULD NOT SAVE THE FILE", 4); break; }
          if (ev.key.key == SDLK_D && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) && !ui.ask) { if (UiMod* u = ui.byId(ui.lastTouched)) if (!u->m->def->fixed) ui.onDuplicate(u); break; }   // Cmd/Ctrl+D: duplicate
          if (ui.ed.open && ((ev.key.key == SDLK_Z && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) || (ev.key.key == SDLK_Y && (ev.key.mod & SDL_KMOD_CTRL)))) {   // the editor open: its own undo
            ui.editorUndo(ev.key.key == SDLK_Y || (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : -1); break; }
          if (ev.key.key == SDLK_Z && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) { restoreStep((ev.key.mod & SDL_KMOD_SHIFT) ? hpos + 1 : hpos - 1); break; }   // Cmd/Ctrl+Z, and with Shift to redo
          if (ev.key.key == SDLK_Y && (ev.key.mod & SDL_KMOD_CTRL)) { restoreStep(hpos + 1); break; }
          if (ev.key.key == SDLK_EQUALS || ev.key.key == SDLK_PLUS || ev.key.key == SDLK_KP_PLUS) pendingZoom++;
          if (ev.key.key == SDLK_MINUS || ev.key.key == SDLK_KP_MINUS) pendingZoom--;
          if (ev.key.key == SDLK_0 && (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) pendingZoom = -zoom;
          if (ev.key.key == SDLK_SPACE) { engine.freeze.store(!engine.freeze.load()); ui.status(engine.freeze.load() ? "FROZEN" : "UNFROZEN"); }
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:   // right click, or Ctrl-click (a Mac trackpad): the module's menu
          if (ev.button.button == SDL_BUTTON_RIGHT || (ev.button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & SDL_KMOD_CTRL))) { ui.contextAt(ev.button.x * dens / scale, ev.button.y * dens / scale); ctxClick = true; break; }
          if (ev.button.button == SDL_BUTTON_LEFT) ui.pointerDown(ev.button.x * dens / scale, ev.button.y * dens / scale); break;
        case SDL_EVENT_MOUSE_MOTION: ui.pointerMove(ev.motion.x * dens / scale, ev.motion.y * dens / scale); break;
        case SDL_EVENT_MOUSE_BUTTON_UP: if (ctxClick) { ctxClick = false; break; } if (ev.button.button == SDL_BUTTON_LEFT) ui.pointerUp(ev.button.x * dens / scale, ev.button.y * dens / scale); break;
        case SDL_EVENT_MOUSE_WHEEL:   // with Ctrl or Cmd held: zoom. Otherwise: move around the patch
          if (SDL_GetModState() & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) { wheelAcc += ev.wheel.y; if (std::fabs(wheelAcc) >= 1) { pendingZoom += wheelAcc > 0 ? 1 : -1; wheelAcc = 0; } }
          else if (ui.ed.open) ui.editorWheel(ev.wheel.mouse_x * dens / scale, ev.wheel.y);   // the sound editor: zoom
          else if (ui.br.open) ui.browserScroll(-ev.wheel.y * 2);   // over the sound browser: scroll its list
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
        if (r.folder && r.target == -1) {   // the old in-app sessions, each as its own .tmbnt in this folder
          int n = 0; for (auto& ss : store.list) { std::string nm; for (char ch : ss.name) nm += (std::isalnum((unsigned char)ch) || ch == ' ' || ch == '-' || ch == '_') ? ch : '_';
            if (nm.empty()) nm = "SESSION"; std::string path = r.path + "/" + nm + ".tmbnt"; for (int k = 2; !readFile(path).empty(); k++) path = r.path + "/" + nm + " " + std::to_string(k) + ".tmbnt";
            if (writeFile(path, sessionFile(ss.data))) n++; }
          ui.status(n == (int)store.list.size() ? "SAVED " + std::to_string(n) + " SESSIONS. YOU CAN NOW REMOVE THEM FROM THE APP" : "SAVED " + std::to_string(n) + " OF " + std::to_string(store.list.size()), 6); continue; }
        if (r.folder) { if (ui.br.open) ui.browseTo(r.path); settings.set("browseDir", Json::string(r.path)); saveSettings(); continue; }
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
          if (const Json* pack = f.get("audio")) if (pack->t == Json::OBJ) for (auto& kv : pack->o) {   // a web export with its sounds packed in
            const std::string fn = audioDir + kv.first + ".wav"; if (!readFile(fn).empty()) continue;
            AudioBuf* b = fromBase64Pcm16(kv.second.str("b64"), (float)kv.second.num("sr", 48000), (int)kv.second.num("ch", 1)); if (b) { writeWavMono16(fn, *b); delete b; } }
          const Json* data = patchIn(f); if (!data) { ui.status("THAT IS NOT A TIMBIENT SESSION", 5); continue; }
          loadData(*data, fileName(r.path)); curFile = r.path;
          if (f.str("timbient") == "sessions") ui.status("THAT FILE HOLDS " + std::to_string(f.get("sessions")->a.size()) + " SESSIONS: THIS IS THE FIRST", 6); }
        else { std::string p = r.path; const size_t sl = p.find_last_of("/\\"); if (p.find('.', sl == std::string::npos ? 0 : sl) == std::string::npos) p += ".tmbnt";
          if (writeFile(p, sessionFile(serializePatch(ui)))) { curFile = p; ui.status("SAVED " + fileName(p), 4); } else ui.status("COULD NOT SAVE THE FILE", 5); }
      } }
    engine.collectGarbage();
    pumpInfo();
    if (rec.on()) { rec.pump(engine); if (Module* o = outMod()) { o->disp[1].store(1); o->disp[2].store(rec.frames / rec.sr); } }
    // MIDI: notice devices coming and going; give MIDI OUT modules their device
    { static float mt = 0; midi.poll(ldt); ui.midiState = midi.state(); mt -= ldt;
      if (mt <= 0) { mt = 2; std::vector<std::string> names; bool listed = false;
        for (auto& up : ui.mods) if (up->m->def->type == "midiout" && up->m->devSlot.load() < 0) { if (!listed) { names = midi.outputs(); listed = true; }
          const std::string want = up->dev.empty() ? (names.empty() ? std::string() : names[0]) : up->dev; if (!want.empty()) up->m->devSlot.store(midi.openOutput(want)); } } }
    // a Grain Looper with nothing in it catches what it hears, every couple of seconds, until it has something
    { static float catchT = 0; catchT += ldt; if (catchT > 2) { catchT = 0; for (auto& up : ui.mods) if (up->m->def->type == "looper" && up->snd[0].peaks.empty() && up->snd[0].id.empty()) catchSound(up.get(), true); } }

    if (dirty) { saveT -= ldt; if (saveT <= 0) { store.writeAutosave(serializePatch(ui)); dirty = false; } }
    if (histDirty) { histT -= ldt; if (histT <= 0) { snapshot(); histDirty = false; } }
    // draw only when it's time: never while the window can't be seen; at the chosen rate when idle; at full speed while you work
    { const float every = hidden ? 1e9f : (busyT > 0 || held || ui.ask) ? 0.f : 1.f / FPS[fpsIdx], since = (SDL_GetTicksNS() - last) / 1e9f;
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
  store.writeAutosave(serializePatch(ui));
  if (rec.on()) rec.stop(engine);
  sound.stop();
  { std::lock_guard<std::mutex> l(job.mx); job.stop = true; job.cv.notify_one(); } if (infoThread.joinable()) infoThread.join();
  SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
  return 0;
}
