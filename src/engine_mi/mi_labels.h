// Timbient native: the MI VOICES module's choices: which Plaits engines it offers (the same 21 as Default Ambience),
// what they're called, and what each one's three main controls do (from the Plaits manual, shortened to fit).
#pragma once

namespace tb {

struct MiEngine { int plaits; const char* name; const char* k[3]; bool selfEnv; };
inline const MiEngine* miEngines() {
  static const MiEngine E[21] = {
    {8, "ANALOG", {"DETUNE", "SQUARE", "SAW"}, false},
    {0, "VCF ANALOG", {"FILTER", "CUTOFF", "WAVE+SUB"}, false},
    {9, "WAVESHAPER", {"SHAPE", "FOLD", "ASYMMETR"}, false},
    {10, "TWO-OP FM", {"RATIO", "FM AMT", "FEEDBACK"}, false},
    {3, "SIX-OP FM", {"PRESET", "MOD LEV", "STRETCH"}, true},
    {1, "PHASE DIST", {"DIST FRQ", "DIST AMT", "ASYMMETR"}, false},
    {11, "FORMANT", {"F RATIO", "F FREQ", "F WIDTH"}, false},
    {12, "ADDITIVE", {"BUMPS", "HARMONIC", "BUMP SHP"}, false},
    {13, "WAVETABLE", {"BANK", "ROW", "COLUMN"}, false},
    {5, "TERRAIN", {"TERRAIN", "RADIUS", "OFFSET"}, false},
    {14, "CHORDS", {"CHORD", "INVERSN", "WAVEFORM"}, false},
    {6, "STRINGS", {"CHORD", "CHORUS", "WAVEFORM"}, false},
    {15, "SPEECH", {"TYPE", "SPECIES", "PHONEME"}, false},
    {16, "SWARM", {"SPREAD", "DENSITY", "GRAIN"}, false},
    {17, "NOISE", {"FILTER", "CLOCK", "RES"}, false},
    {18, "PARTICLES", {"RANDOM", "DENSITY", "FILTER"}, false},
    {19, "STRING", {"MATERIAL", "BRIGHT", "RING"}, true},
    {20, "MODAL", {"MATERIAL", "BRIGHT", "RING"}, true},
    {21, "KICK", {"DRIVE", "BRIGHT", "RING"}, true},
    {22, "SNARE", {"NOISE", "MODES", "RING"}, true},
    {23, "HI-HAT", {"NOISE", "HI PASS", "RING"}, true},
  };
  return E;
}
constexpr int MI_ENGINES = 21;

}  // namespace tb
