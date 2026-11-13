// Timbient native: reading and writing sound files (on the screen thread, never the audio thread).
#pragma once
#include "../engine/engine.h"
#include <cstdio>
#include <string>
#include <vector>

namespace tb {

// any WAV, AIFF, FLAC or MP3, at its own sample rate; at most maxSecs long. Mixed to mono, unless stereo (then left and right, when it has two)
AudioBuf* decodeAudio(const std::string& path, double maxSecs, std::string& err, bool stereo = false);
bool writeWavMono16(const std::string& path, const AudioBuf& b);   // 16-bit: one channel, or two for a stereo sound
// a sound file's channels, rate and length, without reading it all
bool soundFileInfo(const std::string& path, int& channels, float& rate, double& secs);
std::vector<float> peaksOf(const AudioBuf& b, int n = 128);
// a session's packed audio: 16-bit as base64 (ch 2: left and right, interleaved)
AudioBuf* fromBase64Pcm16(const std::string& b64, float sr, int ch = 1);

// RECORD: the master output streamed to a 24-bit stereo WAV as it plays
struct Recorder {
  FILE* f = nullptr; long frames = 0; std::string path; float sr = 48000;
  bool start(Engine& e, const std::string& file);
  void pump(Engine& e);          // call every frame: moves what the audio thread recorded into the file
  bool stop(Engine& e);          // finishes the file
  bool on() const { return f != nullptr; }
};

}  // namespace tb
