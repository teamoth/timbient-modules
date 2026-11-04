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
  bool exp = false, time = false, hz = false, pctFmt = false, play = false, tog = false;
  float step = 0;
  std::vector<std::string> opts;   // a list of named choices (stored as an index)
  std::string only;                // 'free', 'clock', 'own': greyed out depending on the module's state
  bool act = false;                // an action button rather than a value
  std::function<std::string(float)> fmt;
  float toNorm(float v) const {
    if (exp) return std::log(std::max(v, min) / min) / std::log(max / min);
    return (v - min) / (max - min);
  }
  float fromNorm(float n) const {
    n = clampf(n, 0, 1);
    float v = exp ? min * std::pow(max / min, n) : min + n * (max - min);
    if (step > 0) v = std::round(v / step) * step;
    return v;
  }
};

// a note or trigger travelling down a TRIG cable
struct Event { float note = 60, vel = 1, period = 0, ppb = 0; int offset = 0; };

class Engine;
class Module;
struct ModuleDef {
  std::string type, name; Cat cat = Cat::Proc;
  std::vector<PortDef> ins, outs; std::vector<ParamDef> params;
  int vis = 0, width = 58; bool fixed = false;
  std::function<Module*()> make;
  int paramIndex(const std::string& k) const { for (size_t i = 0; i < params.size(); i++) if (params[i].key == k) return (int)i; return -1; }
  int inIndex(const std::string& n) const { for (size_t i = 0; i < ins.size(); i++) if (ins[i].name == n) return (int)i; return -1; }
  int outIndex(const std::string& n) const { for (size_t i = 0; i < outs.size(); i++) if (outs[i].name == n) return (int)i; return -1; }
};
const ModuleDef* findDef(const std::string& type);
std::vector<const ModuleDef*>& allDefs();
void registerDef(ModuleDef* d);

constexpr int MAX_EVENTS = 64;
struct EventQueue { Event ev[MAX_EVENTS]; int port[MAX_EVENTS]; int n = 0; void push(int p, const Event& e) { if (n < MAX_EVENTS) { port[n] = p; ev[n] = e; n++; } } };

class Module {
public:
  const ModuleDef* def = nullptr;
  int id = 0;
  // settings: base values written by the screen thread, read by the audio thread
  std::unique_ptr<std::atomic<float>[]> p;
  std::vector<float> e;            // effective values this block (base + modulation), audio thread only
  // audio buffers: [port][channel][sample]
  std::vector<std::array<std::array<float, BLOCK>, 2>> in, out;
  EventQueue events;               // triggers waiting for this module
  float modv = 0;                  // this module's MOD output (-1..1)
  std::atomic<bool> mute{false}, solo{false};
  std::atomic<float> flash{0};     // lights the header when it fires (shown on screen)
  std::atomic<float> disp[64];     // values the screen draws (playheads, meters, shapes), written by the audio thread
  std::atomic<bool> clockedIn{false};   // something is patched into a CLOCK input
  float sr = 48000; double t = 0;  // sample rate and running time in seconds
  long processed = -1;             // the last block this module was processed in
  Engine* eng = nullptr;
  Rng rng;

  Module() { for (auto& d : disp) d.store(0.f); }
  virtual ~Module() {}
  virtual void prepare() {}                              // after the sample rate is known (allocate here)
  virtual void render(int from, int to) {}               // fill out[] from in[] for samples from..to of this block
  virtual void blockStart(int n) {}                      // once per block, before any rendering
  virtual void trig(int port, const Event& ev) {}       // a trigger arrived (offset = sample within the block)
  virtual void action(const std::string& key) {}         // an action button was pressed (sent from the screen)
  virtual void display(float* vals, int n) {}            // copy display values for the screen (playheads, meters)

  // helpers for modules
  void emit(int port, Event ev);                         // send a trigger down every cable from an output
  bool patched(int inPort) const;                        // is anything plugged into this input?
  int root() const; int scale() const;                   // the current key
  float note(int oct, int step) const;                  // a note from the current key and scale
  float base(int k) const { return p[k].load(std::memory_order_relaxed); }
  int pi(const char* k) const { return def->paramIndex(k); }
};

// a cable: audio, trigger, or modulation (into a setting)
struct Cable { int from = 0, fromPort = 0, to = 0, toPort = -1, toParam = -1; PType type = PType::Audio; bool open = true; };

// commands from the screen thread to the audio thread
struct Command {
  enum Kind { ADD, REMOVE, CONNECT, DISCONNECT, ACTION, CLEAR, TRIGGER } kind;
  Module* mod = nullptr; int id = 0; Cable cable; char key[24] = {0}; Event ev; int port = 0;
};

template <typename T, int N> struct SpscQueue {   // single producer, single consumer, lock-free
  T items[N]; std::atomic<int> head{0}, tail{0};
  bool push(const T& v) { int h = head.load(std::memory_order_relaxed), nh = (h + 1) % N; if (nh == tail.load(std::memory_order_acquire)) return false; items[h] = v; head.store(nh, std::memory_order_release); return true; }
  bool pop(T& v) { int t = tail.load(std::memory_order_relaxed); if (t == head.load(std::memory_order_acquire)) return false; v = items[t]; tail.store((t + 1) % N, std::memory_order_release); return true; }
};

struct Tempo { std::atomic<float> bpm{72}; };
struct Key { std::atomic<int> root{2}, scale{0}; };

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
  void action(int id, const std::string& key) { Command k{Command::ACTION}; k.id = id; std::strncpy(k.key, key.c_str(), sizeof(k.key) - 1); send(k); }
  void trigger(int id, int port, Event ev) { Command k{Command::TRIGGER}; k.id = id; k.port = port; k.ev = ev; send(k); }
  void clear() { send({Command::CLEAR}); }
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
  std::vector<Module*> order;
  bool dirty = true;
};

// scales and keys, shared with the screen
struct ScaleDef { const char* name; std::vector<int> steps; };
const std::vector<ScaleDef>& scales();
float midiOf(int degree, int root, int scale);
inline int degOf(int oct, int step, int scale) { return (oct - 2) * (int)scales()[scale].steps.size() + step; }
extern const char* NOTE_NAMES[12];

}  // namespace tb
