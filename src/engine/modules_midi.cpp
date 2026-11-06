// Timbient native: MIDI IN (notes, clock and one CC from any connected device) and MIDI OUT (notes and clock to a device).
// The devices themselves are opened by the app; here the audio thread only reads and writes queues.
#include "params.h"

namespace tb {

// ---------------- MIDI IN ----------------
struct MidiIn : Module {
  enum { CH, CC, DEPTH, DIR };
  float val = -1; int lastNote = -1;
  void blockStart(int) override {
    if (mute.load(std::memory_order_relaxed)) return;
    eng->pos = 0; const int want = (int)e[CH];
    for (int i = 0; i < eng->midiN; i++) { const MidiMsg& mm = eng->midiNow[i]; const int type = mm.st & 0xF0, ch = (mm.st & 0x0F) + 1;
      if (mm.st >= 0xF0 || (want && want != ch)) continue;
      if (type == 0x90 && mm.d2 > 0) { Event ev; ev.note = mm.d1; ev.vel = mm.d2 / 127.f; emit(0, ev); lastNote = mm.d1; }
      else if (type == 0xB0 && mm.d1 == (int)e[CC]) val = mm.d2 / 127.f * 2 - 1; }
    if (eng->midiClock) { Event ev; ev.note = note(4, 0); ev.vel = 1; ev.period = eng->midiPer; ev.ppb = 2; emit(1, ev); }
    modv = val; disp[0].store((float)lastNote); disp[1].store(val);
  }
};
static std::vector<std::string> chans(bool all) { std::vector<std::string> v; if (all) v.push_back("ALL"); for (int i = 1; i <= 16; i++) v.push_back(std::to_string(i)); return v; }
static Reg rMidiIn(new ModuleDef{"midi", "MIDI IN", Cat::Trig, {}, {{"NOTES", PType::Trig}, {"CLOCK", PType::Trig}, {"CC", PType::Mod}},
  {OP("ch", "CHANNEL", chans(true), 0), ST("cc", "CC NUM", 0, 127, 1), pct("depth", "DEPTH", 0.5f), OP("dir", "MOVES", {"BOTH", "UP", "DOWN"}, 1)}, 9, 68, false, [] { return new MidiIn(); }});

// ---------------- MIDI OUT ----------------
struct MidiOut : Module {
  enum { PICK, CH, LEN, TRANS, CLK };
  bool clkOn = false; double lastClk = 0; int lastNote = -1;
  void send(int slot, double at, int n, uint8_t a, uint8_t b = 0, uint8_t c = 0) { MidiOutMsg m; m.slot = slot; m.n = n; m.b[0] = a; m.b[1] = b; m.b[2] = c; m.at = at; eng->midiOutQ.push(m); }
  void trig(int port, const Event& ev) override {
    const int slot = devSlot.load(std::memory_order_relaxed); if (slot < 0) return;
    const double at = wallSeconds() + eng->outLatency + eng->pos / sr; const uint8_t ch = (uint8_t)clampf(e[CH], 0, 15);
    if (port == 0) { const int n = (int)clampf(std::round(ev.note + e[TRANS]), 0, 127), v = (int)clampf(std::round(ev.vel * 127), 1, 127);
      send(slot, at, 3, 0x90 | ch, (uint8_t)n, (uint8_t)v); send(slot, at + e[LEN], 3, 0x80 | ch, (uint8_t)n, 0); lastNote = n; }
    else if (port == 1 && e[CLK] > 0.5f) { const float per = ev.period > 0 ? ev.period : 0.25f, ppb = ev.ppb > 0 ? ev.ppb : 2; const int ticks = std::max(1, (int)std::lround(24 / ppb));
      if (!clkOn) { send(slot, at, 1, 0xFA); clkOn = true; } lastClk = at;
      for (int k = 0; k < ticks; k++) send(slot, at + k * per / ticks, 1, 0xF8); }
  }
  void render(int, int) override {
    if (clkOn && wallSeconds() - lastClk > 1.5) { clkOn = false; const int slot = devSlot.load(); if (slot >= 0) send(slot, wallSeconds(), 1, 0xFC); }
    disp[0].store((float)lastNote); disp[1].store(clkOn ? 1.f : 0.f);
  }
};
static Reg rMidiOut(new ModuleDef{"midiout", "MIDI OUT", Cat::Trig, {{"NOTES", PType::Trig}, {"CLOCK", PType::Trig}}, {},
  {ACT("pick", "DEVICE"), OP("ch", "CHANNEL", chans(false), 0), T("len", "NOTE LEN", 0.02f, 8, 0.4f), ST("trans", "PITCH", -24, 24, 0), OP("clk", "CLOCK", {"OFF", "ON"}, 1)}, 9, 68, false, [] { return new MidiOut(); }});

}  // namespace tb
