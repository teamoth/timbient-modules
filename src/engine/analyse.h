// What's in a sound file, for the sound browser: how long, how loud, its shape, its key and scale,
// the note it plays (for single notes) and its tempo (for loops). Runs on its own thread, never the audio one.
#pragma once
#include "engine.h"
#include <string>
#include <vector>

namespace tb {

struct SoundInfo {
  std::vector<float> peaks;     // the waveform, 64 columns, 0..1
  float peakDb = -120, rmsDb = -120;
  bool keyKnown = false; int root = 0, scale = 0; float keyConf = 0;   // key and scale (when the sound has clear notes in it)
  int altRoot = 0, altScale = 0; bool altClose = false;               // a close second (C MAJOR / A NAT MINOR share their notes)
  bool noteKnown = false; float note = 0;                             // a single pitched note: its MIDI note (with cents)
  bool tempoKnown = false; float bpm = 0;                             // a loop: roughly how fast
};
SoundInfo analyseSound(const AudioBuf& b);

}  // namespace tb
