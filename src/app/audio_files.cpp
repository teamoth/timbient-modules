// Timbient native: reading and writing sound files (see audio_files.h)
#include "audio_files.h"
#include "miniaudio.h"
#include <cmath>
#include <cstring>

namespace tb {

AudioBuf* decodeAudio(const std::string& path, double maxSecs, std::string& err) {
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
  ma_decoder dec;
  if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) { err = "COULD NOT READ THAT FILE"; return nullptr; }
  const int ch = (int)dec.outputChannels; const float sr = (float)dec.outputSampleRate;
  if (ch < 1 || sr < 1000) { ma_decoder_uninit(&dec); err = "COULD NOT READ THAT FILE"; return nullptr; }
  AudioBuf* b = new AudioBuf(); b->sr = sr; const size_t maxN = (size_t)(maxSecs * sr);
  std::vector<float> chunk(4096 * ch);
  while (b->d.size() < maxN) {
    ma_uint64 got = 0; if (ma_decoder_read_pcm_frames(&dec, chunk.data(), 4096, &got) != MA_SUCCESS && got == 0) break;
    if (got == 0) break;
    for (ma_uint64 i = 0; i < got && b->d.size() < maxN; i++) { float s = 0; for (int c = 0; c < ch; c++) s += chunk[i * ch + c]; b->d.push_back(s / ch); }
  }
  ma_decoder_uninit(&dec);
  if (b->d.size() < 64) { delete b; err = "THAT FILE HAS NO SOUND IN IT"; return nullptr; }
  return b;
}

bool soundFileInfo(const std::string& path, int& channels, float& rate, double& secs) {
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0); ma_decoder dec;
  if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) return false;
  channels = (int)dec.outputChannels; rate = (float)dec.outputSampleRate; ma_uint64 len = 0;
  secs = (ma_decoder_get_length_in_pcm_frames(&dec, &len) == MA_SUCCESS && rate > 0) ? (double)len / rate : 0;
  ma_decoder_uninit(&dec); return channels > 0;
}

static void u32(FILE* f, uint32_t v) { unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)}; std::fwrite(b, 1, 4, f); }
static void u16(FILE* f, uint16_t v) { unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)}; std::fwrite(b, 1, 2, f); }
static void wavHeader(FILE* f, int ch, int bits, uint32_t sr, uint32_t dataBytes) {
  std::fwrite("RIFF", 1, 4, f); u32(f, 36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f); u32(f, 16); u16(f, 1); u16(f, (uint16_t)ch);
  u32(f, sr); u32(f, sr * ch * bits / 8); u16(f, (uint16_t)(ch * bits / 8)); u16(f, (uint16_t)bits); std::fwrite("data", 1, 4, f); u32(f, dataBytes);
}

bool writeWavMono16(const std::string& path, const AudioBuf& b) {
  FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
  wavHeader(f, 1, 16, (uint32_t)b.sr, (uint32_t)(b.d.size() * 2));
  std::vector<int16_t> o(b.d.size()); for (size_t i = 0; i < b.d.size(); i++) o[i] = (int16_t)std::lround(clampf(b.d[i], -1, 1) * 32767);
  const bool ok = std::fwrite(o.data(), 2, o.size(), f) == o.size(); std::fclose(f); return ok;
}

std::vector<float> peaksOf(const AudioBuf& b, int n) {
  std::vector<float> out(n, 0.f); const double step = (double)b.d.size() / n; float mx = 0;
  for (int i = 0; i < n; i++) { float m = 0; for (size_t j = (size_t)(i * step); j < (size_t)((i + 1) * step) && j < b.d.size(); j += 8) m = std::max(m, std::fabs(b.d[j])); out[i] = m; mx = std::max(mx, m); }
  for (auto& v : out) v /= mx > 0 ? mx : 1;
  return out;
}

AudioBuf* fromBase64Pcm16(const std::string& s, float sr) {
  static int T[256]; static bool init = false;
  if (!init) { for (int& v : T) v = -1; const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; i++) T[(unsigned char)A[i]] = i; init = true; }
  std::vector<unsigned char> bytes; bytes.reserve(s.size() * 3 / 4); int val = 0, bits = -8;
  for (unsigned char c : s) { if (T[c] < 0) continue; val = (val << 6) + T[c]; bits += 6; if (bits >= 0) { bytes.push_back((unsigned char)((val >> bits) & 0xFF)); bits -= 8; } }
  if (bytes.size() < 128 || sr < 1000) return nullptr;
  AudioBuf* b = new AudioBuf(); b->sr = sr; b->d.resize(bytes.size() / 2);
  for (size_t i = 0; i < b->d.size(); i++) b->d[i] = (int16_t)(bytes[i * 2] | (bytes[i * 2 + 1] << 8)) / 32767.f;
  return b;
}

bool Recorder::start(Engine& e, const std::string& file) {
  f = std::fopen(file.c_str(), "wb"); if (!f) return false;
  path = file; frames = 0; sr = e.sr; wavHeader(f, 2, 24, (uint32_t)sr, 0);
  if (e.recBuf.empty()) e.recBuf.assign((size_t)(sr * 10) * 2, 0.f);   // ten seconds of room: the screen empties it every frame
  e.recR = e.recW.load(); e.recOn.store(true, std::memory_order_release);
  return true;
}
void Recorder::pump(Engine& e) {
  if (!f) return;
  const long N = (long)e.recBuf.size() / 2, w = e.recW.load(std::memory_order_acquire);
  if (w - e.recR > N) e.recR = w - N;   // fell behind by more than ten seconds (the computer stalled): skip ahead
  std::vector<unsigned char> o; o.reserve((size_t)(w - e.recR) * 6);
  for (; e.recR < w; e.recR++) for (int c = 0; c < 2; c++) {
    const int v = (int)std::lround(clampf(e.recBuf[(e.recR % N) * 2 + c], -1, 1) * 8388607.0f);
    o.push_back((unsigned char)v); o.push_back((unsigned char)(v >> 8)); o.push_back((unsigned char)(v >> 16)); }
  if (!o.empty()) { std::fwrite(o.data(), 1, o.size(), f); frames += (long)(o.size() / 6); }
}
bool Recorder::stop(Engine& e) {
  if (!f) return false;
  e.recOn.store(false, std::memory_order_release); pump(e);
  std::fseek(f, 0, SEEK_SET); wavHeader(f, 2, 24, (uint32_t)sr, (uint32_t)(frames * 6));
  std::fclose(f); f = nullptr; return frames > 0;
}

}  // namespace tb
