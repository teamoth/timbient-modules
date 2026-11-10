// Timbient native: MIDI devices (see midi.h)
#include "midi.h"
#include <SDL3/SDL_timer.h>
#include "RtMidi.h"
#include <algorithm>
#include <chrono>

namespace tb {

namespace {
struct InCtx { Engine* eng; int q; };
InCtx inCtx[Engine::MIDI_INS];
void onIn(double, std::vector<unsigned char>* msg, void* ud) {   // runs on the device's own thread: just queue the bytes
  if (!msg || msg->empty()) return; InCtx* c = (InCtx*)ud; MidiMsg m; m.st = (*msg)[0]; if (msg->size() > 1) m.d1 = (*msg)[1]; if (msg->size() > 2) m.d2 = (*msg)[2];
  c->eng->midiInQ[c->q].push(m);
}
}

Midi::Midi(Engine& e) : eng(e) {}

Midi::~Midi() {
  running = false; if (sender.joinable()) sender.join();
  for (auto& i : ins) { try { i->closePort(); } catch (...) {} }
  for (auto& o : outs) { RtMidiOut* p = o.load(); if (p) { try { for (int ch = 0; ch < 16; ch++) { std::vector<unsigned char> m = {(unsigned char)(0xB0 | ch), 123, 0}; p->sendMessage(&m); } p->closePort(); } catch (...) {} delete p; } }
}

void Midi::start() {
  for (auto& o : outs) o.store(nullptr);
  openInputs();
  running = true; sender = std::thread([this] { sendLoop(); });
}

void Midi::openInputs() {
  std::vector<std::string> names;
  try { RtMidiIn probe; const unsigned n = probe.getPortCount(); for (unsigned i = 0; i < n; i++) names.push_back(probe.getPortName(i)); }
  catch (...) { stateText = "NO MIDI HERE"; return; }
  if (scanned && names == inNames) return; scanned = true;
  for (auto& i : ins) { try { i->closePort(); } catch (...) {} } ins.clear(); inNames = names;
  int k = 0;
  for (unsigned i = 0; i < names.size() && k < Engine::MIDI_INS; i++) {
    try { auto in = std::make_unique<RtMidiIn>(); inCtx[k] = {&eng, k}; in->openPort(i, "Timbient Modules"); in->ignoreTypes(true, false, true);   // keep clock; skip sysex and active sensing
      in->setCallback(onIn, &inCtx[k]); ins.push_back(std::move(in)); k++; } catch (...) {}
  }
  inCount = k; stateText = k ? std::to_string(k) + (k > 1 ? " DEVICES" : " DEVICE") : "NO DEVICES";
}

void Midi::poll(float dt) { rescan -= dt; if (rescan <= 0) { rescan = 2; openInputs(); } }

std::vector<std::string> Midi::outputs() {
  std::vector<std::string> v; try { RtMidiOut probe; const unsigned n = probe.getPortCount(); for (unsigned i = 0; i < n; i++) v.push_back(probe.getPortName(i)); } catch (...) {}
  return v;
}

int Midi::openOutput(const std::string& name) {
  for (int s = 0; s < SLOTS; s++) if (outs[s].load() && outNames[s] == name) return s;
  const auto names = outputs(); const auto it = std::find(names.begin(), names.end(), name); if (it == names.end()) return -1;
  for (int s = 0; s < SLOTS; s++) if (!outs[s].load()) {
    try { RtMidiOut* o = new RtMidiOut(); o->openPort((unsigned)(it - names.begin()), "Timbient Modules"); outNames[s] = name; outs[s].store(o); return s; } catch (...) { return -1; }
  }
  return -1;
}

void Midi::sendLoop() {
  std::vector<MidiOutMsg> pending; pending.reserve(4096);
  while (running) {
    MidiOutMsg m; while (eng.midiOutQ.pop(m)) pending.push_back(m);
    if (!pending.empty()) {
      const double now = wallSeconds();
      std::stable_sort(pending.begin(), pending.end(), [](const MidiOutMsg& a, const MidiOutMsg& b) { return a.at < b.at; });
      size_t k = 0;
      for (; k < pending.size() && pending[k].at <= now; k++) {
        RtMidiOut* o = pending[k].slot >= 0 && pending[k].slot < SLOTS ? outs[pending[k].slot].load() : nullptr; if (!o) continue;
        try { o->sendMessage(pending[k].b, (size_t)pending[k].n); } catch (...) {}
      }
      pending.erase(pending.begin(), pending.begin() + k);
    }
    SDL_DelayNS(800000);   // (not std::this_thread::sleep_for: on Windows that needs Windows 8, and the 32-bit build runs on Windows 7)
  }
}

}  // namespace tb
