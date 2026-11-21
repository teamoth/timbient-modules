// Timbient native: MIDI devices (through RtMidi). Inputs feed the engine's queues from their own threads;
// outputs are sent by a small thread of their own at the time each message is due, so notes and clock stay even.
#pragma once
#include "../engine/engine.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class RtMidiIn; class RtMidiOut;

namespace tb {

class Midi {
public:
  explicit Midi(Engine& e);
  ~Midi();
  void start();                                   // opens what's connected and starts the sending thread
  void poll(float dt);                            // every frame: notices devices being plugged in or out
  std::string state() const { return stateText; }
  std::vector<std::string> outputs();             // the names of the devices notes can be sent to
  int openOutput(const std::string& name);        // a slot number for MIDI OUT modules (-1 if not there)
  bool ok() const { return inCount > 0; }
  // MIDI LEARN: every message also goes to the screen's side; and values go back to a device's output (found by the input's name)
  bool popUi(int& dev, MidiMsg& m);
  const std::vector<std::string>& inputNames() const { return openNames; }
  bool sendTo(const std::string& inName, const unsigned char* b, int n);
  // SETTINGS > MIDI > DEVICES: every input the computer has (open or not), the ones switched off, and how much each has sent
  const std::vector<std::string>& allInputs() const { return inNames; }
  std::vector<std::string> outputsInUse() const { std::vector<std::string> v; for (int s = 0; s < SLOTS; s++) if (outs[s].load()) v.push_back(outNames[s]); return v; }
  void setOff(const std::vector<std::string>& names) { if (names != off) { off = names; scanned = false; rescan = 0; } }
  const std::vector<std::string>& offList() const { return off; }
  uint32_t messagesFrom(const std::string& name) const;   // how many messages (and clock ticks) each open input has sent so far
  uint32_t clockFrom(const std::string& name) const;

  // BLEND TO: MIDI coming in goes to the session on the screen; what every engine sends goes out (so notes still sounding
  // in the session being left get their note-offs)
  void setTarget(Engine& e);
  void alsoSendFrom(Engine& e) { for (auto& x : extra) { Engine* z = nullptr; if (x.load() == &e) return; if (x.compare_exchange_strong(z, &e)) return; } }

private:
  Engine& eng; std::atomic<Engine*> extra[3] = {};
  std::vector<std::unique_ptr<RtMidiIn>> ins; std::vector<std::string> inNames;
  static constexpr int SLOTS = 16;
  std::atomic<RtMidiOut*> outs[SLOTS]; std::string outNames[SLOTS];
  std::thread sender; std::atomic<bool> running{false};
  std::vector<std::string> openNames; std::vector<std::string> outCache; float outCacheT = 0; std::vector<std::pair<std::string, int>> outFor;
  SpscQueue<MidiOutMsg, 4096> uiOutQ;   // the screen's messages (values going back), sent by the same thread
  std::vector<std::string> off;   // inputs switched off (by name)
  std::string stateText = "MIDI OFF"; int inCount = 0; float rescan = 0; bool scanned = false;
  void openInputs();
  void sendLoop();
};

}  // namespace tb
