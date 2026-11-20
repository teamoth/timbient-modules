// The web player's core (src/web/web_main.cpp), run as ordinary C++: make a session with a sound packed inside it (the
// test file the web build is also checked with), open it as the page does, play it, draw it. Writes tests/fixtures/web_test.tmbnt
#include "../src/ui/ui.h"
#include "../src/ui/session.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>
using namespace tb;
extern "C" { int tb_init(float); char* tb_alloc(int); int tb_load(char*, int, const char*); const char* tb_note(); const char* tb_heavy(); float* tb_render(int); uint32_t* tb_frame(int, int, int, float); int tb_imgW(); int tb_imgH(); void tb_level(float); }
static std::string b64(const unsigned char* d, size_t n) { static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; size_t i = 0;
  for (; i + 2 < n; i += 3) { const unsigned v = d[i] << 16 | d[i + 1] << 8 | d[i + 2]; o += A[v >> 18]; o += A[(v >> 12) & 63]; o += A[(v >> 6) & 63]; o += A[v & 63]; }
  if (i < n) { unsigned v = d[i] << 16; if (i + 1 < n) v |= d[i + 1] << 8; o += A[v >> 18]; o += A[(v >> 12) & 63]; o += i + 1 < n ? A[(v >> 6) & 63] : '='; o += '='; } return o; }
int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : "tests/fixtures/web_test.tmbnt"; int fails = 0;
  // the session: a clock playing a SAMPLE (a plucked tone, packed in the file) through a reverb; and a pad for company
  { Engine e2; Ui u2(e2); UiMod* o = u2.add("out", 300, 20); UiMod* c = u2.add("clock", 0, 20); UiMod* s = u2.add("sample", 100, 20); UiMod* r = u2.add("reverb", 200, 20);
    u2.set(c, "sync", 0); u2.set(c, "bpm", 120); u2.connect(c, "PULSE", s, "TRIG"); u2.connect(s, "OUT", r, "IN"); u2.connect(r, "OUT", o, "IN"); s->snd[0].id = "web_test_tone";
    std::vector<unsigned char> raw; for (int i = 0; i < 24000; i++) { const float x = 0.6f * std::sin(6.2831853f * 330 * i / 48000.f) * std::exp(-i / 6000.f); const int16_t v = (int16_t)std::lround(x * 32767); raw.push_back((unsigned char)(v & 255)); raw.push_back((unsigned char)((v >> 8) & 255)); }
    Json f = Json::object(); f.set("timbient", Json::string("patch")); f.set("data", serializePatch(u2)); Json pack = Json::object(); Json a = Json::object(); a.set("sr", Json::number(48000)); a.set("b64", Json::string(b64(raw.data(), raw.size()))); pack.set("web_test_tone", a); f.set("audio", pack);
    FILE* fp = std::fopen(out.c_str(), "wb"); const std::string t = f.dump(); std::fwrite(t.data(), 1, t.size(), fp); std::fclose(fp); u2.clearPatch(); }
  // open it as the page would, and play 4 seconds
  std::string bytes; { FILE* fp = std::fopen(out.c_str(), "rb"); char b[65536]; size_t n; while ((n = std::fread(b, 1, sizeof b, fp)) > 0) bytes.append(b, n); std::fclose(fp); }
  tb_init(48000); auto give = [](const std::string& b) { char* p = tb_alloc((int)b.size()); std::copy(b.begin(), b.end(), p); return p; };
  const int ok = tb_load(give(bytes), (int)bytes.size(), "web_test.tmbnt");
  double s2 = 0; int bad = 0; float pk = 0; for (int b = 0; b < 4 * 48000 / 1024; b++) { const float* x = tb_render(1024); for (int i = 0; i < 2048; i++) { if (!std::isfinite(x[i])) bad++; else { s2 += x[i] * x[i]; pk = std::max(pk, std::fabs(x[i])); } } }
  const double db = 10 * std::log10(s2 / (4 * 48000 * 2) + 1e-20);
  uint32_t* px = tb_frame(240, 160, 3, 0.016f); const int W = tb_imgW(), H = tb_imgH(); int colours = 0; { std::vector<uint32_t> seen; for (int i = 0; i < W * H; i += 37) if (std::find(seen.begin(), seen.end(), px[i]) == seen.end() && seen.size() < 64) seen.push_back(px[i]); colours = (int)seen.size(); }
  if (std::getenv("WEB_SHOT")) { FILE* fp = std::fopen(std::getenv("WEB_SHOT"), "wb"); std::fprintf(fp, "P6 %d %d 255\n", W, H); for (int i = 0; i < W * H; i++) { const uint32_t p = px[i]; unsigned char rgb[3] = {(unsigned char)(p & 255), (unsigned char)((p >> 8) & 255), (unsigned char)((p >> 16) & 255)}; std::fwrite(rgb, 1, 3, fp); } std::fclose(fp); }
  const bool good = ok && std::string(tb_note()).empty() && bad == 0 && db > -45 && pk < 1.5f && W == 720 && H == 480 && colours > 4;
  // and a file that isn't a session
  const int no = tb_load(give("{\"hello\":1}"), 11, "x.json"); const bool refuses = !no && std::string(tb_note()).find("NOT A TIMBIENT") != std::string::npos;
  if (!good || !refuses) fails++;
  std::printf("web player core: opened %d (note \"%s\"), %.1f dB, peak %.2f, %d bad; picture %dx%d, %d colours; a non-session refused %d%s\n", ok, "", db, pk, bad, W, H, colours, refuses, (good && refuses) ? "" : "  WRONG");
  std::printf("test file %s: %zu bytes\n", out.c_str(), bytes.size());
  return fails;
}
