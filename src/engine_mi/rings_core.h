// Timbient: Mutable Instruments' Rings (MIT licence, by Emilie Gillet), wrapped for the RINGS types of PLUCK and RESONATOR.
// Rings works in blocks of 24 samples at 48 kHz: this feeds it a sample at a time (half a millisecond late) and keeps notes in
// tune at other sample rates.
#pragma once
namespace tb {
struct RingsCore {
  RingsCore(); ~RingsCore();
  void init(float sr);
  // model: 0 MODAL, 1 SYMPATHETIC, 2 STRING, 3 FM, 4 CHORD STRINGS, 5 STRING + VERB; poly: 1, 2 or 4 notes ringing at once
  void set(int model, int poly, float structure, float brightness, float damping, float position);
  void strum(float midiNote);          // pluck/strike a new note (on the next block)
  void note(float midiNote);           // the pitch, held (RESONATOR: it rings at this note, no new strike)
  // internal: Rings excites itself on each strum (PLUCK); otherwise the input excites it (RESONATOR). inL/inR may be null.
  void process(const float* inL, const float* inR, float* outL, float* outR, int n, bool internal);
  void clear();
 private:
  struct Impl; Impl* p;
};
}  // namespace tb
