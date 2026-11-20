// Timbient native: MIDI devices (see midi.h)
#include "midi.h"
#include <SDL3/SDL_timer.h>
#include "RtMidi.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>

namespace tb {

namespace {
struct InCtx { Engine* eng; int q; };
InCtx inCtx[Engine::MIDI_INS];
SpscQueue<MidiMsg, 1024> uiQ[Engine::MIDI_INS];   // the same messages, for MIDI LEARN on the screen's thread
void onIn(double, std::vector<unsigned char>* msg, void* ud) {   // runs on the device's own thread: just queue the bytes
  if (!msg || msg->empty()) return; InCtx* c = (InCtx*)ud; MidiMsg m; m.st = (*msg)[0]; if (msg->size() > 1) m.d1 = (*msg)[1]; if (msg->size() > 2) m.d2 = (*msg)[2];
  c->eng->midiInQ[c->q].push(m); if (m.st < 0xF0) uiQ[c->q].push(m);
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
  for (auto& i : ins) { try { i->closePort(); } catch (...) {} } ins.clear(); inNames = names; openNames.clear();
  int k = 0;
  for (unsigned i = 0; i < names.size() && k < Engine::MIDI_INS; i++) {
    try { auto in = std::make_unique<RtMidiIn>(); inCtx[k] = {&eng, k}; in->openPort(i, "Timbient Modules"); in->ignoreTypes(true, false, true);   // keep clock; skip sysex and active sensing
      in->setCallback(onIn, &inCtx[k]); ins.push_back(std::move(in)); openNames.push_back(names[i]); k++; } catch (...) {}
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

bool Midi::popUi(int& dev, MidiMsg& m) { for (int k = 0; k < inCount && k < Engine::MIDI_INS; k++) if (uiQ[k].pop(m)) { dev = k; return true; } return false; }

// a device's output, from its input's name: the same name, or the same once "MIDIIN2" / "MIDIOUT2" (Windows) and the like are taken away
static std::string plainName(const std::string& n) { std::string s; for (char ch : n) if (std::isalnum((unsigned char)ch)) s += (char)std::tolower((unsigned char)ch);
  for (const char* w : {"midiin", "midiout"}) { size_t p; while ((p = s.find(w)) != std::string::npos) { size_t e = p + std::strlen(w); while (e < s.size() && std::isdigit((unsigned char)s[e])) e++; s.erase(p, e - p); } }
  return s; }
bool Midi::sendTo(const std::string& inName, const unsigned char* b, int n) {
  int slot = -1; for (auto& f : outFor) if (f.first == inName) slot = f.second;
  if (slot < 0) {
    if (outCache.empty() || outCacheT < SDL_GetTicks() / 1000.f) { outCache = outputs(); outCacheT = SDL_GetTicks() / 1000.f + 3; }   // (looked up again every few seconds at most)
    std::string match; for (auto& o : outCache) if (o == inName) match = o;
    if (match.empty()) { const std::string pi = plainName(inName); for (auto& o : outCache) { const std::string po = plainName(o); if (!pi.empty() && (po == pi || po.find(pi) != std::string::npos || pi.find(po) != std::string::npos)) { match = o; break; } } }
    if (match.empty()) return false; slot = openOutput(match); if (slot < 0) return false; outFor.push_back({inName, slot}); }
  for (int i = 0; i < n; i += 3) { MidiOutMsg m; m.slot = slot; m.n = std::min(3, n - i); for (int j = 0; j < m.n; j++) m.b[j] = b[i + j]; m.at = 0; uiOutQ.push(m); }
  return true; }

void Midi::sendLoop() {
  std::vector<MidiOutMsg> pending; pending.reserve(4096);
  while (running) {
    MidiOutMsg m; while (eng.midiOutQ.pop(m)) pending.push_back(m); while (uiOutQ.pop(m)) pending.push_back(m);
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
