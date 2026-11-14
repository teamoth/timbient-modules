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
// what each model's three controls do (the HARMONICS, TIMBRE and MORPH knobs of Plaits, after its manual), for the help notes
inline const char* miHelp(int model, int knob) {
  static const char* H[21][3] = {
    {"How far apart the two waveforms are tuned.", "A square that narrows to a thin pulse, then into hard-sync formants.", "A triangle that turns into a saw, with a notch moving through it."},
    {"The filter's resonance and character.", "The filter's cutoff.", "The waveform, and how much sub-oscillator."},
    {"The waveshaper's curve.", "How hard the wave is folded.", "How lopsided the wave is."},
    {"The ratio between the two operators' frequencies.", "How much one modulates the other: brighter and harsher as it goes up.", "Feedback: the modulator shaping itself, rough and noisy at the top."},
    {"Which six-operator preset.", "How loud the modulators are: brighter as it goes up.", "Stretches the envelopes and modulation in time."},
    {"The distortion's frequency.", "How much distortion.", "How lopsided the distortion is."},
    {"The ratio between the two formants.", "The formants' frequency.", "The formants' width and shape."},
    {"How many bumps of harmonics.", "Which harmonic is the most prominent.", "The shape of the bumps."},
    {"Which bank of wavetables.", "The row within it.", "The column within it."},
    {"Which terrain the wave runs over.", "How wide the path round it is.", "Where the path sits on it."},
    {"Which kind of chord.", "The chord's inversion and spread.", "The waveform, from organ-like to string-like."},
    {"Which chord.", "How much chorus.", "The waveform."},
    {"Which kind of speech: simple vowels, then words.", "The species: the size of the throat, from giant to tiny.", "Which phoneme or word."},
    {"How far the swarm's voices are spread in pitch.", "How dense the swarm is.", "How long each grain is, and how they overlap."},
    {"The filter: lowpass, through bandpass, to highpass.", "The clock the noise is made at: lower is grittier.", "The filter's resonance."},
    {"How randomly the particles are tuned.", "How dense the particles are.", "The filter they ring through (towards a reverb at the top)."},
    {"The string's material: from nylon to metal and beyond.", "How bright the pluck is (and some dust).", "How long it rings."},
    {"The material being struck: wood, glass, metal...", "How bright the strike is.", "How long it rings."},
    {"How sharp and overdriven the kick's attack is.", "How bright it is.", "How long it rings."},
    {"The balance of drum and noise.", "The balance of the drum's modes.", "How long it rings."},
    {"The balance of metal and noise.", "The high-pass filter.", "How long it rings."},
  };
  return H[model < 0 ? 0 : model > 20 ? 20 : model][knob < 0 ? 0 : knob > 2 ? 2 : knob];
}

}  // namespace tb
