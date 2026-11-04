// Timbient native: reading and writing sound files (on the screen thread, never the audio thread).
#pragma once
#include "../engine/engine.h"
#include <cstdio>
#include <string>
#include <vector>

namespace tb {

// any WAV, AIFF, FLAC or MP3, mixed to mono, at its own sample rate; at most maxSecs long
AudioBuf* decodeAudio(const std::string& path, double maxSecs, std::string& err);
bool writeWavMono16(const std::string& path, const AudioBuf& b);
std::vector<float> peaksOf(const AudioBuf& b, int n = 128);
// a web export's packed audio: 16-bit mono as base64
AudioBuf* fromBase64Pcm16(const std::string& b64, float sr);

// RECORD: the master output streamed to a 24-bit stereo WAV as it plays
struct Recorder {
  FILE* f = nullptr; long frames = 0; std::string path; float sr = 48000;
  bool start(Engine& e, const std::string& file);
  void pump(Engine& e);          // call every frame: moves what the audio thread recorded into the file
  bool stop(Engine& e);          // finishes the file
  bool on() const { return f != nullptr; }
};

}  // namespace tb
