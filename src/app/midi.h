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

private:
  Engine& eng;
  std::vector<std::unique_ptr<RtMidiIn>> ins; std::vector<std::string> inNames;
  static constexpr int SLOTS = 16;
  std::atomic<RtMidiOut*> outs[SLOTS]; std::string outNames[SLOTS];
  std::thread sender; std::atomic<bool> running{false};
  std::string stateText = "MIDI OFF"; int inCount = 0; float rescan = 0; bool scanned = false;
  void openInputs();
  void sendLoop();
};

}  // namespace tb
