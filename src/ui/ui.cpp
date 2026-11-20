#include <map>
#include <set>
// Timbient native: drawing the patch and handling the pointer (see ui.h)
#include "ui.h"
#include <chrono>
#include "help.h"
#include "../engine_mi/mi_labels.h"
#include "../engine/params.h"
#include <cmath>
#include <ctime>
#include <cstdio>

namespace tb {
static inline bool modulatable(const ParamDef& q) { return q.opts.empty() && !q.act && !q.noMod; }   // levels can be moved by mod cables; choices and buttons cannot


static const int HEAD = 8, ROW = 7, FOOT = 9, TOP = 10;
static int catCol(Cat c) { switch (c) { case Cat::Trig: return 24; case Cat::Voice: return 7; case Cat::Proc: return 17; case Cat::Mod: return 11; default: return 9; } }
// TRACKER, NOTE LOOPER: moving BPM while TEMPO is SHARED makes it OWN, starting from the session's tempo
static void takeOwnTempo(UiMod* u, int k, Engine& eng) { const ModuleDef* d = u->m->def; if (d->type == "clock" || d->params[k].key != "bpm") return; const int si = d->paramIndex("sync");
  if (si < 0 || u->m->p[si].load() < 0.5f || u->m->clockedIn.load()) return; u->m->p[si].store(0); u->m->p[k].store(std::round(eng.tempo.bpm.load())); }
static void clockToTempo(UiMod* u, int k, Engine& eng);
// TAP: the speed controls follow taps in rhythm (three or more). What a tap gap means for each:
// 0 not tappable, 1 BPM, 2 steps a second (shown as BPM), 3 seconds, 4 Hz
static int tapKind(const ModuleDef* d, const ParamDef& q) {
  if (q.key == "bpm") return 1;
  if ((d->type == "clump" && q.key == "speed") || (d->type == "hops" && q.key == "rate")) return 2;
  if ((d->type == "echo" && q.key == "time") || (d->type == "lfo" && q.key == "rate") || (d->type == "rand" && q.key == "rate")) return 3;
  if (d->type == "phaser" && q.key == "rate") return 4;
  return 0; }
bool Ui::tapRow(UiMod* u, int k) {
  const ModuleDef* d = u->m->def; const ParamDef& q = d->params[k]; const int kind = tapKind(d, q); if (!kind) return false;
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (tapM != u || tapK != k) { tapTimes.clear(); tapM = u; tapK = k; }
  if (!tapTimes.empty() && now - tapTimes.back() > 4) tapTimes.clear();   // a long pause: starting again
  tapTimes.push_back(now); if (tapTimes.size() > 5) tapTimes.erase(tapTimes.begin());
  if (tapTimes.size() < 3) return false;   // the first two taps behave as before
  std::vector<double> gaps; for (size_t i = 1; i < tapTimes.size(); i++) gaps.push_back(tapTimes[i] - tapTimes[i - 1]);
  std::vector<double> srt = gaps; std::sort(srt.begin(), srt.end()); const double med = srt[srt.size() / 2];
  double sum = 0; int nn = 0; for (double g : gaps) if (g > med * 0.6 && g < med * 1.6) { sum += g; nn++; }   // a stray slow (or quick) tap is left out
  if (nn < 2 || med < 0.05) return false;
  const double gap = sum / nn; float v = 0;
  switch (kind) { case 1: v = (float)(60 / gap); break; case 2: v = (float)(1 / gap); break; case 3: v = (float)gap; break; default: v = (float)(1 / gap); break; }
  v = clampf(v, q.min, q.max); if (kind == 1) v = std::round(v);
  takeOwnTempo(u, k, eng); u->m->p[k].store(v); clockToTempo(u, k, eng);
  char b[48]; if (kind <= 2) std::snprintf(b, 48, "TAP %d BPM", (int)std::lround(kind == 1 ? v : v * 60)); else if (kind == 3) std::snprintf(b, 48, "TAP %.2f S", v); else std::snprintf(b, 48, "TAP %.2f HZ", v);
  status(d->name + "  " + q.label + "  " + b, 1.5f); changed(); return true;
}
static bool euclidHit(int i, int steps, int hits, int rot) { if (steps < 1 || hits < 1) return false; if (hits >= steps) return true; const int k = ((i - rot) % steps + steps) % steps; return (k * hits) % steps < hits; }   // as EUCLID DRUMS plays it
// modules whose TYPE (or MODE) list has light kinds then heavier ones: the key, where the heavy ones start (past the end: none) and the help prefix
struct TypeInfo { const char* type; const char* key; int from; const char* prefix; };
static const TypeInfo TYPE_INFO[] = {{"reverb", "type", 8, "VERB "}, {"echo", "type", 6, "ECHO "}, {"pitchshift", "mode", 3, "PITCH "}, {"filter", "type", 5, "FILTER "}, {"amp", "type", 6, "DRIVE "}, {"phaser", "type", 4, "MOVE "}, {"pluck", "type", 5, "PLUCK "}, {"resonator", "type", 3, "RES "}, {"pad", "type", 2, "PAD "}, {"analog", "type", 2, "ANALOG "}, {"breath", "type", 3, "BREATH "}, {"formant", "type", 1, "FORMANT "}, {"fold", "type", 1, "FOLD "}, {"tape", "type", 4, "TAPE "}, {"comp", "type", 4, "COMP "}};
static const TypeInfo* typeInfo(const std::string& ty, const std::string& key) { for (const TypeInfo& t : TYPE_INFO) if (ty == t.type && key == t.key) return &t; return nullptr; }
// REVERB: a new TYPE brings its usual DECAY (a ROOM doesn't ring for 6 seconds); INFINITE stays INFINITE
static void verbTypeSet(UiMod* u, int k, float old, float v) {
  const ModuleDef* d = u->m->def; if (d->type != "reverb" || d->params[k].key != "type" || std::lround(old) == std::lround(v)) return;
  static const float DEC[13] = {0.9f, 2.5f, 6, 10, 10, 8, 8, 1.5f, 12, 5, 10, 14, 14}; const int di = d->paramIndex("decay"), t = std::max(0, std::min(12, (int)std::lround(v)));
  if (di >= 0 && u->m->p[di].load() < 99.f) u->m->p[di].store(DEC[t]); }
static int typeCol(PType t) { return t == PType::Audio ? 43 : t == PType::Trig ? 44 : 45; }

// the ADD menu, in the web version's order (types not built yet are skipped)
static const std::vector<std::pair<std::string, std::vector<std::string>>> MENU = {
  {"RHYTHM", {"clock", "tracker", "keys", "clump", "bursts", "orbits", "hops", "walker", "tombola", "tools", "nloop", "journey", "midi", "midiout", "qwerty"}},
  {"VOICES", {"tone", "pluck", "mi", "click", "euclid", "analog", "pad", "formant", "breath", "additive", "fold", "sample", "noinput", "audioin"}},
  {"PROCESS", {"amp", "vca", "filter", "phaser", "echo", "reverb", "pitchshift", "stretch", "resonator", "ducker", "looper", "tape", "comp", "limit", "master", "mixer", "xfade", "level", "listen", "spectrum"}},
  {"MOD", {"lfo", "env", "rand", "follow"}}};

// ---------------- building ----------------
UiMod* Ui::add(const std::string& type, int x, int y, int id) {
  if (id <= 0 || byId(id)) id = nextId++; else nextId = std::max(nextId, id + 1);
  Module* m = eng.create(type, id); if (!m) return nullptr;
  auto u = std::make_unique<UiMod>(); u->m = m; u->x = x; u->y = y; u->cables = &cables; UiMod* r = u.get(); mods.push_back(std::move(u)); eng.add(m); return r;
}
void Ui::remove(UiMod* u) {
  const int id = u->m->id;
  cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const UiCable& c) { return c.from == id || c.to == id; }), cables.end());
  eng.remove(id);   // the engine frees the module once it has stopped using it
  mods.erase(std::remove_if(mods.begin(), mods.end(), [&](const std::unique_ptr<UiMod>& p) { return p.get() == u; }), mods.end());
  drag = Drag();
}
bool Ui::connect(UiMod* a, int fp, UiMod* b, int tp) {
  if (!a || !b || a == b || fp < 0 || tp < 0) return false;
  if (tp == SWITCH_PORT && (a->m->def->outs[fp].type != PType::Trig || !hasSwitch(*b))) return false;
  if (tp == KEY_PORT && (a->m->def->outs[fp].name != "KEY" || !hasKey(*b))) { status("THE KEY SOCKET TAKES A KEY JOURNEY'S KEY", 3); return false; }
  for (auto& c : cables) if (c.from == a->m->id && c.fromPort == fp && c.to == b->m->id && c.toPort == tp) return false;
  Cable c; c.from = a->m->id; c.fromPort = fp; c.to = b->m->id; c.toPort = tp; c.type = a->m->def->outs[fp].type; if (c.type == PType::Audio) c.g = 0;   // audio fades in
  eng.connect(c);
  cables.push_back({c.from, fp, c.to, tp, -1, c.type}); return true;
}
bool Ui::connect(UiMod* a, const std::string& op, UiMod* b, const std::string& ip) { return a && b && connect(a, a->m->def->outIndex(op), b, b->m->def->inIndex(ip)); }
bool Ui::modulate(UiMod* s, UiMod* d, int pk) {
  const int op = s->m->def->outIndex("MOD") >= 0 ? s->m->def->outIndex("MOD") : s->m->def->outIndex("CC"); if (pk < 0 || op < 0 || s == d || !modulatable(d->m->def->params[pk])) return false;
  for (auto& c : cables) if (c.type == PType::Mod && c.from == s->m->id && c.to == d->m->id && c.toParam == pk) return false;
  Cable c; c.from = s->m->id; c.fromPort = op; c.to = d->m->id; c.toParam = pk; c.type = PType::Mod; eng.connect(c);
  cables.push_back({c.from, op, c.to, -1, pk, PType::Mod}); return true;
}
void Ui::unplug(const UiCable& k) {
  Cable c; c.from = k.from; c.fromPort = k.fromPort; c.to = k.to; c.toPort = k.toPort; c.toParam = k.toParam; c.type = k.type;
  if (c.type == PType::Audio) eng.fade(c, 0, true); else eng.disconnect(c);   // audio fades out (no click)
  cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const UiCable& x) { return x.from == k.from && x.fromPort == k.fromPort && x.to == k.to && x.toPort == k.toPort && x.toParam == k.toParam && x.type == k.type; }), cables.end());
}
// the × on a module: it goes without a gap in the music. An effect in the middle of a chain is bridged (what fed it now
// goes straight on to where it went, fading in), and anything that makes sound rings out before it's gone
void Ui::removeGently(UiMod* u) {
  const int id = u->m->id; const ModuleDef* d = u->m->def;
  const bool fx = !d->ins.empty() && d->ins[0].type == PType::Audio && !d->outs.empty() && d->outs[0].type == PType::Audio;
  if (fx) { std::vector<UiCable> ins, outs; for (auto& c : cables) { if (c.to == id && c.type == PType::Audio && c.toPort == 0) ins.push_back(c); if (c.from == id && c.type == PType::Audio && c.fromPort == 0) outs.push_back(c); }
    for (auto& a : ins) for (auto& b : outs) { UiMod *s = byId(a.from), *t = byId(b.to); if (s && t) connect(s, a.fromPort, t, b.toPort); } }
  cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const UiCable& c) { return c.from == id || c.to == id; }), cables.end());
  eng.retire(id);
  mods.erase(std::remove_if(mods.begin(), mods.end(), [&](const std::unique_ptr<UiMod>& p) { return p.get() == u; }), mods.end());
  drag = Drag(); selOn = false;
}
void Ui::set(UiMod* m, const std::string& k, float v) { const int i = m->m->def->paramIndex(k); if (i >= 0) m->m->p[i].store(v); }
void Ui::clearPatch() { for (auto& u : mods) eng.remove(u->m->id); mods.clear(); cables.clear(); drag = Drag(); dd.reset(); menuOpen = false;
  midiMaps.erase(std::remove_if(midiMaps.begin(), midiMaps.end(), [](const MidiMap& m) { return !m.global; }), midiMaps.end()); for (auto& m : midiMaps) { m.mod = 0; m.engaged = false; m.fb = -1; } cancelLearn(); }   // (EVERY SESSION mappings stay)

// a patch in the spirit of your session, so there's something to listen to straight away
void Ui::demoPatch() {
  auto col = [&](int x, std::vector<std::string> types) { std::vector<UiMod*> v; int y = 16; for (auto& t : types) { UiMod* u = add(t, x, y); v.push_back(u); y += geom(*u).h + 5; } return v; };
  auto c1 = col(6, {"clump", "orbits", "clock"}), c2 = col(86, {"pluck", "tone"}), c3 = col(166, {"pad", "additive"}), c4 = col(246, {"echo", "filter", "lfo"}), c5 = col(326, {"mixer"}), c6 = col(406, {"reverb", "ducker"}), c7 = col(486, {"out"});
  UiMod *clump = c1[0], *orb = c1[1], *clk = c1[2], *pl = c2[0], *fm = c2[1], *pad = c3[0], *bell = c3[1], *echo = c4[0], *filt = c4[1], *lfo = c4[2], *mix = c5[0], *verb = c6[0], *duck = c6[1], *out = c7[0];
  set(clump, "oct", 5); set(clump, "mutate", 1); set(clump, "speed", 2.5f); set(pl, "tone", 0.08f); set(pad, "cut", 420); set(verb, "decay", 10); set(verb, "mix", 0.5f);
  set(duck, "depth", 0.9f); set(duck, "curve", 1); set(duck, "rbeats", 3); set(clk, "bpm", 60); set(clk, "sync", 0); set(fm, "decay", 1.6f);
  set(lfo, "rate", 20); set(pad, "air", 0.05f);
  connect(clump, "TRIG", pl, "TRIG"); connect(clump, "TRIG", fm, "TRIG"); connect(orb, "TRIG", pad, "TRIG"); connect(orb, "TRIG", bell, "TRIG");
  connect(pl, "OUT", echo, "IN"); connect(fm, "OUT", echo, "IN"); connect(echo, "OUT", mix, "1"); connect(pad, "OUT", filt, "IN"); connect(filt, "OUT", mix, "2"); connect(bell, "OUT", mix, "3");
  connect(mix, "OUT", verb, "IN"); connect(verb, "OUT", duck, "IN"); connect(duck, "OUT", out, "IN"); connect(clk, "BAR", duck, "KEY");
  modulate(lfo, filt, filt->m->def->paramIndex("cut"));
}

// ---------------- geometry and hit testing ----------------
static int trLen(Module* m) { return std::max(1, std::min(Module::SEQ_MAX, (int)m->p[3].load())); }
static int trCols(Module*) { return 4; }   // the TRACKER always has four
static int trShown(Module* m) { int last = -1; const int nc = trCols(m); for (int i = Module::SEQ_MAX - 1; i >= 0 && last < 0; i--) for (int c = 0; c < nc; c++) if (m->seq[Module::si(i, c)].load() >= 0) { last = i; break; } return std::max(trLen(m), last + 1); }
static constexpr int TR_VIS = 4;   // columns on show at once
static int trHead(Module* m) { return trCols(m) > 1 ? 7 : 0; }   // with more than one column: a strip numbering those on show, with < and > to move along
static int trRows(Module* m) { return std::min(16, trShown(m)); }   // the Tracker grows with the rows in use, up to 16; beyond that it scrolls
// THE ORDER OF THE ROWS (only how they're shown: sessions keep the stored order, by name). Every module reads the same:
// VOICES; a big RECORD button; the settings that change what the others do (SOURCE, SYNC with BPM, KEY and SCALE...);
// its buttons (CLEAR last); what it does (PITCH with GLIDE under it...); tone (CUTOFF, RES); setup (CHANNELS); and
// PAN, WIDTH, LEVEL last. A thin line between the parts.
static const std::set<std::string> REC_BUTTON_TYPES = {"sample", "nloop", "tracker", "out"};
static bool recButton(const ModuleDef* d, const ParamDef& q) { return q.key == "rec" && REC_BUTTON_TYPES.count(d->type); }
struct Layout { std::vector<int> order; std::vector<int> part; };   // part: which part each row is in (a line between parts)
static const Layout& layoutOf(const ModuleDef* d) {
  static std::map<const ModuleDef*, Layout> cache; auto it = cache.find(d); if (it != cache.end()) return it->second;
  Layout L; const int n = (int)d->params.size(); std::vector<char> used(n, 0);
  auto put = [&](int j, int part) { if (j >= 0 && j < n && !used[j]) { used[j] = 1; L.order.push_back(j); L.part.push_back(part); } };
  auto key = [&](const char* k) { return d->paramIndex(k); };
  static const std::map<std::string, std::vector<std::string>> SPEC = {   // modules laid out by hand ("|": a line)
    {"sample", {"poly", "rec", "|", "reclen", "recmode", "keep", "mon", "savetake", "|", "load", "edit", "kind", "slices", "mode", "notes", "every", "start", "len", "pitch", "rev", "atk", "rel", "fade", "drift", "cut", "res", "vsens", "|", "chan", "|", "pan", "width", "level"}},
    {"nloop", {"rec", "thru", "sync", "bpm", "|", "undo", "rrhythm", "rorder", "urand", "clear", "|", "start", "lenb", "dir", "trans", "quant", "drift", "vel", "chance"}},
  };
  auto sp = SPEC.find(d->type);
  if (sp != SPEC.end()) { int part = 0; for (auto& k : sp->second) { if (k == "|") { part++; continue; } put(key(k.c_str()), part); } for (int j = 0; j < n; j++) put(j, part); }   // anything not named: at the end
  else {
    static const std::map<std::string, std::vector<std::string>> MODES = {
      {"looper", {"src", "freeze"}}, {"stretch", {"src", "freeze"}}, {"filter", {"type", "cut", "res", "vowel", "shape", "drive"}}, {"amp", {"type", "gain", "amt", "rate", "tone", "mix"}}, {"phaser", {"type", "rate", "depth", "time", "centre", "stages", "fb"}}, {"pluck", {"type", "rmodel", "rstruct", "rpos", "body", "symp"}}, {"resonator", {"type", "rmodel", "rstruct", "rpos"}}, {"pad", {"type", "wave", "scan", "tilt", "even", "shim", "spread"}}, {"analog", {"type", "shape", "drive"}}, {"breath", {"type", "blow", "jet", "stiff"}}, {"formant", {"type", "size"}}, {"fold", {"type"}}, {"tape", {"type", "age"}}, {"comp", {"type", "colour"}}, {"mi", {"voice", "env"}}, {"clock", {"sync", "bpm"}}, {"tracker", {"sync", "bpm"}},
      {"lfo", {"sync"}}, {"journey", {"steps", "order", "moves"}}, {"noinput", {"route", "pulse", "prate"}}, {"euclid", {"lane", "st1", "hi1", "ro1", "tu1", "de1", "ch1", "lv1", "st2", "hi2", "ro2", "tu2", "de2", "ch2", "lv2", "st3", "hi3", "ro3", "tu3", "de3", "ch3", "lv3", "st4", "hi4", "ro4", "tu4", "de4", "ch4", "lv4"}}, {"pitchshift", {"mode", "shift", "fine", "inkey", "sub", "sub2", "up", "detune", "h1", "h2", "h3"}}, {"echo", {"type", "wow", "drive", "age", "pattern", "shift", "bend", "sweep", "smear", "crush", "scatter", "density"}}, {"reverb", {"type", "shift", "amount", "fall", "spread", "blur", "tilt", "hold", "drive", "wow", "density", "scatter", "lowdec", "hidec", "freeze"}}};
    const bool keyed = d->keyIdx >= 0;
    put(key("poly"), 0); for (int j = 0; j < n; j++) if (recButton(d, d->params[j])) put(j, 0);
    if (auto m = MODES.find(d->type); m != MODES.end()) for (auto& k : m->second) put(key(k.c_str()), 0);
    if (keyed) { put(d->keyIdx, 0); put(d->scaleIdx, 0); }
    const int ld = key("load"), ed = key("edit");   // the buttons, together: EDIT SOUND under LOAD FILE, the ones that wipe things last
    for (int j = 0; j < n; j++) if (d->params[j].act && d->params[j].key != "clear" && d->params[j].key != "del" && j != ed) { put(j, 1); if (j == ld) put(ed, 1); }
    put(ed, 1); put(key("del"), 1); put(key("clear"), 1);
    std::set<int> tail = {key("chan"), key("pan"), key("width"), key("level"), d->lpCut, d->lpRes}; const int gl = std::max(key("glide"), key("pglide"));
    if (d->type != "tone") tail.insert(gl);   // TONE's own GLIDE stays where it is (after WAVE)
    const int pitch = key("pitch");
    for (int j = 0; j < n; j++) if (!tail.count(j)) { put(j, 2); if (j == pitch && d->type != "tone") put(gl, 2); }
    if (pitch < 0 && d->type != "tone") put(gl, 0);
    put(d->lpCut, 3); put(d->lpRes, 3); put(key("chan"), 3); put(key("pan"), 4); put(key("width"), 4); put(key("level"), 4);
  }
  return cache[d] = L;
}
// rows that don't apply, given this module's own settings, fold away (the module grows shorter); rows waiting on a cable stay, dimmed
static bool hiddenRow(const UiMod& u, int j, const std::vector<UiCable>* cables = nullptr) {
  const ModuleDef* d = u.m->def; const ParamDef& q = d->params[j]; const auto P = [&](const char* k) { const int i = d->paramIndex(k); return i >= 0 ? u.m->p[i].load() : 0.f; };
  if (q.only == "file" && P("src") < 0.5f) return true;                 // STRETCH: only for a loaded file
  if (q.only == "wander" && P("path") < 4.5f) return true;              // KEY JOURNEY: HOME PULL is for WANDER
  if (q.only == "held" && P("src") < 0.5f) return true;                 // GRAIN LOOPER LIVE: nothing held to edit
  if (q.only == "overdub" && P("recmode") < 0.5f) return true;          // SAMPLER: KEEP is for OVERDUB
  if (q.only == "adsr" && u.m->p[6].load() < 0.5f) return true;
  if (q.only == "never") return true;   // kept with the session, but set somewhere else (the Tracker's RANDOM NOTE: on its column numbers)
  if (q.only.rfind("l:", 0) == 0) return std::to_string(std::lround(P("lane"))) != q.only.substr(2);   // EUCLID DRUMS: only the chosen LANE's settings
  if (q.only.rfind("v:", 0) == 0) {   // shown for some TYPEs only ("v:4,12": types 4 and 12)
    const std::string want = "," + q.only.substr(2) + ",", t = "," + std::to_string(std::lround(P(d->paramIndex("type") >= 0 ? "type" : d->paramIndex("move") >= 0 ? "move" : "mode"))) + ",";   /* TYPE, or PITCH SHIFT's MODE */ return want.find(t) == std::string::npos; }         // MI VOICES: the envelope is only used in ADSR
  if (d->type == "mi" && (q.key == "decay" || q.key == "colour") && miEngines()[std::max(0, std::min(MI_ENGINES - 1, (int)u.m->p[0].load()))].selfEnv) return true;   // models that shape themselves: Plaits skips its gate
  if (d->type == "xfade" && (q.key == "sync" || q.key == "bpm") && std::lround(P("move")) == 0) return true;   // CROSSFADER: its tempo only matters while it moves
  if (q.only == "own") { const int si = d->paramIndex("sync"); if (si >= 0 && d->params[si].tog && u.m->p[si].load() > 0.5f) return true; }   // BPM: not while it follows the session's tempo
  if (d->type == "sample") { const int kind = (int)P("kind"); const bool once = P("mode") > 0.5f;   // what doesn't apply to this kind of sound or way of playing
    if ((q.only == "slices" && kind != 1) || (q.only == "one" && kind != 0) || (q.only == "region" && kind == 1) || (q.only == "once" && !once) || (q.only == "loop" && once)) return true;
    static const std::set<std::string> REC = {"rec", "reclen", "recmode", "keep", "mon", "savetake"};   // recording: once something comes into IN (or REC), or while it records
    if (REC.count(q.key) && cables && P("rec") < 0.5f && u.m->disp[40].load() < 0.5f) { bool in = false; for (auto& c : *cables) if (c.to == u.m->id && (c.toPort == 2 || c.toPort == 3)) in = true; if (!in) return true; } }
  return false; }
static std::vector<int> rowOrder(const UiMod& u) { std::vector<int> o; for (int j : layoutOf(u.m->def).order) if (!hiddenRow(u, j, u.cables)) o.push_back(j); return o; }
static bool lineAbove(const UiMod& u, const std::vector<int>& o, int r) {   // a thin line where one part of the module gives way to the next
  if (r <= 0 || r >= (int)o.size()) return false; const Layout& L = layoutOf(u.m->def); int pa = -1, pb = -1;
  for (size_t k = 0; k < L.order.size(); k++) { if (L.order[k] == o[r - 1]) pa = L.part[k]; if (L.order[k] == o[r]) pb = L.part[k]; } return pa != pb; }
std::vector<int> Ui::shownRows(const UiMod& u) const { return rowOrder(u); }
std::vector<std::string> Ui::cutOff() const {   // rows whose name, or one of whose choices, wouldn't fit on its module
  std::vector<std::string> v;
  for (auto* d : allDefs()) for (auto& q : d->params) { if (q.act) { if (Canvas::tw(q.label) > d->width - 12) v.push_back(d->type + " " + q.label); continue; }
    if (q.label.size() > 11) v.push_back(d->type + " " + q.label + " (NAME)");
    const size_t room = (size_t)std::max(6, (d->width - Canvas::tw(q.label.substr(0, 11)) - 10 - (jacksIn ? 7 : 0)) / 4);
    for (auto& o : q.opts) if (o.size() > room) v.push_back(d->type + " " + q.label + ": " + o); }
  return v; }
static constexpr int LINE_GAP = 3;   // the room a line between parts takes
static bool recRow(const UiMod& u, int j) { return j >= 0 && recButton(u.m->def, u.m->def->params[j]); }
static int rowTopOf(const UiMod& u, const std::vector<int>& o, int r) { int y = r * ROW;   // a line between parts takes a little room; so does a RECORD button (a little taller)
  for (int k = 0; k <= r && k < (int)o.size(); k++) { if (k > 0 && lineAbove(u, o, k)) y += LINE_GAP; if (recRow(u, o[k])) y += 2; if (k < r && recRow(u, o[k])) y += 2; } return y; }
static int rowsHeight(const UiMod& u, const std::vector<int>& o) { return o.empty() ? 0 : rowTopOf(u, o, (int)o.size() - 1) + ROW + (recRow(u, o.back()) ? 2 : 0); }
static int rowPx(const UiMod& u, int j) { const auto o = rowOrder(u); for (int r = 0; r < (int)o.size(); r++) if (o[r] == j) return rowTopOf(u, o, r); return 0; }   // a hidden row: where the first is
static int rowOf(const UiMod& u, int j) { const auto o = rowOrder(u); for (int r = 0; r < (int)o.size(); r++) if (o[r] == j) return r; return -1; }
static int paramOfRow(const UiMod& u, int r) { const auto o = rowOrder(u); return r >= 0 && r < (int)o.size() ? o[r] : -1; }
static int rowY(const UiMod& u, int j) { return std::max(0, rowOf(u, j)); }   // a cable to a hidden row lands on the first one
// effects can be bypassed: a straight audio IN to an audio OUT (not MIXER, which has four)
static bool bypassable(const ModuleDef* d) { return d->cat == Cat::Proc && d->type != "mixer" && !d->ins.empty() && d->ins[0].type == PType::Audio && !d->outs.empty() && d->outs[0].type == PType::Audio; }
bool Ui::hasSwitch(const UiMod& u) const { return !u.m->def->fixed; }
bool Ui::hasKey(const UiMod& u) const { return u.m->def->keyIdx >= 0; }   // sequencers that play in a key: a KEY socket beside the SWITCH
int Ui::kyX(const UiMod& u) const { return swX(u) + 8; }
bool Ui::feedsBack(int id, int port) const {
  std::set<int> seen{id}; std::vector<int> todo{id};
  while (!todo.empty()) { const int at = todo.back(); todo.pop_back();
    for (auto& c : cables) if (c.from == at && c.type == PType::Audio && c.toParam < 0) { if (c.to == id && c.toPort == port) return true; if (seen.insert(c.to).second) todo.push_back(c.to); } }
  return false; }
int Ui::swX(const UiMod& u) const { const ModuleDef* d = u.m->def; const bool onOff = d->cat == Cat::Trig || d->cat == Cat::Mod;
  return u.x + (onOff ? 23 : (d->cat == Cat::Voice || bypassable(d)) ? 28 : 17); }
int Ui::swY(const UiMod& u) const { return u.y + geom(u).h - FOOT + 4; }
// a sequencer's own KEY and SCALE, or the session's when set to GLOBAL
static int keyOf(const UiMod& u, const Engine& eng) { if (u.m->keyIn.load() >= 0) return u.m->keyIn.load(); const int k = u.m->def->keyIdx; const int v = k >= 0 ? (int)u.m->p[k].load() : 0; return v >= 1 ? v - 1 : eng.key.root.load(); }
static int scaleOf(const UiMod& u, const Engine& eng) { if (u.m->scaleIn.load() >= 0) return u.m->scaleIn.load(); const int k = u.m->def->scaleIdx; const int v = k >= 0 ? (int)u.m->p[k].load() : 0; return v >= 1 && v <= (int)scales().size() ? v - 1 : eng.key.scale.load(); }
// NOTE GRID's SIZE: bigger buttons (and a wider module) so long note names sit comfortably
void keysCell(const UiMod& u, int& cw, int& ch) { cw = 15; ch = 9; if (u.m->def->type != "keys") return; const int k = u.m->def->paramIndex("size"); const int z = k >= 0 ? (int)u.m->p[k].load() : 0;
  static const int W[3] = {15, 19, 24}, H[3] = {9, 11, 13}; cw = W[std::max(0, std::min(2, z))]; ch = H[std::max(0, std::min(2, z))]; }
Ui::Geom Ui::geom(const UiMod& u) const {
  const ModuleDef* d = u.m->def; const int jr = (int)std::max(d->ins.size(), outInHead(u) ? (size_t)0 : d->outs.size()), np = u.fold ? 0 : (int)rowOrder(u).size();
  int cw = 15, ch = 9; keysCell(u, cw, ch);
  const int vh = d->type == "tracker" && d->vis ? trRows(u.m) * 6 + 1 + trHead(u.m) : d->type == "keys" ? ch * 4 : d->vis;
  Geom g; g.jr = jr; g.vh = vh; g.vy = HEAD + jr * ROW + (vh ? 1 : 0); g.py = HEAD + jr * ROW + (vh ? vh + 2 : 0); g.h = g.py + (np ? rowsHeight(u, rowOrder(u)) : 0) + (np ? 2 : 0) + FOOT;   // a little room under the last row (for the tails of g, p, y)
  g.w = d->width + (d->type == "keys" ? 8 * (cw - 15) : 0); return g;
}
UiMod* Ui::hit(int wx, int wy, int& lx, int& ly) {
  for (int i = (int)mods.size() - 1; i >= 0; i--) { UiMod& u = *mods[i]; const Geom g = geom(u);
    if (wx >= u.x && wx < u.x + g.w && wy >= u.y && wy < u.y + g.h) { lx = wx - u.x; ly = wy - u.y; return &u; } }
  return nullptr;
}
Ui::JackHit Ui::jackAt(int wx, int wy, int tol) {
  // a jack only counts where it can be seen: not when another module sits on top of it, or of the pointer
  auto covered = [this](int i, int x, int y) { for (size_t j = (size_t)i + 1; j < mods.size(); j++) { const UiMod& o = *mods[j]; const Geom g = geom(o);
      if (x >= o.x && x < o.x + g.w && y >= o.y && y < o.y + g.h) return true; } return false; };
  for (int i = (int)mods.size() - 1; i >= 0; i--) { UiMod& u = *mods[i]; const ModuleDef* d = u.m->def;
    if (covered(i, wx, wy)) continue;
    for (size_t k = 0; k < d->ins.size(); k++) { const int jx = inJackX(u), jy = u.y + jackY((int)k); if (std::abs(wx - jx) <= tol && std::abs(wy - jy) <= tol && !covered(i, jx, jy)) return {&u, false, (int)k, d->ins[k].type, jx, jy}; }
    for (size_t k = 0; k < d->outs.size(); k++) { const int jx = outJackX(u), jy = u.y + outJackY(u, (int)k); if (std::abs(wx - jx) <= tol && std::abs(wy - jy) <= tol && !covered(i, jx, jy)) return {&u, true, (int)k, d->outs[k].type, jx, jy}; }
    if (hasSwitch(u)) { const int jx = swX(u), jy = swY(u); if (std::abs(wx - jx) <= std::min(tol, 3) && std::abs(wy - jy) <= std::min(tol, 3) && !covered(i, jx, jy)) return {&u, false, SWITCH_PORT, PType::Trig, jx, jy}; }
    if (hasKey(u)) { const int jx = kyX(u), jy = swY(u); if (std::abs(wx - jx) <= std::min(tol, 3) && std::abs(wy - jy) <= std::min(tol, 3) && !covered(i, jx, jy)) return {&u, false, KEY_PORT, PType::Trig, jx, jy}; }
    if (!u.fold) { const Geom g = geom(u); const int st = std::min(tol, 3);   // the mod socket beside each setting that can be moved
      for (size_t k = 0; k < d->params.size(); k++) { if (!modulatable(d->params[k]) || rowOf(u, (int)k) < 0) continue; const int jx = sockX(u), jy = u.y + g.py + rowPx(u, (int)k) + 3;
        if (std::abs(wx - jx) <= st && std::abs(wy - jy) <= st && !covered(i, jx, jy)) { JackHit h{&u, false, -1, PType::Mod, jx, jy}; h.param = (int)k; return h; } } } }
  return {};
}
int Ui::paramAt(UiMod* u, int ly) { const Geom g = geom(*u); if (u->fold || ly < g.py) return -1; const auto o = rowOrder(*u);
  for (int r = 0; r < (int)o.size(); r++) { const int t = rowTopOf(*u, o, r); if (ly - g.py >= t && ly - g.py < t + ROW) return o[r]; } return -1; }

// ---------------- drawing ----------------
// a cable hangs as a curve that sags between its ends (the same shape as the web version)
static inline void cablePt(float ax, float ay, float bx, float by, float t, float& x, float& y) {
  const float sag = std::min(28.f, std::hypot(bx - ax, by - ay) * 0.3f + 4), mx = (ax + bx) / 2, my = (ay + by) / 2 + sag;
  x = (1 - t) * (1 - t) * ax + 2 * (1 - t) * t * mx + t * t * bx; y = (1 - t) * (1 - t) * ay + 2 * (1 - t) * t * my + t * t * by;
}
void Ui::drawCable(Canvas& c, int ax, int ay, int bx, int by, int col, int flowCol, float flow, float period, float dash, int tailCol) {
  // two pixels thick, so what flows along it can be seen. Flowing: dashes moving from the output towards the input, a bright head at the front
  int px0 = ax, py0 = ay; float run = 0;
  for (int i = 1; i <= 20; i++) { float x, y; cablePt((float)ax, (float)ay, (float)bx, (float)by, i / 20.f, x, y); const int xi = (int)std::lround(x), yi = (int)std::lround(y);
    const int dx = xi - px0, dy = yi - py0, n = std::max(1, std::max(std::abs(dx), std::abs(dy))); const float step = std::hypot((float)dx, (float)dy) / n;
    const int ox = std::abs(dx) > std::abs(dy) ? 0 : 1, oy = 1 - ox;
    for (int s2 = 0; s2 <= n; s2++) { int cc = col;
      if (flowCol >= 0) { float ph = std::fmod(run + s2 * step - flow, period); if (ph < 0) ph += period; if (ph < dash) cc = ph >= dash - 1 ? flowCol : tailCol >= 0 ? tailCol : col; }
      const int qx = px0 + (int)std::lround((float)dx * s2 / n), qy = py0 + (int)std::lround((float)dy * s2 / n); c.px(qx, qy, cc); c.px(qx + ox, qy + oy, cc); }
    run += n * step; px0 = xi; py0 = yi; }
}
bool Ui::cableEnds(const UiCable& cb, float& ax, float& ay, float& bx, float& by) {
  UiMod *a = byId(cb.from), *b = byId(cb.to); if (!a || !b) return false;
  ax = (float)outJackX(*a); ay = (float)(a->y + outJackY(*a, cb.fromPort));
  if (cb.type == PType::Mod) { const Geom g = geom(*b); bx = (float)sockX(*b); by = (float)(b->y + (b->fold ? g.h - FOOT : g.py + rowPx(*b, cb.toParam) + 3)); }
  else if (cb.toPort == SWITCH_PORT) { bx = (float)swX(*b); by = (float)swY(*b); }
  else if (cb.toPort == KEY_PORT) { bx = (float)kyX(*b); by = (float)swY(*b); }
  else { bx = (float)inJackX(*b); by = (float)(b->y + jackY(cb.toPort)); }
  return true;
}
int Ui::cableAt(int wx, int wy, float maxD, bool audioOnly) {   // the nearest cable within maxD pixels, or -1
  int best = -1; float bd = maxD;
  for (size_t k = 0; k < cables.size(); k++) { const UiCable& cb = cables[k]; if (audioOnly && cb.type != PType::Audio) continue; float ax, ay, bx, by; if (!cableEnds(cb, ax, ay, bx, by)) continue;
    float x0 = ax, y0 = ay;
    for (int i = 1; i <= 20; i++) { float x1, y1; cablePt(ax, ay, bx, by, i / 20.f, x1, y1);
      const float vx = x1 - x0, vy = y1 - y0, l = std::max(1e-6f, vx * vx + vy * vy), t = clampf(((wx - x0) * vx + (wy - y0) * vy) / l, 0, 1), d = std::hypot(wx - x0 - t * vx, wy - y0 - t * vy);
      if (d < bd) { bd = d; best = (int)k; } x0 = x1; y0 = y1; } }
  return best;
}
std::string Ui::cableName(const UiCable& cb) {
  UiMod *a = byId(cb.from), *b = byId(cb.to); if (!a || !b) return "";
  std::string s = a->m->def->name + " > " + b->m->def->name;
  if (cb.type == PType::Mod) s += " " + b->m->def->params[cb.toParam].label; else if (cb.toPort == SWITCH_PORT) s += " SWITCH"; else if (cb.toPort == KEY_PORT) s += " KEY"; else if (b->m->def->ins[cb.toPort].name != "IN") s += " " + b->m->def->ins[cb.toPort].name;
  return s;
}
static bool sameCable(const UiCable& x, const UiCable& k) { return x.from == k.from && x.fromPort == k.fromPort && x.to == k.to && x.toPort == k.toPort && x.toParam == k.toParam && x.type == k.type; }
void Ui::drawJack(Canvas& c, int jx, int jy, PType t, bool conn, bool lit) {
  const int col = lit ? 46 : typeCol(t);
  if (t == PType::Trig) { for (int k = -2; k <= 2; k++) { const int s = 2 - std::abs(k); c.px(jx + k, jy - s, col); c.px(jx + k, jy + s, col); if (conn) for (int q = -s; q <= s; q++) c.px(jx + k, jy + q, col); } }
  else if (t == PType::Mod) {   // round
    for (int k = -1; k <= 1; k++) { c.px(jx + k, jy - 2, col); c.px(jx + k, jy + 2, col); c.px(jx - 2, jy + k, col); c.px(jx + 2, jy + k, col); }
    if (conn) c.rect(jx - 1, jy - 1, 3, 3, col); }
  else { if (conn) c.rect(jx - 2, jy - 2, 5, 5, col); else c.frame(jx - 2, jy - 2, 5, 5, col); }   // audio: square
}


// each module's own little display
// a stereo level meter, the same everywhere: L and R bars (-48 dB to 0 dB), each with its loudest moment held for a second
// ---------------- SPECTRUM: where things sit, and the shading behind ----------------
static inline float specHz(float k) { return 10.f * std::pow(2400.f, k / 64.f); }   // the edge of band k: 10 Hz to 24 kHz (as the engine's)
static int specZone(float hz) { return hz < 20 || hz > 20000 ? 2 : hz < 30 || hz > 16000 ? 1 : 0; }   // 2: no one can hear it; 1: the edges of hearing
static const char* specBuild(float hz) { return hz >= 200 && hz <= 500 ? "MUD" : hz >= 2000 && hz <= 5000 ? "HARSHNESS" : hz > 5000 && hz <= 9000 ? "SIBILANCE" : nullptr; }
// a ringing peak: far above the bands a little either side, the top of its own hill, and among the loudest things in the sound
// (a note in a chord stands out a little; a whistling resonance stands out a lot, and dominates). Shown once it has lasted a moment
static bool specPeakNow(const std::vector<float>& v, int k) {
  if (k < 3 || k > 60 || v[k] < -60) return false; float a = 0, top = -200; int n = 0; for (int j : {k - 5, k - 4, k + 4, k + 5}) if (j >= 0 && j < 64) { a += v[j]; n++; } for (float x : v) top = std::max(top, x);
  return v[k] - a / std::max(1, n) > 24 && v[k] >= v[k - 1] && v[k] >= v[k + 1] && v[k] > top - 6; }
static bool specPeak(const UiMod& u, int k) { return u.specRing.size() == 64 && u.specRing[k] > 1.5f; }
static std::string hzText(float hz) { char b[16]; if (hz >= 1000) std::snprintf(b, 16, hz >= 10000 ? "%.0fKHZ" : "%.1fKHZ", hz / 1000); else std::snprintf(b, 16, "%.0fHZ", hz); return b; }
static void stereoMeter(Canvas& c, UiMod& u, int x, int y, int w, float linL, float linR, float dt) {
  for (int s = 0; s < 2; s++) {
    const float lin = s ? linR : linL, n = clampf((20 * std::log10(lin + 1e-6f) + 48) / 48, 0, 1); const int bx = x + 5, bw = w - 5, fw = (int)(n * bw), by = y + 2 + s * 7;
    float& hold = s ? u.holdR : u.holdL; float& ht = s ? u.holdRT : u.holdLT; if (n >= hold) { hold = n; ht = 1; } else if ((ht -= dt) <= 0) hold = std::max(n, hold - dt * 0.6f);
    c.text(s ? "R" : "L", x, by - 1, 18);
    for (int i = 0; i < bw; i += 2) c.px(bx + i, by + 3, 21);
    for (int i = 0; i < fw; i++) { const int col = i > bw * 0.92f ? 14 : i > bw * 0.7f ? 12 : 3; c.px(bx + i, by, col); c.px(bx + i, by + 1, col); }
    const int hx = bx + std::min(bw - 1, (int)(hold * bw)); if (hold > 0.02f) { c.px(hx, by, hold > 0.92f ? 14 : 9); c.px(hx, by + 1, hold > 0.92f ? 14 : 9); } }
}
void Ui::drawDisplay(Canvas& c, UiMod& u, int x, int y, int w, float dt) {
  Module* m = u.m; const std::string& ty = m->def->type; auto D = [&](int i) { return m->disp[i].load(std::memory_order_relaxed); };
  const int vh = geom(u).vh;
  if (ty == "clump") {   // the loop as a line; each step a bar, taller when stronger, lit when it fires
    const int base = y + vh - 3;
    for (int i = 0; i < w; i += 2) c.px(x + i, base + 1, 18);
    const int n = (int)D(1); for (int k = 0; k < n && k < 16; k++) { const int sx = x + (int)std::lround(D(2 + k) * (w - 3)), h = 2 + (int)std::lround(D(18 + k) * (vh - 6)); c.rect(sx, base + 1 - h, 3, h, D(34 + k) > 0 ? 4 : 24); }
    const int hx = x + (int)std::lround(D(0) * (w - 1)); for (int j = 0; j < vh - 1; j++) if (j % 2 == 0 || j > vh - 5) c.px(hx, y + j, 17);
  } else if (ty == "orbits") {   // points of light going round (the rings themselves aren't drawn)
    const float cx = x + w / 2.f, cy = y + vh / 2.f - 0.5f; const int n = std::max(1, (int)D(0));
    const float RX = w / 2.f - 2, RY = vh / 2.f - 2;
    for (int i = 0; i < n && i < 8; i++) { const float f = (i + 1.f) / n, rx = 3 + (RX - 3) * f, ry = 2 + (RY - 2) * f, a0 = D(1 + i);
      const bool lit = D(9 + i) > 0; const int px = (int)std::lround(cx + rx * std::cos(a0)), py = (int)std::lround(cy + ry * std::sin(a0));
      c.rect(px - 1, py - 1, lit ? 3 : 2, lit ? 3 : 2, lit ? 4 : std::fabs(D(17 + i)) > 0.5f ? 11 : 24); }   // green: MUTATE has moved its note
  } else if (ty == "audioin") {   // where it's coming from, and a meter for each side (red near the top)
    const int st = (int)D(2); std::string l = st == 0 ? "NO INPUT DEVICE" : st == 2 ? "NOT ON THIS DEVICE" : (inputName.empty() ? std::string("DEFAULT INPUT") : inputName);
    l = l.substr(0, (size_t)std::max(4, w / 4)); c.text(l, x + 1, y + 1, st == 1 ? 18 : 14);
    for (int k = 0; k < 2; k++) { const float pkv = D(k); const float db = pkv > 1e-5f ? 20 * std::log10(pkv) : -60; const int fw = (int)std::lround(clampf((db + 60) / 60, 0, 1) * (w - 2));
      c.rect(x + 1, y + 9 + k * 3, w - 2, 2, 21); for (int i = 0; i < fw; i++) c.px(x + 1 + i, y + 9 + k * 3, i > (w - 2) * 0.9f ? 14 : 11), c.px(x + 1 + i, y + 10 + k * 3, i > (w - 2) * 0.9f ? 14 : 11); }
  } else if (ty == "tombola") {   // the turning drum and its balls
    const float cx = x + w / 2.f, cy = y + vh / 2.f, R = std::min(w, vh) / 2.f - 1.5f, ang = D(0); const int S = std::max(3, (int)D(1)), n = (int)D(2);
    const int wm = (int)D(51);
    for (int s = 0; s < S; s++) { const float a0 = ang + s * TAU / S, a1 = ang + (s + 1) * TAU / S; const bool plays = wm == 0 || (wm == 1 && s % 2 == 0) || (wm == 2 && s == 0);   // walls that play are brighter
      c.line((int)std::lround(cx + R * std::cos(a0)), (int)std::lround(cy + R * std::sin(a0)), (int)std::lround(cx + R * std::cos(a1)), (int)std::lround(cy + R * std::sin(a1)), plays ? 9 : 21); }
    for (int i = 0; i < n && i < 12; i++) { const int bx = (int)std::lround(cx + D(3 + i * 4) * R), by = (int)std::lround(cy + D(4 + i * 4) * R); const float vel = D(5 + i * 4); const bool hitNow = D(6 + i * 4) > 0;
      const int col = hitNow ? 12 : vel > 0.75f ? 4 : vel > 0.5f ? 24 : vel > 0.25f ? 44 : 19; c.rect(bx - 1, by - 1, 3, 3, col); }   // brighter = louder (dissolving balls dim)
  } else if (ty == "hops") {   // each hop a stepping stone, placed by pitch, drifting left and fading
    const int fired = (int)D(0); if (fired != u.hopSeen) { u.hopSeen = fired; if (u.dots.size() > 40) u.dots.erase(u.dots.begin()); u.dots.push_back({0.f, D(1)}); }
    for (auto& dt2 : u.dots) dt2.first += dt; u.dots.erase(std::remove_if(u.dots.begin(), u.dots.end(), [](const std::pair<float, float>& q) { return q.first > 4; }), u.dots.end());
    for (int i = 0; i < w; i += 4) c.px(x + i, y + vh - 1, 21);
    for (auto& q : u.dots) { const int px = x + w - 3 - (int)(q.first * (w - 4) / 4), py = y + vh - 3 - (int)std::lround(q.second * (vh - 5)), col = q.first < 0.15f ? 4 : q.first < 1 ? 24 : q.first < 2.5f ? 44 : 18;
      c.rect(px, py, q.first < 0.15f ? 3 : 2, 2, col); }
  } else if (ty == "ducker") {   // how far the sound is being pushed down, over the last few seconds
    u.ht += dt; if (u.ht > 0.03f) { u.ht = 0; u.hist[u.hi] = D(0); u.hi = (u.hi + 1) % 48; }
    for (int k = 0; k < 48; k++) { const float v = clampf(u.hist[(u.hi + k) % 48], 0, 1); const int xx = x + (int)std::lround(k * (w - 2) / 47.f), hh = (int)std::lround(v * (vh - 2));
      c.px(xx, y + vh - 2, 21); c.px(xx + 1, y + vh - 2, 21); if (hh > 0) c.rect(xx, y, 2, hh, k == 47 ? 4 : 12); }
  } else if (ty == "clock") {
    for (int i = 0; i < 4; i++) c.rect(x + 4 + i * 12, y + 1, 8, 3, D(1) > 0.5f && i == (int)D(0) ? 4 : 21);
  } else if (ty == "lfo" || ty == "env" || ty == "rand" || ty == "follow") {   // the modulation's recent path
    float every = ty == "lfo" ? 0.05f : 0.03f; if (ty == "rand") every = std::min(0.08f, m->p[0].load() / 12);
    u.ht += dt; if (u.ht > every) { u.ht = 0; u.hist[u.hi] = D(0); u.hi = (u.hi + 1) % 48; }
    int py = -1; for (int k = 0; k < 48; k++) { const float v = u.hist[(u.hi + k) % 48]; const int yy = y + 3 - (int)std::lround(v * 3), xx = x + (int)std::lround(k * (w - 1) / 47.f); if (py >= 0) c.line(xx - 1, py, xx, yy, 11); py = yy; }
  } else if (ty == "bursts") {   // the burst's notes along a line, and where it has got to
    for (int i = 0; i < w; i += 2) c.px(x + i, y + 5, 18);
    const int n = (int)D(0); for (int i = 0; i < n && i < 9; i++) c.rect(x + (int)std::lround(D(1 + i) * (w - 2)), y + 1, 2, 5, 7);
    const float k = D(10); if (k >= 0 && k <= 1) for (int j = 0; j < 7; j++) c.px(x + (int)std::lround(k * (w - 2)), y + j, 24);
  } else if (ty == "walker") {
    const int range = std::max(1, (int)m->p[4].load());
    for (int i = 0; i < w; i++) c.px(x + i, y + 4, 21); for (int i = 0; i < range; i++) c.px(x + (int)std::lround((float)i / range * (w - 1)), y + 5, 18);
    c.rect(x + (int)std::lround(D(0) * (w - 3)), y + 2, 3, 2, 24);
  } else if (ty == "tools") {   // which of the last triggers got through
    const int n = (int)D(0); const unsigned h = (unsigned)D(1);
    for (int i = 0; i < 14; i++) c.rect(x + 1 + i * 4, y + 1, 3, 2, i < n ? (((h >> (n - 1 - i)) & 1u) ? 24 : 18) : 21);
  } else if (ty == "phaser") {
    for (int i = 0; i < w; i += 2) c.px(x + i, y + 2, 18);
    const float k = 0.5f + 0.5f * std::sin(D(0) * TAU) * m->p[1].load(); c.rect(x + (int)std::lround(k * (w - 3)), y, 3, 5, 17);
    if (m->p[5].load() > 0.5f) c.rect(x + (int)std::lround((1 - k) * (w - 3)), y + 1, 3, 3, 3);
  } else if (ty == "sample" || ty == "looper" || ty == "stretch") {   // the sound's outline; the region or slices (Sample); playheads and grains
    const bool smp = ty == "sample"; const int kind = smp ? (int)m->p[1].load() : 0, multi = kind >= 2; int top = y;
    if (multi) {   // KIT / MULTI: a numbered place for each sound; tap one to load, replace, set its ROOT or remove it
      for (int k = 0; k < UiMod::SLOTS; k++) { const int bx = x + k * 6; const bool has = !u.snd[k].id.empty(), sel = k == u.shown;
        if (sel) c.rect(bx, y, 5, 5, has ? 24 : 18); else c.frame(bx, y, 5, 5, has ? 24 : 21); c.text(std::to_string(k + 1), bx + 1, y, sel ? 15 : has ? 9 : 18); }
      top = y + 6; }
    const UiMod::Snd& sd = u.snd[multi ? u.shown : 0]; const int mid = multi ? top + 3 : y + 6, amp = multi ? 2 : 5;
    auto tag = [&](const std::string& l, int col) { c.rect(x, y, Canvas::tw(l) + 2, 7, 25); c.text(l, x + 1, y, col); };   // a little label in the corner
    if (ty == "stretch") {   // the stretched sound's spectrum (low on the left), gliding from one window to the next; green and still when frozen
      const bool fz = D(40) > 0.5f; if ((int)u.spec.size() != 32) u.spec.assign(32, 0.f);
      for (int k = 0; k < 32; k++) u.spec[k] += (D(64 + k) - u.spec[k]) * std::min(1.f, dt * (fz ? 20.f : 3.f));
      for (int i = 0; i < w; i++) { const float f = (float)i / std::max(1, w - 1) * 31; const int k0 = (int)f; const float v = u.spec[k0] + (u.spec[std::min(31, k0 + 1)] - u.spec[k0]) * (f - k0);
        const int h = std::max(0, (int)std::lround(v * 11)); for (int j = 0; j < h; j++) c.px(x + i, y + 11 - j, fz ? 11 : j > 7 ? 4 : 48); c.px(x + i, y + 11, 21); }
      tag(m->p[0].load() < 0.5f ? (fz ? "FROZEN" : "LIVE") : (sd.peaks.empty() ? "TAP LOAD FILE" : fz ? "FROZEN" : "FILE"), fz ? 11 : 18);
      if (m->p[0].load() > 0.5f && D(0) >= 0) { const int hx = x + (int)std::lround(clampf(D(0), 0, 1) * (w - 1)); c.rect(hx - 1, y + 11, 3, 1, 12); } }   // FILE: where it is
    else if (ty == "looper" && m->p[0].load() < 0.5f) {   // LIVE: the window it listens to scrolling by (new on the right), the grains on it; green and still when frozen
      const bool fz = D(40) > 1.5f;
      for (int i = 0; i < w; i++) { const int k = std::min(63, i * 64 / std::max(1, w)); const int h = (int)std::lround(std::min(1.f, D(64 + k) * 1.4f) * 5);
        for (int j = -h; j <= h; j++) c.px(x + i, y + 6 + j, fz ? 11 : 48); if (!h) c.px(x + i, y + 6, 21); }
      char b[32]; std::snprintf(b, 32, fz ? "FROZEN" : "LIVE %dS", (int)m->p[3].load()); tag(b, fz ? 11 : 18); }
    else if (sd.peaks.empty()) { c.text(sd.missing ? "LOAD IT AGAIN" : multi ? (u.snd[u.shown].id.empty() ? "TAP A NUMBER" : "") : smp ? "LOAD OR RECORD" : ty == "stretch" ? "TAP LOAD FILE" : "LISTENING", x + (multi ? 2 : 2), multi ? top : y + 4, sd.missing ? 14 : 18); }
    else {
      const float a0 = smp && kind != 1 ? m->p[5].load() : 0, b0 = smp && kind != 1 ? a0 + m->p[6].load() * (1 - a0) : 1;
      for (int i = 0; i < w; i++) { const float fx = (float)i / w; const int h = (int)std::lround(sd.peaks[std::min(sd.peaks.size() - 1, (size_t)(fx * sd.peaks.size()))] * amp), col = fx >= a0 && fx <= b0 ? 48 : 21;
        for (int j = -h; j <= h; j++) c.px(x + i, mid + j, col); }
      if (smp && kind == 1) { const int N = std::max(1, (int)m->p[2].load()); for (int k = 1; k < N; k++) { const int sx = x + (int)std::lround((float)k / N * (w - 1)); for (int j = 0; j < 12; j += 2) c.px(sx, y + j, 24); } }   // where the slices are cut
      else if (smp) for (float xx : {a0, b0}) for (int j = top - y; j < 12; j += 2) c.px(x + (int)std::lround(xx * (w - 1)), y + j, 24);
      if (smp) { const int n = (int)D(0); for (int k = 0; k < n && k < 12; k++) { if (multi && (int)D(20 + k) != u.shown) continue; const int px = x + (int)std::lround(clampf(D(1 + k), 0, 1) * (w - 1)); for (int j = top - y; j < 12; j++) c.px(px, y + j, 12); } }
    }
    const int monK = smp ? m->def->paramIndex("mon") : -1; const int monV = monK >= 0 ? (int)std::lround(m->p[monK].load()) : 0;
    const bool fbShow = smp && D(45) > 0.5f && (monV == 2 || (monV == 1 && D(40) >= 1.5f));   // the loop warning only when it would have been monitoring
    if (smp && fbShow) {   // SAMPLER recording: what it's doing, in red, over the sound
      const int st = (int)std::lround(D(40)); char b[40]; const bool od = m->p[m->def->paramIndex("recmode")].load() > 0.5f && !u.snd[0].peaks.empty();   // OVERDUB needs a sound to lay over
      if (st == 1) std::snprintf(b, 40, "WAITING FOR THE BAR"); else if (st == 3) std::snprintf(b, 40, "FINISHING");
      else if (st == 2) { if (D(44) > 0) std::snprintf(b, 40, "%s %.1F/%.0FS", od ? "OVERDUB" : "REC", D(41), D(44)); else std::snprintf(b, 40, "%s %.1FS", od ? "OVERDUB" : "REC", D(41)); }
      else std::snprintf(b, 40, "LOOP: NO MONITOR");
      std::string l = b; l = l.substr(0, (size_t)std::max(4, (w - 2) / 4)); if (sd.peaks.empty()) c.rect(x, y, w, 12, 25); else c.rect(x, y, Canvas::tw(l) + 2, 7, 25); c.text(l, x + 1, y + (sd.peaks.empty() ? 3 : 1), st ? 14 : 18); }
    if (!smp && D(0) >= 0) { const int hx = x + (int)std::lround(clampf(D(0), 0, 1) * (w - 1)); for (int j = ty == "looper" && m->p[0].load() < 0.5f ? 8 : 0; j < 12; j++) c.px(hx, y + j, 12);   // LIVE: below its words
      const int n = (int)D(1); for (int k = 0; k < n && k < 30; k++) c.px(x + (int)std::lround(clampf(D(2 + k), 0, 1) * (w - 1)), y + 6 + (int)(std::rand() % 9) - 4, 4); }
  } else if (ty == "comp" || ty == "limit" || ty == "master") {   // how loud it is (top) and how much it's turning down (red, from the right)
    stereoMeter(c, u, x, y, w, std::pow(10.f, D(1) / 20), std::pow(10.f, D(3) / 20), dt);   // L and R: how loud
    const int bx = x + 10, bw = w - 10;
    const float gr = clampf(-D(0) / 18, 0, 1); const int gy = y + 16;   // GR: how much it's turning down (red, from the right)
    c.text("GR", x, gy - 1, 18); c.rect(bx, gy, bw, 2, 21); if (gr > 0.003f) c.rect(bx + bw - (int)std::lround(gr * bw), gy, (int)std::lround(gr * bw), 2, 14);
    if (ty == "master") { const float gg = clampf(-D(2) / 12, 0, 1); if (gg > 0.003f) c.rect(bx + bw - (int)std::lround(gg * bw), gy + 1, (int)std::lround(gg * bw), 1, 12); }
  } else if (ty == "qwerty") {   // the 36 notes, an octave a row (C-0 at the bottom), each with its key. Tap one, then press a key to put it there
    const int cw = (w - 6) / 12; static const char* NN = "C#D#EF#G#A#B"; for (int k = 0; k < 12; k++) { const std::string l(1, NN[k]); c.text(l, x + 6 + k * cw + (cw - 1 - Canvas::tw(l)) / 2, y, NN[k] == '#' ? 21 : 18); }   // C # D # E F # G # A # B
    for (int r = 0; r < 3; r++) { const int ry = y + 7 + (2 - r) * 8; c.text(std::to_string(r), x, ry + 1, 18);
      for (int k = 0; k < 12; k++) { const int i = r * 12 + k, cx = x + 6 + k * cw; u.lit[i] = std::max(0.f, u.lit[i] - dt);
        const bool learn = qwLearn == i && std::fmod(blink, 0.5f) < 0.3f, lit = u.lit[i] > 0, sharp = NN[k] == '#';
        if (lit || learn) c.rect(cx, ry, cw - 1, 7, learn ? 12 : 4); else c.rect(cx, ry, cw - 1, 7, sharp ? 26 : 21);
        std::string s = qwKeys[i]; if (s.size() > 2) s = s.substr(0, 2); if (qwLearn == i) s = "?";
        if (!s.empty()) c.text(s, cx + (cw - 1 - Canvas::tw(s)) / 2, ry + 1, lit || learn ? 15 : 9); } }
  } else if (ty == "euclid") {   // EUCLID DRUMS: each lane's rhythm as a ring (its hits filled in, where it's got to lit), the chosen lane brighter
    static const char* NM[4] = {"KICK", "SNARE", "HAT", "PERC"}; const int lane = (int)std::lround(m->p[m->def->paramIndex("lane")].load());
    const int r = std::max(6, std::min(14, w / 8 - 2));
    for (int l = 0; l < 4; l++) {
      const int cx = x + w * (2 * l + 1) / 8, cy = y + r + 1; const int st = std::max(1, std::min(32, (int)std::lround(m->p[m->def->paramIndex("st" + std::to_string(l + 1))].load())));
      const int pk = (int)D(4 + l), hits = pk / 64, rot = pk % 64 - 32, at = (int)D(l); const bool sel = l == lane;
      for (int k = 0; k < st; k++) { const float an = -PI / 2 + TAU * k / st; const int px = cx + (int)std::lround(std::cos(an) * r), py = cy + (int)std::lround(std::sin(an) * r);
        const bool hit = euclidHit(k, st, hits, rot), now = k == at;
        if (hit) c.rect(px - 1, py - 1, 2, 2, now ? 4 : sel ? 9 : 18); else c.px(px, py, now ? 4 : sel ? 18 : 21); }
      const std::string t = NM[l]; c.text(t, cx - Canvas::tw(t) / 2, y + 2 * r + 4, D(9 + l) > 0.5f ? 4 : sel ? 9 : 21); }
  } else if (ty == "nloop") {   // everything recorded (time along, pitch up), the part that plays round (START to its LENGTH) lit, the playhead
    const int H = vh, n = m->evN.load(); const int st = (int)D(1); const float S = D(6), L = std::max(0.01f, D(2));
    float lo = 200, hi = -1, tmax = 0; for (int i = 0; i < n; i++) { const float nt = m->ev[i * 4 + 1].load(); lo = std::min(lo, nt); hi = std::max(hi, nt); tmax = std::max(tmax, m->ev[i * 4].load()); }
    if (hi < lo) { lo = 48; hi = 72; } if (hi - lo < 12) { const float mid = (hi + lo) / 2; lo = mid - 6; hi = mid + 6; }
    const float span = st == 1 ? std::max(1.f, D(3)) : std::max(S + L, tmax + 0.25f); const int top = (int)D(5);
    auto X = [&](float tt) { return x + (int)std::lround(tt / span * (w - 2)); };
    c.rect(x, y, w, H, 26); if (st != 1) c.rect(X(S), y, std::max(1, X(S + L) - X(S)), H, 25);   // the part that plays
    for (float bt = 0; bt <= span && span / std::max(1.f, 1.f) < 600; bt += span > 32 ? 4 : 1) { const int gx = X(bt); for (int j = 0; j < H; j += (std::fmod(bt, 4.f) < 0.01f ? 1 : 3)) c.px(gx, y + j, 21); }   // beats (bars brighter)
    for (int i = 0; i < n; i++) { const float tt = m->ev[i * 4].load(); const float nt = m->ev[i * 4 + 1].load(), vel = m->ev[i * 4 + 2].load(); const int ly = (int)m->ev[i * 4 + 3].load();
      const bool in = st == 1 || (tt >= S && tt < S + L); const int px0 = X(tt), py0 = y + H - 2 - (int)std::lround((nt - lo) / (hi - lo) * (H - 3));
      c.rect(px0, py0, 2, 2, !in ? 21 : ly == top ? (vel > 0.6f ? 4 : 9) : ly == top - 1 ? 24 : 44); }
    auto label = [&](const std::string& t, int col) { c.rect(x + 1, y + 1, Canvas::tw(t) + 3, 7, 26); c.text(t, x + 2, y + 2, col); };   // on its own patch, clear of the grid
    if (st == 1) { char b[32]; std::snprintf(b, 32, "RECORDING %.1f BEATS", D(3)); label(b, 14); }
    else if (n == 0) label("RECORD SOME NOTES", 18);
    else { const int hx = X(D(0)); for (int j = 0; j < H; j++) c.px(hx, y + j, st == 2 ? 14 : 9);
      for (int e2 = 0; e2 < 2; e2++) { const int lx = std::min(x + w - 2, X(e2 ? S + L : S)), col = drag.kind == D_NLOOP && drag.m == &u && drag.dir == e2 ? 4 : 12;   // the start and end lines: drag them
        for (int j = 0; j < H; j += 2) c.px(lx, y + j, col); c.rect(lx - 1, y, 3, 3, col); c.rect(lx - 1, y + H - 3, 3, 3, col); } }
  } else if (ty == "listen") {   // what key and scale it hears, how sure it is, the runner-up, and the 12 notes it's hearing
    const int r = (int)D(0), sc = std::max(0, std::min((int)scales().size() - 1, (int)D(1))), r2 = (int)D(3), sc2 = std::max(0, std::min((int)scales().size() - 1, (int)D(4)));
    if (D(2) <= 0 && D(18) < 1e-6f) c.text("LISTENING...", x + 1, y + 1, 18);
    else { const std::string b1 = std::string(KEY_NAMES[r % 12]) + " " + scales()[sc].name, p1 = std::to_string((int)std::lround(D(2) * 100)) + "%";
      c.text(b1.substr(0, 12), x + 1, y + 1, 4); c.text(p1, x + w - 1 - Canvas::tw(p1), y + 1, 9);
      const std::string b2 = std::string("OR ") + KEY_NAMES[r2 % 12] + " " + scales()[sc2].name; c.text(b2.substr(0, 15), x + 1, y + 8, 18); }
    bool inSc[12] = {false}; for (int st : scales()[sc].steps) inSc[(r + st) % 12] = true;
    const int bw = std::max(2, (w - 2) / 12);
    for (int i = 0; i < 12; i++) { const int h = (int)std::lround(clampf(D(6 + i), 0, 1) * 9), bx = x + 1 + i * bw; c.px(bx, y + 25, 21);
      if (h > 0) c.rect(bx, y + 25 - h, bw - 1, h, inSc[i] ? (i == r ? 4 : 11) : 21); }
    { const float bp = D(19); const std::string t = bp > 0 ? "TEMPO " + std::to_string((int)std::lround(bp)) + " BPM" : "NO CLEAR TEMPO", pc = bp > 0 ? std::to_string((int)std::lround(D(20) * 100)) + "%" : "";   // the tempo it hears
      c.text(t, x + 1, y + 28, bp > 0 ? 4 : 18); if (!pc.empty()) c.text(pc, x + w - 1 - Canvas::tw(pc), y + 28, 9); }
  } else if (ty == "journey") {   // the steps (tap one to choose its key; below it, its own scale), coloured by how smooth the change into each is
    const int N = std::max(1, std::min(8, (int)std::lround(m->p[0].load()))), sw = std::min(16, (w + 1) / N), cur = (int)D(8), nxt = (int)D(9), hsc = (int)D(10), nsc = (int)scales().size();
    auto keyAt = [&](int i) { return (int)std::lround(m->seq[Module::si(i, 0)].load()); };
    auto scAt = [&](int i) { const float v = m->seq[Module::si(i, 0) + 1].load(); return std::max(0, std::min(nsc - 1, v >= 10 ? (int)std::lround(v) - 10 : hsc)); };
    auto mask = [&](int r, int sc) { int mm = 0; for (int st : scales()[sc].steps) mm |= 1 << ((r + st) % 12); return mm; };
    for (int i = 0; i < N; i++) { const int bx = x + i * sw, k = keyAt(i); const int bw = sw - 1;
      int col = 21; if (k >= 0) { int pj = -1; for (int j = 1; j <= N; j++) { const int q = ((i - j) % N + N) % N; if (keyAt(q) >= 0) { pj = q; break; } }   // the step before (round the end)
        if (pj >= 0) { int dd = 0; for (int xm = mask(keyAt(pj), scAt(pj)) ^ mask(k, scAt(i)); xm; xm &= xm - 1) dd++; dd /= 2; col = dd <= 1 ? 11 : dd == 2 ? 4 : 14; } else col = 11; }
      if (i == cur && k >= 0) c.rect(bx, y, bw, 9, 9); c.frame(bx, y, bw, 9, col);
      const std::string kt = k >= 0 ? std::string(KEY_NAMES[k % 12]) : std::string("-"); c.text(kt, bx + (bw - Canvas::tw(kt) + 1) / 2, y + 1, i == cur && k >= 0 ? 27 : k >= 0 ? 9 : 21);   /* the step's key in the module's own text colour (it was black, lost on dark themes) */
      if (i == nxt && i != cur) c.rect(bx + 1, y + 7, bw - 2, 1, 4);   // the next step: a line along its foot
      const float sv = m->seq[Module::si(i, 0) + 1].load(); if (k >= 0 && sv >= 10) { std::string ab = scales()[std::max(0, std::min(nsc - 1, (int)std::lround(sv) - 10))].name; ab = ab.substr(0, 2); c.text(ab, bx + (bw - Canvas::tw(ab) + 1) / 2, y + 10, 18); } }
    auto kn = [&](float r, float sc) { return std::string(KEY_NAMES[((int)r % 12 + 12) % 12]) + " " + scales()[std::max(0, std::min(nsc - 1, (int)sc))].name; };
    const int room = std::max(4, w / 4); c.text(kn(D(0), D(1)).substr(0, room), x + 1, y + 18, D(6) > 0.5f ? 11 : 4);
    const int bl = (int)D(4); std::string when = u.m->mute.load() ? "STOPPED" : D(2) < 0 ? "NO STEPS" : D(5) > 0.5f ? "NEXT ON THE BAR" : bl < 0 ? "WAITING FOR NEXT" : bl == 1 ? "NEXT IN 1 BAR" : "NEXT IN " + std::to_string(bl) + " BARS";
    if (D(6) > 0.5f && !u.m->mute.load()) when += " (SOFT)"; c.text(when.substr(0, room), x + 1, y + 25, 21);
    if (D(7) >= 0) { const int bw = (int)std::lround(clampf(D(7), 0, 1) * (w - 2)); c.rect(x + 1, y + 32, w - 2, 1, 21); c.rect(x + 1, y + 32, bw, 1, 9); }
  } else if (ty == "tracker") {
    drawTracker(c, u, x, y, w, dt);
  } else if (ty == "keys") {
    const int root = keyOf(u, eng), sc = scaleOf(u, eng), n = (int)scales()[sc].steps.size();
    const int oct = (int)m->p[1].load(), rowStep = 8;   // each row carries on up the scale from the row below
    int kcw, kch; keysCell(u, kcw, kch);
    for (int i = 0; i < 32; i++) { const int deg = (i / 8) * rowStep + (i % 8), note = (int)midiOf(degOf(oct, deg, sc), root, sc), cx = (i % 8) * kcw, cy = (3 - i / 8) * kch;
      u.lit[i] = std::max(0.f, u.lit[i] - dt);
      const bool lit = u.lit[i] > 0, isRoot = deg % n == 0;
      c.rect(x + cx, y + cy, kcw - 1, kch - 1, lit ? 4 : isRoot ? 24 : 21); const std::string t = noteName(note); c.text(t, x + cx + kcw / 2 - Canvas::tw(t) / 2, y + cy + (kch - 5) / 2, lit || isRoot ? 15 : 9); }
  } else if (ty == "mixer") {
    for (int k = 0; k < 4; k++) { const int cx = x + k * 13; const float v = clampf((20 * std::log10(D(k) + 1e-6f) + 48) / 48, 0, 1);
      const float vr = clampf((20 * std::log10(D(4 + k) + 1e-6f) + 48) / 48, 0, 1);   // each channel: L and R
      c.text(std::to_string(k + 1), cx, y + 1, 18); for (int j = 0; j < 7; j++) for (int sd = 0; sd < 2; sd++) { const bool on = j < (int)std::lround((sd ? vr : v) * 7); c.rect(cx + 5 + sd * 3, y + 6 - j, 2, 1, on ? (j > 5 ? 14 : j > 3 ? 12 : 48) : 21); } }
  } else if (ty == "spectrum") {   // 64 bars, low on the left; behind them the shading (GUIDE); a mark over narrow peaks; the frequencies along the bottom
    if (u.spec.size() != 64) { u.spec.assign(64, -120.f); u.specPk.assign(64, -120.f); u.specPkT.assign(64, 0.f); u.specRing.assign(64, 0.f); }
    for (int k = 0; k < 64; k++) { const float v = D(k); u.spec[k] = v > u.spec[k] ? v : std::max(v, u.spec[k] - dt * 30);   // up at once, down 30 dB a second
      if (u.spec[k] >= u.specPk[k]) { u.specPk[k] = u.spec[k]; u.specPkT[k] = 1.5f; } else if ((u.specPkT[k] -= dt) <= 0) u.specPk[k] = std::max(u.spec[k], u.specPk[k] - dt * 15); }
    for (int k = 0; k < 64; k++) u.specRing[k] = specPeakNow(u.spec, k) ? std::min(5.f, u.specRing[k] + dt) : std::max(0.f, u.specRing[k] - dt * 2);   // how long it has been ringing
    const int guide = (int)std::lround(m->p[0].load()), H = 36, top = y + 2, base = top + H; const float sr = std::max(8000.f, D(64));
    auto hAt = [&](float db) { return (int)std::lround(clampf((db + 72) / 72, 0, 1) * H); };
    for (int i = 0; i < w; i++) { const float k = (float)i / w * 64; const int kb = std::min(63, (int)k); const float hz = specHz(k + 0.5f / w * 64); const int z = guide ? specZone(hz) : 0;
      for (int j = 0; j < H; j++) { const int py = base - 1 - j; if ((z == 2 && ((i + j) & 1) == 0) || (z == 1 && (i & 1) == 0 && (j & 1) == 0) || (z == 0 && j % 12 == 11 && (i & 1) == 0)) c.px(x + i, py, 21); }   // shading; and faint lines every 24 dB
      if (hz > sr / 2) continue;
      const int h = hAt(u.spec[kb]), col = guide && specZone(hz) == 2 && u.spec[kb] > -60 ? 14 : guide && specZone(hz) == 1 ? 12 : 3;
      for (int j = 0; j < h; j++) c.px(x + i, base - 1 - j, col);
      const int ph = hAt(u.specPk[kb]); if (ph > 0 && ph > h) c.px(x + i, base - ph, 9);
      if (specPeak(u, kb) && (int)((float)(i + 1) / w * 64) != kb) { const int ty2 = base - h - 3; c.px(x + i, ty2, 14); c.px(x + i - 1, ty2 - 1, 14); c.px(x + i + 1, ty2 - 1, 14); } }   // over the middle of a peak's bar
    if (guide == 2) for (auto zr : {std::pair<float, float>{200, 500}, {2000, 5000}, {5000, 9000}}) {   // ALL: where sound often piles up, marked along the top
      const int a0 = x + (int)std::lround(std::log(zr.first / 10) / std::log(2400.f) * w), a1 = x + (int)std::lround(std::log(zr.second / 10) / std::log(2400.f) * w);
      for (int i = a0; i < a1; i++) if ((i & 1) == 0) c.px(i, top - 1, 18); c.px(a0, top, 18); c.px(a1 - 1, top, 18); }
    for (auto l : {std::pair<float, const char*>{20, "20"}, {100, "100"}, {1000, "1K"}, {10000, "10K"}}) { const int lx2 = x + (int)std::lround(std::log(l.first / 10) / std::log(2400.f) * w);
      c.px(lx2, base, 18); c.text(l.second, std::max(x, std::min(x + w - Canvas::tw(l.second), lx2 - Canvas::tw(l.second) / 2)), base + 2, 18); }
  } else if (ty == "amp" || ty == "level") {
    stereoMeter(c, u, x, y, w, D(0), D(1), dt);
  } else if (ty == "xfade") {   // the fader: where it is now (the handle) and where FADE sits (a mark under the track); drag it to set FADE
    const int sy = y + 5, tx0 = x + 6, tw0 = w - 12; const float now = clampf(D(0), 0, 1), fd = clampf(m->p[0].load(), 0, 1); const int hx = tx0 + (int)std::lround(now * tw0);
    c.text("A", x, sy - 2, now < 0.5f ? 9 : 18); c.text("B", x + w - 3, sy - 2, now > 0.5f ? 9 : 18);
    for (int i = 0; i <= tw0; i++) { c.px(tx0 + i, sy, 21); c.px(tx0 + i, sy + 1, 21); }
    const int fx = tx0 + (int)std::lround(fd * tw0); c.px(fx, sy + 5, 9); c.px(fx - 1, sy + 6, 9); c.px(fx, sy + 6, 9); c.px(fx + 1, sy + 6, 9);
    c.rect(hx - 2, sy - 3, 5, 8, 4); c.px(hx, sy - 2, 15); c.px(hx, sy + 3, 15);
  } else if (ty == "out") {
    stereoMeter(c, u, x, y, w, D(0), D(3), dt);
    if (m->disp[1].load() > 0.5f) { const int sec = (int)m->disp[2].load(); char b[24]; std::snprintf(b, 24, "REC %d:%02d", sec / 60, sec % 60);
      if (((int)(blink * 2)) % 2) c.rect(x + 2, y + 16, 3, 3, 14); c.text(b, x + 8, y + 15, 14); }
    else c.text("NOT RECORDING", x + 2, y + 15, 18);
  }
}

// ---------------- the Tracker's list of steps ----------------
static void trClamp(UiMod& u) { u.scroll = clampf(std::round(u.scroll), 0, (float)std::max(0, trShown(u.m) - trRows(u.m))); }
static void trShow(UiMod& u, int row) { if (row < u.scroll) u.scroll = (float)row; else if (row >= u.scroll + trRows(u.m)) u.scroll = (float)(row - trRows(u.m) + 1); trClamp(u); }
void Ui::drawTracker(Canvas& c, UiMod& u, int x, int y0, int w, float dt) {
  Module* m = u.m; const int TR_ROWS = trRows(m), len = trLen(m), shown = trShown(m), pos = (int)m->disp[0].load(), div = std::max(1, (int)m->disp[1].load()), cur = m->cursor.load();
  const int nc = trCols(m), curC = std::min(nc - 1, m->cursorCol.load()), head = trHead(m), y = y0 + head;
  u.colScroll = std::max(0, std::min(std::max(0, nc - TR_VIS), u.colScroll)); const int vis = std::min(TR_VIS, nc), cw = (w - 16) / vis;
  u.hold = std::max(0.f, u.hold - dt);
  if (u.hold <= 0 && pos >= 0 && (pos < u.scroll || pos >= u.scroll + TR_ROWS)) u.scroll = (float)pos;   // follows the playhead, except for a few seconds after you touch it
  trClamp(u); const int sc = (int)u.scroll;
  if (head) {   // which columns are on show, and < > to move along
    if (u.colScroll > 0) c.text("<", x + 1, y0, 9);
    if (u.colScroll + TR_VIS < nc) c.text(">", x + w - 4, y0, 9);
    for (int k = 0; k < vis; k++) { const int col = u.colScroll + k; const std::string t = std::to_string(col + 1); c.text(t, x + 16 + k * cw, y0, col == curC ? 4 : 18);
      const int wi = m->def->paramIndex("w" + std::to_string(col + 1)); const int wn = wi >= 0 ? (int)std::lround(m->p[wi].load()) : 0;
      if (wn > 0) { const std::string wt = std::string("\x03") + std::to_string(wn); c.text(wt, x + 16 + k * cw + Canvas::tw(t) + 2, y0, 9); } } }   // RANDOM NOTE on: how far it can wander
  auto num = [](int r) { char b[16]; std::snprintf(b, 16, "%03d", r + 1); return std::string(b); };
  for (int i = 0; i < TR_ROWS; i++) { const int r = sc + i, ry = y + 1 + i * 6; if (r >= shown) break;
    if (r >= len) { if (r == len) { for (int k = 0; k < w; k += 2) c.px(x + k, ry - 1, 18); c.text("OFF", x + w - 13, ry, 18); }
      c.text(num(r), x + 1, ry, 21);
      for (int k = 0; k < vis; k++) { const float nt = m->seq[Module::si(r, u.colScroll + k)].load(); c.text(nt >= 0 ? noteName((int)nt) : "---", x + 16 + k * cw, ry, 21); } continue; }
    const bool play = r == pos, isCur = r == cur, beat = r % div == 0;
    if (play) c.rect(x, ry - 1, w, 7, 17);
    c.text(num(r), x + 1, ry, play ? 15 : beat ? 9 : 18);
    for (int k = 0; k < vis; k++) { const int col = u.colScroll + k, cx = x + 16 + k * cw; const float nt = m->seq[Module::si(r, col)].load(); const bool has = nt >= 0;
      const bool here = isCur && col == curC && !play; if (here) c.rect(cx - 1, ry - 1, cw, 7, 21);   // the cursor: this step, this column
      c.text(has ? noteName((int)nt) : "---", cx, ry, play ? 15 : has || here ? 9 : 18);
    }
    if (isCur) { c.px(x - 2, ry + 1, 4); c.px(x - 2, ry + 2, 4); c.px(x - 2, ry + 3, 4); c.px(x - 1, ry + 2, 4); }
  }
  if (shown > TR_ROWS) {   // where the view is, along the right edge
    const int H = TR_ROWS * 6, h = std::max(3, (int)std::lround((float)TR_ROWS / shown * H)), sy = (int)std::lround(u.scroll / (shown - TR_ROWS) * (H - h));
    for (int j = 0; j < H; j++) c.px(x + w + 1, y + j - 1, j >= sy && j < sy + h ? 9 : 21);
    if (pos >= 0) { const int py = (int)std::lround((float)pos / std::max(1, shown - 1) * (H - 2)); c.px(x + w + 1, y + py - 1, 17); c.px(x + w + 1, y + py, 17); c.px(x + w, y + py, 17); }
  }
}
// the keyboard on the Tracker last touched: arrows move the cursor between steps and columns; Delete empties the step
bool Ui::trackerKey(int key) {
  UiMod* u = byId(trFocus); if (!u || u->m->def->type != "tracker") return false; Module* m = u->m;
  const int nc = trCols(m), len = trLen(m); int r = m->cursor.load(), c = std::min(nc - 1, m->cursorCol.load());
  if (key == 1) r = (r + len - 1) % len; else if (key == 2) r = (r + 1) % len; else if (key == 3) c = std::max(0, c - 1); else if (key == 4) c = std::min(nc - 1, c + 1);
  else if (key == 5) { eng.action(m->id, "del"); status("CLEARED THE STEP", 1); u->hold = 6; return true; } else return false;
  m->cursor.store(r); m->cursorCol.store(c); u->hold = 6; trShow(*u, r);
  if (c < u->colScroll) u->colScroll = c; else if (c >= u->colScroll + TR_VIS) u->colScroll = c - TR_VIS + 1;
  char b[40]; std::snprintf(b, 40, "STEP %03d  COLUMN %d", r + 1, c + 1); status(b, 1); return true;
}
void Ui::trackerWatch(UiMod& u) {   // notes typed in through ENTER arrive from the audio side: show and save them
  const int v = u.m->seqVer.load(); if (v == u.seqVer) return; u.seqVer = v;
  const int en = (int)u.m->disp[4].load();
  if (en != u.entered) { u.entered = en; char b[32]; std::snprintf(b, 32, "STEP %03d  ", (int)u.m->disp[2].load() + 1); status(b + noteName((int)u.m->disp[3].load()), 1.2f); }
  u.hold = 6; trShow(u, u.m->cursor.load()); changed();
}

// a setting's name for the status line. MI VOICES: Plaits' names, plus what HARMONICS / TIMBRE / MORPH do in this model
// a time as the screen shows it: 0:02.40, or 12.4S
static std::string timeText(double t) { char b[24]; if (t >= 60) std::snprintf(b, 24, "%d:%05.2f", (int)(t / 60), t - 60 * (int)(t / 60)); else std::snprintf(b, 24, t < 10 ? "%.2fS" : "%.1fS", t); return b; }
// a setting's value for the status line. SAMPLE's START and LENGTH: as times in the loaded sound
static std::string valueText(const UiMod& u, int j, float v, int root) {
  const ParamDef& q = u.m->def->params[j];
  if (u.m->def->type == "sample" && (q.key == "start" || q.key == "len")) { const double dur = u.snd[std::max(0, std::min(UiMod::SLOTS - 1, u.shown))].secs;
    if (dur > 0) { const bool slices = u.m->p[1].load() > 0.5f && u.m->p[1].load() < 1.5f; const float st = u.m->p[u.m->def->paramIndex("start")].load();
      if (q.key == "start") return timeText(v * dur);
      if (slices) { const int n = std::max(1, (int)u.m->p[2].load()); return timeText(dur / n * v); }
      return timeText(std::max(0.05, v * (dur - st * dur))); } }
  if (j == u.m->def->keyIdx && u.m->keyIn.load() >= 0) return std::string(KEY_NAMES[u.m->keyIn.load() % 12]) + " (JOURNEY)";   // a patched KEY socket: the key it's being sent
  if (j == u.m->def->scaleIdx && u.m->scaleIn.load() >= 0 && u.m->scaleIn.load() < (int)scales().size()) return std::string(scales()[u.m->scaleIn.load()].name);
  if (u.m->def->type == "nloop" && (q.key == "lenb" || q.key == "start")) { const float bs = u.m->disp[7].load(); if (bs > 0) return fmtParam(q, v, root) + " (" + timeText(v * bs) + ")"; }   // beats, and how long that is now
  return fmtParam(q, v, root);
}
static std::string statusLabel(const UiMod& u, const ParamDef& q) {
  if (u.m->def->type != "mi") return q.label;
  const int w3 = q.key == "harm" ? 0 : q.key == "timb" ? 1 : q.key == "morph" ? 2 : -1; if (w3 < 0) return q.label;
  const MiEngine& e = miEngines()[std::max(0, std::min(MI_ENGINES - 1, (int)u.m->p[0].load()))];
  return e.k[w3];
}
int Ui::minusX(const UiMod& u, int j) const {   // after the name: normally at 39, further for a long name or an inside socket
  const ParamDef& q = u.m->def->params[j]; std::string lab = q.label;
  const int ins = jacksIn && (modulatable(q) || q.noMod) ? 7 : 0;   // a clear gap after the socket (kept on a row without one, so its slider lines up), so the name doesn't look like it starts with O
  (void)lab; return u.m->def->labelEnd + ins;   // the same for every row of a module, so all its sliders are the same length
}
void Ui::drawModule(Canvas& c, UiMod& u, float dt) {
  if (u.m->seq) trackerWatch(u);
  const ModuleDef* d = u.m->def; const Geom g = geom(u); const int x = u.x - viewX, y = u.y - viewY, w = g.w;
  if (x > c.W || y > c.H || x + w < 0 || y + g.h < 0) return;
  static uint8_t inkTbl[128]; static bool init = false; if (!init) { for (int i = 0; i < 128; i++) inkTbl[i] = (uint8_t)i; inkTbl[9] = 28; inkTbl[18] = 29; inkTbl[21] = 30; inkTbl[4] = 47; init = true; }
  static uint8_t txtTbl[128]; static bool tInit = false; if (!tInit) { for (int i = 0; i < 128; i++) txtTbl[i] = inkTbl[i]; txtTbl[21] = 31; tInit = true; }   // text: quiet text has its own colour
  c.textInk = txtTbl;
  if (shadows) c.rect(x + 2, y + 2, w, g.h, 26);   // THEME > SHADOWS
  c.ink = inkTbl; c.rect(x, y, w, g.h, 27); if (borders) c.frame(x, y, w, g.h, 21);
  c.ink = nullptr; const bool fl = false, mu = u.m->mute.load();
  c.rect(x, y, w, HEAD, fl ? 4 : mu ? 18 : catCol(d->cat)); c.text(d->name, x + 2, y + 2, 15); if (!d->fixed) c.text("x", x + w - 6 - (outInHead(u) && !jacksIn ? 4 : 0), y + 2, 15);   // out of the way of an OUT on the edge
  c.ink = inkTbl;
  for (size_t k = 0; k < d->ins.size(); k++) { bool conn = false; for (auto& cb : cables) if (cb.to == u.m->id && cb.toPort == (int)k) conn = true;
    drawJack(c, inJackX(u) - viewX, y + jackY((int)k), d->ins[k].type, conn, false); const int lx0 = x + 4 + (jacksIn ? 5 : 0), ly0 = y + jackY((int)k) - 2;
    if (d->type == "tombola" && d->ins[k].name == "SHAKE") {   // TOMBOLA: its SHAKE input's name is also the button (lit for a moment when tapped)
      const int bw = Canvas::tw("SHAKE") + 4; const bool lit = u.shakeT > 0; u.shakeT = std::max(0.f, u.shakeT - dt);
      if (lit) c.rect(lx0 - 2, ly0 - 1, bw, 8, 9); else c.frame(lx0 - 2, ly0 - 1, bw, 8, 9); c.text("SHAKE", lx0, ly0, lit ? 27 : 9); continue; }
    c.text(d->ins[k].name, lx0, ly0, 9); }
  for (size_t k = 0; k < d->outs.size(); k++) { bool conn = false; for (auto& cb : cables) if (cb.from == u.m->id && cb.fromPort == (int)k) conn = true;
    const int ojx = outJackX(u) - viewX, ojy = y + outJackY(u, (int)k);
    if (outInHead(u)) { c.rect(ojx - 3, ojy - 3, 7, 7, 27); drawJack(c, ojx, ojy, d->outs[k].type, conn, false); continue; }   // in the title bar: on a patch of the module's own colour, no name
    drawJack(c, ojx, ojy, d->outs[k].type, conn, fl && d->outs[k].type == PType::Trig); c.text(d->outs[k].name, x + w - 4 - (jacksIn ? 5 : 0) - Canvas::tw(d->outs[k].name), ojy - 2, 9); }
  if (d->vis) drawDisplay(c, u, x + 3, y + g.vy, w - 6, dt);
  if (d->type == "clock" && u.m->p[0].load() > 0.5f) u.m->p[1].store(eng.tempo.bpm.load());   // SHARED: BPM follows the session's tempo
  const int root = eng.key.root.load(); const bool clocked = u.m->clockedIn.load();
  const std::vector<int> ord = u.fold ? std::vector<int>() : rowOrder(u); u.clearAsk = std::max(0.f, u.clearAsk - dt); u.valT = std::max(0.f, u.valT - dt);
  for (size_t r = 0; r < ord.size(); r++) { const size_t j = (size_t)ord[r];
    const ParamDef& q = d->params[j]; const int ry = y + g.py + rowTopOf(u, ord, (int)r); const float v = u.m->p[j].load(); const bool active = drag.kind == D_PARAM && drag.m == &u && drag.k == (int)j;
    if (lineAbove(u, ord, (int)r)) for (int i = x + 3; i < x + w - 3; i += 2) c.px(i, ry - 2, 21);   // where one part gives way to the next
    if (learningRow(u, q.key) && std::fmod(blink, 0.6f) < 0.35f) c.frame(x + 2, ry - 1, w - 4, 9, 12);   // MIDI LEARN: waiting for a knob or button
    if (mapFor(u, q.key)) { c.px(x + 1, ry + 1, 4); c.px(x + 1, ry + 2, 4); c.px(x + 1, ry + 3, 4); c.px(x + 1, ry + 4, 4); }   // moved by MIDI: a mark at the left edge
    if (recButton(d, q)) {   // RECORD: a big red button (filled while it records)
      const bool outMod = d->type == "out"; const float st = d->type == "sample" ? u.m->disp[40].load() : 0; const bool waiting = st > 0.5f && st < 1.5f;
      const bool on = outMod ? u.m->disp[1].load() > 0.5f : (v > 0.5f || st > 0.5f); const bool lit = on && !(waiting && std::fmod(blink, 1.f) > 0.5f);
      std::string t = outMod ? (on ? "STOP + SAVE" : "RECORD") : d->type == "tracker" ? (on ? "STEP RECORD ON" : "STEP RECORD OFF") : on ? "RECORDING" : "RECORD";
      if (d->type == "sample" && on) { char b[32]; const bool od = u.m->p[d->paramIndex("recmode")].load() > 0.5f && !u.snd[0].peaks.empty();
        if (waiting) std::snprintf(b, 32, "WAITING FOR THE BAR"); else if (st > 2.5f) std::snprintf(b, 32, "FINISHING"); else std::snprintf(b, 32, "%s %.1fS", od ? "OVERDUB" : "REC", u.m->disp[41].load()); t = b; }
      if (lit) c.rect(x + 3, ry - 1, w - 6, 9, 14); else c.frame(x + 3, ry - 1, w - 6, 9, 14);
      c.rect(x + 6, ry + 2, 3, 3, lit ? 15 : 14); c.text(t.substr(0, (size_t)std::max(4, (w - 16) / 4)), x + 12, ry + 1, lit ? 15 : 14); continue; }
    if (q.key == "clear" && q.act && u.clearAsk > 0) { c.text("TAP AGAIN: CLEAR", x + 3, ry + 1, 14); continue; }   // asked once
    if (q.act) { const bool recOn = d->type == "out" && q.key == "rec" && u.m->disp[1].load() > 0.5f; c.text(recOn ? "STOP + SAVE" : q.label, x + 3, ry + 1, recOn ? 14 : 9); c.text(">", x + w - 6, ry + 1, 9); continue; }
    if (q.play) { const bool on = v > 0.5f; c.rect(x + 3, ry, w - 6, 7, on ? 17 : 21); const int ix = x + 6, iy = ry + 1;
      if (on) c.rect(ix, iy + 1, 3, 3, 15); else for (int k = 0; k < 3; k++) for (int jj = k; jj < 5 - k; jj++) c.px(ix + k, iy + jj, 9);
      c.text(on ? "PLAYING" : "PLAY", x + 13, ry + 1, on ? 15 : 9); continue; }
    int syncI = d->paramIndex("sync"); const bool syncOn = syncI >= 0 && d->params[syncI].tog && u.m->p[syncI].load() > 0.5f;
    // rows that wait on a cable: dimmed (the ones that don't apply to this module's own settings are folded away)
    bool idle = (q.only == "free" && clocked) || (q.only == "clock" && !clocked) || (q.only == "own" && clocked);
    if (q.only == "side") { bool sp = false; for (auto& cb : cables) if (cb.to == u.m->id && cb.toPort == 2) sp = true; if (!sp) idle = true; }   // DUCKER: THRESHOLD is for SIDE
    std::string lab = d->type == "mi" ? statusLabel(u, q) : q.label;   // MI VOICES: the chosen model's own names for its three controls
    if ((active || (u.valK == (int)j && u.valT > 0)) && q.opts.empty() && !q.stepper) lab = fmtParam(q, v, root);   // while it's dragged (or just after - or +): its value where its name was
    c.text(lab.substr(0, 11), x + 3 + (jacksIn && modulatable(q) ? 7 : 0), ry + 1, active ? 4 : idle ? 21 : 18);
    bool modded = false; for (auto& cb : cables) if (cb.type == PType::Mod && cb.to == u.m->id && cb.toParam == (int)j) modded = true;
    if (modulatable(q)) drawJack(c, sockX(u) - viewX, ry + 3, PType::Mod, modded, false);   // its mod socket: filled when something moves it
    const int kIn = u.m->keyIn.load(), sIn = u.m->scaleIn.load(); const bool fromJ = kIn >= 0 && ((int)j == d->keyIdx || (int)j == d->scaleIdx);   // a KEY JOURNEY patched in: its key, in green
    if (!q.opts.empty()) { std::string s = (fromJ ? ((int)j == d->keyIdx ? std::string(KEY_NAMES[kIn % 12]) : std::string(scales()[std::max(0, std::min((int)scales().size() - 1, sIn))].name)) : fmtParam(q, v, root)).substr(0, (size_t)std::max(6, (w - Canvas::tw(lab.substr(0, 11)) - 10 - (jacksIn ? 7 : 0)) / 4)); /* as much of the choice as fits beside its name */ c.text(s, x + w - 3 - Canvas::tw(s), ry + 1, active ? 4 : idle ? 21 : fromJ ? 11 : 9); c.px(x + w - 3 - Canvas::tw(s) - 3, ry + 3, idle ? 21 : 18); continue; }
    const bool stepping = drag.kind == D_STEP && drag.m == &u && drag.k == (int)j;
    const int mx = x + minusX(u, (int)j); c.text("-", mx, ry + 1, stepping && drag.dir < 0 ? 4 : idle ? 21 : 9); c.text("+", x + w - 6, ry + 1, stepping && drag.dir > 0 ? 4 : idle ? 21 : 9);   // nudge it a little at a time
    if (q.stepper) {   // a small whole number: - 3 + (what it's moved to, when something modulates it)
      const float sv = modded ? std::round(u.m->eff[j].load(std::memory_order_relaxed) / q.step) * q.step : v; const std::string t = fmtParam(q, clampf(sv, q.min, q.max), root);
      const int l = mx + 5, r = x + w - 8; c.text(t, l + (r - l - Canvas::tw(t)) / 2, ry + 1, active ? 4 : idle ? 21 : modded ? 11 : 9); continue; }
    const int bx = mx + 5, bw = x + w - 8 - bx; const float nb = clampf(q.toNorm(v), 0, 1), n = modded ? clampf(q.toNorm(u.m->eff[j].load(std::memory_order_relaxed)), 0, 1) : nb;
    for (int i = 0; i < bw; i += 2) c.px(bx + i, ry + 4, 18);
    const int fw = (int)std::lround(n * bw); for (int i = 0; i < fw; i++) { c.px(bx + i, ry + 4, active ? 4 : idle ? 21 : modded ? 11 : 7); if (active) c.px(bx + i, ry + 3, 4); }   // modulated: green, and moving with it
    if (modded) { const int bxm = bx + (int)std::lround(nb * (bw - 1)); c.px(bxm, ry + 2, 9); c.px(bxm, ry + 3, 9); c.px(bxm, ry + 5, 9); }   // where you set it
  }
  if (!d->fixed) { const int fy = y + g.h - FOOT + 1; for (int i = 3; i < w - 3; i += 2) c.px(x + i, fy - 2, 21);
    auto box = [&](int bx, bool on, const char* l, int col) { if (on) c.rect(bx, fy, 9, 7, col); else c.frame(bx, fy, 9, 7, 18); c.text(l, bx + 3, fy + 1, on ? 15 : 9); };
    if (d->cat == Cat::Trig || d->cat == Cat::Mod) {   // sequencers and mod sources: simply on or off
      const bool on = !mu; if (on) c.rect(x + 3, fy, 15, 7, 17); else c.frame(x + 3, fy, 15, 7, 18); c.text(on ? "ON" : "OFF", x + (on ? 7 : 5), fy + 1, on ? 15 : 9); }
    else { box(x + 3, mu, "M", 14); if (d->cat == Cat::Voice) box(x + 14, u.m->solo.load(), "S", 4); else if (bypassable(d)) box(x + 14, u.m->bypass.load(), "B", 12); }   // only sound makers can be soloed; effects can be bypassed
    { bool conn = false; for (auto& cb : cables) if (cb.to == u.m->id && cb.toPort == SWITCH_PORT) conn = true;
      drawJack(c, swX(u) - viewX, swY(u) - viewY, PType::Trig, conn, false); }
    if (hasKey(u)) { bool conn = false; for (auto& cb : cables) if (cb.to == u.m->id && cb.toPort == KEY_PORT) conn = true;   // the KEY socket, named
      const int kx = kyX(u) - viewX; drawJack(c, kx, swY(u) - viewY, PType::Trig, conn, false); if (!clocked || kx + 18 < x + w - 15 - Canvas::tw("CLOCKED")) c.text("KEY", kx + 5, fy + 1, conn ? 9 : 21); }
    box(x + w - 12, false, u.fold ? "+" : "-", 9);
    if (clocked) c.text("CLOCKED", x + w - 15 - Canvas::tw("CLOCKED"), fy + 1, 24); }
  c.ink = nullptr; c.textInk = nullptr;
}

// after loading: the view goes to the middle of the session (or its top-left corner, if it's bigger than the screen)
void Ui::centreView() {
  if (mods.empty() || lastW <= 0) { viewX = viewY = 0; return; }
  int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
  for (auto& u : mods) { const Geom g = geom(*u); x0 = std::min(x0, u->x - 4); y0 = std::min(y0, u->y); x1 = std::max(x1, u->x + g.w + 4); y1 = std::max(y1, u->y + g.h); }
  const int vw = lastW, vh = lastH - TOP;
  viewX = x1 - x0 <= vw ? (x0 + x1) / 2 - vw / 2 : x0 - 4;
  viewY = (y1 - y0 <= vh ? (y0 + y1) / 2 - vh / 2 : y0 - 4) - TOP;
}
void Ui::draw(Canvas& c, float dt) {
  lastW = c.W; lastH = c.H; if (centrePending) { centrePending = false; centreView(); }
  blink += dt;
  lastW = c.W;
  c.rect(0, 0, c.W, c.H, 25);
  // cables first, so modules sit on top of them
  for (auto& cb : cables) {
    UiMod *a = byId(cb.from), *b = byId(cb.to); if (!a || !b) continue;
    const int ax = outJackX(*a) - viewX, ay = a->y + outJackY(*a, cb.fromPort) - viewY;
    int bx, by; if (cb.type == PType::Mod) { const Geom g = geom(*b); bx = sockX(*b) - viewX; by = b->y + (b->fold ? g.h - FOOT : g.py + rowPx(*b, cb.toParam) + 3) - viewY; } else if (cb.toPort == SWITCH_PORT) { bx = swX(*b) - viewX; by = swY(*b) - viewY; } else if (cb.toPort == KEY_PORT) { bx = kyX(*b) - viewX; by = swY(*b) - viewY; } else { bx = inJackX(*b) - viewX; by = b->y + jackY(cb.toPort) - viewY; }
    const bool hi = (selT > 0 && sameCable(cb, sel)) || (drag.kind == D_MOVE && drag.splice >= 0 && drag.splice < (int)cables.size() && sameCable(cb, cables[drag.splice]));
    if (!cableMotion) { drawCable(c, ax, ay, bx, by, hi ? 4 : typeCol(cb.type)); continue; }   // still: nothing moves or flashes
    if (cb.type == PType::Audio && !hi) {   // audio: dashes that run faster, longer and brighter the louder it is; still when silent
      const float lin = cb.fromPort < 8 ? a->m->outLev[cb.fromPort].load(std::memory_order_relaxed) : 0, n = clampf((20 * std::log10(lin + 1e-6f) + 48) / 48, 0, 1);
      if (n < 0.03f) drawCable(c, ax, ay, bx, by, typeCol(cb.type));
      else { cb.flow += dt * (5 + 40 * n); drawCable(c, ax, ay, bx, by, typeCol(cb.type), n > 0.25f ? 4 : 9, cb.flow, 12, 2 + std::lround(4 * n), n > 0.5f ? 9 : 21); } }
    else if (cb.type == PType::Mod && !hi) {   // mod: little arrows that speed up as the signal changes faster, brighter the further it's moving the setting
      float v = 0;
      if (b && cb.toParam >= 0 && cb.toParam < (int)b->m->def->params.size()) { const ParamDef& q = b->m->def->params[cb.toParam];
        v = q.toNorm(b->m->eff[cb.toParam].load(std::memory_order_relaxed)) - q.toNorm(b->m->p[cb.toParam].load()); }
      if (dt > 0) cb.rate += (std::fabs(v - cb.lastV) / dt - cb.rate) * std::min(1.f, dt * 4); cb.lastV = v;
      const float amt = std::fabs(v); cb.flow += dt * (3 + std::min(60.f, cb.rate * 120));
      drawCable(c, ax, ay, bx, by, typeCol(cb.type), amt > 0.05f ? 4 : 9, cb.flow, 7, 3, 21); }
    else drawCable(c, ax, ay, bx, by, hi ? 4 : typeCol(cb.type));
    if (cb.type == PType::Trig) {   // each trigger sent: a pulse that runs along the cable to where it's going
      const unsigned n = cb.fromPort < 8 ? a->m->fires[cb.fromPort].load(std::memory_order_relaxed) : 0;
      if (n != cb.seen) { if (cb.seen != 0) for (unsigned k = 0; k < std::min(3u, n - cb.seen); k++) cb.pulses.push_back(-0.08f * k); cb.seen = n; }
      const float len = std::max(20.f, std::hypot((float)(bx - ax), (float)(by - ay)) * 1.2f), sp = 260.f / len * dt;   // about 260 pixels a second
      for (auto& t : cb.pulses) { t += sp; if (t < 0 || t > 1) continue; float x, y; cablePt((float)ax, (float)ay, (float)bx, (float)by, t, x, y);
        for (int k = 3; k >= 1; k--) { const float tt = t - k * 0.025f; if (tt < 0) continue; float tx, ty; cablePt((float)ax, (float)ay, (float)bx, (float)by, tt, tx, ty);   // a fading tail behind it
          c.px((int)std::lround(tx), (int)std::lround(ty), k == 1 ? 46 : k == 2 ? 9 : 18); }
        const int xi = (int)std::lround(x), yi = (int)std::lround(y); c.rect(xi - 1, yi - 1, 3, 3, 46); c.px(xi, yi, 4); }
      cb.pulses.erase(std::remove_if(cb.pulses.begin(), cb.pulses.end(), [](float t) { return t > 1; }), cb.pulses.end());
      if (cb.pulses.size() > 24) cb.pulses.erase(cb.pulses.begin(), cb.pulses.end() - 24); }
  }
  flowT += dt * 7.f;
  for (auto& u : mods) drawModule(c, *u, dt);
  if (selOn) { selT = 1;   // a picked cable: its UNPLUG button, until you tap anywhere else
    bool alive = false; for (auto& cb : cables) if (sameCable(cb, sel)) alive = true; if (!alive) selOn = false; }
  selT = std::max(0.f, selT - dt);
  if (selOn) { const int bw = Canvas::tw("UNPLUG") + 6; ubx = std::max(2, std::min(c.W - bw - 2, ubx)); uby = std::max(TOP + 2, std::min(c.H - 11, uby));
    c.rect(ubx + 1, uby + 1, bw, 9, 26); c.rect(ubx, uby, bw, 9, 14); c.text("UNPLUG", ubx + 3, uby + 2, 15); }
  // holding - or + keeps nudging: a short wait, then steadily
  if (drag.kind == D_STEP) { drag.holdT += dt; drag.holdSum += dt;   // held: it repeats; with many steps (STRETCH's 1000) it speeds up the longer it's held
    const ParamDef& hq = drag.m->m->def->params[drag.k]; const bool many = hq.step > 0 && (hq.max - hq.min) / hq.step > 64;
    const int by = !many || drag.holdSum < 1.5f ? 1 : drag.holdSum < 3 ? 5 : drag.holdSum < 5 ? 20 : 50;
    while (drag.holdT > 0.45f) { drag.holdT -= 0.06f; nudge(drag.m, drag.k, drag.dir * by); } }
  // a cable being dragged, and the jacks it could go to
  if (drag.kind == D_CABLE) {
    drawCable(c, drag.from.x - viewX, drag.from.y - viewY, drag.wx - viewX, drag.wy - viewY, 4);
    for (auto& up : mods) { UiMod& u = *up; const auto& list = drag.rev ? u.m->def->outs : u.m->def->ins;
      for (size_t k = 0; k < list.size(); k++) if (list[k].type == drag.from.type && &u != drag.from.u) drawJack(c, (drag.rev ? outJackX(u) : inJackX(u)) - viewX, u.y + (drag.rev ? outJackY(u, (int)k) : jackY((int)k)) - viewY, list[k].type, true, true); }
  }
  // top bar, menus and panels: drawn in the MENUS colours (light text on black, or text to suit a pale shade)
  static uint8_t menuInk[128]; static bool mInit = false; if (!mInit) { for (int i = 0; i < 128; i++) menuInk[i] = (uint8_t)i; menuInk[4] = MENU_HI; menuInk[9] = MENU_TEXT; menuInk[18] = MENU_MID; menuInk[21] = MENU_LINE; mInit = true; }
  c.ink = menuInk;
  barHits.clear();
  c.rect(0, 0, c.W, TOP, MENU_BG); for (int x = 0; x < c.W; x += 2) c.px(x, TOP, 21);
  int bx = 3;
  auto btn = [&](const std::string& l, int col, std::function<void()> fn) { c.text(l, bx, 3, col); barHits.push_back({bx, bx + Canvas::tw(l), fn, l}); bx += Canvas::tw(l) + 8; };
  if (viewOnly) {   // the web player: the session's name, and how hard the sound is working
    std::string t = viewTitle; const std::string info = "AUDIO " + std::to_string((int)std::lround(eng.cpuLoad.load() * 100)) + "%"; const int room = (c.W - Canvas::tw(info) - 16) / 4;
    if ((int)t.size() > room) t = t.substr(0, std::max(0, room)); c.text(t, 3, 3, 9); c.text(info, c.W - Canvas::tw(info) - 4, 3, 18);
    if (statusT > 0) statusT -= dt; c.ink = nullptr; c.textInk = nullptr; return; }
  btn("+ ADD", 4, [this] { menuOpen = !menuOpen; dd.reset(); });
  for (auto& b : bar) { const int x0 = bx; auto fn = b.fn; btn(b.label, b.col, [fn, x0] { fn(x0); }); }
  { bool open = false; for (auto& u : mods) if (!u->m->def->fixed && !u->fold) open = true;
    btn(open ? "FOLD ALL" : "OPEN ALL", 9, [this, open] { for (auto& u : mods) if (!u->m->def->fixed) u->fold = open; changed(); }); }
  btn("?", helpOn ? 4 : 18, [this] { helpOn = !helpOn; hoverT = -1; if (onHelpToggle) onHelpToggle(helpOn); status(helpOn ? "HELP ON: REST THE POINTER ON ANYTHING FOR A NOTE ON IT" : "HELP OFF (THE ? BRINGS IT BACK)", 3); });
  const std::string fs = isFullscreen ? "WINDOW" : "FULLSCREEN"; const int fx = c.W - Canvas::tw(fs) - 4; c.text(fs, fx, 3, 9); barHits.push_back({fx, fx + Canvas::tw(fs), [this] { wantFullscreen = !isFullscreen; }, fs});
  int ix0 = fx;
  if (onZoom) {   // zoom out / in
    const int zx = fx - 24; for (int i = 0; i < 5; i++) c.px(zx + i, 5, 9); for (int i = 0; i < 5; i++) { c.px(zx + 10 + i, 5, 9); c.px(zx + 12, 3 + i, 9); }
    barHits.push_back({zx - 1, zx + 5, [this] { onZoom(-1); }, "ZOOM"}); barHits.push_back({zx + 9, zx + 15, [this] { onZoom(1); }, "ZOOM"}); ix0 = zx;
  }
  int nIn = 0; for (auto& d : midiDevs) if (d.input && d.open) nIn++;
  char info[64]; std::snprintf(info, sizeof info, "MIDI %d  AUDIO %d%%", nIn, (int)std::lround(eng.cpuLoad.load() * 100));   // (how many MIDI inputs are listened to: SETTINGS > MIDI > DEVICES)
  const int ix = ix0 - Canvas::tw(info) - 10; infoX0 = ix; infoX1 = ix + Canvas::tw(info);
  if (statusT > 0) { statusT -= dt; std::string s = statusMsg; const int room = (ix - bx - 4) / 4; if ((int)s.size() > room) s = s.substr(0, std::max(0, room)); c.text(s, bx + 4, 3, 17); }
  else if (ix > bx) c.text(info, ix, 3, 18);
  // the ADD menu
  menuItems.clear();
  if (menuOpen) {
    // four columns, RHYTHM, VOICES, PROCESS, MOD, each A to Z
    std::vector<std::vector<const ModuleDef*>> colsD; std::vector<int> ws; int tot = 4;
    for (auto& cat : MENU) { std::vector<const ModuleDef*> v; for (auto& t : cat.second) if (const ModuleDef* d = findDef(t)) v.push_back(d);
      std::sort(v.begin(), v.end(), [](const ModuleDef* a, const ModuleDef* b) { return a->name < b->name; }); int w = Canvas::tw(cat.first);
      for (auto* d : v) w = std::max(w, Canvas::tw(d->name) + 6); ws.push_back(w + 8); tot += w + 8; colsD.push_back(v); }
    size_t most = 0; for (auto& v : colsD) most = std::max(most, v.size()); const int mh = (int)most * 8 + 16;
    c.rect(1, TOP + 1, tot, mh, MENU_BG); c.frame(1, TOP + 1, tot, mh, 21);
    int x = 4; for (size_t k = 0; k < colsD.size(); k++) { c.text(MENU[k].first, x, TOP + 4, 18);
      for (size_t r = 0; r < colsD[k].size(); r++) { const ModuleDef* d = colsD[k][r]; const int y = TOP + 13 + (int)r * 8; c.rect(x, y + 1, 3, 3, catCol(d->cat)); c.text(d->name, x + 6, y, 9); menuItems.push_back({d->type, x, y, ws[k] - 6}); }
      x += ws[k]; }
  }
  // a dropdown list
  if (dd) {
    if (dd->live) dd->live(*dd);
    // a long list wraps into columns so it always fits on screen
    int cw = 0; for (auto& it : dd->items) cw = std::max(cw, Canvas::tw(it)); cw += dd->sw.empty() ? 8 : 16;
    const int n = (int)dd->items.size(), rowsFit = std::max(1, (c.H - TOP - 8) / 8), cols = (n + rowsFit - 1) / rowsFit, rows = (n + cols - 1) / cols;
    const int w = cw * cols, h = rows * 8 + 4;
    dd->x = std::max(2, std::min(dd->x, c.W - w)); dd->y = std::max(TOP + 1, std::min(dd->y, c.H - h - 1)); dd->w = w; dd->rows = rows; dd->colW = cw;
    c.rect(dd->x, dd->y + 2, w, h, 26); c.rect(dd->x - 2, dd->y, w, h, MENU_BG); c.frame(dd->x - 2, dd->y, w, h, 21);
    for (int i = 0; i < n; i++) { const int cx = dd->x + (i / rows) * cw, y = dd->y + 2 + (i % rows) * 8; if (i == dd->cur) c.rect(cx - 1, y - 1, cw - 2, 7, 21);
      const int sw = i < (int)dd->sw.size() ? dd->sw[i] : -1, tx = dd->sw.empty() ? cx + 1 : cx + 9;   // a colour swatch in front, in the theme menus
      if (sw >= 0) { c.rect(cx + 1, y, 6, 5, sw); c.frame(cx + 1, y, 6, 5, 21); }
      c.text(dd->items[i], tx, y, i == dd->title || i == dd->title2 ? 18 : i == dd->danger ? 14 : i == dd->cur ? 4 : 9); }
  }
  // the tempo panel
  if (tp.open) {
    tp.x = std::max(2, std::min(tp.x, c.W - TPW - 2)); tp.y = std::max(TOP + 1, std::min(tp.y, c.H - TPH - 2)); const int x = tp.x, y = tp.y, bpm = (int)std::lround(eng.tempo.bpm.load());
    c.rect(x + 2, y + 2, TPW, TPH, 26); c.rect(x, y, TPW, TPH, MENU_BG); c.frame(x, y, TPW, TPH, 21);
    c.text("SHARED TEMPO", x + 4, y + 3, 18); const std::string v = std::to_string(bpm) + " BPM"; c.text(v, x + TPW - 4 - Canvas::tw(v), y + 3, 4);
    auto btn = [&](int bx, int by, int w, const char* l) { c.frame(bx, by, w, 7, 18); c.text(l, bx + (w - Canvas::tw(l)) / 2, by + 1, 9); };
    btn(x + 4, y + 11, 9, "-"); btn(x + TPW - 13, y + 11, 9, "+");
    const int tx = x + 17, tw = TPW - 34, fx = tx + (int)std::lround((bpm - 20) / 220.f * (tw - 1));
    for (int i = 0; i < tw; i += 2) c.px(tx + i, y + 14, 18); for (int i = tx; i < fx; i++) c.px(i, y + 14, 7); c.rect(fx - 1, y + 12, 3, 5, 4);
    btn(x + 4, y + 21, 21, "TAP"); btn(x + 29, y + 21, 17, "-10"); btn(x + 50, y + 21, 17, "+10");
  }
  drawBrowser(c);
  drawEditor(c);
  // the name box
  if (ask) {
    const int w = std::min(c.W - 10, 150), h = 34, x = (c.W - w) / 2, y = TOP + 30;
    c.rect(x + 2, y + 2, w, h, 26); c.rect(x, y, w, h, MENU_BG); c.frame(x, y, w, h, 21);
    c.text(ask->title, x + 4, y + 4, 18); c.rect(x + 4, y + 12, w - 8, 9, 21);
    std::string t = ask->text; const int room = (w - 14) / 4; if ((int)t.size() > room) t = t.substr(t.size() - room);
    c.text(t, x + 6, y + 14, 4); if (std::fmod(blink, 1.f) < 0.5f) c.rect(x + 6 + Canvas::tw(t) + 2, y + 13, 1, 7, 4);
    c.text("OK", x + 6, y + 25, 9); c.text("CANCEL", x + 24, y + 25, 9);
    if (ask->dice) { const int dx = x + w - 52; c.frame(dx, y + 24, 7, 7, 9); for (int k : {0, 2, 4}) c.px(dx + 1 + k, y + 25 + k, 4);   // a dice: another name
      c.text("NEW NAME", dx + 10, y + 25, 9); }
  }
  drawHelp(c, dt);
  c.ink = nullptr;
  c.toRGBA();
}
// HELP: what's under the pointer, as "TITLE|text"
std::string Ui::helpAt(float sx, float sy) {
  auto withRange = [&](const ModuleDef* d, int j) {   // a setting's note, with its range or choices
    const ParamDef& q = d->params[j]; std::string s = helpParam(d, q); if (s.empty() || q.act) return s; const int root = eng.key.root.load();
    if (!q.opts.empty()) { s += " ("; for (size_t i = 0; i < q.opts.size() && i < 6; i++) s += (i ? ", " : "") + q.opts[i]; s += q.opts.size() > 6 ? "...)" : ")"; }
    else s += " (" + fmtParam(q, q.min, root) + " TO " + fmtParam(q, q.max, root) + ")";
    if (q.key == "pitch" || ((d->type == "midiout" || d->type == "nloop") && q.key == "trans"))   // a tap (not a drag) on these opens the octave jumps
      s += d->type == "sample" ? " TAP IT (WITHOUT DRAGGING) TO JUMP STRAIGHT TO QUARTER, HALF, NORMAL, DOUBLE OR 4X SPEED." : " TAP IT (WITHOUT DRAGGING) TO JUMP STRAIGHT TO -24, -12, 0, +12 OR +24.";
    return s; };
  auto modNote = [&](UiMod* u, int j) {   // a green ring: how to modulate it, and what does now
    const ModuleDef* d = u->m->def; std::string body = helpOther("MODRING"); body = body.substr(body.find('|') + 1);
    for (auto& cb : cables) if (cb.to == u->m->id && cb.toParam == j) if (UiMod* a = byId(cb.from)) body += " NOW: MOVED BY " + a->m->def->name + " (DRAG THE CABLE'S END AWAY TO UNPLUG IT).";
    return "MODULATE " + d->params[j].label + "|" + body; };
  if (menuOpen) { for (auto& it : menuItems) if (sx >= it.x - 1 && sx < it.x + it.w && sy >= it.y - 1 && sy < it.y + 7) return helpModule(it.type);   // the ADD menu: what each module is
    if (sy >= TOP) return {}; }
  if (sy < TOP) { for (auto& b : barHits) if (!b.label.empty() && sx >= b.x0 - 2 && sx <= b.x1 + 2) return helpOther(b.label);
    return sx >= infoX0 && sx <= infoX1 && statusT <= 0 ? helpOther("AUDIO") : std::string(); }
  const int wx = (int)sx + viewX, wy = (int)sy + viewY;
  const JackHit jh = jackAt(wx, wy, 3);
  if (jh.u) { const ModuleDef* d = jh.u->m->def;
    if (jh.param >= 0) return modNote(jh.u, jh.param);
    if (jh.port == SWITCH_PORT) return helpOther("SWITCH"); if (jh.port == KEY_PORT) return helpOther("KEYSOCKET");
    return helpPort(d, jh.out, jh.port); }
  int lx, ly; if (UiMod* u = hit(wx, wy, lx, ly)) { const ModuleDef* d = u->m->def; const Geom g = geom(*u);
    if (ly < HEAD) return !d->fixed && lx >= g.w - 8 ? helpOther("CLOSE") : helpModule(d->type);
    if (d->type == "tracker" && ly >= g.vy && ly < g.vy + trHead(u->m) && lx >= 3 + 16 && lx < g.w - 3 - 8) return helpOther("TRACKER COLUMN");   // its column numbers: RANDOM NOTE
    if (!d->fixed && ly >= g.h - FOOT) { const bool onOff = d->cat == Cat::Trig || d->cat == Cat::Mod;
      if (lx >= g.w - 13) return helpOther("FOLD"); if (onOff && lx >= 3 && lx < 18) return helpOther("ON"); if (!onOff && lx >= 3 && lx < 12) return helpOther("M");
      if (lx >= 14 && lx < 23) return d->cat == Cat::Voice ? helpOther("S") : d->type == "xfade" ? std::string("Bypass|Only A goes through, at full level, wherever the fade was: as if the crossfader weren't there. B is silent until you switch it back.") : bypassable(d) ? helpOther("B") : std::string(); return {}; }
    if (d->type == "spectrum" && d->vis && ly >= g.vy && ly < g.vy + g.vh) {   // SPECTRUM: the frequency under the pointer, how loud it is there, and what the shading means
      const int w = g.w - 6; const float k = clampf((float)(lx - 3) / std::max(1, w) * 64, 0, 63.99f); const float hz = specHz(k); const int kb = (int)k;
      const float db = u->spec.size() == 64 ? u->spec[kb] : -120; char b[48]; std::snprintf(b, 48, "%s  %s", hzText(hz).c_str(), db < -71 ? "NOTHING" : (std::to_string((int)std::lround(db)) + "DB").c_str());
      const int z = specZone(hz); const char* bz = specBuild(hz);
      std::string t = z == 2 ? "OUTSIDE HEARING (BELOW 20 HZ OR ABOVE 20 KHZ): NO ONE CAN HEAR THIS, IT ONLY TAKES UP ROOM. ANYTHING HERE IS WORTH FILTERING OUT."
        : z == 1 ? (hz < 100 ? "THE LOW EDGE OF HEARING (20 TO 30 HZ): FELT MORE THAN HEARD, AND MOST SPEAKERS CAN'T PLAY IT. WORTH A LOOK." : "THE HIGH EDGE OF HEARING (16 TO 20 KHZ): MOST ADULTS HEAR LITTLE UP HERE. WORTH A LOOK.")
        : bz ? std::string("WHERE SOUND OFTEN PILES UP (") + bz + "). NOT WRONG IN ITSELF; IF IT'S CROWDED HERE, TRY TAKING A LITTLE AWAY." : "THE MAIN RANGE OF HEARING.";
      if (specPeak(*u, kb)) t = "A NARROW PEAK, WELL ABOVE ITS NEIGHBOURS: A RESONANCE THAT CAN RING OR WHISTLE. TRY A NARROW CUT HERE. " + t;
      return std::string(b) + "|" + t; }
    if (d->type == "journey" && ly >= g.vy && ly < g.vy + 17) {   // KEY JOURNEY: a step, and how smooth the change into it is, in words
      const int N = std::max(1, std::min(8, (int)std::lround(u->m->p[0].load()))), sw = std::min(16, (g.w - 6 + 1) / N), i = (lx - 3) / std::max(1, sw); const int nsc = (int)scales().size();
      if (i >= 0 && i < N) { auto keyAt = [&](int k) { return (int)std::lround(u->m->seq[Module::si(k, 0)].load()); };
        auto scAt = [&](int k) { const float v = u->m->seq[Module::si(k, 0) + 1].load(); return std::max(0, std::min(nsc - 1, v >= 10 ? (int)std::lround(v) - 10 : (int)u->m->disp[10].load())); };
        auto mask = [&](int r, int sc) { int mm = 0; for (int st : scales()[sc].steps) mm |= 1 << ((r + st) % 12); return mm; };
        const int k = keyAt(i); std::string t = "STEP " + std::to_string(i + 1);
        if (k < 0) return t + "|EMPTY: SKIPPED. TAP IT TO CHOOSE A KEY; TAP UNDER IT FOR A SCALE OF ITS OWN.";
        t += ": " + std::string(KEY_NAMES[k % 12]) + " " + scales()[scAt(i)].name; int pj = -1; for (int j = 1; j <= N; j++) { const int q = ((i - j) % N + N) % N; if (keyAt(q) >= 0) { pj = q; break; } }
        std::string how = "THE ONLY STEP.";
        if (pj >= 0 && pj != i) { int dd = 0; for (int xm = mask(keyAt(pj), scAt(pj)) ^ mask(k, scAt(i)); xm; xm &= xm - 1) dd++; dd /= 2;
          const std::string from = "FROM " + std::string(KEY_NAMES[keyAt(pj) % 12]) + " (STEP " + std::to_string(pj + 1) + "): ";
          how = from + (dd == 0 ? "THE SAME NOTES, A NEW HOME." : dd == 1 ? "SMOOTH: ONE NOTE CHANGES." : dd == 2 ? "A BIGGER CHANGE: TWO NOTES." : "JARRING: " + std::to_string(dd) + " NOTES CHANGE."); }
        return t + "|" + how + " TAP IT TO CHANGE ITS KEY; TAP UNDER IT FOR A SCALE OF ITS OWN."; } }
    const int pj = paramAt(u, ly);
    if (pj >= 0 && d->type == "mi" && !(jacksIn && lx < 9)) { const std::string& k = d->params[pj].key; const int w3 = k == "harm" ? 0 : k == "timb" ? 1 : k == "morph" ? 2 : -1;   // MI VOICES: what this model does with it
      if (w3 >= 0) { const int mdl = std::max(0, std::min(MI_ENGINES - 1, (int)u->m->p[0].load())); return std::string(miEngines()[mdl].k[w3]) + "|" + miHelp(mdl, w3) + " (PLAITS' " + d->params[pj].label + " CONTROL, ON " + miEngines()[mdl].name + ")"; } }
    if (pj >= 0 && typeInfo(d->type, d->params[pj].key)) {   // REVERB, ECHO, FILTER...: what this type does, and the others
      const TypeInfo* ti = typeInfo(d->type, d->params[pj].key); const ParamDef& q = d->params[pj]; const int t = std::max(0, std::min((int)q.opts.size() - 1, (int)std::lround(u->m->p[pj].load()))), H = std::min((int)q.opts.size(), ti->from);
      std::string s = helpOther(ti->prefix + q.opts[t]); const size_t bar = s.find('|'); std::string lt, hv; for (size_t i = 0; i < q.opts.size(); i++) ((int)i < H ? lt : hv) += (((int)i < H ? lt : hv).empty() ? "" : ", ") + q.opts[i];
      if (bar != std::string::npos) return [&] { std::string t = q.label; for (size_t i = 1; i < t.size(); i++) t[i] = (char)std::tolower((unsigned char)t[i]); return t + ": "; }() + s.substr(0, bar) + (t < H ? " (light)" : " (heavy)") + "|" + s.substr(bar + 1) + (hv.empty() ? " (ALL LIGHT: " + lt + ".)" : " (LIGHT: " + lt + ". HEAVY, MORE CPU: " + hv + ".") + (hv.empty() ? "" : ")") + (d->type == "reverb" ? " CHANGING TYPE ALSO SETS DECAY TO ITS USUAL LENGTH." : ""); }
    if (pj >= 0) return jacksIn && lx < 9 && modulatable(d->params[pj]) ? modNote(u, pj) : withRange(d, pj) + (tapKind(d, d->params[pj]) ? " TAP IT THREE TIMES OR MORE IN RHYTHM TO SET IT." : "");
    return helpModule(d->type); }
  const int ci = cableAt(wx, wy, 3); if (ci >= 0) {   // a cable: what kind (its colour), what it joins, and what you can do with it
    const UiCable& cb = cables[ci]; const std::string b = helpOther("CABLE"), how = b.substr(b.find('|') + 1);
    if (cb.type == PType::Audio) return "AUDIO CABLE: " + cableName(cb) + "|CARRIES SOUND IN STEREO: LEFT AND RIGHT TOGETHER ON THE ONE CABLE. DRAG AN EFFECT THAT ISN'T PATCHED YET (A REVERB, SAY) ONTO IT AND LET GO: THE EFFECT GOES IN BETWEEN. " + how;
    if (cb.type == PType::Mod) return "MODULATION CABLE: " + cableName(cb) + "|CARRIES MODULATION: IT MOVES THE SETTING AT ITS END BY ITSELF (THE SOURCE'S DEPTH SETS HOW FAR). " + how;
    return "TRIGGER CABLE: " + cableName(cb) + "|CARRIES NOTES AND TRIGGERS: EACH ONE PLAYS OR STARTS WHAT'S AT ITS END. " + how; }
  return {}; }
void Ui::setAddMenu(bool open) { menuOpen = open; }
bool Ui::menuItemAt(const std::string& type, int& x, int& y) const { for (auto& it : menuItems) if (it.type == type) { x = it.x; y = it.y; return true; } return false; }
void Ui::drawHelp(Canvas& c, float dt) {   // the note: after the pointer rests for half a second, beside it (never under a drag, a menu or a dialog)
  if (!helpOn || hoverT < 0) return; hoverT += dt;
  if (hoverT < 0.5f || drag.kind != D_NONE || dd || ed.open || br.open || tp.open || ask) return;
  std::string t = helpAt(hoverX, hoverY); if (t.empty()) return; for (auto& ch : t) ch = (char)std::toupper((unsigned char)ch);
  const size_t bar = t.find('|'); const std::string title = bar == std::string::npos ? std::string() : t.substr(0, bar), body = bar == std::string::npos ? t : t.substr(bar + 1);
  const int maxW = std::min(c.W - 8, 176), cols = (maxW - 8) / 4; std::vector<std::string> lines;
  { std::string line, word; auto flush = [&] { if (!line.empty()) lines.push_back(line); line.clear(); };
    for (size_t i = 0; i <= body.size(); i++) { const char ch = i < body.size() ? body[i] : ' '; if (ch != ' ') { word += ch; continue; } if (word.empty()) continue;
      if ((int)(line.size() + (line.empty() ? 0 : 1) + word.size()) > cols) flush(); line += (line.empty() ? "" : " ") + word; word.clear(); } flush(); }
  int w = Canvas::tw(title); for (auto& l : lines) w = std::max(w, Canvas::tw(l)); w += 8; const int h = 5 + (title.empty() ? 0 : 9) + (int)lines.size() * 8;
  int x = (int)hoverX + 8, y = (int)hoverY + 10; if (x + w > c.W - 2) x = std::max(2, (int)hoverX - w - 4); if (y + h > c.H - 2) y = std::max(TOP + 2, (int)hoverY - h - 4);
  c.rect(x + 2, y + 2, w, h, 26); c.rect(x, y, w, h, MENU_BG); c.frame(x, y, w, h, 21);
  int ty = y + 3; if (!title.empty()) { c.text(title, x + 4, ty, 4); ty += 9; } for (auto& l : lines) { c.text(l, x + 4, ty, 9); ty += 8; } }

void Ui::typed(const std::string& t) { if (!ask) return; for (char ch : t) { const char u = (char)std::toupper((unsigned char)ch);
    if (((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || std::string(" #-/+.").find(u) != std::string::npos) && ask->text.size() < 24) ask->text += u; } }
// a name for a session, from two lists of quiet words (SAVE AS: the dice gives another)
std::string Ui::randomName() {
  /* describing words: nature, weather, colour, materials, machines, space, the city, moods, time, the odd strange one */
  static const char* A[] = {"SLOW", "PALE", "QUIET", "AMBER", "MOSS", "SALT", "HOLLOW", "DRIFTING", "SOFT", "DISTANT", "SILVER", "MISTY", "LOW", "WARM", "FADED", "STILL", "VELVET",
    "COPPER", "GLASS", "WINTER", "EVENING", "NORTHERN", "SLEEPY", "GENTLE", "DUSK", "OCHRE", "INDIGO", "LICHEN", "FERN", "TIDAL", "PAPER", "WOOLLEN", "CEDAR", "HAZY", "LATE",
    "NEON", "CHROME", "ELECTRIC", "MAGNETIC", "ORBITAL", "LUNAR", "SOLAR", "POLAR", "BINARY", "ANALOGUE", "DIGITAL", "STATIC", "BROKEN", "BORROWED", "FORGOTTEN", "SECRET",
    "INVISIBLE", "UPSIDE DOWN", "SIDEWAYS", "ENDLESS", "TINY", "ENORMOUS", "CURIOUS", "LAZY", "RESTLESS", "HUMMING", "WHISPERING", "SPINNING", "FLOATING", "FALLING", "RISING",
    "SUNDAY", "MIDNIGHT", "MORNING", "SUMMER", "AUTUMN", "SPRING", "OCTOBER", "APRIL", "TUESDAY", "YESTERDAY", "TOMORROW", "ANCIENT", "FUTURE", "RETRO", "VINTAGE",
    "CRYSTAL", "MARBLE", "CONCRETE", "PLASTIC", "RUBBER", "IRON", "CARBON", "COBALT", "VIOLET", "CRIMSON", "TEAL", "LEMON", "CORAL", "JADE", "ULTRAVIOLET", "INFRARED",
    "SUGAR", "HONEY", "PEPPER", "MINT", "CITRUS", "SMOKY", "FIZZY", "FROZEN", "MELTING", "BURNING", "WET", "DUSTY", "ELASTIC", "WOBBLY", "GLITCHY", "FUZZY", "CRACKLING",
    "URBAN", "SUBURBAN", "COASTAL", "ALPINE", "TROPICAL", "DESERT", "ARCTIC", "UNDERGROUND", "OVERHEAD", "ELSEWHERE", "PARALLEL", "MIRRORED", "HALF", "SECOND", "LAST", "LOST",
    "LONELY", "HAPPY", "WISTFUL", "DREAMY", "STRANGE", "COSMIC", "TELEPHONE", "RADIO", "CASSETTE", "PIXEL", "VECTOR", "ROBOT", "CLOCKWORK", "PAPERBACK", "MOTORWAY"};
  /* things and places */
  static const char* B[] = {"HARBOUR", "LANTERN", "MEADOW", "TIDE", "ORCHARD", "RIVER", "MOTH", "CHAPEL", "HILLS", "RAIN", "GARDEN", "FIELD", "LIGHT", "SHORE", "CLOUD", "FOREST",
    "WIRE", "BELLS", "ROOM", "WINDOW", "STATION", "ISLAND", "COAST", "SIGNAL", "DUNES", "VALLEY", "HEATH", "EMBERS", "ATLAS", "LAKE", "HORIZON", "PATH", "HAZE", "STARS", "PIER",
    "SATELLITE", "NEBULA", "COMET", "ORBIT", "PLANET", "MOON", "ECLIPSE", "GALAXY", "ROCKET", "ASTRONAUT", "TELESCOPE", "MACHINE", "ENGINE", "CIRCUIT", "TRANSISTOR", "TAPE",
    "RECORD", "JUKEBOX", "ARCADE", "TERMINAL", "ESCALATOR", "LIFT", "SUBWAY", "TRAMLINE", "ROOFTOP", "CAR PARK", "LAUNDERETTE", "DINER", "MOTEL", "LIBRARY", "MUSEUM",
    "AQUARIUM", "GREENHOUSE", "LIGHTHOUSE", "OBSERVATORY", "BALLROOM", "CORRIDOR", "STAIRWELL", "BASEMENT", "ATTIC", "KITCHEN", "BATHTUB", "TEACUP", "SPOON", "UMBRELLA",
    "BICYCLE", "KITE", "BALLOON", "ZEPPELIN", "SUBMARINE", "FERRY", "CARAVAN", "TRAIN", "POSTCARD", "LETTER", "PHOTOGRAPH", "MAP", "COMPASS", "ALARM", "CLOCK", "MIRROR",
    "WHALE", "OCTOPUS", "JELLYFISH", "HERON", "OWL", "FOX", "HARE", "CROW", "BEETLE", "SNAIL", "GOLDFISH", "PENGUIN", "CAMEL", "TORTOISE", "BEE", "SPARROW", "WOLF",
    "GLACIER", "VOLCANO", "CANYON", "CAVE", "REEF", "LAGOON", "WATERFALL", "FOG", "THUNDER", "LIGHTNING", "SNOWFALL", "MONSOON", "AURORA", "PRISM", "CRYSTALS", "MAGNET",
    "DREAM", "MEMORY", "PUZZLE", "RIDDLE", "ECHO", "WHISPER", "LULLABY", "PARADE", "CARNIVAL", "PICNIC", "HOLIDAY", "WEEKEND", "AFTERNOON", "SUNRISE", "STATIC", "PULSE"};
  static unsigned seed = (unsigned)std::time(nullptr) ^ (unsigned)std::clock();
  auto pick = [&](unsigned n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; };
  const unsigned nA = sizeof(A) / sizeof(*A), nB = sizeof(B) / sizeof(*B);
  for (int tries = 0; tries < 20; tries++) {
    const std::string a = A[pick(nA)], b = B[pick(nB)], b2 = B[pick(nB)];
    std::string s;
    switch (pick(9)) {                                   // mostly two words, sometimes another shape
      case 0: s = b + " OF " + b2; break;                  // LIGHTHOUSE OF FOG
      case 1: s = "THE " + a + " " + b; break;
      case 2: s = b + " " + std::to_string(1 + pick(99)); break;   // ORBIT 42
      default: s = a + " " + b; break;
    }
    if (s.size() <= 24 && a != b && b != b2) return s;    // the name box holds 24 letters
  }
  return std::string(A[0]) + " " + B[0];
}
void Ui::keyBackspace() { if (ask && !ask->text.empty()) ask->text.pop_back(); }
void Ui::keyEscape() { ask.reset(); }
void Ui::keyEnter() { if (!ask) return; auto a = std::move(ask); std::string t = a->text; while (!t.empty() && t.back() == ' ') t.pop_back(); if (!t.empty() && a->done) a->done(t); }

// ---------------- input ----------------
bool Ui::hasQwerty() const { for (auto& u : mods) if (u->m->def->type == "qwerty" && !u->m->mute.load()) return true; return false; }
void Ui::qwertyPlay(int i) {
  if (i < 0 || i >= 36) return;
  for (auto& u : mods) if (u->m->def->type == "qwerty" && !u->m->mute.load()) {
    Event ev; ev.note = (float)(12 * ((int)std::lround(u->m->p[1].load()) + 1) + i); ev.vel = u->m->p[0].load(); if (ev.note > 127) continue;
    eng.trigger(u->m->id, -1, ev); u->lit[i] = 0.2f; }
}
void Ui::keysPress(UiMod& u, int x, int y) {
  int kcw, kch; keysCell(u, kcw, kch); const int col = x / kcw, row = 3 - y / kch; if (col < 0 || col > 7 || row < 0 || row > 3) return;
  const int i = row * 8 + col, sc = scaleOf(u, eng), n = (int)scales()[sc].steps.size(), rowStep = 8;
  Event ev; ev.note = midiOf(degOf((int)u.m->p[1].load(), row * rowStep + col, sc), keyOf(u, eng), sc); ev.vel = u.m->p[0].load();
  eng.trigger(u.m->id, -1, ev); u.lit[i] = 0.25f;
}

void Ui::setBpm(float v) { const float b = std::round(clampf(v, 20, 240)); eng.tempo.bpm.store(b); char t[40]; std::snprintf(t, 40, "SHARED TEMPO %d BPM", (int)b); status(t, 1.5f); changed(); }
bool Ui::tempoDown(float sx, float sy) {   // true if the tap was inside the panel
  const int x = tp.x, y = tp.y; if (sx < x || sx >= x + TPW || sy < y || sy >= y + TPH) return false;
  const float bpm = eng.tempo.bpm.load();
  if (sy >= y + 10 && sy < y + 19) {
    if (sx < x + 15) setBpm(bpm - 1); else if (sx >= x + TPW - 15) setBpm(bpm + 1);
    else { drag.kind = D_TEMPO; pointerMove(sx, sy); }
  } else if (sy >= y + 20 && sy < y + 29) {
    if (sx >= x + 4 && sx < x + 25) { taps.erase(std::remove_if(taps.begin(), taps.end(), [&](float t) { return blink - t > 3; }), taps.end()); taps.push_back(blink);
      if (taps.size() > 1) setBpm(60.f / ((taps.back() - taps.front()) / (taps.size() - 1))); else status("KEEP TAPPING", 1.5f); }
    else if (sx >= x + 29 && sx < x + 46) setBpm(bpm - 10); else if (sx >= x + 50 && sx < x + 67) setBpm(bpm + 10);
  }
  return true;
}

void Ui::contextAt(float sx, float sy) {
  if (ask || br.open || ed.open || sy < TOP) return;
  const int wx = (int)sx + viewX, wy = (int)sy + viewY; int lx, ly; UiMod* u = hit(wx, wy, lx, ly); dd.reset(); menuOpen = false;
  if (!u) return; lastTouched = u->m->id; const int id = u->m->id;
  // what it offers: MIDI LEARN for the row under the pointer (or, on the title or the foot, for M, B and S), FORGET MIDI if it has one, and DUPLICATE
  std::vector<std::pair<std::string, std::function<void()>>> it; const ModuleDef* d = u->m->def; const Geom g = geom(*u);
  std::vector<std::pair<std::string, std::string>> rows; const int pj = paramAt(u, ly);
  if (pj >= 0) rows.push_back({d->params[pj].key, d->params[pj].label});
  else if (!d->fixed && (ly < HEAD || ly >= g.h - FOOT)) { const bool onOff = d->cat == Cat::Trig || d->cat == Cat::Mod; rows.push_back({"#mute", onOff ? "ON/OFF" : "MUTE"});
    if (bypassable(d)) rows.push_back({"#bypass", "BYPASS"}); if (d->cat == Cat::Voice) rows.push_back({"#solo", "SOLO"}); }
  for (auto& r : rows) { const std::string key = r.first, lab = r.second;
    it.push_back({"MIDI LEARN: " + lab, [this, id, key] { if (UiMod* m = byId(id)) startLearn(m, key); }});
    if (mapFor(*u, key)) it.push_back({"FORGET MIDI: " + lab, [this, id, key, lab] { if (UiMod* m = byId(id)) { forgetMap(*m, key); status("MIDI: " + lab + " LET GO", 2); } }}); }
  if (!d->fixed) it.push_back({"DUPLICATE", [this, id] { if (UiMod* m = byId(id)) if (onDuplicate) onDuplicate(m); }});
  if (it.empty()) return;
  Dropdown q; for (auto& e : it) q.items.push_back(e.first); q.x = (int)sx + 2; q.y = (int)sy + 2;
  q.pick = [it](int i) { if (i >= 0 && i < (int)it.size()) it[i].second(); }; openDropdown(q);
}
void Ui::pointerDown(float sx, float sy) {
  hoverT = -1;   // a tap hides the note until the pointer moves on
  const int wx = (int)sx + viewX, wy = (int)sy + viewY;
  { int lx0, ly0; if (sy >= TOP && !ask) if (UiMod* hu = hit(wx, wy, lx0, ly0)) lastTouched = hu->m->id; }
  drag = Drag(); drag.sx = sx; drag.sy = sy; drag.wx = wx; drag.wy = wy;
  if (ask) {   // OK / CANCEL in the name box
    const int w = std::min(lastW - 10, 150), x = (lastW - w) / 2, y = TOP + 30;
    if (sy >= y + 22 && sy <= y + 32) { if (sx >= x + 3 && sx <= x + 16) keyEnter(); else if (sx >= x + 21 && sx <= x + 50) keyEscape(); else if (ask->dice && sx >= x + w - 54) ask->text = randomName(); }
    return;
  }
  if (selOn) { const int bw = Canvas::tw("UNPLUG") + 6; selOn = false; selT = 0.3f;   // the picked cable's UNPLUG button; any other tap just lets go of it
    if (sx >= ubx - 1 && sx < ubx + bw + 1 && sy >= uby - 1 && sy < uby + 10) { selOn = true; unplugSelected(); return; } }
  if (tp.open) { if (!tempoDown(sx, sy)) tp.open = false; return; }   // a tap away closes it
  if (br.open) { if (!browserDown(sx, sy)) closeBrowser(); return; }
  if (ed.open) {   // the editor stays open until CLOSE (or Escape); the bar's UNDO and REDO still work under it
    if (sy < TOP) for (auto& b : barHits) if ((b.label == "UNDO" || b.label == "REDO") && sx >= b.x0 - 2 && sx <= b.x1 + 2) { b.fn(); return; }
    editorDown(sx, sy); return; }
  if (dd) {   // a tap picks from the open list, or closes it
    auto d0 = std::move(dd); const int r = (int)std::floor((sy - d0->y - 1) / 8), col = (int)std::floor((sx - d0->x + 2) / std::max(1, d0->colW)), i = col * d0->rows + r;
    const bool hit = sx >= d0->x - 2 && sx < d0->x - 2 + d0->w && r >= 0 && r < d0->rows && i >= 0 && i < (int)d0->items.size() && i != d0->title;
    if (hit && d0->stay) { d0->cur = i; auto pk = d0->pick; dd = std::move(d0); if (pk) pk(i); return; }   // stays open: try another, tap away when done
    if (hit && d0->pick) d0->pick(i);
    return;
  }
  if (menuOpen) { for (auto& it : menuItems) if (sx >= it.x - 1 && sx < it.x + it.w && sy >= it.y - 1 && sy < it.y + 7) {
      UiMod* u = add(it.type, viewX + 120 + (int)(std::rand() % 40), viewY + TOP + 20 + (int)(std::rand() % 30)); if (u) status("ADDED " + u->m->def->name); changed(); break; }
    menuOpen = false; return; }
  if (sy < TOP) { for (auto& b : barHits) if (sx >= b.x0 - 2 && sx <= b.x1 + 2) { b.fn(); return; } return; }
  // jacks: drag a cable out of an output. A plugged input or socket: tap to pick its cable (then UNPLUG), or drag the cable to another jack.
  // Nothing comes out by accident: a cable dropped anywhere else springs back to where it was
  const JackHit j = jackAt(wx, wy);
  if (j.u) {
    drag.kind = D_CABLE;
    if (j.out) { drag.from = j; return; }
    for (int k = (int)cables.size() - 1; k >= 0; k--) { const UiCable& cb = cables[k];
      const bool mine = j.param >= 0 ? (cb.type == PType::Mod && cb.to == j.u->m->id && cb.toParam == j.param) : (cb.type != PType::Mod && cb.to == j.u->m->id && cb.toPort == j.port);
      if (mine) { drag.kind = D_PULL; drag.grab = cb; return; } }
    drag.from = j; drag.rev = true; return;
  }
  int lx, ly; UiMod* u = hit(wx, wy, lx, ly);
  trFocus = u && u->m->def->type == "tracker" ? u->m->id : 0;   // the arrow keys work on a Tracker once you touch it
  // a cable under the pointer: tap to see it, pull it away to unplug it. Title bars and a module's own controls still win
  if (!u) {   // cables run behind modules, so only where you can see them
    bool onControl = false;
    if (u) { const Geom g0 = geom(*u); const ModuleDef* d0 = u->m->def;
      onControl = (!d0->fixed && ly >= g0.h - FOOT) || paramAt(u, ly) >= 0 || (d0->vis && ly >= g0.vy && ly < g0.vy + g0.vh && (d0->type == "keys" || d0->type == "xfade" || d0->type == "tracker" || d0->type == "sample" || (d0->type == "tombola" && ly < g0.vy + 7 && lx >= g0.w - 30))); }
    const int ci = onControl ? -1 : cableAt(wx, wy, u ? 2.5f : 4.f);
    if (ci >= 0) { drag.kind = D_GRAB; drag.grab = cables[ci]; return; }
  }
  if (!u) { drag.kind = D_PAN; drag.vx0 = viewX; drag.vy0 = viewY; return; }
  // bring it to the front
  for (size_t i = 0; i < mods.size(); i++) if (mods[i].get() == u) { auto p = std::move(mods[i]); mods.erase(mods.begin() + i); mods.push_back(std::move(p)); break; }
  const Geom g = geom(*u); const ModuleDef* d = u->m->def;
  if (ly < HEAD) {
    if (!d->fixed && (outInHead(*u) && !jacksIn ? lx >= g.w - 12 && lx < g.w - 4 : lx >= g.w - 8)) {   // delete, after asking (the x moves left of an OUT on the edge)
      Dropdown q; q.items = {"DELETE " + d->name + "?", "YES, DELETE", "NO, KEEP IT"}; q.title = 0; q.danger = 1; q.x = u->x - viewX + g.w - 40; q.y = u->y - viewY + HEAD;
      q.pick = [this, u](int i) { if (i == 1) { status("REMOVED " + u->m->def->name); removeGently(u); changed(); } }; openDropdown(q); return; }
    drag.kind = D_MOVE; drag.m = u; drag.ox = wx - u->x; drag.oy = wy - u->y; return; }
  if (!d->fixed && ly >= g.h - FOOT) {
    const bool onOff = d->cat == Cat::Trig || d->cat == Cat::Mod;
    if (onOff && lx >= 3 && lx < 18) { u->m->mute.store(!u->m->mute.load()); status(d->name + (u->m->mute.load() ? " OFF" : " ON")); changed(); return; }
    if (!onOff && lx >= 3 && lx < 12) { u->m->mute.store(!u->m->mute.load()); status(std::string(u->m->mute.load() ? "MUTED " : "UNMUTED ") + d->name); changed(); return; }
    if (bypassable(d) && lx >= 14 && lx < 23) { u->m->bypass.store(!u->m->bypass.load());
      status(u->m->bypass.load() ? "BYPASS " + d->name + ": THE DRY SOUND GOES STRAIGHT THROUGH" : d->name + " BACK IN", 3); changed(); return; }
    if (d->cat == Cat::Voice && lx >= 14 && lx < 23) { u->m->solo.store(!u->m->solo.load());
      bool any = false; for (auto& up : mods) if (up->m->def->cat == Cat::Voice && up->m->solo.load()) any = true;
      status(u->m->solo.load() ? "SOLO " + d->name + ": OTHER VOICES SILENT, EFFECTS STILL WORKING" : any ? "UNSOLO " + d->name : "SOLO OFF: EVERY VOICE PLAYS", 3); changed(); return; }
    if (lx >= g.w - 13 && lx < g.w - 2) { u->fold = !u->fold; changed(); return; }
  }
  // the module's own display (Note Grid pads, the Output's crossfader)
  if (d->vis && ly >= g.vy && ly < g.vy + g.vh) {
    const int vx = lx - 3, vy = ly - g.vy;
    if (d->type == "keys") { keysPress(*u, vx, vy); return; }
    if (d->type == "journey" && vy < 17) {   // KEY JOURNEY: a step's key (top), or its own scale (below)
      const int N = std::max(1, std::min(8, (int)std::lround(u->m->p[0].load()))), sw = std::min(16, (g.w - 6 + 1) / N), i = vx / std::max(1, sw); if (i < 0 || i >= N) return;
      Module* m = u->m; Dropdown q; q.x = u->x - viewX + 3 + i * sw; q.y = u->y - viewY + g.vy + (vy < 9 ? 9 : 17); const int id = m->id;
      if (vy < 9) { q.items.push_back("EMPTY (SKIPPED)"); for (int k = 0; k < 12; k++) q.items.push_back(KEY_NAMES[k]); q.cur = (int)std::lround(m->seq[Module::si(i, 0)].load()) + 1;
        q.pick = [this, id, i](int j) { if (UiMod* um = byId(id)) { um->m->seq[Module::si(i, 0)].store((float)(j - 1)); changed(); } }; }
      else { q.items.push_back("HOME SCALE"); for (auto& sc : scales()) q.items.push_back(sc.name); const float sv = m->seq[Module::si(i, 0) + 1].load(); q.cur = sv >= 10 ? (int)std::lround(sv) - 9 : 0;
        q.pick = [this, id, i](int j) { if (UiMod* um = byId(id)) { um->m->seq[Module::si(i, 0) + 1].store(j == 0 ? 0.8f : (float)(9 + j)); changed(); } }; }
      openDropdown(q); return; }
    if (d->type == "euclid") {   // tap a ring to choose that lane; drag it up or down for HITS, sideways for ROTATE
      Module* m = u->m; const int w = g.w - 6, l = std::max(0, std::min(3, vx * 4 / std::max(1, w))); static const char* NM[4] = {"KICK", "SNARE", "HAT", "PERC"};
      m->p[d->paramIndex("lane")].store((float)l); status(std::string("LANE: ") + NM[l] + "  (DRAG UP OR DOWN FOR HITS, SIDEWAYS TO ROTATE)", 2);
      const std::string k = std::to_string(l + 1); drag.kind = D_EUCLID; drag.m = u; drag.k = l; drag.n0 = m->p[d->paramIndex("hi" + k)].load(); drag.dir = (int)std::lround(m->p[d->paramIndex("ro" + k)].load()); changed(); return; }
    if (d->type == "nloop") {   // grab the start or end line (whichever is nearer, within a few pixels)
      Module* m = u->m; const int n = m->evN.load(); if ((int)m->disp[1].load() == 1 || n == 0) return; const float S = m->disp[6].load(), L = std::max(0.01f, m->disp[2].load());
      float tmax = 0; for (int i = 0; i < n; i++) tmax = std::max(tmax, m->ev[i * 4].load()); const float span = std::max(S + L, tmax + 0.25f); const int w = g.w - 6;
      const int xs = (int)std::lround(S / span * (w - 2)), xe = std::min(w - 2, (int)std::lround((S + L) / span * (w - 2))), ds = std::abs(vx - xs), de = std::abs(vx - xe);
      if (std::min(ds, de) > 4) return; drag.kind = D_NLOOP; drag.m = u; drag.k = m->id; drag.dir = de <= ds ? 1 : 0; drag.n0 = span; return; }
    if (d->type == "qwerty" && vy >= 7) {   // pick a note, then press the key for it
      const int cw = (g.w - 12) / 12, k = (vx - 6) / std::max(1, cw), r = 2 - (vy - 7) / 8; if (vx < 6 || k < 0 || k > 11 || r < 0 || r > 2) return;
      const int i = r * 12 + k; static const char* NM[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
      if (qwLearn == i) { qwLearn = -1; status("NO CHANGE", 1); return; }
      qwLearn = i; status(std::string("PRESS A KEY FOR ") + NM[k] + std::to_string(r) + "  (DELETE: NO KEY, ESCAPE: LEAVE IT)", 6); return; }
    if (d->type == "xfade") { drag.kind = D_XFADE; drag.m = u; pointerMove(sx, sy); return; }
    if (d->type == "sample" && (int)u->m->p[1].load() >= 2 && vy < 6) {   // KIT / MULTI: a sound's place
      const int k = std::min(UiMod::SLOTS - 1, std::max(0, lx - 3) / 6); u->shown = k; const bool has = !u->snd[k].id.empty(), multi = (int)u->m->p[1].load() == 3;
      Dropdown z; z.x = u->x - viewX + 4; z.y = u->y - viewY + g.vy + 6;
      z.items = {"SOUND " + std::to_string(k + 1) + (has ? "  " + (u->snd[k].name.empty() ? std::string("(CAUGHT)") : u->snd[k].name.substr(0, 20)) : std::string("  (EMPTY)")), has ? "REPLACE..." : "LOAD..."};
      if (has && multi) z.items.push_back("ROOT  " + noteName((int)u->snd[k].root)); if (has) z.items.push_back("REMOVE");
      z.title = 0; const int id = u->m->id;
      z.pick = [this, id, k, has, multi](int i) { UiMod* t = byId(id); if (!t) return;
        if (i == 1) { if (onAction) onAction(t, "load:" + std::to_string(k)); return; }
        if (has && multi && i == 2) { Dropdown r; r.x = t->x - viewX + 4; r.y = TOP + 2; for (int n = 24; n <= 96; n++) r.items.push_back(noteName(n)); r.cur = (int)t->snd[k].root - 24;
          r.pick = [this, id, k](int j) { UiMod* t2 = byId(id); if (!t2) return; t2->snd[k].root = (float)(24 + j); t2->m->roots[k].store((float)(24 + j)); status("SOUND " + std::to_string(k + 1) + " ROOT " + noteName(24 + j)); changed(); }; openDropdown(r); return; }
        if (has) { eng.setBuffer(id, nullptr, k); t->snd[k] = UiMod::Snd(); status("REMOVED SOUND " + std::to_string(k + 1)); changed(); } };
      openDropdown(z); return; }
    if (d->type == "tracker") {   // tap a step (and column) to put the cursor there; drag up and down to scroll. < > move along the columns
      u->hold = 6; trFocus = u->m->id; const int head = trHead(u->m), nc = trCols(u->m), vis = std::min(TR_VIS, nc), cw = (g.w - 6 - 16) / vis;
      if (head && vy < head) { if (vx < 8) { u->colScroll = std::max(0, u->colScroll - 1); return; } if (vx >= g.w - 6 - 8) { u->colScroll = std::min(std::max(0, nc - TR_VIS), u->colScroll + 1); return; }
        if (vx >= 16) {   // a column's number: drag up or down for its RANDOM NOTE; tap twice to set it back to 0
          const int col = std::min(nc - 1, u->colScroll + std::min(vis - 1, (vx - 16) / std::max(1, cw))), wi = d->paramIndex("w" + std::to_string(col + 1));
          if (wi >= 0) {
            if (wanderTapM == u && wanderTapK == wi && blink - wanderTapT < 0.4f) { u->m->p[wi].store(0); wanderTapM = nullptr; status("COLUMN " + std::to_string(col + 1) + " RANDOM NOTE OFF", 1.5f); return; }
            wanderTapM = u; wanderTapK = wi; wanderTapT = blink;
            drag.kind = D_WANDER; drag.m = u; drag.k = wi; drag.n0 = u->m->p[wi].load(); return; } }
        return; }
      const int r = (int)u->scroll + std::max(0, (vy - head - 1) / 6), col = vx >= 16 ? std::min(nc - 1, u->colScroll + std::min(vis - 1, (vx - 16) / std::max(1, cw))) : u->m->cursorCol.load();
      if (r < trLen(u->m)) { u->m->cursor.store(r); u->m->cursorCol.store(col); char b[40]; std::snprintf(b, 40, "CURSOR AT STEP %03d  COLUMN %d", r + 1, col + 1); status(b, 1.2f); }
      drag.kind = D_SCROLL; drag.m = u; drag.n0 = u->scroll; return; }
  }
  const int pj = paramAt(u, ly);
  if (pj >= 0) {
    const ParamDef& q = d->params[pj];
    if (q.act) {
      if (d->type == "tracker") { u->hold = 6; trShow(*u, u->m->cursor.load()); }
      if (d->type == "tracker" && q.key == "clear") {   // ask first: this wipes every step
        Dropdown z; z.items = {"CLEAR EVERY STEP?", "YES, CLEAR ALL", "NO, KEEP THEM"}; z.title = 0; z.danger = 1; z.x = u->x - viewX + 8; z.y = u->y - viewY + g.py + rowPx(*u, pj) + 4;
        z.pick = [this, u](int i) { if (i == 1) { eng.action(u->m->id, "clear"); u->scroll = 0; status("TRACKER CLEARED"); } }; openDropdown(z); return; }
      if (d->type == "listen" && q.key == "usetempo") {   // the session's tempo to what it hears
        const float bp = u->m->disp[19].load(); if (bp <= 0) { status("NO CLEAR TEMPO HEARD YET (IT NEEDS A FEW SECONDS OF A BEAT)", 3); return; }
        setBpm(bp); status("TEMPO " + std::to_string((int)std::lround(bp)) + " BPM: CLOCKS AND SEQUENCERS ON SHARED TEMPO FOLLOW IT", 4); changed(); return; }
      if (d->type == "listen" && q.key == "use") {   // set the app's key and scale to what it hears
        if (u->m->disp[2].load() <= 0) { status("NOTHING HEARD YET"); return; }
        const int r = (int)u->m->disp[0].load(), sc = (int)u->m->disp[1].load(); eng.key.root.store(r); eng.key.scale.store(sc);
        status(std::string("KEY ") + KEY_NAMES[r % 12] + " " + scales()[sc].name + ": THE SEQUENCERS FOLLOW IT NOW", 4); changed(); return; }
      if (q.key == "clear" && u->clearAsk <= 0) { u->clearAsk = 2.5f; status(d->name + ": TAP CLEAR AGAIN TO CLEAR", 2.5f); return; }   // it wipes things: a second tap
      if (q.key == "clear") { u->clearAsk = 0; status(d->name + " CLEARED", 1.5f); }
      if (onAction && onAction(u, q.key)) return;
      eng.action(u->m->id, q.key); return; }
    if (!q.opts.empty()) {
      if (q.opts.size() == 2 && (q.play || q.tog || q.opts[0] == "OFF")) { const float v = u->m->p[pj].load() > 0.5f ? 0.f : 1.f; u->m->p[pj].store(v); status(d->name + "  " + q.label + "  " + q.opts[(int)v]); changed(); return; }
      Dropdown l; l.items = q.opts; l.cur = (int)std::lround(u->m->p[pj].load()); l.x = u->x - viewX + 12; l.y = u->y - viewY + g.py + rowPx(*u, pj) + 4;
      if (typeInfo(d->type, q.key) && typeInfo(d->type, q.key)->from < (int)q.opts.size()) {   // the light types, then the heavy ones under a heading
        const int H = typeInfo(d->type, q.key)->from; l.items.insert(l.items.begin() + H, "HEAVIER (MORE CPU):"); l.title = H; if (l.cur >= H) l.cur++;
        l.pick = [this, u, pj, H](int i) { if (i > H) i--; verbTypeSet(u, pj, u->m->p[pj].load(), (float)i); u->m->p[pj].store((float)i); status(u->m->def->name + "  " + u->m->def->params[pj].label + "  " + u->m->def->params[pj].opts[i] + (i >= H ? "  (HEAVY: USES MORE CPU)" : "  (LIGHT)")); changed(); };
        openDropdown(l); return; }
      l.pick = [this, u, pj](int i) { verbTypeSet(u, pj, u->m->p[pj].load(), (float)i); u->m->p[pj].store((float)i); const ParamDef& qq = u->m->def->params[pj]; status(u->m->def->name + "  " + qq.label + "  " + qq.opts[i] + (qq.key == "chan" ? (u->m->def->type == "sample" ? "  (FOR THE NEXT SOUND YOU LOAD OR RECORD)" : "  (FOR THE NEXT SOUND YOU LOAD OR CATCH)") : ""), qq.key == "chan" ? 4.f : 2.f); changed(); };
      openDropdown(l); return; }
    if (lx >= minusX(*u, pj) - 2 && lx < minusX(*u, pj) + 5) { drag.kind = D_STEP; drag.m = u; drag.k = pj; drag.dir = -1; drag.holdT = 0; drag.holdSum = 0; nudge(u, pj, -1); return; }
    if (lx >= g.w - 8) { drag.kind = D_STEP; drag.m = u; drag.k = pj; drag.dir = 1; drag.holdT = 0; drag.holdSum = 0; nudge(u, pj, 1); return; }
    takeOwnTempo(u, pj, eng); drag.kind = D_PARAM; drag.m = u; drag.k = pj; drag.n0 = q.toNorm(u->m->p[pj].load()); return;
  }
  drag.kind = D_MOVE; drag.m = u; drag.ox = wx - u->x; drag.oy = wy - u->y;
}

// a CLOCK on SHARED tempo: its BPM is the session's tempo, shown on the top bar
static void clockToTempo(UiMod* u, int k, Engine& eng) { if (u->m->def->type == "clock" && u->m->def->params[k].key == "bpm" && u->m->p[0].load() > 0.5f) eng.tempo.bpm.store(std::round(u->m->p[k].load())); }
// - and +: one step for settings that move in steps, otherwise about a hundredth of the way along
void Ui::nudge(UiMod* u, int k, int dir) { u->valK = k; u->valT = 1.2f;   /* - and +: the value shows where the name was, for a moment */
  takeOwnTempo(u, k, eng);
  const ParamDef& q = u->m->def->params[k]; const float v0 = u->m->p[k].load();
  float v = q.step > 0 ? clampf(v0 + dir * q.step, std::min(q.min, q.max), std::max(q.min, q.max)) : q.fromNorm(q.toNorm(v0) + dir * q.nudge);
  if (q.step > 0 && q.rev) v = clampf(v0 - dir * q.step, q.min, q.max);
  if (q.step > 0) v = std::round(v / q.step) * q.step;   // onto the steps (a value from an older session may sit between them)
  verbTypeSet(u, k, v0, v); u->m->p[k].store(v); clockToTempo(u, k, eng); status(u->m->def->name + "  " + statusLabel(*u, q) + "  " + valueText(*u, k, v, eng.key.root.load()), 1.5f);
}
void Ui::selectCable(const UiCable& c, float sx, float sy) {
  sel = c; selOn = true; selT = 1; ubx = (int)sx + 4; uby = (int)sy - 12; status(cableName(c), 3);
}
bool Ui::unplugSelected() {
  if (!selOn) return false; selOn = false; selT = 0;
  for (auto& cb : cables) if (sameCable(cb, sel)) { const UiCable c0 = cb; unplug(c0); status("UNPLUGGED " + cableName(c0), 2.5f); changed(); return true; }
  return false;
}
void Ui::pointerMove(float sx, float sy) {
  if (std::fabs(sx - hoverX) + std::fabs(sy - hoverY) > 1.5f) { hoverX = sx; hoverY = sy; hoverT = 0; }   // the pointer moved: the note goes, and the wait starts again
  const int wx = (int)sx + viewX, wy = (int)sy + viewY; drag.wx = wx; drag.wy = wy;
  if (drag.kind == D_PAN) { viewX = drag.vx0 - (int)(sx - drag.sx); viewY = drag.vy0 - (int)(sy - drag.sy); }
  else if (drag.kind == D_MOVE) { drag.m->x = wx - drag.ox; drag.m->y = std::max(viewY + TOP + 1, wy - drag.oy);
    // an unpatched effect dragged over an audio cable can be dropped into it
    const ModuleDef* d = drag.m->m->def; bool free = d->inIndex("IN") >= 0 && d->outIndex("OUT") >= 0 && d->ins[d->inIndex("IN")].type == PType::Audio && d->outs[d->outIndex("OUT")].type == PType::Audio;
    for (auto& cb : cables) if (cb.from == drag.m->m->id || cb.to == drag.m->m->id) free = false;
    drag.splice = free ? cableAt(wx, wy, 5, true) : -1; if (drag.splice >= 0) status("DROP TO INSERT INTO " + cableName(cables[drag.splice]), 0.3f); }
  else if (drag.kind == D_GRAB || drag.kind == D_PULL) {
    if (std::hypot(sx - drag.sx, sy - drag.sy) > 5) {   // moved far enough to mean it: carry the cable's end (it stays plugged in until it lands)
      const UiCable c0 = drag.grab; UiMod* src = byId(c0.from);
      if (src) { drag.kind = D_CABLE; drag.from = {src, true, c0.fromPort, c0.type, outJackX(*src), src->y + outJackY(*src, c0.fromPort)}; drag.rev = false; drag.hasOrig = true; drag.orig = c0;
        status("DROP IT ON ANOTHER " + std::string(c0.type == PType::Mod ? "SETTING" : "INPUT") + " TO MOVE IT", 2); } else drag.kind = D_NONE; } }
  else if (drag.kind == D_PARAM && !drag.moved && !drag.held && std::fabs(sx - drag.sx) < 2) {}   // not moved far enough to count as a drag yet
  else if (drag.kind == D_PARAM && drag.held) {}
  else if (drag.kind == D_PARAM) { drag.moved = true; const ParamDef& q = drag.m->m->def->params[drag.k]; const float v = q.fromNorm(drag.n0 + (sx - drag.sx) / (q.step > 0 && !q.exp ? 40.f : 70.f));
    verbTypeSet(drag.m, drag.k, drag.m->m->p[drag.k].load(), v); drag.m->m->p[drag.k].store(v); clockToTempo(drag.m, drag.k, eng); status(drag.m->m->def->name + "  " + statusLabel(*drag.m, q) + "  " + valueText(*drag.m, drag.k, v, eng.key.root.load()), 1.5f); }
  else if (drag.kind == D_TEMPO) { const float tx = tp.x + 17.f, tw = TPW - 34.f; const float b = std::round(20 + clampf((sx - tx) / (tw - 1), 0, 1) * 220); if ((int)b != (int)std::lround(eng.tempo.bpm.load())) { eng.tempo.bpm.store(b); char t[40]; std::snprintf(t, 40, "SHARED TEMPO %d BPM", (int)b); status(t, 1.5f); } }
  else if (drag.kind == D_EDIT) editorMove(sx, sy);
  else if (drag.kind == D_BROWSE) { if (std::fabs(sy - drag.sy) > 2) drag.moved = true; if (drag.moved) { br.scroll = drag.n0; browserScroll(-(sy - drag.sy) / 8); } }
  else if (drag.kind == D_XFADE) { const Geom g = geom(*drag.m); const float x = (float)(wx - drag.m->x - 3); const float v = clampf((x - 6) / (g.w - 6 - 12), 0, 1);
    drag.m->m->p[drag.m->m->def->paramIndex("fade")].store(v); char b[32]; std::snprintf(b, 32, "FADE  A %d / B %d", (int)std::lround((1 - v) * 100), (int)std::lround(v * 100)); status(b, 1); }
  else if (drag.kind == D_EUCLID) {   // EUCLID DRUMS: up for more HITS, sideways to ROTATE (about 6 pixels a step)
    Module* m = drag.m->m; const ModuleDef* d = m->def; const std::string k = std::to_string(drag.k + 1); const int st = (int)std::lround(m->p[d->paramIndex("st" + k)].load());
    const float h = std::max(0.f, std::min((float)st, std::round(drag.n0 + (drag.sy - sy) / 6))); const int ro = (((drag.dir + (int)std::lround((sx - drag.sx) / 6)) % st) + st) % st;
    if (h != m->p[d->paramIndex("hi" + k)].load() || ro != (int)m->p[d->paramIndex("ro" + k)].load()) { m->p[d->paramIndex("hi" + k)].store(h); m->p[d->paramIndex("ro" + k)].store((float)ro);
      status(std::to_string((int)h) + " HITS IN " + std::to_string(st) + "  ROTATE " + std::to_string(ro), 1.2f); } }
  else if (drag.kind == D_WANDER) {   // up for more, about 6 pixels a step
    const float v = std::max(0.f, std::min(7.f, std::round(drag.n0 + (drag.sy - sy) / 6))); if (v != drag.m->m->p[drag.k].load()) { drag.m->m->p[drag.k].store(v);
      const int col = drag.k - drag.m->m->def->paramIndex("w1") + 1; status("COLUMN " + std::to_string(col) + (v > 0 ? " RANDOM NOTE: UP TO " + std::to_string((int)v) + " STEP" + (v > 1 ? "S" : "") + " OF THE SCALE EITHER WAY" : " RANDOM NOTE OFF"), 1.5f); } }
  else if (drag.kind == D_SCROLL) { drag.m->hold = 6; drag.m->scroll = drag.n0 - (sy - drag.sy) / 6; trClamp(*drag.m); }
  else if (drag.kind == D_NLOOP) {   // NOTE LOOPER: the start or end line, onto the QUANTISE grid (1/16 when off; Shift: anywhere)
    UiMod& u = *drag.m; Module* m = u.m; const int w = geom(u).w - 6, vx = (int)std::lround(wx) - u.x - 3; const float span = drag.n0;
    float t = clampf((float)vx / std::max(1, w - 2) * span, 0, 256); static const float GR[4] = {0.25f, 0.25f, 0.5f, 1};
    const float g = shiftHeld ? 0.01f : GR[std::max(0, std::min(3, (int)m->p[m->def->paramIndex("quant")].load()))]; t = std::round(t / g) * g;
    const int si = m->def->paramIndex("start"), li = m->def->paramIndex("lenb"); const float S = m->p[si].load(), E = S + m->p[li].load();
    if (drag.dir == 0) { const float S2 = clampf(t, 0, std::max(0.f, E - 0.25f)); m->p[si].store(std::min(128.f, S2)); m->p[li].store(clampf(E - S2, 0.25f, 128)); status("NOTE LOOPER  START  " + valueText(u, si, m->p[si].load(), 0), 1.5f); }
    else { m->p[li].store(clampf(t - S, 0.25f, 128)); status("NOTE LOOPER  LENGTH  " + valueText(u, li, m->p[li].load(), 0), 1.5f); } }
}

// drop a module into a cable (once you've said yes): the cable's sound goes into it and out on to where the cable went
void Ui::insertInto(const UiCable& c0, int mid) { UiMod *a = byId(c0.from), *b = byId(c0.to), *m = byId(mid); bool still = false; for (auto& cb : cables) if (sameCable(cb, c0)) still = true;
      if (!a || !b || !m || !still) { status("THAT CABLE HAS GONE", 3); return; }
      // its IN (or, with none, its first sound input: CROSSFADER's A, MIXER's 1) and its OUT (or its first sound output)
      const ModuleDef* md = m->m->def; int ii = md->inIndex("IN"), oi = md->outIndex("OUT");
      if (ii < 0) for (int k = 0; k < (int)md->ins.size() && ii < 0; k++) if (md->ins[k].type == PType::Audio) ii = k;
      if (oi < 0) for (int k = 0; k < (int)md->outs.size() && oi < 0; k++) if (md->outs[k].type == PType::Audio) oi = k;
      if (ii < 0 || oi < 0) { status(md->name + " HAS NO SOUND IN AND OUT TO INSERT", 3); return; }
      unplug(c0); connect(a, c0.fromPort, m, ii); connect(m, oi, b, c0.toPort);
      const int mi = md->paramIndex("mix"); if (mi >= 0) m->m->p[mi].store(0);   // fully dry, as the web version does, so nothing jumps out
      if (md->type == "xfade") { m->m->p[md->paramIndex("fade")].store(0); m->m->p[md->paramIndex("move")].store(0); }   // CROSSFADER: all A and still, so nothing changes until B is plugged in
      status("INSERTED " + md->name + (mi >= 0 ? " (FULLY DRY: RAISE ITS MIX)" : md->type == "xfade" ? " (ALL A: PLUG SOMETHING INTO B)" : ""), 4); changed(); }

void Ui::pointerUp(float sx, float sy) {
  const int wx = (int)sx + viewX, wy = (int)sy + viewY; const Drag d = drag; drag = Drag();
  if (d.kind == D_TEMPO) { changed(); return; }
  if (d.kind == D_PARAM && !d.moved && !d.held && d.m) { tapRow(d.m, d.k); }   // a tap that didn't drag: three or more in rhythm set a speed row
  if (d.kind == D_NLOOP) { changed(); return; }
  if (d.kind == D_BROWSE) { if (!d.moved) browserPick(d.k); return; }
  if (d.kind == D_GRAB || d.kind == D_PULL) { selectCable(d.grab, sx, sy); return; }
  if (d.kind == D_MOVE && d.splice >= 0 && d.splice < (int)cables.size()) {   // drop an effect into a cable
    // only if you say so: dropping a module on a cable asks first. Then the cable crossfades over to the new route
    const UiCable c0 = cables[d.splice]; const int mid = d.m->m->id; changed();
    Dropdown q; q.items = {"INSERT INTO THIS CABLE?", "YES, INSERT IT", "NO, JUST PUT IT HERE"}; q.title = 0; q.x = (int)sx + 4; q.y = (int)sy + 4;
    q.pick = [this, c0, mid](int i) { if (i == 1) insertInto(c0, mid); };
    openDropdown(q); return; }
  if (d.kind == D_PARAM && !d.moved && !d.held && (d.m->m->def->params[d.k].key == "pitch" || ((d.m->m->def->type == "midiout" || d.m->m->def->type == "nloop") && d.m->m->def->params[d.k].key == "trans"))) {
    // a tap on PITCH: jump by octaves (on Sample, pitch is speed, so the speeds are named too)
    const ModuleDef* df = d.m->m->def; const int k = d.k, id = d.m->m->id; const bool smp = df->type == "sample", grains = df->type == "looper";
    static const int V[5] = {-24, -12, 0, 12, 24};
    static const char* SMP[5] = {"QUARTER SPEED  -24", "HALF SPEED  -12", "NORMAL SPEED  0", "DOUBLE SPEED  +12", "4X SPEED  +24"};
    static const char* GRN[5] = {"-24  GRAINS 2 OCT DOWN", "-12  GRAINS 1 OCT DOWN", "0  AS RECORDED", "+12  GRAINS 1 OCT UP", "+24  GRAINS 2 OCT UP"};
    static const char* NRM[5] = {"-24  2 OCTAVES DOWN", "-12  1 OCTAVE DOWN", "0", "+12  1 OCTAVE UP", "+24  2 OCTAVES UP"};
    Dropdown z; z.x = d.m->x - viewX + 30; z.y = d.m->y - viewY + geom(*d.m).py + rowPx(*d.m, k) + 4;
    for (int i = 0; i < 5; i++) { z.items.push_back(smp ? SMP[i] : grains ? GRN[i] : NRM[i]); if ((int)std::lround(d.m->m->p[k].load()) == V[i]) z.cur = i; }
    z.pick = [this, id, k, smp](int i) { UiMod* t = byId(id); if (!t) return; t->m->p[k].store((float)V[i]);
      status(t->m->def->name + " " + t->m->def->params[k].label + " " + (smp ? std::string(SMP[i]) : std::to_string(V[i])), 2); changed(); };
    openDropdown(z); return; }
  if (d.kind == D_PARAM && !d.moved && !d.held) { const ParamDef& q = d.m->m->def->params[d.k]; status(d.m->m->def->name + "  " + statusLabel(*d.m, q) + "  " + valueText(*d.m, d.k, d.m->m->p[d.k].load(), eng.key.root.load()) + "  (DRAG, OR TAP - +)", 2.5f); return; }
  if (d.kind == D_MOVE && !d.moved && d.m && d.m->m->def->type == "tombola") {   // TOMBOLA: a tap on SHAKE (its input's name, or the drum's corner) shakes it
    const int si = d.m->m->def->inIndex("SHAKE"), ly = d.oy, lx = d.ox; if (si >= 0 && ly >= jackY(si) - 3 && ly <= jackY(si) + 3 && lx < 40) { eng.action(d.m->m->id, "shake"); d.m->shakeT = 0.25f; status("SHAKE!", 1.5f); return; } }
  if (d.kind == D_MOVE || d.kind == D_PARAM || d.kind == D_XFADE || d.kind == D_STEP) { changed(); return; }
  if (d.kind != D_CABLE) return;
  bool made = false;
  [&]() {
  if (!d.rev && d.from.type == PType::Mod) {   // a mod cable goes onto a setting
    int lx, ly; UiMod* u = hit(wx, wy, lx, ly); int pj = u ? paramAt(u, ly) : -1;
    { const JackHit sk = jackAt(wx, wy, 4); if (sk.u && sk.param >= 0) { u = sk.u; pj = sk.param; } }   // dropped on a socket
    if (u && pj >= 0 && !modulatable(u->m->def->params[pj])) { status(u->m->def->params[pj].label + (u->m->def->params[pj].noMod ? " IS A SETTING: IT CAN'T BE MODULATED" : " IS A CHOICE, NOT A LEVEL: IT CAN'T BE MODULATED")); return; }
    if (u && pj >= 0 && u != d.from.u) { if (modulate(d.from.u, u, pj)) { status(d.from.u->m->def->name + " NOW MOVES " + u->m->def->name + " " + u->m->def->params[pj].label); made = true; } }
    else status("DROP A MOD CABLE ON A SETTING");
    return;
  }
  JackHit j = jackAt(wx, wy); if (!j.u) j = jackAt(wx, wy, 6);   // a little forgiving: near enough to a jack counts
  if (!j.u) return;
  if (j.type != d.from.type) { status(std::string("THAT ") + (j.out ? "OUTPUT" : "INPUT") + " TAKES A DIFFERENT KIND OF CABLE"); return; }
  if (d.rev && d.from.param >= 0) { if (j.out && j.u != d.from.u && modulate(j.u, d.from.u, d.from.param)) { status(j.u->m->def->name + " NOW MOVES " + d.from.u->m->def->name + " " + d.from.u->m->def->params[d.from.param].label); made = true; } return; }
  if (!d.rev && j.param >= 0) return;
  bool ok = false;
  if (!d.rev && !j.out) ok = connect(d.from.u, d.from.port, j.u, j.port);
  else if (d.rev && j.out) ok = connect(j.u, j.port, d.from.u, d.from.port);
  made = ok;
  }();
  if (made) { if (d.hasOrig) { unplug(d.orig); status("MOVED THE CABLE", 2); } changed(); }
  else if (d.hasOrig) status("NOT MOVED. TO TAKE A CABLE OUT: TAP IT, THEN UNPLUG", 3);
}

}  // namespace tb

namespace tb {
// ---------------- MIDI LEARN ----------------
// A message from a device, on the screen's thread. While learning, messages are only gathered (learnDecide reads them); otherwise
// each mapping that listens for it moves its control. NRPN arrives in parts (99 and 98 name the parameter, 6 and 38 carry the value)
static bool relMode(int mode) { return mode >= 1 && mode <= 3; }
static int relDelta(int mode, int v) { return mode == 1 ? v - 64 : mode == 2 ? (v < 64 ? v : v - 128) : (v < 64 ? v : -(v - 64)); }
static std::string devShort(const std::string& n, size_t len = 14) { std::string s = n; for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch); if (s.size() > len) s = s.substr(0, len); return s; }

void Ui::midiIn(int dev, unsigned char st, unsigned char d1, unsigned char d2) {
  { // the MONITOR: what arrived, in words
    const std::string dn = dev >= 0 && dev < (int)midiDevNames.size() ? devShort(midiDevNames[dev], 24) : std::string("?"); const int ty = st & 0xF0, ch = (st & 15) + 1; char b[64] = "";
    if (st == 0xFA) std::snprintf(b, 64, "START"); else if (st == 0xFB) std::snprintf(b, 64, "CONTINUE"); else if (st == 0xFC) std::snprintf(b, 64, "STOP");
    else if (ty == 0x90 && d2 > 0) std::snprintf(b, 64, "NOTE %s (%d) VEL %d, CH %d", noteName(d1).c_str(), d1, d2, ch);
    else if (ty == 0x80 || ty == 0x90) std::snprintf(b, 64, "NOTE OFF %s, CH %d", noteName(d1).c_str(), ch);
    else if (ty == 0xB0) std::snprintf(b, 64, "CC %d = %d, CH %d", d1, d2, ch);
    else if (ty == 0xE0) std::snprintf(b, 64, "PITCH BEND %d, CH %d", d2 * 128 + d1, ch);
    else if (ty == 0xC0) std::snprintf(b, 64, "PROGRAM %d, CH %d", d1 + 1, ch);
    else if (ty == 0xD0) std::snprintf(b, 64, "PRESSURE %d, CH %d", d1, ch);
    else if (ty == 0xA0) std::snprintf(b, 64, "KEY PRESSURE %s %d, CH %d", noteName(d1).c_str(), d2, ch);
    if (b[0]) { midiLog.push_back(std::string(b) + "  " + dn); if (midiLog.size() > 14) midiLog.erase(midiLog.begin()); } }
  if (st >= 0xF0) return;   // start, stop and the like: not for mapping
  const int ty = st & 0xF0, ch = st & 15;
  if (ty != 0xB0 && ty != 0x90 && ty != 0x80 && ty != 0xE0) return;
  if (learnMod) { if (learnSeen.empty() && ty == 0x80) return; learnSeen.push_back({dev, st, d1, d2}); if (learnWait < 0) learnWait = 0.6f; return; }   // gather for a moment, then decide
  const std::string dn = dev >= 0 && dev < (int)midiDevNames.size() ? midiDevNames[dev] : std::string();
  auto devOk = [&](const MidiMap& m) {   // the same device; or the mapped one isn't here (another computer, say): any
    if (m.dev.empty() || m.dev == dn) return true; return std::find(midiDevNames.begin(), midiDevNames.end(), m.dev) == midiDevNames.end(); };
  // NRPN: keep track of which parameter each device and channel is talking about
  int nrpnNum = -1; int nrpnPart = 0; int nrpnDelta = 0;
  if (ty == 0xB0 && dev >= 0 && dev < 8) { Nrpn& n = nrpn[dev][ch];
    if (d1 == 99) { n.pm = d2; return; } if (d1 == 98) { n.pl = d2; return; }
    if (n.pm >= 0 && n.pl >= 0 && (d1 == 6 || d1 == 38 || d1 == 96 || d1 == 97)) { nrpnNum = n.pm * 128 + n.pl; nrpnPart = d1 == 6 ? 1 : d1 == 38 ? 2 : 0; if (d1 == 96) nrpnDelta = 1; if (d1 == 97) nrpnDelta = -1; if (d1 == 6) n.dm = d2; } }
  for (auto& m : midiMaps) { if (m.ch != ch || !devOk(m)) continue;
    switch (m.kind) {
      case 0: if (ty != 0xB0) break;
        if (m.mode == 4) { if (d1 == m.num) { m.msb = d2; mapApply(m, d2 / 127.f, 0, -1); } else if (d1 == m.num + 32 && m.msb >= 0) mapApply(m, (m.msb * 128 + d2) / 16383.f, 0, -1); break; }
        if (d1 != m.num) break;
        if (relMode(m.mode)) mapApply(m, -1, relDelta(m.mode, d2), -1); else mapApply(m, d2 / 127.f, 0, -1);
        break;
      case 1: if ((ty == 0x90 || ty == 0x80) && d1 == m.num) mapApply(m, -1, 0, ty == 0x90 && d2 > 0 ? 1 : 0); break;
      case 2: if (nrpnNum != m.num) break;
        if (nrpnDelta) { mapApply(m, -1, nrpnDelta, -1); break; }
        if (nrpnPart == 1) { m.msb = d2; mapApply(m, m.lsbSeen ? d2 * 128 / 16383.f : d2 / 127.f, 0, -1); }
        else if (nrpnPart == 2 && m.msb >= 0) { m.lsbSeen = true; mapApply(m, (m.msb * 128 + d2) / 16383.f, 0, -1); }
        break;
      case 3: if (ty == 0xE0) mapApply(m, (d2 * 128 + d1) / 16383.f, 0, -1); break; }
  }
}

UiMod* Ui::mapMod(MidiMap& m) {
  if (!m.global) return byId(m.mod);
  if (m.mod) if (UiMod* u = byId(m.mod)) if (u->m->def->type == m.gType) return u;
  int n = 0; for (auto& up : mods) if (up->m->def->type == m.gType) { if (n == m.gNth) { m.mod = up->m->id; return up.get(); } n++; }   // EVERY SESSION: the nth of its type here
  return nullptr; }

float Ui::mapValue(MidiMap& m) {
  UiMod* u = mapMod(m); if (!u) return 0;
  if (m.key == "#bypass") return u->m->bypass.load() ? 1.f : 0.f; if (m.key == "#mute") return u->m->mute.load() ? 1.f : 0.f; if (m.key == "#solo") return u->m->solo.load() ? 1.f : 0.f;
  const int k = u->m->def->paramIndex(m.key); if (k < 0) return 0; const ParamDef& q = u->m->def->params[k]; const float v = u->m->p[k].load();
  if (q.act) return 0; if (!q.opts.empty()) return q.opts.size() > 1 ? clampf(v / (float)(q.opts.size() - 1), 0, 1) : 0.f;
  return clampf(q.toNorm(v), 0, 1); }

// move the control: v an absolute 0..1 (or -1), delta relative steps, press a button going down (1) or up (0) (or -1)
void Ui::mapApply(MidiMap& m, float v, int delta, int press) {
  UiMod* u = mapMod(m); if (!u) return; const ModuleDef* d = u->m->def;
  if (press < 0 && v >= 0 && !relMode(m.mode)) { const bool on = v >= 0.5f; press = on != m.down ? (on ? 1 : 0) : -1; m.down = on; }   // a value as a button: crossing the middle presses and lets go
  else if (press >= 0) m.down = press == 1;
  const int res = m.mode == 4 || m.kind >= 2 ? 16383 : 127;
  auto done = [&] { m.fb = (int)std::lround(mapValue(m) * res); midiQuiet = 0.5f; };   // (what it was set to needn't go back to the device)
  if (m.key[0] == '#') {   // M, B and S: a button press turns it over; a value or a turn sets it
    std::atomic<bool>& f = m.key == "#bypass" ? u->m->bypass : m.key == "#mute" ? u->m->mute : u->m->solo; bool want = f.load();
    if (m.kind == 1 || (press == 1 && v < 0)) { if (press != 1) return; want = !want; }
    else if (delta) want = delta > 0; else if (press == 1) want = !want; else return;
    f.store(want); status(d->name + (m.key == "#bypass" ? (want ? "  BYPASS" : "  BACK IN") : m.key == "#mute" ? (want ? "  MUTED" : "  ON") : (want ? "  SOLO" : "  SOLO OFF")), 1.2f); done(); return; }
  const int k = d->paramIndex(m.key); if (k < 0) return; const ParamDef& q = d->params[k]; const float v0 = u->m->p[k].load();
  auto set = [&](float nv) { if (nv == v0) return; takeOwnTempo(u, k, eng); verbTypeSet(u, k, v0, nv); u->m->p[k].store(nv); clockToTempo(u, k, eng); u->valK = k; u->valT = 1.2f;
    status(d->name + "  " + statusLabel(*u, q) + "  " + valueText(*u, k, nv, eng.key.root.load()), 1.2f); };
  if (q.act) {   // a button (RECORD, CLEAR...): on the press, as a tap would
    if (press != 1 && !(delta > 0)) return;
    if (q.key == "clear" && u->clearAsk <= 0) { u->clearAsk = 2.5f; status(d->name + ": PRESS AGAIN TO CLEAR", 2.5f); return; }
    if (q.key == "clear") u->clearAsk = 0;
    if (d->type == "tracker" && q.key == "clear") return;   // (that one only from the screen: it wipes every step)
    if (onAction && onAction(u, q.key)) { done(); return; } eng.action(u->m->id, q.key); status(d->name + "  " + q.label, 1.2f); done(); return; }
  if (!q.opts.empty()) { const int n = (int)q.opts.size(); int i = (int)std::lround(v0);
    if (m.kind == 1 || (press == 1 && v < 0)) { if (press != 1) return; i = n == 2 ? 1 - i : (i + 1) % n; }   // a button: on/off, or the next one round
    else if (delta) { m.acc += delta; while (m.acc >= 2) { i++; m.acc -= 2; } while (m.acc <= -2) { i--; m.acc += 2; } i = std::max(0, std::min(n - 1, i)); }   // a turn: two clicks a step
    else if (v >= 0) { const int want = std::min(n - 1, (int)(v * n)); if (!m.engaged) { if (want == i) m.engaged = true; else { status(d->name + "  " + q.label + ": TURN TO " + q.opts[i] + " TO PICK IT UP", 1.2f); return; } } i = want; }
    else return;
    set((float)i); done(); return; }
  // a level
  if (m.kind == 1) {   // a button on a level: a speed row takes taps; others go to their lowest and back
    if (press != 1) return; if (tapRow(u, k)) { done(); return; } if (tapKind(d, q)) { status(d->name + "  " + q.label + ": KEEP TAPPING", 1); return; }
    if (q.toNorm(v0) > 0.001f) { m.keep = v0; set(q.fromNorm(0)); } else set(m.keep > 0 ? m.keep : q.def); done(); return; }
  float nv;
  if (delta) { const float n0 = q.toNorm(v0); nv = q.step > 0 ? clampf(v0 + delta * q.step * (q.rev ? -1 : 1), std::min(q.min, q.max), std::max(q.min, q.max)) : q.fromNorm(clampf(n0 + delta / 128.f, 0, 1)); }
  else if (v >= 0) {
    const float cur = q.toNorm(v0);
    if (!m.engaged) {   // PICKUP: the knob takes over once it reaches where the control is (so nothing jumps)
      if (std::fabs(v - cur) < 0.02f || (m.hw >= 0 && (m.hw - cur) * (v - cur) <= 0)) m.engaged = true;
      m.hw = v; if (!m.engaged) { status(d->name + "  " + q.label + ": TURN " + (v < cur ? "UP" : "DOWN") + " TO PICK IT UP", 1.2f); return; } }
    m.hw = v; nv = q.fromNorm(v); }
  else return;
  if (q.step > 0) nv = std::round(nv / q.step) * q.step;
  set(nv); done();
}

// the control's value back to the device, so its encoders and display match the screen
void Ui::mapSend(MidiMap& m, float v) {
  if (!onMidiSend || m.dev.empty()) return; unsigned char b[12]; int n = 0; const int ch = m.ch & 15;
  if (m.kind == 0 && m.mode == 0) { b[0] = (unsigned char)(0xB0 | ch); b[1] = (unsigned char)m.num; b[2] = (unsigned char)std::lround(v * 127); n = 3; }
  else if (m.kind == 0 && m.mode == 4) { const int x = (int)std::lround(v * 16383); b[0] = (unsigned char)(0xB0 | ch); b[1] = (unsigned char)m.num; b[2] = (unsigned char)(x >> 7); b[3] = (unsigned char)(0xB0 | ch); b[4] = (unsigned char)(m.num + 32); b[5] = (unsigned char)(x & 127); n = 6; }
  else if (m.kind == 2) { const int x = m.lsbSeen ? (int)std::lround(v * 16383) : (int)std::lround(v * 127) * 128; const unsigned char c = (unsigned char)(0xB0 | ch);
    unsigned char t[12] = {c, 99, (unsigned char)(m.num >> 7), c, 98, (unsigned char)(m.num & 127), c, 6, (unsigned char)(x >> 7), c, 38, (unsigned char)(x & 127)}; std::copy(t, t + 12, b); n = m.lsbSeen ? 12 : 9; }
  else if (m.kind == 3) { const int x = (int)std::lround(v * 16383); b[0] = (unsigned char)(0xE0 | ch); b[1] = (unsigned char)(x & 127); b[2] = (unsigned char)(x >> 7); n = 3; }
  else return;   // relative turns and buttons: nothing to show back
  if (onMidiSend(m.dev, b, n)) m.engaged = true;   // the device now shows where the control is: no need to pick it up
}

void Ui::midiFrame(float dt) {
  if (learnMod) { learnT -= dt; if (learnWait >= 0 && (learnWait -= dt) < 0) learnDecide(); else if (learnT <= 0) { cancelLearn(); status("MIDI LEARN: NOTHING CAME, SO NOTHING CHANGED", 3); } }
  if (midiQuiet > 0 && (midiQuiet -= dt) <= 0) changed();   // a run of knob turns: saved (and one step to undo) once it settles
  fbT -= dt; if (fbT > 0) return; fbT = 0.03f;
  if (!midiFeedback) return;
  for (auto& m : midiMaps) { if (!mapMod(m)) continue; const int res = m.mode == 4 || m.kind >= 2 ? 16383 : 127; const float v = mapValue(m); const int qv = (int)std::lround(v * res);
    if (qv != m.fb) { m.fb = qv; mapSend(m, v); } }
}

void Ui::startLearn(UiMod* u, const std::string& key) {
  learnMod = u->m->id; learnKey = key; learnT = 12; learnWait = -1; learnSeen.clear();
  std::string lab = key == "#bypass" ? "BYPASS" : key == "#mute" ? "MUTE" : key == "#solo" ? "SOLO" : u->m->def->params[u->m->def->paramIndex(key)].label;
  status("MIDI LEARN " + u->m->def->name + "  " + lab + ": MOVE A KNOB OR PRESS A BUTTON (ESCAPE: CANCEL)", 12); }

// what came in while learning: which control, and how it sends
void Ui::learnDecide() {
  learnWait = -1; UiMod* u = byId(learnMod); std::vector<Seen> seen; seen.swap(learnSeen); const std::string key = learnKey; learnMod = 0;
  if (!u || seen.empty()) return;
  const int dev = seen[0].dev, ch = seen[0].st & 15; MidiMap m; m.dev = dev >= 0 && dev < (int)midiDevNames.size() ? midiDevNames[dev] : std::string(); m.ch = ch;
  std::vector<Seen> s; for (auto& x : seen) if (x.dev == dev && (x.st & 15) == ch) s.push_back(x);
  int pm = -1, pl = -1; bool nrpnSeen = false, lsb38 = false, note = false, pb = false;
  for (auto& x : s) { const int ty = x.st & 0xF0; if (ty == 0xB0 && x.d1 == 99) { pm = x.d2; nrpnSeen = true; } if (ty == 0xB0 && x.d1 == 98) { pl = x.d2; nrpnSeen = true; } if (ty == 0xB0 && x.d1 == 38) lsb38 = true;
    if (ty == 0x90 && x.d2 > 0) note = true; if (ty == 0xE0) pb = true; }
  if (nrpnSeen && pm >= 0 && pl >= 0) { m.kind = 2; m.num = pm * 128 + pl; m.lsbSeen = lsb38; }
  else if (note) { m.kind = 1; for (auto& x : s) if ((x.st & 0xF0) == 0x90 && x.d2 > 0) { m.num = x.d1; break; } }
  else if (pb) m.kind = 3;
  else { int cc = -1; for (auto& x : s) if ((x.st & 0xF0) == 0xB0) { cc = x.d1; break; } if (cc < 0) return; m.kind = 0; m.num = cc;
    bool pair = false; if (cc < 32) for (auto& x : s) if ((x.st & 0xF0) == 0xB0 && x.d1 == cc + 32) pair = true;
    if (pair) m.mode = 4;
    else { std::vector<int> v; for (auto& x : s) if ((x.st & 0xF0) == 0xB0 && x.d1 == cc) v.push_back(x.d2);
      int same = 0; for (size_t i = 1; i < v.size(); i++) if (v[i] == v[i - 1]) same++;
      auto all = [&](auto f) { for (int x : v) if (!f(x)) return false; return true; };
      if (v.size() >= 3 && same * 2 >= (int)v.size() - 1) {   // the same few values again and again: an encoder sending turns, not a position
        if (all([](int x) { return x >= 49 && x <= 79 && x != 64; })) m.mode = 1;
        else if (all([](int x) { return (x >= 1 && x <= 15) || (x >= 113 && x <= 127); })) m.mode = 2;
        else if (all([](int x) { return (x >= 1 && x <= 15) || (x >= 65 && x <= 79); })) m.mode = 3; } } }
  m.mod = u->m->id; m.key = key; m.engaged = relMode(m.mode) || m.kind == 1;
  // one source can move several controls; a control has one source
  for (size_t i = 0; i < midiMaps.size(); i++) if (mapMod(midiMaps[i]) == u && midiMaps[i].key == key) { const bool g = midiMaps[i].global; if (g) { m.global = true; m.gType = midiMaps[i].gType; m.gNth = midiMaps[i].gNth; } midiMaps.erase(midiMaps.begin() + i); break; }
  std::string also; for (auto& o : midiMaps) if (o.dev == m.dev && o.ch == m.ch && o.kind == m.kind && o.num == m.num) { also = mapTarget(o); break; }
  midiMaps.push_back(m); MidiMap& mm = midiMaps.back(); mm.fb = -1;   // (its value goes to the device on the next frame)
  static const char* MODE[5] = {"", "  (TURNS, ROUND 64)", "  (TURNS)", "  (TURNS, SIGN AND SIZE)", "  (FINE: 14-BIT)"};
  status("MIDI: " + mapSource(mm) + " NOW MOVES " + mapTarget(mm) + (mm.kind == 0 ? MODE[mm.mode] : "") + (also.empty() ? "" : "  (IT ALSO MOVES " + also + ")"), 5);
  changed(); if (mm.global && onGlobalMaps) onGlobalMaps();
}

const Ui::MidiMap* Ui::mapFor(const UiMod& u, const std::string& key) const {
  for (auto& m : midiMaps) if (m.key == key && (m.mod == u.m->id || (!m.global && false))) return &m;
  for (auto& m : midiMaps) if (m.global && m.key == key && m.gType == u.m->def->type) { int n = 0; for (auto& up : mods) if (up->m->def->type == m.gType) { if (up.get() == &u) { if (n == m.gNth) return &m; break; } n++; } }
  return nullptr; }

void Ui::forgetMap(const UiMod& u, const std::string& key) {
  const MidiMap* f = mapFor(u, key); if (!f) return; const bool g = f->global; midiMaps.erase(midiMaps.begin() + (f - midiMaps.data())); changed(); if (g && onGlobalMaps) onGlobalMaps(); }

std::string Ui::mapSource(const MidiMap& m) const {
  char b[48]; if (m.kind == 0) std::snprintf(b, 48, "CC %d", m.num); else if (m.kind == 1) std::snprintf(b, 48, "NOTE %d", m.num); else if (m.kind == 2) std::snprintf(b, 48, "NRPN %d", m.num); else std::snprintf(b, 48, "PITCH BEND");
  return std::string(b) + " CH " + std::to_string(m.ch + 1) + (m.dev.empty() ? "" : " " + devShort(m.dev)); }

std::string Ui::mapTarget(const MidiMap& m) const {
  UiMod* u = const_cast<Ui*>(this)->mapMod(const_cast<MidiMap&>(m)); if (!u) return m.global ? m.gType + " (NOT IN THIS SESSION)" : "(GONE)";
  std::string lab = m.key == "#bypass" ? "BYPASS" : m.key == "#mute" ? "MUTE" : m.key == "#solo" ? "SOLO" : m.key;
  if (m.key[0] != '#') { const int k = u->m->def->paramIndex(m.key); if (k >= 0) lab = u->m->def->params[k].label; }
  return u->m->def->name + " " + lab; }

Json Ui::midiMapsJson(bool global) const {
  Json a = Json::array();
  for (auto& m : midiMaps) { if (m.global != global) continue; if (!global && !const_cast<Ui*>(this)->byId(m.mod)) continue;
    Json e = Json::object(); e.set("dev", Json::string(m.dev)); e.set("ch", Json::number(m.ch)); e.set("kind", Json::number(m.kind)); e.set("num", Json::number(m.num)); e.set("mode", Json::number(m.mode)); e.set("key", Json::string(m.key));
    if (m.lsbSeen) e.set("fine", Json::number(1));
    if (global) { e.set("type", Json::string(m.gType)); e.set("nth", Json::number(m.gNth)); } else e.set("mod", Json::number(m.mod));
    a.push(e); }
  return a; }

void Ui::midiMapsLoad(const Json& a, bool global, const std::map<int, UiMod*>* idmap) {
  if (a.t != Json::ARR) return;
  for (auto& e : a.a) { MidiMap m; m.dev = e.str("dev"); m.ch = (int)e.num("ch", 0) & 15; m.kind = (int)e.num("kind", 0); m.num = (int)e.num("num", 0); m.mode = (int)e.num("mode", 0); m.key = e.str("key"); m.lsbSeen = e.num("fine", 0) > 0;
    if (m.key.empty()) continue; m.global = global;
    if (global) { m.gType = e.str("type"); m.gNth = (int)e.num("nth", 0); if (m.gType.empty()) continue;
      bool dup = false; for (auto& o : midiMaps) if (o.global && o.gType == m.gType && o.gNth == m.gNth && o.key == m.key) dup = true; if (dup) continue; }
    else { const int old = (int)e.num("mod", 0); if (idmap) { auto it = idmap->find(old); if (it == idmap->end()) continue; m.mod = it->second->m->id; } else m.mod = old; if (!byId(m.mod)) continue;
      midiMaps.erase(std::remove_if(midiMaps.begin(), midiMaps.end(), [&](const MidiMap& o) { return !o.global && o.mod == m.mod && o.key == m.key; }), midiMaps.end()); }
    m.engaged = relMode(m.mode) || m.kind == 1; midiMaps.push_back(m); }
}

// SETTINGS, MIDI MAP: every mapping (tap one: its kind, EVERY SESSION, forget it), values back to the devices, clear all
void Ui::openMidiMap(int x, int y) {
  Dropdown d; d.x = x; d.y = y; std::vector<std::function<void()>> act;
  d.items.push_back(midiMaps.empty() ? "NO MAPPINGS YET: RIGHT-CLICK A CONTROL, MIDI LEARN" : "MIDI MAP"); d.title = 0; act.push_back([] {});
  for (size_t i = 0; i < midiMaps.size(); i++) { const MidiMap& m = midiMaps[i];
    static const char* MODE[5] = {"", "  TURNS 64", "  TURNS", "  TURNS SIGN", "  14-BIT"};
    d.items.push_back(mapSource(m) + (m.kind == 0 ? MODE[m.mode] : "") + "  >  " + mapTarget(m) + (m.global ? "  (EVERY SESSION)" : ""));
    act.push_back([this, i, x, y] { if (i >= midiMaps.size()) return; MidiMap& m = midiMaps[i];
      Dropdown q; q.x = x + 8; q.y = y + 8; std::vector<std::function<void()>> a2;
      if (m.kind == 0) for (int md = 0; md < 5; md++) { static const char* MN[5] = {"KIND: A POSITION (ABSOLUTE)", "KIND: TURNS, ROUND 64 (RELATIVE)", "KIND: TURNS, 1 UP / 127 DOWN (RELATIVE)", "KIND: TURNS, 1 UP / 65 DOWN (RELATIVE)", "KIND: A FINE POSITION (14-BIT)"};
        q.items.push_back(std::string(MN[md]) + (m.mode == md ? "  *" : "")); a2.push_back([this, i, md] { if (i < midiMaps.size()) { midiMaps[i].mode = md; midiMaps[i].engaged = relMode(md); midiMaps[i].fb = -1; changed(); if (midiMaps[i].global && onGlobalMaps) onGlobalMaps(); } }); }
      q.items.push_back(m.global ? "EVERY SESSION: ON (TAP: THIS SESSION ONLY)" : "EVERY SESSION: OFF (TAP: KEEP IT IN EVERY SESSION)");
      a2.push_back([this, i] { if (i >= midiMaps.size()) return; MidiMap& m = midiMaps[i]; UiMod* u = mapMod(m); if (!u) return;
        if (!m.global) { m.global = true; m.gType = u->m->def->type; m.gNth = 0; for (auto& up : mods) { if (up.get() == u) break; if (up->m->def->type == m.gType) m.gNth++; } status("MIDI: " + mapTarget(m) + " IN EVERY SESSION", 3); }
        else { m.global = false; m.mod = u->m->id; status("MIDI: " + mapTarget(m) + " IN THIS SESSION ONLY", 3); }
        changed(); if (onGlobalMaps) onGlobalMaps(); });
      q.items.push_back("FORGET IT"); a2.push_back([this, i] { if (i >= midiMaps.size()) return; const bool g = midiMaps[i].global; status("MIDI: " + mapTarget(midiMaps[i]) + " LET GO", 2); midiMaps.erase(midiMaps.begin() + i); changed(); if (g && onGlobalMaps) onGlobalMaps(); });
      q.pick = [a2](int j) { if (j >= 0 && j < (int)a2.size()) a2[j](); }; openDropdown(q); }); }
  d.items.push_back(std::string("VALUES BACK TO THE DEVICES: ") + (midiFeedback ? "ON" : "OFF")); act.push_back([this, x, y] { midiFeedback = !midiFeedback; for (auto& m : midiMaps) m.fb = -1; if (onGlobalMaps) onGlobalMaps(); status(midiFeedback ? "MIDI: VALUES GO BACK TO THE DEVICES (ENCODERS AND DISPLAYS MATCH THE SCREEN)" : "MIDI: NOTHING SENT BACK", 3); openMidiMap(x, y); });
  if (!midiMaps.empty()) { d.items.push_back("FORGET EVERY MAPPING IN THIS SESSION"); act.push_back([this] { const size_t n0 = midiMaps.size(); midiMaps.erase(std::remove_if(midiMaps.begin(), midiMaps.end(), [](const MidiMap& m) { return !m.global; }), midiMaps.end()); status("MIDI: " + std::to_string(n0 - midiMaps.size()) + " LET GO (EVERY-SESSION ONES KEPT)", 3); changed(); }); }
  d.pick = [act](int i) { if (i >= 0 && i < (int)act.size()) act[i](); }; openDropdown(d);
}

// SETTINGS > MIDI
void Ui::openMidiMenu(int x, int y) {
  Dropdown d; d.x = x; d.y = y; d.items = {"MIDI", "DEVICES", "MONITOR", "MIDI MAP"}; d.title = 0;
  d.pick = [this, x, y](int i) { if (i == 1) openMidiDevices(x, y); else if (i == 2) openMidiMonitor(x, y); else if (i == 3) openMidiMap(x, y); }; openDropdown(d); }

// every input and output: a light (lit while it's sending, steady while clock is arriving), and tap an input to switch it on or off
void Ui::openMidiDevices(int x, int y) {
  Dropdown d; d.x = x; d.y = y; d.stay = true;
  auto fill = [this](Dropdown& q) {
    std::vector<std::string> it; std::vector<int> sw; std::vector<std::string> names; int title = 0;
    it.push_back("INPUTS (TAP: ON OR OFF)"); sw.push_back(-1); names.push_back("");
    bool anyIn = false;
    for (auto& m : midiDevs) if (m.input) { anyIn = true;
      std::string t = m.name.substr(0, 24); for (auto& ch : t) ch = (char)std::toupper((unsigned char)ch);
      t += !m.on ? "  OFF" : !m.open ? "  (CAN'T OPEN)" : m.seen < 0.3f ? "  SENDING" : m.clock ? "  CLOCK" : "  ON";
      it.push_back(t); sw.push_back(!m.on || !m.open ? 26 : m.seen < 0.3f ? 4 : m.clock ? 12 : 21); names.push_back(m.name); }
    if (!anyIn) { it.push_back("NONE FOUND: PLUG ONE IN (IT'S NOTICED IN A MOMENT)"); sw.push_back(-1); names.push_back(""); }
    it.push_back("OUTPUTS (FOR MIDI OUT, AND VALUES GOING BACK)"); sw.push_back(-1); names.push_back(""); const int title2 = (int)it.size() - 1;
    bool anyOut = false;
    for (auto& m : midiDevs) if (!m.input) { anyOut = true; std::string t = m.name.substr(0, 24); for (auto& ch : t) ch = (char)std::toupper((unsigned char)ch); t += m.inUse ? "  IN USE" : ""; it.push_back(t); sw.push_back(m.inUse ? 4 : 21); names.push_back(""); }
    if (!anyOut) { it.push_back("NONE FOUND"); sw.push_back(-1); names.push_back(""); }
    q.items = it; q.sw = sw; q.title = title; q.title2 = title2;
    q.pick = [this, names](int i) { if (i > 0 && i < (int)names.size() && !names[i].empty() && onMidiDevToggle) onMidiDevToggle(names[i]); };
  };
  d.live = fill; fill(d); openDropdown(d); }

// what has just arrived, newest at the top
void Ui::openMidiMonitor(int x, int y) {
  Dropdown d; d.x = x; d.y = y; d.stay = true;
  auto fill = [this](Dropdown& q) { std::vector<std::string> it = {"MIDI MONITOR: THE LATEST MESSAGES (CLOCK TICKS ARE IN DEVICES)"};
    if (midiLog.empty()) it.push_back("NOTHING YET: PLAY A NOTE OR TURN A KNOB ON A DEVICE");
    for (int i = (int)midiLog.size() - 1; i >= 0; i--) it.push_back(midiLog[i]);
    q.items = it; q.title = 0; q.pick = [](int) {}; };
  d.live = fill; fill(d); openDropdown(d); }

}  // namespace tb
