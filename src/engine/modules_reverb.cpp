// Timbient native: REVERB. One module, eight kinds of space (TYPE). Two engines underneath:
//  - a figure-of-eight plate tank (after Jon Dattorro's 1997 paper) for PLATE and HALL, and
//  - an eight-line feedback delay network (a Hadamard matrix mixing every line into every other) for the rest:
//    ROOM, CATHEDRAL, SHIMMER (a pitch shifter in its loop), BLOOM (two networks in a row: the first swells,
//    the second carries it), NEBULA (SPREAD from equal lines, which ring as echoes, to scattered ones, a wash)
//    and FROZEN (a flat tail that holds, then stops).
// Every type: DECAY to INFINITE, FREEZE, MOTION (the lines drift), TONE and LOW CUT inside the loop, WIDTH, MIX.
#include "params.h"

namespace tb {
namespace {

enum VerbType { ROOM, PLATE, HALL, CATHEDRAL, SHIMMER, BLOOM, NEBULA, FROZEN, NTYPES };

inline float softLimit(float x) { x = std::max(-3.f, std::min(3.f, x)); return x * (27 + x * x) / (27 + 9 * x * x); }   // tanh, near enough, and cheap
// a smoother read between samples than a straight line: a straight line dulls the highs a little every trip round,
// which in a tail that goes round hundreds of times is the difference between INFINITE and slowly fading
inline float readCubic(const DelayLine& d, float delay) {
  float rp = (float)d.w - delay - 1.f; while (rp < 0) rp += (float)(d.mask + 1);
  const int i = (int)rp; const float f = rp - i;
  const float xm = d.buf[(i - 1) & d.mask], x0 = d.buf[i & d.mask], x1 = d.buf[(i + 1) & d.mask], x2 = d.buf[(i + 2) & d.mask];
  const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2 * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
  return ((c3 * f + c2) * f + c1) * f + x0;
}
inline float onePoleA(float hz, float sr) { return 1 - std::exp(-TAU * std::min(hz, 0.45f * sr) / sr); }

struct AP {   // a diffuser: smears a sound into many small echoes without colouring it
  DelayLine d; float n = 1, g = 0.5f;
  float run(float x, float extra = 0) { const float z = readCubic(d, std::max(2.f, n + extra) - 1); const float w = x + g * z; d.write(w); return z - g * w; }
};

// ---- the network: eight lines, each read, filtered, scaled for the decay, mixed and written back ----
struct FDN {
  static constexpr int N = 8;
  DelayLine d[N]; float len[N] = {}, g[N] = {}, lp[N] = {}, hp[N] = {};
  float m0[N] = {}, m1[N] = {};   // the drift at the start and end of this block (in samples)
  double ph[N] = {}; float rate[N] = {}, wander[N] = {};
  void init(float maxSec, float sr) { for (int i = 0; i < N; i++) { d[i].init(maxSec, sr); rate[i] = 0.07f + 0.053f * i; ph[i] = 0.137 * i; } }
  void clear() { for (int i = 0; i < N; i++) { d[i].clear(); lp[i] = hp[i] = 0; } }
  void setDecay(float t60, float sr, bool inf) { for (int i = 0; i < N; i++) g[i] = inf ? 1.f : std::pow(10.f, -3.f * len[i] / (sr * std::max(0.05f, t60))); }
  void drift(float depth, int n, float sr, Rng& rng) {   // MOTION: each line's length sways slowly (sine plus a little wander)
    for (int i = 0; i < N; i++) { m0[i] = m1[i]; ph[i] += (double)rate[i] * n / sr; if (ph[i] >= 1) ph[i] -= 1;
      wander[i] += ((rng.uni() * 2 - 1) - wander[i]) * std::min(1.f, 0.3f * n / sr);
      m1[i] = depth * (0.75f * std::sin((float)(ph[i] * TAU)) + 0.5f * wander[i]); }
  }
  // one sample: inL feeds the even lines, inR the odd ones; f = how far through the block (for the drift)
  inline void tick(float inL, float inR, float& oL, float& oR, float f, float aLP, float aHP, bool filt, bool limit) {
    float y[N];
    for (int i = 0; i < N; i++) { const float md = m0[i] + (m1[i] - m0[i]) * f; y[i] = md == 0.f && len[i] == std::floor(len[i]) ? d[i].read(len[i] - 1) : readCubic(d[i], std::max(2.f, len[i] + md) - 1); }
    oL = 0.5f * (y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] - y[7]));   // listen at different lines on each side: a wide sound
    oR = 0.5f * (y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] - y[6]));
    float x[N];
    for (int i = 0; i < N; i++) { float v = y[i];
      if (filt) { lp[i] += aLP * (v - lp[i]); v = lp[i]; hp[i] += aHP * (v - hp[i]); v -= hp[i]; }   // TONE and LOW CUT, every trip round
      x[i] = v * g[i]; }
    for (int h = 1; h < N; h <<= 1) for (int i = 0; i < N; i += h * 2) for (int j = i; j < i + h; j++) { const float a = x[j], b = x[j + h]; x[j] = a + b; x[j + h] = a - b; }   // every line into every other, keeping the energy
    for (int i = 0; i < N; i++) { float v = x[i] * 0.35355339f + ((i & 1) ? inR : inL); if (limit) v = softLimit(v); d[i].write(sane(v)); }
  }
};

// ---- the plate tank ----
struct Tank {
  DelayLine pre; AP inAp[4]; AP mAp[2], ap2[2]; DelayLine dl1[2], dl2[2]; float n1[2] = {}, n2[2] = {}, lp[2] = {}, hp[2] = {}, bw = 0, fbk[2] = {};
  double ph[4] = {0, 0.3, 0.55, 0.8}; float drift[4] = {}; Smooth size; float sr = 48000;
  float sc() const { return sr / 29761.f; }
  void init(float sr_) {
    sr = sr_; const float k = sc() * 2.2f;
    const float inN[4] = {142, 107, 379, 277}, inG[4] = {0.75f, 0.75f, 0.625f, 0.625f};
    for (int i = 0; i < 4; i++) { inAp[i].d.init(inN[i] * sc() / sr + 0.01f, sr); inAp[i].n = inN[i] * sc(); inAp[i].g = inG[i]; }
    for (int h = 0; h < 2; h++) { mAp[h].d.init((h ? 908 : 672) * k / sr + 0.01f, sr); mAp[h].g = -0.7f; ap2[h].d.init((h ? 2656 : 1800) * k / sr + 0.01f, sr); ap2[h].g = 0.5f;
      dl1[h].init((h ? 4217 : 4453) * k / sr + 0.01f, sr); dl2[h].init((h ? 3163 : 3720) * k / sr + 0.01f, sr); }
    size.setTime(1.5f, sr);
  }
  void clear() { for (auto& a : inAp) a.d.clear(); for (int h = 0; h < 2; h++) { mAp[h].d.clear(); ap2[h].d.clear(); dl1[h].clear(); dl2[h].clear(); lp[h] = hp[h] = fbk[h] = 0; } bw = 0; }
  float loopLen(float S) const { return (4453 + 3720) * sc() * S / sr; }   // seconds for a trip round one half
  void block(int n, float mot, Rng& rng) {
    for (int k = 0; k < 4; k++) drift[k] += ((rng.uni() * 2 - 1) - drift[k]) * 0.0004f * n;
  }
  // diff: how much the input is smeared first (PLATE more); mot: MOTION
  inline void tick(float x, float& oL, float& oR, float g, float aLP, float aHP, bool filt, bool limit, float mot, float diff) {
    const float s0 = sc(), S = size.tick(), k2 = s0 * S;
    bw += 0.7f * (x - bw); float v = bw;
    for (int i = 0; i < 4; i++) { inAp[i].g = (i < 2 ? 0.75f : 0.625f) * diff; v = inAp[i].run(v); }
    for (int k = 0; k < 4; k++) { ph[k] += (0.11 + k * 0.067) / sr; if (ph[k] >= 1) ph[k] -= 1; }
    oL = oR = 0;
    for (int h = 0; h < 2; h++) {
      const float wob = mot * (std::sin((float)(ph[h] * TAU)) * 14.f + drift[h] * 10.f) * s0;
      float t = v + fbk[1 - h];
      mAp[h].n = (h ? 908 : 672) * k2; t = mAp[h].run(t, wob);
      dl1[h].write(t); n1[h] = (h ? 4217 : 4453) * k2; t = readCubic(dl1[h], std::max(2.f, n1[h] + mot * std::sin((float)(ph[h + 2] * TAU)) * 6 * s0) - 1);
      if (filt) { lp[h] += aLP * (t - lp[h]); t = lp[h]; hp[h] += aHP * (t - hp[h]); t -= hp[h]; }
      t *= g;
      ap2[h].n = (h ? 2656 : 1800) * k2; t = ap2[h].run(t);
      dl2[h].write(t); n2[h] = (h ? 3163 : 3720) * k2; t = readCubic(dl2[h], std::max(2.f, n2[h]) - 1) * g;
      fbk[h] = limit ? softLimit(t) : t;
      const float tap = dl1[h].read(n1[h] * 0.37f) + dl1[h].read(n1[h] * 0.81f) - dl2[h].read(n2[h] * 0.55f);
      (h ? oL : oR) += tap; (h ? oR : oL) -= 0.4f * dl2[h].read(n2[h] * 0.21f);
    }
  }
};

// ---- a pitch shifter: two read heads sliding through a short delay, crossfaded so the jumps don't show ----
struct Shifter {
  DelayLine d; float ph = 0, W = 3000;
  void init(float sr) { W = 0.07f * sr; d.init(0.1f, sr); }
  void clear() { d.clear(); }
  inline float run(float x, float ratio) {
    d.write(x); ph += (1 - ratio) / W; ph -= std::floor(ph);
    float p2 = ph + 0.5f; if (p2 >= 1) p2 -= 1;
    const float s = std::sin(PI * ph), wa = s * s;
    return d.read(ph * W + 1) * wa + d.read(p2 * W + 1) * (1 - wa);
  }
};

}  // namespace

struct Reverb : Module {
  enum { TYPE, DECAY, SIZE, PRE, TONE, LOWCUT, MOTION, SHIFT, AMOUNT, SPREAD, FREEZE, WIDTH, MIX };
  FDN net, swell; Tank tank; Shifter shL, shR, shD; DelayLine pdl[2]; AP diff[2][4];
  Smooth dry, wet, pd, sz, spr; float outLP = 1, outHP = 0, oz[2][2] = {}, sz2[4] = {}; float shEnv = 0, fbL = 0, fbR = 0, frzRms = 0, holdLeft = 0, inEnv = 0, xf = 1;
  int cur = -1, want = HALL; bool fading = false;
  static constexpr float MAXLINE = 1.6f;
  void prepare() override {
    net.init(MAXLINE, sr); swell.init(0.35f, sr); tank.init(sr); shL.init(sr); shR.init(sr); shD.init(sr);
    for (auto& d : pdl) d.init(0.51f, sr);
    for (int s = 0; s < 2; s++) for (int k = 0; k < 4; k++) diff[s][k].d.init(0.12f, sr);
    dry.setTime(0.012f, sr); wet.setTime(0.012f, sr); dry.snap(1 - e[MIX]); wet.snap(e[MIX]); pd.setTime(0.2f, sr); pd.snap(e[PRE] * sr);
    sz.setTime(0.6f, sr); sz.snap(e[SIZE]); spr.setTime(0.6f, sr); spr.snap(e[SPREAD]); tank.size.snap(hallScale(e[SIZE], (int)e[TYPE]));
    cur = -1;
  }
  static float hallScale(float s, int ty) { return ty == PLATE ? 0.3f + s * 0.55f : 0.4f + s * 1.8f; }
  bool frozen() const { return e[FREEZE] > 0.5f || eng->freeze.load(std::memory_order_relaxed); }
  void trig(int port, const Event&) override { if (port == 1) p[FREEZE].store(p[FREEZE].load() > 0.5f ? 0.f : 1.f); }   // FREEZE in: each trigger flips it

  // each type's lines (in seconds, before SIZE) and its input smearing
  void setLines(int ty, float s) {
    static const float R[8] = {0, 0.53f, 0.27f, 0.81f, 0.13f, 0.67f, 0.39f, 0.95f};   // NEBULA: how far each line moves away from the longest
    static const float base[8] = {1.0f, 1.13f, 1.27f, 1.41f, 1.59f, 1.73f, 1.89f, 2.07f};   // unrelated ratios: no line lines up with another
    float L[8];
    switch (ty) {
      case ROOM: { const float k = 0.012f * (0.6f + s * 1.4f); for (int i = 0; i < 8; i++) L[i] = k * base[i]; break; }
      case CATHEDRAL: { const float k = 0.075f * (0.7f + s * 1.2f); for (int i = 0; i < 8; i++) L[i] = k * base[i]; break; }
      case NEBULA: { const float top = 0.06f * std::pow(25.f, s), sp = spr.v; for (int i = 0; i < 8; i++) L[i] = top * (1 - 0.85f * sp * R[i]); break; }
      case FROZEN: { const float k = 0.045f * (0.6f + s * 1.2f); for (int i = 0; i < 8; i++) L[i] = k * base[i]; break; }
      default: { const float k = 0.04f * (0.6f + s * 1.6f); for (int i = 0; i < 8; i++) L[i] = k * base[i]; break; }   // SHIMMER, BLOOM
    }
    const bool still = std::fabs(sz.t - sz.v) < 1e-4f && std::fabs(spr.t - spr.v) < 1e-4f;   // whole samples once SIZE is still (nothing lost between them); in between while it moves, so it glides
    for (int i = 0; i < 8; i++) { const float v = std::min(MAXLINE * 0.98f, L[i]) * sr; net.len[i] = still ? std::round(v) : v; }
    if (ty == BLOOM) for (int i = 0; i < 8; i++) swell.len[i] = std::round(0.018f * base[(i + 3) & 7] * (0.7f + s) * sr);
    // input smearing: ROOM short and light, CATHEDRAL long and thick, NEBULA as much as SPREAD says
    static const float dl[4] = {0.0047f, 0.0036f, 0.0127f, 0.0093f};
    const float dk = ty == ROOM ? 0.6f : ty == CATHEDRAL ? 4.5f : ty == NEBULA ? 3.f : 1.5f;
    const float dg = ty == NEBULA ? 0.72f * spr.v : ty == ROOM ? 0.55f : 0.68f;
    for (int c = 0; c < 2; c++) for (int k = 0; k < 4; k++) { diff[c][k].n = std::min(0.11f * sr, dl[k] * dk * (c ? 1.07f : 1.f) * sr); diff[c][k].g = dg; }
  }
  void clearAll() { net.clear(); swell.clear(); tank.clear(); shL.clear(); shR.clear(); shD.clear(); for (auto& s : diff) for (auto& a : s) a.d.clear(); fbL = fbR = shEnv = frzRms = 0; holdLeft = 0; }

  void render(int a, int b) override {
    const int n = b - a; if (n <= 0) return;
    want = std::max(0, std::min(NTYPES - 1, (int)std::lround(e[TYPE])));
    if (cur < 0) { cur = want; clearAll(); xf = 1; }
    if (want != cur && !fading) fading = true;   // a new TYPE: the old tail fades out quickly, then the new one starts clean
    const int ty = cur; const bool fz = frozen();
    const bool inf = e[DECAY] >= 99.f;
    const float T60 = e[DECAY];
    sz.t = e[SIZE]; const float S = sz.v;   // SIZE moves slowly (the lines stretch rather than jump)
    spr.t = e[SPREAD]; for (int k = 0; k < n; k++) { sz.tick(); spr.tick(); }
    dry.t = 1 - e[MIX]; wet.t = e[MIX]; pd.t = e[PRE] * sr;
    const float aLP = e[TONE] >= 19500.f ? 1.f : onePoleA(e[TONE], sr), aHP = e[LOWCUT] <= 21.f ? 0.f : onePoleA(e[LOWCUT], sr);
    const float mot = e[MOTION], wd = e[WIDTH];
    // TONE and LOW CUT work inside the loop, so the tail darkens as it fades. When it never fades (INFINITE, FREEZE, FROZEN)
    // they'd slowly drain it, so there they shape what comes out instead
    const bool filt = !(fz || inf || ty == FROZEN); outLP = filt ? 1.f : aLP; outHP = filt ? 0.f : aHP;
    // gain so every type sits at about the same loudness
    static const float LOUD[NTYPES] = {0.9f, 0.77f, 0.9f, 1.67f, 1.2f, 0.85f, 1.5f, 1.3f};
    const float lvl = LOUD[ty];

    if (ty == PLATE || ty == HALL) {
      tank.size.t = hallScale(S, ty);
      const float loopT = tank.loopLen(tank.size.v) * 0.62f, g =   /* each half of the tank is scaled twice */ (inf || fz) ? 1.f : std::pow(10.f, -3.f * loopT / std::max(0.1f, T60));
      tank.block(n, mot, rng);
      const float diffAmt = ty == PLATE ? 1.f : 0.85f, m = ty == PLATE ? mot * 0.4f : mot;
      for (int i = a; i < b; i++) {
        const float d = dry.tick(), w = wet.tick(), pdt = pd.tick();
        const float x0 = 0.5f * (in[0][0][i] + in[0][1][i]);
        pdl[0].write(x0); const float x = fz ? 0.f : (pdt >= 1 ? pdl[0].read(pdt) : x0);
        float oL, oR; tank.tick(x, oL, oR, g, aLP, aHP, filt, inf || fz, m, diffAmt);
        finish(i, oL * 0.5f * lvl, oR * 0.5f * lvl, d, w, wd);
      }
      return;
    }

    // the network types
    setLines(ty, S);
    float t60 = T60 * 1.25f; bool netInf   /* (the loop's TONE shortens it: measured, this lands on DECAY) */ = inf || fz;
    if (ty == FROZEN && !fz) netInf = true;   // FROZEN holds without fading, and the gate below decides when it ends
    net.setDecay(t60, sr, netInf);
    if (ty == FROZEN && !fz) {   // while the gate is open: no loss at all, but if it gets loud it eases back
      const float over = std::max(0.f, frzRms - 0.18f); const float gg = std::max(0.9f, 1.f - over * 0.5f);
      if (holdLeft <= 0 && !inf) { net.setDecay(0.12f, sr, false); } else for (auto& g : net.g) g = gg; }
    if (ty == BLOOM) swell.setDecay(std::min(8.f, std::max(0.4f, (inf ? 10.f : T60) * 0.8f)), sr, false);   // the swell: nearly as long as the decay, so it rises slowly
    const float depth = mot * (ty == ROOM ? 0.0006f : ty == NEBULA ? 0.004f : ty == CATHEDRAL ? 0.0025f : 0.0018f) * sr;
    net.drift(depth, n, sr, rng); if (ty == BLOOM) swell.drift(depth * 0.5f, n, sr, rng);
    static const float SH[6][2] = {{2.f, 0}, {1.5f, 0}, {1.3348f, 0}, {0.5f, 0}, {2.f, 0.5f}, {4.f, 0}};   // +12, +7, +5, -12, +12 & -12, +24
    const int si = std::max(0, std::min(5, (int)std::lround(e[SHIFT])));
    const float amt = e[AMOUNT], shA = onePoleA(std::min(e[TONE], 9000.f), sr), shH = onePoleA(std::max(e[LOWCUT], 80.f), sr);
    float frzAcc = 0;
    for (int i = a; i < b; i++) {
      const float f = (float)(i - a) / n;
      const float d = dry.tick(), w = wet.tick(), pdt = pd.tick();
      float xl = in[0][0][i], xr = in[0][1][i];
      if (ty == FROZEN) { const float lv = std::max(std::fabs(xl), std::fabs(xr)); inEnv = lv > inEnv ? lv : inEnv * 0.9995f; }
      pdl[0].write(xl); pdl[1].write(xr);
      if (pdt >= 1) { xl = pdl[0].read(pdt); xr = pdl[1].read(pdt); }
      if (fz) xl = xr = 0;
      for (int k = 0; k < 4; k++) { xl = diff[0][k].run(xl); xr = diff[1][k].run(xr); }
      xl *= 0.35f; xr *= 0.35f;
      if (ty == SHIMMER && !fz) {   // the tail, shifted, goes back in: each trip round it climbs (or falls) again
        float sl = shL.run(fbL, SH[si][0]), sr2 = SH[si][1] > 0 ? shD.run(fbR, SH[si][1]) : shR.run(fbR, SH[si][0]);
        // the shifted part is always darkened and trimmed (even on INFINITE), or each octave up would climb out of hearing
        sz2[0] += shA * (sl - sz2[0]); sl = sz2[0]; sz2[1] += shH * (sl - sz2[1]); sl -= sz2[1];
        sz2[2] += shA * (sr2 - sz2[2]); sr2 = sz2[2]; sz2[3] += shH * (sr2 - sz2[3]); sr2 -= sz2[3];
        shEnv += (std::fabs(fbL) + std::fabs(fbR) - shEnv) * 0.0005f;
        const float k = amt * 0.55f / (1 + std::max(0.f, shEnv - 0.3f) * 6);   // if it builds up too far, it backs off
        xl += sl * k; xr += sr2 * k;
      }
      if (ty == BLOOM && !fz) { float sl, sr2; swell.tick(xl, xr, sl, sr2, f, 1.f, 0.f, false, fz); xl = sl * 0.6f; xr = sr2 * 0.6f; }
      if (ty == FROZEN && (holdLeft <= 0 && !inf)) { xl *= 0; xr *= 0; }   // the gate has shut: nothing new comes in until the next sound
      float oL, oR; net.tick(xl, xr, oL, oR, f, aLP, aHP, filt && ty != FROZEN, netInf || ty == SHIMMER);
      fbL = oL; fbR = oR;
      if (ty == FROZEN) frzAcc += oL * oL + oR * oR;
      finish(i, oL * lvl, oR * lvl, d, w, wd);
    }
    if (ty == FROZEN) {
      frzRms += (std::sqrt(frzAcc / (2 * n)) - frzRms) * 0.3f;
      if (inEnv > 0.003f) holdLeft = std::max(0.2f, T60);   // a sound: the wall holds for DECAY after it
      else holdLeft -= (float)n / sr;
    }
  }
  // WIDTH, the type change fade, and the mix
  inline void finish(int i, float L, float R, float d, float w, float wd) {
    if (outLP < 1.f || outHP > 0.f) { oz[0][0] += outLP * (L - oz[0][0]); L = oz[0][0]; oz[0][1] += outHP * (L - oz[0][1]); L -= oz[0][1];
      oz[1][0] += outLP * (R - oz[1][0]); R = oz[1][0]; oz[1][1] += outHP * (R - oz[1][1]); R -= oz[1][1]; }
    const float mid = 0.5f * (L + R), side = 0.5f * (L - R) * wd; L = mid + side; R = mid - side;
    if (fading) { xf -= 1.f / (0.03f * sr); if (xf <= 0) { xf = 0; fading = false; cur = want; clearAll(); setLines(cur, sz.v); tank.size.snap(hallScale(sz.v, cur)); } }
    else if (xf < 1) xf = std::min(1.f, xf + 1.f / (0.03f * sr));
    const float g = w * xf;
    out[0][0][i] = in[0][0][i] * d + sane(L) * g; out[0][1][i] = in[0][1][i] * d + sane(R) * g;
  }
};

static Reg rReverb(new ModuleDef{"reverb", "REVERB", Cat::Proc, {{"IN", PType::Audio}, {"FREEZE", PType::Trig}}, {{"OUT", PType::Audio}},
  {noMod(OP("type", "TYPE", {"ROOM", "PLATE", "HALL", "CATHEDRAL", "SHIMMER", "BLOOM", "NEBULA", "FROZEN"}, 2)),
   fmt(prExp("decay", "DECAY", 0.2f, 100, 6), [](float v) { if (v >= 99.f) return std::string("INFINITE"); char b[16]; std::snprintf(b, 16, v < 10 ? "%.1fS" : "%.0fS", v); return std::string(b); }),
   pctX("size", "SIZE", 1, 0.45f),
   fmt(pr("pre", "PRE-DELAY", 0, 0.5f, 0.02f), [](float v) { return std::to_string((int)std::lround(v * 1000)) + "MS"; }),
   fmt(prHz("tone", "TONE", 500, 20000, 9000), [](float v) { if (v >= 19500.f) return std::string("OPEN"); char b[16]; if (v >= 1000) std::snprintf(b, 16, "%.1fK", v / 1000); else std::snprintf(b, 16, "%d", (int)v); return std::string(b); }),
   fmt(prHz("lowcut", "LOW CUT", 20, 1000, 60), [](float v) { if (v <= 21.f) return std::string("OFF"); char b[16]; std::snprintf(b, 16, "%dHZ", (int)std::lround(v)); return std::string(b); }),
   pct("motion", "MOTION", 0.4f),
   only(noMod(OP("shift", "SHIFT", {"+12", "+7", "+5", "-12", "+12 & -12", "+24"}, 0)), "shimmer"),
   only(pct("amount", "SHIMMER", 0.5f), "shimmer"),
   only(pct("spread", "SPREAD", 0.6f), "nebula"),
   OP("freeze", "FREEZE", {"OFF", "ON"}, 0), pct("width", "WIDTH", 1), pct("mix", "MIX", 0.4f)}, 0, 68, false, [] { return new Reverb(); }});

}  // namespace tb
