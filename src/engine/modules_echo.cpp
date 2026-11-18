// Timbient native: ECHO. One module, thirteen kinds of repeat (TYPE):
//  light: DIGITAL (clean, the original), TAPE (wobble and saturation), ANALOGUE (a murky bucket-brigade delay),
//         PING-PONG (bouncing left and right), MULTI-TAP (several heads in a rhythm), REVERSE (each repeat backwards)
//  heavy: SHIMMER (each repeat pitched), SPIRAL (repeats bending further up or down), DUB (a swept resonant filter in the
//         loop, pushed into overload), DIFFUSE (each repeat smearing towards a reverb), LO-FI (each repeat crushed further),
//         CLOUD (repeats broken into scattered grains), STRUM (tiny echoes tuned to the key, ringing like strings)
// Shared: TIME (or NOTE when a clock is patched in), WIDTH, FEEDBACK, TONE, MIX.
#include "params.h"
#include "dsp2.h"

namespace tb {
namespace {

enum EchoType { DIGITAL, TAPE, ANALOGUE, PINGPONG, MULTITAP, REVERSE, SHIMMER, SPIRAL, DUB, DIFFUSE, LOFI, CLOUD, STRUM, NTYPES };

struct EGrain { bool on = false; double pos = 0; float rate = 1, t = 0, len = 1, gl = 0, gr = 0; };

}  // namespace

// how much each TYPE is lifted on the way out (a few spread their repeats thinner: MULTI-TAP, SHIMMER, SPIRAL, DUB, CLOUD,
// STRUM), so switching TYPE doesn't drop the level. DIGITAL, TAPE, ANALOGUE, PING-PONG, MULTI-TAP, REVERSE, SHIMMER, SPIRAL, DUB, DIFFUSE, LO-FI, CLOUD, STRUM
static const float TRIM[13] = {1, 1, 1, 1, 1.5f, 1, 1.45f, 1.5f, 1.5f, 1.2f, 1.3f, 1.5f, 2.4f};
struct Echo : Module {
  enum { TIME, DIV, SPREAD, FB, TONE, MIX, TYPE, WOW, DRIVE, AGE, PATTERN, SHIFT, BEND, SWEEP, SMEAR, CRUSH, SCATTER, DENSITY };
  DelayLine dl, dr; Biquad lp, lp2, bpL, bpR; Smooth tl, trr, fb, dry, wet, toneS; float lastTone = -1; int cnt = 0;
  double last = -1; float cp = 0, ppb = 1;   // the clock patched into CLOCK: how far apart its pulses are
  int cur = -1, want = 0; bool fading = false; float xf = 1;
  // TAPE / ANALOGUE wobble
  double wowPh = 0, flutPh = 0, chPh = 0, sweepPh = 0; float wowDrift = 0, wowTo = 0, hp[2] = {}, lpz[2] = {};
  // REVERSE: two heads
  float revPh = 0;
  // SHIMMER / SPIRAL
  Shifter4 shL, shR;
  // DIFFUSE
  DelayLine ad[2][4]; float adN[4] = {}, adC[4] = {}, smG = 0.5f;
  // LO-FI
  float hold[2] = {}, holdT = 0;
  // CLOUD
  static constexpr int G = 48; EGrain gr[G]; double nextG = 0;
  // STRUM: four strings
  DelayLine str[4]; float strLp[4] = {}, strLen[4] = {}; DelayLine strIn;
  // MULTI-TAP: how loud each eighth of TIME is, for RANDOM
  float tapG[8] = {}, tapTo[8] = {}; double tapT = 0;

  float secs() const { static const float B[8] = {0.25f, 1 / 3.f, 0.5f, 0.75f, 1, 1.5f, 2, 4};
    return clockedIn.load(std::memory_order_relaxed) && cp > 0 ? std::min(4.f, B[(int)e[DIV]] * cp * ppb) : std::min(4.f, e[TIME]); }
  void blockStart(int) override { clockedIn.store(patched(1)); }
  void trig(int port, const Event& ev) override { if (port != 1) return; const double now = t + eng->pos / sr;
    if (ev.period > 0) cp = ev.period; else if (last >= 0) { const float p = (float)(now - last); if (p > 0.01f && p < 8) cp = cp > 0 ? cp * 0.6f + p * 0.4f : p; }
    last = now; ppb = ev.ppb > 0 ? ev.ppb : (ppb > 0 ? ppb : 1); }
  void prepare() override {
    dl.init(8, sr); dr.init(8, sr); for (Smooth* s : {&tl, &trr}) s->setTime(0.3f, sr); for (Smooth* s : {&fb, &dry, &wet}) s->setTime(0.012f, sr);
    tl.snap(secs() * sr); trr.snap(std::min(7.9f, secs() * (1 + e[SPREAD] * 0.6f)) * sr); fb.snap(e[FB]); dry.snap(1 - e[MIX]); wet.snap(e[MIX]);
    toneS.setTime(0.01f, sr); toneS.snap(std::log(e[TONE])); lp.set(Biquad::LP, e[TONE], 0.707f, sr);
    shL.init(sr); shR.init(sr);
    for (int c = 0; c < 2; c++) for (int k = 0; k < 4; k++) ad[c][k].init(0.06f, sr);
    for (int k = 0; k < 4; k++) str[k].init(0.05f, sr); strIn.init(1.f, sr);
    for (int k = 0; k < 8; k++) tapG[k] = tapTo[k] = 1;
    cur = -1;
  }
  void clearAll() { dl.clear(); dr.clear(); shL.clear(); shR.clear(); for (auto& c : ad) for (auto& a : c) a.clear(); for (auto& s : str) s.clear(); strIn.clear();
    for (auto& g : gr) g.on = false; hp[0] = hp[1] = lpz[0] = lpz[1] = 0; for (auto& z : strLp) z = 0; revPh = 0; }

  void render(int a, int b) override {
    const int n = b - a; if (n <= 0) return;
    want = std::max(0, std::min(NTYPES - 1, (int)std::lround(e[TYPE])));
    if (cur < 0) { cur = want; clearAll(); }
    if (want != cur && !fading) fading = true;   // a new TYPE: the old repeats fade out quickly, then the new one starts clean
    const int ty = cur;
    const bool frz = eng->freeze.load(std::memory_order_relaxed);
    const float tm = secs();
    tl.t = tm * sr; trr.t = std::min(7.9f, tm * (ty == PINGPONG ? 1.f : 1 + e[SPREAD] * 0.6f)) * sr;
    fb.t = frz ? 0.995f : e[FB]; dry.t = 1 - e[MIX]; wet.t = e[MIX]; toneS.t = std::log(std::max(100.f, e[TONE]));
    // per-type settings for this block
    static const float SH[6] = {2.f, 1.5f, 1.3348f, 0.5f, 2.f, 4.f};
    const int si = std::max(0, std::min(5, (int)std::lround(e[SHIFT])));
    const float bendR = std::pow(2.f, e[BEND] / 12.f);
    if (ty == TAPE || ty == ANALOGUE) { if (rng.uni() < (float)n / sr * 0.7f) wowTo = rng.uni() * 2 - 1; }
    if (ty == DIFFUSE) { static const float L[4] = {0.0071f, 0.0113f, 0.0173f, 0.0241f}; for (int k = 0; k < 4; k++) adN[k] = L[k] * sr; }
    if (ty == STRUM) {   // four strings on chord notes of the key: 1st, 3rd, 5th and the octave, around middle C
      const int rt = root(), sc = scale(), ns = (int)scales()[sc].steps.size(); const int deg[4] = {ns, ns + 2, ns + 4, 2 * ns};
      for (int k = 0; k < 4; k++) { const float hz = 440.f * std::pow(2.f, (midiOf(deg[k] + ns, rt, sc) - 69) / 12.f); strLen[k] = sr / hz; } }
    if (ty == MULTITAP && (int)std::lround(e[PATTERN]) == 5) { tapT += (double)n / sr; if (tapT > std::max(0.25f, tm)) { tapT = 0; for (int k = 0; k < 8; k++) tapTo[k] = rng.uni() < 0.45f ? 1.f : 0.f; tapTo[7] = 1; } }
    const float drive = 1 + e[DRIVE] * e[DRIVE] * 10, age = e[AGE], crush = e[CRUSH];
    const float grains = 4 + e[DENSITY] * e[DENSITY] * 60;

    for (int i = a; i < b; i++) {
      const float x0 = 0.5f * (in[0][0][i] + in[0][1][i]), x = frz ? 0.f : x0, f = fb.tick();
      const float T = tl.tick(), TR = trr.tick();
      const float lt = toneS.tick(); if ((cnt++ & 7) == 0 && std::fabs(lt - lastTone) > 1e-4f) { lastTone = lt; lp.set(Biquad::LP, std::exp(lt), 0.707f, sr); }   // TONE glides
      float yl = 0, yr = 0;
      switch (ty) {
        default: case DIGITAL: {   // left feeds right through the tone filter; right comes back into left
          yl = dl.read(T); yr = dr.read(TR); dl.write(sane(x + yr * f)); dr.write(sane(lp.run(yl, 0))); break; }
        case TAPE: case ANALOGUE: {
          // the tape (or the bucket brigade's clock) wobbles: slow wow, a little flutter, a slow drift, all smooth
          const bool tp = ty == TAPE; const float wd = tp ? e[WOW] : 0.25f + age * 0.75f;
          wowPh += (tp ? 0.42 : 0.31) / sr; if (wowPh >= 1) wowPh -= 1; flutPh += 6.1 / sr; if (flutPh >= 1) flutPh -= 1; wowDrift += (wowTo - wowDrift) * (1.5f / sr);
          const float wob = wd * (std::sin((float)(wowPh * TAU)) * (tp ? 0.0025f : 0.0012f) + std::sin((float)(flutPh * TAU)) * (tp ? 0.00006f : 0.f) + wowDrift * (tp ? 0.003f : 0.0008f)) * sr;
          yl = readCubic(dl, std::max(2.f, T + wob)); yr = readCubic(dr, std::max(2.f, TR + wob * 1.07f));
          auto shape = [&](float v, int c) {   // darkening, a little low cut, and saturation, every repeat
            v = (float)lp.run(v, c);
            if (!tp) { lpz[c] += onePoleA(2600 - age * 1700, sr) * (v - lpz[c]); v = lpz[c]; v += (rng.uni() * 2 - 1) * 0.0006f * age; }   // the bucket brigade: darker, with a faint hiss of its clock
            hp[c] += onePoleA(tp ? 70.f : 110.f, sr) * (v - hp[c]); v -= hp[c];
            const float dv = tp ? drive : 1.4f + age; return softLimit(v * dv) / std::pow(dv, 0.9f); };
          dl.write(sane(x + shape(yr, 0) * f)); dr.write(sane(shape(yl, 1))); break; }
        case PINGPONG: {   // in on the left only; each repeat crosses to the other side
          yl = dl.read(T); yr = dr.read(T); dl.write(sane(x + (float)lp.run(yr, 0) * f)); dr.write(sane((float)lp.run(yl, 1) * (0.7f + 0.3f * f)));
          const float w = e[SPREAD]; const float m = 0.5f * (yl + yr); yl = m + (yl - m) * w; yr = m + (yr - m) * w; break; }
        case MULTITAP: {   // several heads along TIME in a rhythm; the last one feeds back
          static const float P[5][4] = {{0.25f, 0.5f, 0.75f, 1}, {0.375f, 0.75f, 1, 1}, {1 / 3.f, 2 / 3.f, 1, 1}, {0.25f, 0.375f, 1, 1}, {1 / 3.f, 0.5f, 5 / 6.f, 1}};
          const int pt = std::max(0, std::min(5, (int)std::lround(e[PATTERN])));
          if (pt < 5) { for (int k = 0; k < 4; k++) { if (k > 0 && P[pt][k] == P[pt][k - 1]) break; const float v = dl.read(std::max(1.f, T * P[pt][k])); const float pan = (k & 1) ? 0.8f : 0.2f; yl += v * (1 - pan) * 0.75f; yr += v * pan * 0.75f; } }
          else for (int k = 0; k < 8; k++) { tapG[k] += (tapTo[k] - tapG[k]) * 0.002f; if (tapG[k] < 0.001f) continue; const float v = dl.read(std::max(1.f, T * (k + 1) / 8.f)) * tapG[k]; const float pan = (k & 1) ? 0.8f : 0.2f; yl += v * (1 - pan) * 0.7f; yr += v * pan * 0.7f; }
          dl.write(sane(x + (float)lp.run(dl.read(T), 0) * f)); break; }
        case REVERSE: {   // two heads reading backwards over the last TIME, crossfaded so their jumps don't show
          revPh += 1.f / std::max(64.f, T); if (revPh >= 1) revPh -= 1;
          float o = 0; for (int k = 0; k < 2; k++) { float q = revPh + 0.5f * k; if (q >= 1) q -= 1; const float s = std::sin(PI * q); o += readCubic(dl, std::max(2.f, 2 * q * T)) * s * s; }
          yl = o; yr = dr.read(std::max(1.f, TR - T + 1));   // the right a little later, for width
          dl.write(sane(x + (float)lp.run(o, 0) * f)); dr.write(sane(o)); break; }
        case SHIMMER: case SPIRAL: {   // every repeat pitched: SHIMMER by an interval, SPIRAL by BEND (a little or a lot)
          yl = dl.read(T); yr = dr.read(TR);
          const float rl = ty == SHIMMER ? SH[si] : bendR, rr = ty == SHIMMER ? (si == 4 ? 0.5f : SH[si]) : bendR;
          float sl = shL.run((float)lp.run(yr, 0), rl), sr2 = shR.run((float)lp.run(yl, 1), rr);
          hp[0] += onePoleA(60, sr) * (sl - hp[0]); sl -= hp[0]; hp[1] += onePoleA(60, sr) * (sr2 - hp[1]); sr2 -= hp[1];   // nothing too deep builds up
          const float k = std::min(0.9f, f * 1.25f);   // the pitched heads add up a little quieter than they went in: made up, but never past 0.9 round the loop
          dl.write(sane(x + softLimit(sl * k))); dr.write(sane(softLimit(sr2 * 1.25f * (0.6f + 0.4f * f)))); break; }
        case DUB: {   // a resonant filter swept slowly inside the loop, pushed into overload
          sweepPh += (0.05 + e[SWEEP] * e[SWEEP] * 2.0) / sr; if (sweepPh >= 1) sweepPh -= 1;
          if ((cnt & 7) == 0) { const float c = std::exp(lt) * std::pow(2.f, 1.6f * std::sin((float)(sweepPh * TAU))); bpL.set(Biquad::BP, c, 3.5f, sr); bpR.set(Biquad::BP, c * 1.06f, 3.5f, sr); }
          yl = dl.read(T); yr = dr.read(TR);
          const float vl = softLimit((float)bpL.run(yr, 0) * 2.2f + yr * 0.25f), vr = softLimit((float)bpR.run(yl, 0) * 2.2f + yl * 0.25f);
          dl.write(sane(x + vl * f)); dr.write(sane(vr)); break; }
        case DIFFUSE: {   // each repeat through four diffusers: it smears a little more every time round
          yl = dl.read(T); yr = dr.read(TR);
          float vl = (float)lp.run(yr, 0), vr = (float)lp.run(yl, 1); smG += (0.3f + 0.45f * e[SMEAR] - smG) * 0.002f; const float g = smG;   /* SMEAR: how strongly each diffuser smears (it glides) */
          for (int k = 0; k < 4; k++) { adC[k] += (adN[k] - adC[k]) * 0.001f;   /* SMEAR glides the diffusers' lengths */
            for (int c = 0; c < 2; c++) { float& v = c ? vr : vl; const float z = readCubic(ad[c][k], std::max(2.f, adC[k] * (c ? 1.09f : 1.f))); const float w = v + g * z; ad[c][k].write(sane(w)); v = z - g * w; } }
          dl.write(sane(x + vl * f)); dr.write(sane(vr)); break; }
        case LOFI: {   // each repeat loses detail: fewer samples a second and fewer levels, more every time round
          yl = dl.read(T); yr = dr.read(TR);
          const float rateHz = 22000 * std::pow(0.08f, crush), levels = std::pow(2.f, 12 - crush * 9);
          holdT += rateHz / sr; if (holdT >= 1) { holdT -= std::floor(holdT); hold[0] = std::round(yr * levels) / levels; hold[1] = std::round(yl * levels) / levels; }
          dl.write(sane(x + (float)lp.run(hold[0], 0) * f * 0.93f));   /* (the crushing adds a little each time round: a touch less feedback keeps it from building) */ dr.write(sane((float)lp.run(hold[1], 1))); break; }
        case CLOUD: {   // grains taken from around TIME ago, scattered in time, pitch and space; the cloud feeds back
          nextG -= 1; while (nextG <= 0) { for (auto& g : gr) if (!g.on) {
              g.on = true; g.t = 0; g.len = (0.04f + 0.18f * rng.uni()) * sr; const float sc = e[SCATTER];
              float st = (rng.uni() * 2 - 1) * sc * 0.5f; const float u = rng.uni(); if (u < sc * 0.2f) st += 12; else if (u < sc * 0.3f) st -= 12;
              g.rate = std::pow(2.f, st / 12.f); g.pos = std::max(T * (1 + (rng.uni() * 2 - 1) * sc * 0.5f), (g.rate - 1) * g.len + 4);
              const float an = (0.5f + (rng.uni() - 0.5f) * (0.2f + sc)) * PI / 2; g.gl = std::cos(an); g.gr = std::sin(an); break; }
            nextG += sr / grains * (0.5f + rng.uni()); }
          for (auto& g : gr) if (g.on) { const float ph = g.t / g.len; if (ph >= 1) { g.on = false; continue; } const float s = std::sin(PI * ph), v = readCubic(dl, std::max(2.f, (float)g.pos)) * s * s;
            yl += v * g.gl; yr += v * g.gr; g.pos += 1 - g.rate; g.t += 1; }
          yl *= 0.9f; yr *= 0.9f;
          dl.write(sane(x + softLimit((float)lp.run(0.5f * (yl + yr), 0) * f * 1.5f))); break; }
        case STRUM: {   // the sound plucks four strings in turn (TIME sets how far apart), each ringing on a chord note of the key
          strIn.write(x);
          const float ring = 0.3f + f * f * 9, damp = onePoleA(std::exp(lt) * 2, sr);   // FEEDBACK: how long the strings ring (0.3 s to about 8)
          for (int k = 0; k < 4; k++) { const float ex = k == 0 ? x : strIn.read(std::max(1.f, k * T * 0.08f));
            const float z = readCubic(str[k], std::max(2.f, strLen[k] - 1)); strLp[k] += damp * (z - strLp[k]);
            const float dk = std::pow(10.f, -3.f * strLen[k] / (sr * ring)); str[k].write(sane(ex * 0.35f + strLp[k] * dk)); const float pan = 0.2f + 0.2f * k; yl += z * (1 - pan); yr += z * pan; }
          yl *= 0.8f; yr *= 0.8f; break; }
      }
      // WIDTH: 0 in the middle, 1 as it comes (PING-PONG did its own)
      if (ty != PINGPONG) { const float w = ty == DIGITAL || ty == TAPE || ty == ANALOGUE ? 1.f : e[SPREAD]; const float m = 0.5f * (yl + yr); yl = m + (yl - m) * w; yr = m + (yr - m) * w; }
      if (fading) { xf -= 1.f / (0.03f * sr); if (xf <= 0) { xf = 0; fading = false; cur = want; clearAll(); } }
      else if (xf < 1) xf = std::min(1.f, xf + 1.f / (0.03f * sr));
      const float d = dry.tick(), w = wet.tick() * xf;
      yl *= TRIM[ty]; yr *= TRIM[ty];   // each type brought to about the level of what comes in
      out[0][0][i] = in[0][0][i] * d + softLimit(sane(yl)) * w; out[0][1][i] = in[0][1][i] * d + softLimit(sane(yr)) * w;   // a gentle ceiling (the resonant types can build)
    }
  }
};

static Reg rEcho(new ModuleDef{"echo", "ECHO", Cat::Proc, {{"IN", PType::Audio}, {"CLOCK", PType::Trig}}, {{"OUT", PType::Audio}},
  {only(T("time", "TIME", 0.05f, 4, 0.71f), "free"), only(OP("div", "NOTE", {"1/16", "1/8T", "1/8", "1/8 DOT", "1/4", "1/4 DOT", "1/2", "1 BAR"}, 4), "clock"),
   pct("spread", "WIDTH", 0.6f), pr("fb", "FEEDBACK", 0, 0.95f, 0.5f), prHz("tone", "TONE", 300, 8000, 1600), pct("mix", "MIX", 0.4f),
   noMod(OP("type", "TYPE", {"DIGITAL", "TAPE", "ANALOGUE", "PING-PONG", "MULTI-TAP", "REVERSE", "SHIMMER", "SPIRAL", "DUB", "DIFFUSE", "LO-FI", "CLOUD", "STRUM"}, 0)),
   only(pct("wow", "WOW", 0.4f), "v:1"), only(pct("drive", "SATURATE", 0.35f), "v:1"),
   only(pct("age", "AGE", 0.4f), "v:2"),
   only(noMod(OP("pattern", "PATTERN", {"EVEN", "DOTTED", "TRIPLET", "GALLOP", "SWING", "RANDOM"}, 0)), "v:4"),
   only(noMod(OP("shift", "SHIFT", {"+12", "+7", "+5", "-12", "+12 & -12", "+24"}, 0)), "v:6"),
   only(fmt(pr("bend", "BEND", -12, 12, 1), [](float v) { char b[24]; std::snprintf(b, 24, "%+.1f PER REPEAT", v); return std::string(b); }), "v:7"),
   only(pct("sweep", "SWEEP", 0.3f), "v:8"),
   only(pct("smear", "SMEAR", 0.5f), "v:9"),
   only(pct("crush", "CRUSH", 0.4f), "v:10"),
   only(pct("scatter", "SCATTER", 0.4f), "v:11"), only(pct("density", "DENSITY", 0.5f), "v:11")}, 0, 68, false, [] { return new Echo(); }});

}  // namespace tb
