// Timbient native: voices. Each module plays up to 12 notes at once; when it runs out, the oldest note gives way.
#include "voice.h"
#include "../engine_mi/rings_core.h"

namespace tb {

// ---------------- PLUCK STRING: a plucked string, and the instruments built round one ----------------
// TYPE: STRING is the plain Karplus-Strong string it always was. GUITAR, HARP and KOTO pluck it in a different place (near the
// bridge is brighter and thinner, the middle rounder) and add their wooden body (BODY: how much). KOTO also bends up a little as
// it's plucked and its partials stretch. MUSIC BOX is a struck steel tine in a small box. SYMPATHY: six open strings tuned to the
// key ring along. Heavy: RINGS, Mutable Instruments' Rings resonator (MODEL picks which of its kinds) with its own pluck.
struct PluckV : VoiceBase { float buf[8192]; int w = 0; float D = 100, gp = 0.99f, dc = 0, bend = 0, ap = 0, apc = 0, t = 0; float re[4] = {}, im[4] = {}, cr[4] = {}, ci[4] = {}, dk[4] = {}, click = 0; };
struct Pluck : Poly<PluckV> {
  enum { STRING, GUITAR, HARP, KOTO, MBOX, RINGS };
  int iType = -1, iBody = -1, iSym = -1, iModel = -1, iStruct = -1, iPos = -1, iTone = 1, iRing = 2, curBody = -1;
  Biquad body[3]; float bodyG[3] = {}; RingsCore rings; bool ringsOn = false;
  struct Sym { DelayLine d; float len = 100, lp = 0, g = 0.999f; } sym[6]; float symE = 0; int symRoot = -1, symScale = -1;
  Pluck() { gainScale = 0.4f; }
  void prepare() override { Poly<PluckV>::prepare(); iType = pi("type"); iBody = pi("body"); iSym = pi("symp"); iModel = pi("rmodel"); iStruct = pi("rstruct"); iPos = pi("rpos");
    for (auto& q : sym) q.d.init(0.06f, sr); rings.init(sr); curBody = -1; ringsOn = false; }
  int type() const { return iType >= 0 ? std::max(0, std::min(5, (int)std::lround(e[iType]))) : 0; }
  void trig(int port, const Event& ev) override {
    if (type() == RINGS) { rings.strum(ev.note + (iPitch >= 0 ? e[iPitch] : 0)); return; }
    Poly<PluckV>::trig(port, ev); }
  void start(PluckV& x) override {
    const int ty = type(); const float bright = std::min(0.98f, e[iTone] * (ty == HARP ? 0.6f : ty == KOTO ? 1.3f : 1.f)), t60 = e[iRing] * (ty == HARP ? 1.4f : ty == KOTO ? 0.8f : 1.f);
    x.t = 0; x.bend = ty == KOTO ? 0.35f : 0; x.apc = ty == KOTO ? -0.3f : 0; x.ap = 0;
    if (ty == MBOX) {   // a steel tine: its partials sit far apart (1, 6.27, 17.5, 34.4 times), the high ones dying quickly
      static const float R[4] = {1, 6.267f, 17.55f, 34.39f}, A[4] = {1, 0.4f, 0.16f, 0.07f};
      for (int k = 0; k < 4; k++) { const float f = x.f * R[k]; const bool ok = f < 0.45f * sr; const float w = TAU * f / sr; x.cr[k] = std::cos(w); x.ci[k] = std::sin(w);
        x.re[k] = 0; x.im[k] = ok ? A[k] * (k == 0 ? 1 : 0.3f + bright) : 0; x.dk[k] = std::exp(std::log(0.001f) / (sr * t60 * 1.5f / (1 + k * k * 2.5f))); }
      x.click = 1; return; }
    x.D = std::max(2.f, sr / x.f - 0.5f); x.gp = std::exp(std::log(0.001f) / (x.f * t60));
    const int P = std::min(8191, (int)x.D + 2); float lp = 0, mean = 0;
    for (int i = 0; i < 8192; i++) x.buf[i] = 0;
    // excite with lowpassed noise plus one cycle of the fundamental (keeps the pitch clear)
    for (int i = 0; i < P; i++) { lp += bright * (rng.bi() - lp); x.buf[i] = lp + 0.35f * std::sin(TAU * i / x.D); }
    if (ty == GUITAR || ty == HARP || ty == KOTO) {   // where it's plucked: a copy from that far along taken away (near the bridge: thin and bright)
      const int bp = std::max(1, (int)(P * (ty == GUITAR ? 0.13f : ty == HARP ? 0.3f : 0.07f))); for (int i = P - 1; i >= bp; i--) x.buf[i] -= 0.9f * x.buf[i - bp]; }
    for (int i = 0; i < P; i++) mean += x.buf[i]; mean /= P; for (int i = 0; i < P; i++) x.buf[i] -= mean;
    x.w = P;
  }
  void voice(PluckV& x, float& l, float& r) override {
    if (type() == MBOX) { float y = 0;
      for (int k = 0; k < 4; k++) { const float nr = (x.re[k] * x.cr[k] - x.im[k] * x.ci[k]) * x.dk[k], ni = (x.re[k] * x.ci[k] + x.im[k] * x.cr[k]) * x.dk[k]; x.re[k] = nr; x.im[k] = ni; y += ni; }
      if (x.click > 0.001f) { y += rng.bi() * x.click * 0.25f; x.click *= 0.985f; }
      l = r = y * 0.3f; return; }
    auto rd = [&](float d) { float rp = x.w - d; while (rp < 0) rp += 8192; int i = (int)rp; float f = rp - i; return x.buf[i & 8191] + (x.buf[(i + 1) & 8191] - x.buf[i & 8191]) * f; };
    if (x.bend > 0.001f) { x.bend *= 0.9997f; x.D = std::max(2.f, std::min(8190.f, sr / (x.f * std::pow(2.f, x.bend / 12)) - 0.5f)); }   // KOTO: pulled sharp as it's plucked, settling back
    else if (x.glLen > 0) x.D = std::max(2.f, std::min(8190.f, sr / x.f - 0.5f));   // GLIDE: the string's length follows the pitch
    float y = x.gp * 0.5f * (rd(x.D) + rd(x.D + 1));
    if (x.apc != 0) { const float o = x.apc * y + x.ap; x.ap = y - x.apc * o; y = o; }   // KOTO: the partials stretch a little, like a stiffer string
    x.buf[x.w & 8191] = y; x.w = (x.w + 1) & 8191;
    l = r = type() == HARP ? y * 0.6f : y;
  }
  void setBody(int ty) {   // three wooden resonances for each instrument (Hz, sharpness, how loud)
    static const float B[5][9] = {{0}, {98, 6, 1, 204, 8, 0.8f, 420, 5, 0.5f}, {160, 4, 0.8f, 330, 4, 0.6f, 760, 3, 0.4f}, {280, 6, 0.8f, 620, 5, 0.6f, 1450, 4, 0.5f}, {520, 8, 0.9f, 1250, 6, 0.6f, 2900, 4, 0.3f}};
    curBody = ty; if (ty < GUITAR || ty > MBOX) return; for (int k = 0; k < 3; k++) { body[k].set(Biquad::BP, B[ty][k * 3], B[ty][k * 3 + 1], sr); body[k].reset(); bodyG[k] = B[ty][k * 3 + 2]; } }
  void tuneSym() { if (root() == symRoot && scale() == symScale) return; symRoot = root(); symScale = scale();
    static const int DEG[6] = {0, 4, 7, 2, 9, 11};
    for (int k = 0; k < 6; k++) { const float f = mtof(note(3, DEG[k])); sym[k].len = std::max(2.f, std::min(0.055f * sr, sr / f - 0.5f)); sym[k].g = std::pow(10.f, -3.f / (4 * f)); } }
  void render(int a, int b) override {
    const int ty = type();
    if (ty == RINGS) {   // RINGS: its own voices; MONO rings one note at a time, POLY four
      if (!ringsOn) { rings.clear(); ringsOn = true; }
      const float dmp = std::log(std::max(0.2f, e[iRing]) / 0.2f) / std::log(25.f);
      rings.set(iModel >= 0 ? (int)std::lround(e[iModel]) : 0, iPoly >= 0 && e[iPoly] > 0.5f ? 1 : 4, iStruct >= 0 ? e[iStruct] : 0.4f, (e[iTone] - 0.01f) / 0.94f, dmp, iPos >= 0 ? e[iPos] : 0.4f);
      rings.process(nullptr, nullptr, out[0][0].data() + a, out[0][1].data() + a, b - a, true);
      const float lev = iLevel >= 0 ? e[iLevel] : 1; for (int i = a; i < b; i++) { out[0][0][i] = sane(out[0][0][i]) * lev * 0.55f; out[0][1][i] = sane(out[0][1][i]) * lev * 0.55f; }
      return; }
    ringsOn = false;
    Poly<PluckV>::render(a, b);
    if (ty != curBody) setBody(ty);
    const float bd = iBody >= 0 && ty >= GUITAR && ty <= MBOX ? e[iBody] * 2.5f : 0, sy = iSym >= 0 ? e[iSym] : 0;
    if (bd > 0) for (int i = a; i < b; i++) for (int c = 0; c < 2; c++) { const float x = out[0][c][i]; float y = 0; for (int k = 0; k < 3; k++) y += (float)body[k].run(x, c) * bodyG[k]; out[0][c][i] = x * (1 - 0.25f * bd / 2.5f) + y * bd; }
    if (sy > 0.001f || symE > 1e-7f) { tuneSym(); float en = 0;   // SYMPATHY: the open strings, fed by what's played
      for (int i = a; i < b; i++) { const float x = (out[0][0][i] + out[0][1][i]) * 0.5f * 0.04f * sy; float L = 0, R = 0;
        for (int k = 0; k < 6; k++) { Sym& q = sym[k]; float y = q.d.read(q.len - 1); q.lp += 0.5f * (y - q.lp); y = q.lp; q.d.write(sane(x + y * q.g)); (k & 1 ? R : L) += y; en += y * y; }
        out[0][0][i] += L * 0.6f; out[0][1][i] += R * 0.6f; }
      symE = en / std::max(1, b - a); }
  }
};
static std::vector<ParamDef> pluckParams() { std::vector<ParamDef> p = {PITCH(), prExp("tone", "TONE", 0.01f, 0.95f, 0.5f), T("decay", "RING", 0.2f, 5, 1.6f)};
  for (auto& q : envP(0.003f, 0.3f, 1, 4, 2)) { q.only = "v:0,1,2,3,4"; p.push_back(q); } p.push_back(only(pct("vsens", "VEL SENS", 1), "v:0,1,2,3,4")); p.push_back(only(pct("width", "WIDTH", 0.4f), "v:0,1,2,3,4")); p.push_back(LEVEL());
  p.push_back(noMod(OP("type", "TYPE", {"STRING", "GUITAR", "HARP", "KOTO", "MUSIC BOX", "RINGS"}, 0))); p.push_back(only(pct("body", "BODY", 0.5f), "v:1,2,3,4")); p.push_back(only(pct("symp", "SYMPATHY", 0), "v:0,1,2,3,4"));
  p.push_back(only(noMod(OP("rmodel", "MODEL", {"MODAL", "SYMPATHY", "STRING", "FM", "CHORDS", "STR+VERB"}, 2)), "v:5")); p.push_back(only(pct("rstruct", "STRUCTURE", 0.4f), "v:5")); p.push_back(only(pct("rpos", "POSITION", 0.4f), "v:5"));
  return p; }
static Reg rPluck(new ModuleDef{"pluck", "PLUCK STRING", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, pluckParams(), 0, 68, false, [] { return new Pluck(); }});

// ---------------- TONE: a clean sine or triangle, with glide, vibrato and an optional detuned twin ----------------
struct ToneV : VoiceBase { Osc o1, o2; float from = 220, gt = 0, vt = 0, vph = 0; };
struct Tone : Poly<ToneV> {
  float lastF = 0, detR = 1;
  void voicePrep() override { detR = std::pow(2.f, e[5] / 1200.f); }
  enum { PITCHK, WAVE, GLIDE, VIB, VRATE, DET };
  Tone() { gainScale = 0.2f; }
  void start(ToneV& x) override { x.from = lastF > 0 ? lastF : x.f; lastF = x.f; x.gt = 0; x.vt = 0; x.vph = 0; x.o1.ph = x.o2.ph = 0; }
  void voice(ToneV& x, float& l, float& r) override {
    const float dt = 1.f / sr; x.gt += dt; x.vt += dt;
    const float g = e[GLIDE] > 0.005f ? std::min(1.f, x.gt / e[GLIDE]) : 1.f, f0 = x.from * std::pow(x.f / x.from, g);
    x.vph += e[VRATE] * dt; if (x.vph >= 1) x.vph -= 1;
    const float f = f0 + std::sin(TAU * x.vph) * f0 * 0.012f * e[VIB] * std::min(1.f, x.vt / 0.5f);
    const Osc::Wave w = e[WAVE] > 0.5f ? Osc::TRI : Osc::SINE;
    float y = x.o1.tick(w, f, sr);
    if (e[DET] > 0.5f) y = 0.5f * (y + x.o2.tick(w, f * detR, sr));
    l = r = y;
  }
};
static std::vector<ParamDef> toneParams() { std::vector<ParamDef> p = {PITCH(), OP("wave", "WAVE", {"SINE", "TRI"}, 0), T("glide", "GLIDE", 0.001f, 4, 0.001f), pct("vib", "VIBRATO", 0.15f),
  fmt(prExp("vrate", "VIB RATE", 0.1f, 9, 4.5f), [](float v) { char b[16]; std::snprintf(b, 16, "%.1fHZ", v); return std::string(b); }),
  fmt(pr("det", "DETUNE", 0, 30, 0), [](float v) { return std::to_string((int)std::round(v)) + "C"; })};
  for (auto& q : envP(0.02f, 0.4f, 0.8f, 1.5f, 2.5f)) p.push_back(q); p.push_back(pct("vsens", "VEL SENS", 1)); p.push_back(pct("width", "WIDTH", 0.3f)); p.push_back(LEVEL()); return p; }
static Reg rTone(new ModuleDef{"tone", "TONE", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, toneParams(), 0, 68, false, [] { return new Tone(); }});

// ---------------- PAD: detuned saws across the stereo field, a slowly moving filter, a breath of air ----------------
// TYPE: CLASSIC is the pad it always was. WAVETABLE swaps the saws for a row of eight waves (WAVE picks where along it; SCAN lets
// it wander back and forth, so the colour keeps shifting). Heavy: SPECTRAL builds each note from 32 separate partials: BRIGHT
// tilts them, EVEN brings in or takes out the even ones (hollow to full), SHIMMER makes each one swell and fade on its own, SPREAD
// pulls them a little out of tune and across the stereo field.
namespace {
struct WaveTables {   // eight single-cycle waves, each at ten sizes (fewer harmonics for higher notes, so nothing aliases)
  static constexpr int N = 2048, W = 8, L = 10; std::vector<float> t;
  WaveTables() : t((size_t)W * L * N) { std::vector<float> sn(N); for (int i = 0; i < N; i++) sn[i] = std::sin(TAU * i / N);
    for (int w = 0; w < W; w++) { float norm = 0;
      for (int l = 0; l < L; l++) { const int H = 512 >> l; float* d = &t[((size_t)w * L + l) * N];
        for (int h = 1; h <= H; h++) { float a = 0; const float n = (float)h;
          switch (w) { case 0: a = h == 1; break; case 1: a = h & 1 ? 1 / (n * n) * ((h / 2) & 1 ? -1 : 1) : 0; break; case 2: a = 1 / n; break; case 3: a = h & 1 ? 1 / n : 0; break;
            case 4: a = std::sin(PI * n * 0.2f) / n; break; case 5: a = (h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 8) ? 1 / std::sqrt(n) : 0; break;
            case 6: a = (1 / n) * (1 + 3 * std::exp(-std::pow((n - 4) / 1.5f, 2.f)) + 2 * std::exp(-std::pow((n - 7) / 2.f, 2.f)) + 1.5f * std::exp(-std::pow((n - 15) / 3.f, 2.f))); break;   // VOCAL: three humps, like a voice's resonances
            default: a = std::exp(-std::pow((n - 10) / 4.f, 2.f)) + 0.3f * (h == 1); }   // GLASS: a bright band
          if (a != 0) for (int i = 0; i < N; i++) d[i] += a * sn[(size_t)(h * i) & (N - 1)]; }
        if (l == 0) for (int i = 0; i < N; i++) norm = std::max(norm, std::fabs(d[i])); }
      for (int l = 0; l < L; l++) { float* d = &t[((size_t)w * L + l) * N]; for (int i = 0; i < N; i++) d[i] /= std::max(1e-6f, norm); } } }
  inline float read(int w, int l, float ph) const { const float x = ph * N; const int i = (int)x; const float f = x - i; const float* d = &t[((size_t)w * L + l) * N]; return d[i & (N - 1)] + (d[(i + 1) & (N - 1)] - d[i & (N - 1)]) * f; }
  static int level(float f, float sr) { int l = 0; while (l < L - 1 && (512 >> l) * f > 0.45f * sr) l++; return l; }
};
const WaveTables& waves() { static const WaveTables w; return w; }
const float* sinTab() { static float t[4097]; static bool done = false; if (!done) { for (int i = 0; i <= 4096; i++) t[i] = std::sin(TAU * i / 4096); done = true; } return t; }
}
struct PadV : VoiceBase { Osc o[7]; float pan[7], ratio[7], gl[7], gr[7]; Biquad lp, bp; float lph = 0, lrate = 0.1f, ft = 0, wph = 0, wrate = 0.1f; int n = 5; int cnt = 0;
  static constexpr int NP = 32; float sph[NP], sAmp[NP], sTo[NP], sDet[NP], sl[NP], sr_[NP], sGo[NP], sA[NP], sInc[NP]; int sN = 0; };
struct Pad : Poly<PadV> {
  enum { PITCHK, VOICES, DETUNE, CUT, RES, FENV, FDEC, MOTION, AIR };
  enum { CLASSIC, WAVET, SPECTRAL };
  int iType = -1, iWave = -1, iScan = -1, iTilt = -1, iEven = -1, iShim = -1, iSpread = -1;
  Pad() { gainScale = 0.18f; selfPan = true; }
  void prepare() override { Poly<PadV>::prepare(); iType = pi("type"); iWave = pi("wave"); iScan = pi("scan"); iTilt = pi("tilt"); iEven = pi("even"); iShim = pi("shim"); iSpread = pi("spread"); waves(); sinTab(); }
  int type() const { return iType >= 0 ? std::max(0, std::min(2, (int)std::lround(e[iType]))) : 0; }
  void start(PadV& x) override {
    x.n = (int)e[VOICES]; for (int k = 0; k < x.n; k++) { x.o[k].ph = rng.uni(); x.pan[k] = x.n > 1 ? (float)k / (x.n - 1) * 2 - 1 : 0; x.ratio[k] = std::pow(2.f, x.pan[k] * e[DETUNE] * 30 / 1200.f); panGains(x.pan[k] * e[iWidth], x.gl[k], x.gr[k]); }
    x.lph = rng.uni(); x.lrate = rng.range(0.07f, 0.2f); x.lp.reset(); x.bp.reset(); x.bp.set(Biquad::BP, std::min(12000.f, x.f * 6), 1.5f, sr); x.cnt = 0; x.ft = 0;
    x.wph = rng.uni(); x.wrate = rng.range(0.05f, 0.13f); x.sN = 0;
    if (type() == SPECTRAL) for (int k = 0; k < PadV::NP; k++) { x.sph[k] = rng.uni(); x.sAmp[k] = x.sTo[k] = 1; x.sGo[k] = rng.uni(); x.sDet[k] = rng.bi(); float l, r; panGains((k & 1 ? 1 : -1) * std::min(1.f, 0.15f + k * 0.05f) * e[iWidth], l, r); x.sl[k] = l; x.sr_[k] = r; }
  }
  void voice(PadV& x, float& l, float& r) override {
    const int ty = type();
    if ((x.cnt++ & 31) == 0) { x.lph += 32 * x.lrate / sr; x.ft += 32 / sr; x.wph += 32 * x.wrate / sr; const float fe = 1 + e[FENV] * 12 * x.vel * std::exp(-x.ft * 3 / std::max(0.01f, e[FDEC]));   // FILT ENV opens the filter at each note, FILT DEC closes it again
      const float c = std::min(16000.f, (e[CUT] + std::sin(TAU * x.lph) * e[CUT] * e[MOTION] * 0.7f) * fe); x.lp.set(Biquad::LP, std::max(40.f, c), 0.7f + e[RES] * e[RES] * 9, sr);   // RES: none at 0 (as before), a singing peak at full
      if (ty == SPECTRAL) { const float sh = e[iShim]; for (int k = 0; k < PadV::NP; k++) { x.sGo[k] += 32 * (0.05f + 0.1f * (k % 5)) / sr; if (x.sGo[k] >= 1) { x.sGo[k] -= 1; x.sTo[k] = 1 - sh * rng.uni(); } x.sAmp[k] += 0.02f * (x.sTo[k] - x.sAmp[k]); }
        const float tilt = 0.5f + (1 - e[iTilt]) * 1.5f, ev = e[iEven], sp = e[iSpread] * 0.012f; x.sN = 0;   // each partial's pitch and level, worked out every 32 samples
        for (int k = 0; k < PadV::NP; k++) { const float n = (float)(k + 1), f = x.f * n * (1 + sp * x.sDet[k] * std::sqrt(n)); if (f > 0.45f * sr) break; x.sN = k + 1;
          x.sInc[k] = f / sr; x.sA[k] = std::pow(n, -tilt) * (k & 1 ? ev * 2 : 1) * x.sAmp[k] * 0.6f; } } }
    float L = 0, R = 0;
    if (ty == SPECTRAL) {   // 32 partials, each its own sine
      const float* st = sinTab();
      for (int k = 0; k < x.sN; k++) { x.sph[k] += x.sInc[k]; x.sph[k] -= (int)x.sph[k]; const float u = x.sph[k] * 4096; const int i = (int)u; const float s = (st[i] + (st[i + 1] - st[i]) * (u - i)) * x.sA[k];
        L += s * x.sl[k]; R += s * x.sr_[k]; } }
    else { const float sc = 1.f / std::sqrt((float)x.n);
      if (ty == WAVET) { const WaveTables& wt = waves(); const float pos = std::max(0.f, std::min(0.999f, e[iWave] + std::sin(TAU * x.wph) * e[iScan] * 0.5f)) * (WaveTables::W - 1); const int w0 = (int)pos; const float fr = pos - w0;
        for (int k = 0; k < x.n; k++) { const float f = x.f * x.ratio[k]; Osc& o = x.o[k]; o.ph += f / sr; o.ph -= (int)o.ph; const int lv = WaveTables::level(f, sr);
          const float s = (wt.read(w0, lv, o.ph) * (1 - fr) + wt.read(std::min(WaveTables::W - 1, w0 + 1), lv, o.ph) * fr) * sc * 0.8f; L += s * x.gl[k]; R += s * x.gr[k]; } }
      else for (int k = 0; k < x.n; k++) { const float s = x.o[k].tick(Osc::SAW, x.f * x.ratio[k], sr) * sc; L += s * x.gl[k]; R += s * x.gr[k]; } }
    L = x.lp.run(L, 0); R = x.lp.run(R, 1);
    if (e[AIR] > 0) { const float nz = x.bp.run(rng.bi(), 0) * e[AIR] * 0.83f; L += nz; R += nz; }
    l = L; r = R;
  }
};
static std::vector<ParamDef> padParams() { std::vector<ParamDef> p = {PITCH(), only(ST("voices", "UNISON", 2, 7, 5), "v:0,1"), only(pct("detune", "DETUNE", 0.35f), "v:0,1"), prHz("cut", "CUTOFF", 40, 20000, 1400), pct("res", "RES", 0), pct("fenv", "FILT ENV", 0), T("fdec", "FILT DEC", 0.05f, 10, 1.5f), pct("motion", "MOTION", 0.4f), pct("air", "AIR", 0.2f)};
  for (auto& q : envP(1.6f, 1, 0.8f, 3, 3.5f)) p.push_back(q); p.push_back(pct("vsens", "VEL SENS", 1)); p.push_back(pct("width", "WIDTH", 0.8f)); p.push_back(LEVEL());
  p.push_back(noMod(OP("type", "TYPE", {"CLASSIC", "WAVETABLE", "SPECTRAL"}, 0))); p.push_back(only(pct("wave", "WAVE", 0.3f), "v:1")); p.push_back(only(pct("scan", "SCAN", 0.3f), "v:1"));
  p.push_back(only(pct("tilt", "BRIGHT", 0.5f), "v:2")); p.push_back(only(pct("even", "EVEN", 0.5f), "v:2")); p.push_back(only(pct("shim", "SHIMMER", 0.5f), "v:2")); p.push_back(only(pct("spread", "SPREAD", 0.3f), "v:2"));
  return p; }
static Reg rPad(new ModuleDef{"pad", "PAD", Cat::Voice, {{"TRIG", PType::Trig}}, {{"OUT", PType::Audio}}, padParams(), 0, 68, false, [] { return new Pad(); }});



}  // namespace tb
