// Timbient native: PITCH SHIFT. Shifts any sound passing through, as it plays.
//  light: CLEAN (one shifted voice), OCTAVER (an octave below, two below and one above, blended in), DETUNE (two voices a few
//         cents either side, for thickness and width)
//  heavy: HARMONY (up to three extra voices at chosen intervals), VOICE (keeps a voice's character, its formants, when shifted)
// IN KEY counts SHIFT (and HARMONY's intervals) in steps of the session's scale: it listens for the note coming in and
// works out each interval from that note's place in the scale, so a harmony always lands in the key.
// FEEDBACK with DELAY sends the shifted sound round again, for spirals climbing or falling.
//
// The shifter: two read heads on a short delay, each sliding at the new speed. When one runs out of room the other takes
// over, starting from the point that best matches what the first was playing (found by comparing their waveforms), with a
// short crossfade. Matching the waveform is what keeps it clean on notes and chords, rather than warbling.
#include "params.h"
#include "dsp2.h"

namespace tb {
namespace {

struct SpliceShifter {
  DelayLine d; float L = 2000, XF = 500, dmin = 480, R = 300; float dA = 0, dB = 0, xf = -1; bool useB = false; float ratio = 1;
  void init(float sr) { d.init(0.3f, sr); L = 0.04f * sr; XF = 0.012f * sr; dmin = 0.012f * sr; R = 0.007f * sr; dA = dmin + L * 0.5f; dB = dA; xf = -1; useB = false; }
  void clear() { d.clear(); dA = dB = dmin + L * 0.5f; xf = -1; useB = false; }
  float corr(float da, float dc, int n) const {   // how alike two heads' next n samples are (1: the same shape)
    const int size = d.mask + 1; double ab = 0, aa = 1e-12, cc = 1e-12; const float base = (float)d.w;
    for (int k = 0; k < n; k += 2) {
      int ia = (int)(base - da + k * ratio) - 1, ic = (int)(base - dc + k * ratio) - 1; ia = ((ia % size) + size) & d.mask; ic = ((ic % size) + size) & d.mask;
      const float a = d.buf[ia], c = d.buf[ic]; ab += a * c; aa += a * a; cc += c * c; }
    return (float)(ab / std::sqrt(aa * cc));
  }
  inline float run(float x, float r) {
    d.write(x); ratio = r; const float rate = 1 - r;
    float& act = useB ? dB : dA; float& nxt = useB ? dA : dB;
    if (xf < 0 && std::fabs(rate) > 1e-6f && ((rate < 0 && act + rate * XF < dmin) || (rate > 0 && act + rate * XF > dmin + L))) {
      // time for the other head: where to start it so the hand-over doesn't show
      const float tgt = rate < 0 ? dmin + L : dmin + R + 2;
      const int n = std::max(16, std::min(256, (int)(std::min(act, tgt - R) / std::max(1.f, r)) - 2));
      float best = tgt, bc = -2; for (float c = std::max(dmin * 0.5f, tgt - R); c <= tgt + R; c += 1) { const float q = corr(act, c, n); if (q > bc) { bc = q; best = c; } }
      nxt = best; xf = 0; }
    float o;
    if (xf >= 0) { const float a = readCubic(d, std::max(2.f, act)), b = readCubic(d, std::max(2.f, nxt)); o = a * (1 - xf) + b * xf; xf += 1.f / XF; nxt += rate;
      if (xf >= 1) { xf = -1; useB = !useB; } }
    else o = readCubic(d, std::max(2.f, act));
    act += rate; act = std::max(2.f, std::min((float)d.mask - 4, act)); nxt = std::max(2.f, std::min((float)d.mask - 4, nxt));
    return o;
  }
};

// which note is coming in (for IN KEY): a quick look for the strongest repeat in the last few hundredths of a second
struct PitchFollow {
  std::vector<float> buf; int w = 0, n = 0, cnt = 0; float note = -1, srD = 12000;
  void init(float sr) { srD = sr / 4; buf.assign(512, 0); }
  void push(float x) { if ((cnt++ & 3) != 0) return; buf[w] = x; w = (w + 1) & 511; if (++n >= 128) { n = 0; detect(); } }
  void detect() {
    const int N = 300, lo = (int)(srD / 1000), hi = (int)(srD / 60); double e0 = 0; float x[512]; for (int i = 0; i < 512; i++) x[i] = buf[(w + i) & 511];
    for (int i = 0; i < N; i++) e0 += x[i] * x[i]; if (e0 < 1e-5) return;   // quiet: keep the last note
    float best = 0; int bl = -1;
    for (int l = lo; l <= hi && l + N < 512; l++) { double s = 0, el = 1e-12; for (int i = 0; i < N; i++) { s += x[i] * x[i + l]; el += x[i + l] * x[i + l]; } const float c = (float)(s / std::sqrt(e0 * el));
      if (c > best + 0.02f) { best = c; bl = l; } }
    if (bl > 0 && best > 0.6f) note = 69 + 12 * std::log2(srD / bl / 440.f);
  }
};

}  // namespace

struct PitchShift : Module {
  enum { MODE, SHIFT, FINE, INKEY, SUB, SUB2, UP, DETUNE, H1, H2, H3, FB, DELAY, MIX };
  SpliceShifter sh[4]; PitchFollow pf; DelayLine fbd; Smooth dry, wet, fbS, rS[4]; float last[2] = {}; int cur = -1; bool fading = false; float xf = 1;
  // VOICE: the voice's own shape (its formants) taken off before shifting and put back after
  static constexpr int P = 32; float bw[P + 1] = {}, lpc[P + 1] = {1}, lpcTo[P + 1] = {1}, fir[P + 1] = {}, iir[P + 1] = {}; std::vector<float> win; int winW = 0, lpcCnt = 0;
  void prepare() override {
    for (auto& s : sh) s.init(sr); pf.init(sr); fbd.init(1.1f, sr);
    for (Smooth* s : {&dry, &wet, &fbS}) s->setTime(0.02f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX]); fbS.snap(e[FB]);
    for (auto& s : rS) { s.setTime(0.03f, sr); s.snap(1); }
    win.assign(1024, 0); cur = -1; for (int k = 0; k <= P; k++) bw[k] = std::pow(0.993f, (float)k);
  }
  void clearAll() { for (auto& s : sh) s.clear(); fbd.clear(); for (int i = 0; i <= P; i++) { fir[i] = iir[i] = 0; lpc[i] = lpcTo[i] = i == 0; } last[0] = last[1] = 0; }
  // a shift in semitones, or (IN KEY) in steps of the scale from the note coming in
  float semis(float steps) {
    if (e[INKEY] < 0.5f || pf.note < 0) return steps;
    const int rt = root(), sc = scale(), k = (int)std::lround(steps), d0 = nearDeg(pf.note, rt, sc);
    return midiOf(d0 + k, rt, sc) - midiOf(d0, rt, sc) + (steps - k);
  }
  void lpcAnalyse() {   // the shape of the last 1024 samples, as 16 numbers (autocorrelation, then Levinson-Durbin)
    double r[P + 1] = {}; for (int l = 0; l <= P; l++) { for (int i = 0; i + l < 1024; i++) { const int a = (winW + i) & 1023, b = (winW + i + l) & 1023; const float wa = 0.5f - 0.5f * std::cos(TAU * i / 1024), wb = 0.5f - 0.5f * std::cos(TAU * (i + l) / 1024); r[l] += win[a] * wa * win[b] * wb; } }
    if (r[0] < 1e-8) return; r[0] *= 1.01;   /* a touch of 'noise floor', so pure tones don't make it ring */ double a[P + 1] = {1}, err = r[0];
    for (int i = 1; i <= P; i++) { double k = -r[i]; for (int j = 1; j < i; j++) k -= a[j] * r[i - j]; k /= err; double tmp[P + 1]; for (int j = 0; j <= P; j++) tmp[j] = a[j];
      for (int j = 1; j < i; j++) a[j] = tmp[j] + k * tmp[i - j]; a[i] = k; err *= (1 - k * k); if (err <= 0) return; }
    for (int i = 0; i <= P; i++) lpcTo[i] = (float)a[i];
  }
  void render(int a, int b) override {
    const int want = std::max(0, std::min(4, (int)std::lround(e[MODE])));
    if (cur < 0) { cur = want; clearAll(); }
    if (want != cur && !fading) fading = true;
    const int m = cur;
    dry.t = 1 - e[MIX]; wet.t = e[MIX]; fbS.t = e[FB];
    // the speed each head runs at this block
    float st[4] = {0, 0, 0, 0}; int nv = 1;
    switch (m) {
      case 0: case 4: st[0] = semis(e[SHIFT]) + e[FINE] / 100.f; break;
      case 1: st[0] = -12; st[1] = -24; st[2] = 12; nv = 3; break;
      case 2: st[0] = e[DETUNE] / 100.f; st[1] = -e[DETUNE] / 100.f; nv = 2; break;
      case 3: st[0] = semis(e[H1]); st[1] = semis(e[H2]); st[2] = semis(e[H3]); nv = 3; break;
    }
    for (int v = 0; v < 4; v++) rS[v].t = std::pow(2.f, st[v] / 12.f);
    const float dl = std::max(0.f, e[DELAY]) * sr;
    for (int i = a; i < b; i++) {
      const float d = dry.tick(), w = wet.tick(), fb = fbS.tick();
      const float xl = in[0][0][i], xr = in[0][1][i], x = 0.5f * (xl + xr);
      pf.push(x);
      float src = x + (dl > 1 ? fbd.read(dl) : last[0]) * fb;   // what goes into the shifter: the sound, and some of what came out
      float L = 0, R = 0;
      if (m == 4) {   // VOICE: take the shape off, shift what's left (the buzz), put the shape back
        win[winW] = src; winW = (winW + 1) & 1023; if (++lpcCnt >= 256) { lpcCnt = 0; lpcAnalyse(); }
        if (lpcCnt == 0) for (int k = 1; k <= P; k++) lpc[k] = lpcTo[k];   // a new look at the shape, every 256 samples (each one steady on its own)
        for (int k = P; k > 0; k--) fir[k] = fir[k - 1]; fir[0] = src; float res = 0; for (int k = 0; k <= P; k++) res += lpc[k] * bw[k] * fir[k];
        const float rr = rS[0].tick(); for (int v = 1; v < 4; v++) rS[v].tick();
        float y = sh[0].run(res, rr);
        float yy = y; for (int k = 1; k <= P; k++) yy -= lpc[k] * bw[k] * iir[k];   // the shape put back (very slightly softened, so it always stays steady)
        yy = softLimit(yy * 0.8f) / 0.8f; for (int k = P; k > 1; k--) iir[k] = iir[k - 1]; iir[1] = sane(yy);
        L = R = yy * 1.5f;
      } else {
        for (int v = 0; v < nv; v++) {
          const float y = sh[v].run(src, rS[v].tick());
          if (m == 1) { const float g = v == 0 ? e[SUB] : v == 1 ? e[SUB2] : e[UP]; L += y * g; R += y * g; }
          else if (m == 2) { if (v == 0) L += y; else R += y; }
          else if (m == 3) { if (std::lround(e[H1 + v]) == 0) continue; const float pan = v == 0 ? 0.25f : v == 1 ? 0.75f : 0.5f; L += y * (1 - pan) * 1.2f; R += y * pan * 1.2f; }
          else { L += y; R += y; } }
        for (int v = nv; v < 4; v++) rS[v].tick();
        if (m == 1 || m == 3) { L += x; R += x; }   // OCTAVER and HARMONY: the original stays in, with the new voices round it
      }
      last[0] = 0.5f * (L + R); if (dl > 1) fbd.write(sane(softLimit(last[0])));
      if (fading) { xf -= 1.f / (0.03f * sr); if (xf <= 0) { xf = 0; fading = false; cur = want; clearAll(); } } else if (xf < 1) xf = std::min(1.f, xf + 1.f / (0.03f * sr));
      out[0][0][i] = xl * d + softLimit(sane(L)) * w * xf; out[0][1][i] = xr * d + softLimit(sane(R)) * w * xf;
    }
  }
};

static std::string semiFmt(float v) { char b[24]; const int k = (int)std::lround(v); std::snprintf(b, 24, k > 0 ? "+%d" : "%d", k); return b; }

static std::string hFmt(float v) { return std::lround(v) == 0 ? std::string("OFF") : semiFmt(v); }   // HARMONY: a voice at 0 is off

static Reg rPitch(new ModuleDef{"pitchshift", "PITCH SHIFT", Cat::Proc, {{"IN", PType::Audio}}, {{"OUT", PType::Audio}},
  {noMod(OP("mode", "MODE", {"CLEAN", "OCTAVER", "DETUNE", "HARMONY", "VOICE"}, 0)),
   only(fmt(pr("shift", "SHIFT", -24, 24, 12), semiFmt), "v:0,4"),
   only(fmt(pr("fine", "FINE", -50, 50, 0), [](float v) { char b[16]; std::snprintf(b, 16, "%+d CENTS", (int)std::lround(v)); return std::string(b); }), "v:0,4"),
   only(OP("inkey", "IN KEY", {"OFF", "ON"}, 0), "v:0,3,4"),
   only(pct("sub", "OCTAVE DOWN", 0.6f), "v:1"), only(pct("sub2", "TWO DOWN", 0.f), "v:1"), only(pct("up", "OCTAVE UP", 0.3f), "v:1"),
   only(fmt(pr("detune", "DETUNE", 0, 50, 12), [](float v) { char b[16]; std::snprintf(b, 16, "%d CENTS", (int)std::lround(v)); return std::string(b); }), "v:2"),
   only(stepper(fmt(ST("h1", "VOICE 2", -12, 12, 4), hFmt)), "v:3"), only(stepper(fmt(ST("h2", "VOICE 3", -12, 12, 7), hFmt)), "v:3"), only(stepper(fmt(ST("h3", "VOICE 4", -12, 12, 0), hFmt)), "v:3"),
   pr("fb", "FEEDBACK", 0, 0.9f, 0), fmt(pr("delay", "DELAY", 0, 1, 0), [](float v) { return v < 0.002f ? std::string("NONE") : std::to_string((int)std::lround(v * 1000)) + "MS"; }),
   pct("mix", "MIX", 1)}, 0, 72, false, [] { return new PitchShift(); }});

}  // namespace tb
