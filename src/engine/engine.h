// Timbient native: the patch engine.
// The audio thread owns the running patch. The screen thread never touches it directly: it sends
// commands through a lock-free queue (add module, connect, set a value...) and reads display values
// that the audio thread publishes in atomics. Nothing the screen does can make the audio wait.
#pragma once
#include "dsp.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <array>

namespace tb {

enum class PType { Audio, Trig, Mod };
enum class Cat { Trig, Voice, Proc, Mod, Out };

struct PortDef { std::string name; PType type; };

// a setting on a module (the same ranges and mapping as the web version, so sessions carry over)
struct ParamDef {
  std::string key, label;
  float min = 0, max = 1, def = 0;
  bool exp = false, rev = false, time = false, hz = false, pctFmt = false, play = false, tog = false;
  float step = 0, nudge = 0.01f;   // nudge: how far one tap of - or + moves it (of the whole slider)
  std::vector<std::string> opts;   // a list of named choices (stored as an index)
  std::string only;                // 'free', 'clock', 'own': greyed out depending on the module's state
  bool act = false;                // an action button rather than a value
  bool stepper = false;            // a small whole number: shown as - 3 + rather than a slider
  bool noMod = false;              // a setting rather than a level: no mod socket (its name takes the socket's room)
  std::function<std::string(float)> fmt;
  float toNorm(float v) const {   // rev: the slider runs the other way (a time shown as a rate: longer times sit at the left)
    const float n = exp ? std::log(std::max(v, min) / min) / std::log(max / min) : (v - min) / (max - min);
    return rev ? 1 - n : n;
  }
  float fromNorm(float n) const {
    n = clampf(n, 0, 1); if (rev) n = 1 - n;
    float v = exp ? min * std::pow(max / min, n) : min + n * (max - min);
    if (step > 0) v = std::round(v / step) * step;
    return v;
  }
};

// a note or trigger travelling down a TRIG cable
struct Event { float note = 60, vel = 1, period = 0, ppb = 0; int offset = 0; int sw = -1; };   // sw: a TIMELINE's switch event (0 off, 1 on, 2 over); -1 for an ordinary trigger

class Engine;
class Module;

// a loaded or caught sound (mono), made on the screen thread and handed to a module whole
struct AudioBuf { std::vector<float> d, r; float sr = 48000; double dur() const { return d.size() / (double)sr; }   // d: the sound (left, in stereo); r: its right, or empty (mono)
  bool stereo() const { return r.size() == d.size() && !r.empty(); } const std::vector<float>& R() const { return stereo() ? r : d; } };
struct ModuleDef {
  std::string type, name; Cat cat = Cat::Proc;
  std::vector<PortDef> ins, outs; std::vector<ParamDef> params;
  int vis = 0, width = 58; bool fixed = false;
  std::function<Module*()> make;
  int labelEnd = 39;   // where its sliders start: after its longest name (the module is widened to keep its sliders full length)
  int keyIdx = -1, scaleIdx = -1;   // sequencers: their own KEY and SCALE (0 = GLOBAL, the session's)
  int lpCut = -1, lpRes = -1;   // GRAIN LOOPER, STRETCH, TAPE LOOP, NOISE CLICK, WAVEFOLD: CUTOFF and RES, a low-pass the engine puts on their output
  int panIdx = -1;   // PAN: every module with an audio output (but MIXER, which pans each channel) has one, added last
  int paramIndex(const std::string& k) const { for (size_t i = 0; i < params.size(); i++) if (params[i].key == k) return (int)i; return -1; }
  int inIndex(const std::string& n) const { for (size_t i = 0; i < ins.size(); i++) if (ins[i].name == n) return (int)i; return -1; }
  int outIndex(const std::string& n) const { for (size_t i = 0; i < outs.size(); i++) if (outs[i].name == n) return (int)i; return -1; }
};
const ModuleDef* findDef(const std::string& type);
std::vector<const ModuleDef*>& allDefs();
void registerDef(ModuleDef* d);

constexpr int MAX_EVENTS = 64;
// a trigger into a module's SWITCH socket (next to its ON / M button) turns it on, off or over: a Timeline's event says on (sw 1), off (0) or over (2); any other trigger turns it over
constexpr int SWITCH_PORT = 99;
// a KEY JOURNEY's KEY output into a sequencer's KEY socket (next to its SWITCH): while patched, the sequencer plays in the
// journey's key and scale instead of its own (read every block, not sent as triggers)
constexpr int KEY_PORT = 98;
struct EventQueue { Event ev[MAX_EVENTS]; int port[MAX_EVENTS]; int n = 0; void push(int p, const Event& e) { if (n < MAX_EVENTS) { port[n] = p; ev[n] = e; n++; } } };

class Module {
public:
  const ModuleDef* def = nullptr;
  int id = 0;
  // settings: base values written by the screen thread, read by the audio thread
  std::unique_ptr<std::atomic<float>[]> p;
  std::vector<float> e;            // effective values this block (base + modulation), audio thread only
  std::unique_ptr<std::atomic<float>[]> eff;   // the same, published for the screen (a modulated slider shows where it really is)
  // audio buffers: [port][channel][sample]
  std::vector<std::array<std::array<float, BLOCK>, 2>> in, out;
  EventQueue events;               // triggers waiting for this module
  float modv = 0;                  // this module's MOD output (-1..1)
  std::atomic<bool> mute{false}, solo{false};
  std::atomic<float> flash{0};     // lights the header when it fires (shown on screen)
  std::atomic<unsigned> fires[8] = {};
  float panNow = 0;   // PAN as last applied (the engine ramps from it)
  Biquad postLp; float lpSig[2] = {-1, -1};   // the CUTOFF and RES some modules get from the engine
  std::atomic<float> outLev[8] = {};   // how loud each audio output is (peak, falling slowly): the screen shows it flowing along the cables
  std::atomic<bool> bypass{false}; float byNow = 0;   // BYPASS: an effect fades over to its dry input
  bool retiring = false; float retireT = 0, quietT = 0;   // being taken away, letting its tail ring out   // how many triggers each output has sent: the screen sends a pulse along its cables
  std::atomic<float> disp[64];     // values the screen draws (playheads, meters, shapes), written by the audio thread
  std::atomic<bool> clockedIn{false};   // something is patched into a CLOCK input
  float sr = 48000; double t = 0;  // sample rate and running time in seconds
  long processed = -1;             // the last block this module was processed in
  Engine* eng = nullptr;
  Rng rng;
  // typed-in steps (the Tracker): note and velocity per step, a note below 0 is an empty step.
  // Written by either thread, one value at a time; seqVer counts changes so the screen can save them.
  static constexpr int SEQ_MAX = 128;
  std::unique_ptr<std::atomic<float>[]> seq;
  // NOTE LOOPER: its recorded notes, four numbers each (when, note, velocity, layer); evN of them. TIMELINE: its events (seconds, lane, what, 0)
  static constexpr int EV_MAX = 1024; std::unique_ptr<std::atomic<float>[]> ev; std::atomic<int> evN{0};
  void allocEv() { ev.reset(new std::atomic<float>[EV_MAX * 4]); for (int i = 0; i < EV_MAX * 4; i++) ev[i].store(0); }
  std::atomic<int> cursor{0}, seqVer{0};
  std::atomic<int> devSlot{-1};    // MIDI OUT: which opened device it sends to
  std::atomic<float> roots[8];      // SAMPLE in MULTI mode: the note each of its sounds was recorded at
  static constexpr int SEQ_COLS = 8;   // the Tracker's columns: each step can hold a note in each
  static int si(int row, int col) { return (row * SEQ_COLS + col) * 2; }   // where a step's note is (its velocity is next)
  std::atomic<int> cursorCol{0};
  void allocSeq() { seq.reset(new std::atomic<float>[SEQ_MAX * SEQ_COLS * 2]); for (int i = 0; i < SEQ_MAX * SEQ_COLS; i++) { seq[i * 2].store(-1); seq[i * 2 + 1].store(0.8f); } }

  Module() { for (auto& d : disp) d.store(0.f); }
  virtual ~Module() {}
  virtual void prepare() {}                              // after the sample rate is known (allocate here)
  virtual void render(int from, int to) {}               // fill out[] from in[] for samples from..to of this block
  virtual void blockStart(int n) {}                      // once per block, before any rendering
  virtual void trig(int port, const Event& ev) {}       // a trigger arrived (offset = sample within the block)
  virtual void action(const std::string& key) {}         // an action button was pressed (sent from the screen)
  virtual void display(float* vals, int n) {}            // copy display values for the screen (playheads, meters)
  virtual AudioBuf* swapBuf(AudioBuf* b, int slot = 0) { return b; }   // take a new sound; hand back the old one (freed off the audio thread)
  // the last few seconds of this module's input, for CATCH (written here, copied out by the screen thread)
  std::atomic<AudioBuf*> recBuf{nullptr};   // SAMPLER: where it records (given by the screen; the screen copies each take out of it)
  std::vector<float> ring, ringR; std::atomic<long> ringW{0};   // ringR: the right, for CHANNELS STEREO

  // helpers for modules
  void emit(int port, Event ev);                         // send a trigger down every cable from an output
  bool patched(int inPort) const;                        // is anything plugged into this input?
  int root() const; int scale() const;                   // the current key (a patched KEY socket's, else its own, else the session's)
  std::atomic<int> keyIn{-1}, scaleIn{-1}; int softIn = 0;   // what its KEY socket brings (-1: nothing patched); softIn: SOFT CHANGE's notes
  int keyOut = -1, scaleOut = -1, softOut = 0;               // KEY JOURNEY: the key it sends (softOut: the notes old and new share, just after a change)
  float note(int oct, int step) const;                  // a note from the current key and scale
  float base(int k) const { return p[k].load(std::memory_order_relaxed); }
  int pi(const char* k) const { return def->paramIndex(k); }
};

// a cable: audio, trigger, or modulation (into a setting)
struct Cable { int from = 0, fromPort = 0, to = 0, toPort = -1, toParam = -1; PType type = PType::Audio; bool open = true;
  float g = 1, gt = 1; bool dieAtZero = false;   // an audio cable's level: it glides to gt (so cables fade in and out), and can go when silent
  bool same(const Cable& o) const { return from == o.from && fromPort == o.fromPort && to == o.to && toPort == o.toPort && toParam == o.toParam && type == o.type; } };

// commands from the screen thread to the audio thread
struct Command {
  enum Kind { ADD, REMOVE, CONNECT, DISCONNECT, ACTION, CLEAR, TRIGGER, SETBUF, PREVIEW, FADE, RETIRE } kind;
  Module* mod = nullptr; int id = 0; Cable cable; char key[24] = {0}; Event ev; int port = 0; AudioBuf* buf = nullptr;
};

template <typename T, int N> struct SpscQueue {   // single producer, single consumer, lock-free
  T items[N]; std::atomic<int> head{0}, tail{0};
  bool push(const T& v) { int h = head.load(std::memory_order_relaxed), nh = (h + 1) % N; if (nh == tail.load(std::memory_order_acquire)) return false; items[h] = v; head.store(nh, std::memory_order_release); return true; }
  bool pop(T& v) { int t = tail.load(std::memory_order_relaxed); if (t == head.load(std::memory_order_acquire)) return false; v = items[t]; tail.store((t + 1) % N, std::memory_order_release); return true; }
};

struct Tempo { std::atomic<float> bpm{72}; };
// MIDI: bytes from the devices (each input has its own queue, filled by that device's thread), and bytes going out
struct MidiMsg { uint8_t st = 0, d1 = 0, d2 = 0; };
struct MidiOutMsg { int slot = 0, n = 0; uint8_t b[3] = {0, 0, 0}; double at = 0; };   // at: when to send, in seconds on the steady clock
double wallSeconds();
struct Key { std::atomic<int> root{2}, scale{0}; int soft = 0; };   // soft: a KEY JOURNEY moving the session's key, just after a change

class Engine {
public:
  Engine() { mods.reserve(512); cables.reserve(4096); order.reserve(512); }
  float sr = 48000;
  Tempo tempo; Key key;
  std::atomic<bool> freeze{false};
  std::atomic<int> noteLimit{48};
  // what the screen reads back
  std::atomic<float> outPeak{0};
  std::atomic<float> cpuLoad{0};

  // ---- screen thread API ----
  Module* create(const std::string& type, int id);       // builds a module (allocation happens here, not on the audio thread)
  void add(Module* m) { send({Command::ADD, m, m->id}); }
  void remove(int id) { send({Command::REMOVE, nullptr, id}); }
  void connect(const Cable& c) { Command k{Command::CONNECT}; k.cable = c; send(k); }
  void disconnect(const Cable& c) { Command k{Command::DISCONNECT}; k.cable = c; send(k); }
  void fade(const Cable& c, float to, bool thenRemove) { Command k{Command::FADE}; k.cable = c; k.cable.gt = to; k.cable.dieAtZero = thenRemove; send(k); }   // glide a cable in or out
  // take a module away gently: it stops listening at once, but its sound rings out (tails) before it goes
  void retire(int id) { Command k{Command::RETIRE}; k.id = id; send(k); }
  void action(int id, const std::string& key) { Command k{Command::ACTION}; k.id = id; std::strncpy(k.key, key.c_str(), sizeof(k.key) - 1); send(k); }
  void trigger(int id, int port, Event ev) { Command k{Command::TRIGGER}; k.id = id; k.port = port; k.ev = ev; send(k); }
  void clear() { send({Command::CLEAR}); }
  void setBuffer(int id, AudioBuf* b, int slot = 0) { Command k{Command::SETBUF}; k.id = id; k.buf = b; k.port = slot; send(k); }
  // the sound browser: play a file straight to the speakers, outside the patch (nullptr stops it). Not recorded
  // a: where to start (samples of the buffer), e: where to stop (-1: the end); loop: go round a..e until stopped.
  // The sound editor moves a, e and loop while it plays (they're read every block)
  void preview(AudioBuf* b, double a = 0, double e = -1, bool loop = false) { prevA.store(a); prevE.store(e); prevLoop.store(loop); previewAt.store(b ? 0.f : -1.f); Command k{Command::PREVIEW}; k.buf = b; send(k); }
  std::atomic<double> prevA{0}, prevE{-1}; std::atomic<bool> prevLoop{false};   // playing from now (so the screen doesn't think it's already finished)
  std::atomic<float> previewAt{-1};   // how far through the preview is (0..1), or -1 when nothing is playing
  // recording the master output: the audio thread writes here, the screen thread drains it to a file
  static constexpr int MIDI_INS = 8;
  SpscQueue<MidiMsg, 1024> midiInQ[MIDI_INS];
  SpscQueue<MidiOutMsg, 4096> midiOutQ;
  MidiMsg midiNow[256]; int midiN = 0; bool midiClock = false; float midiPer = 0.25f; int midiTick = -1; double midiLastT = -1, now = 0;
  float outLatency = 0.03f;   // how far ahead of the speakers the engine runs (MIDI OUT waits this long so notes line up)
  std::atomic<bool> recOn{false}; std::vector<float> recBuf; std::atomic<long> recW{0}; long recR = 0;
  void collectGarbage();                                  // frees modules the audio thread has finished with

  // ---- audio thread ----
  void render(float* L, float* R, int n);                 // called by the sound card for n samples
  void setSampleRate(float s) { sr = s; }

  // used by modules
  Module* byId(int id) { for (auto* m : mods) if (m->id == id) return m; return nullptr; }
  std::vector<Cable> cables;
  std::vector<Module*> mods;
  int activeNotes = 0; long blockNo = 0; int pos = 0;   // pos: the sample being rendered within the current block
  float masterL[BLOCK], masterR[BLOCK];
  Compressor comp;
  bool anySolo = false;

private:
  void send(const Command& c) { while (!cmds.push(c)) {} }
  void applyCommands();
  void reorder();
  void soloGates();
  void block(float* L, float* R, int n);
  int soloSig = 0;
  SpscQueue<Command, 4096> cmds;
  SpscQueue<Module*, 1024> garbage;
  SpscQueue<AudioBuf*, 256> bufGarbage;
  std::vector<Module*> order;
  bool dirty = true;
  AudioBuf* prevBuf = nullptr; double prevPos = 0;
};

// scales and keys, shared with the screen
struct ScaleDef { const char* name; std::vector<int> steps; };
const std::vector<ScaleDef>& scales();
float midiOf(int degree, int root, int scale);
// which key and scale best explain how much of each of the 12 notes there is (LISTEN, and the sound browser)
struct KeyMatch { int root = 0, scale = 0, root2 = 0, scale2 = 0; float conf = 0, conf2 = 0; };
KeyMatch matchKey(const float chroma[12]);
inline int degOf(int oct, int step, int scale) { return (oct - 2) * (int)scales()[scale].steps.size() + step; }
inline int nearDeg(float note, int root, int scale) { int best = 0; float bd = 1e9f; for (int i = -12; i < 90; i++) { const float d = std::fabs(midiOf(i, root, scale) - note); if (d < bd) { bd = d; best = i; } } return best; }
extern const char* NOTE_NAMES[12];
inline std::string noteName(int n) { return std::string(NOTE_NAMES[((n % 12) + 12) % 12]) + std::to_string(n / 12 - 1); }

}  // namespace tb
