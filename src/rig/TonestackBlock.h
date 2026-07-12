#pragma once
// TonestackBlock -- circuit-exact passive tone stack emulation, one instance per
// amp (A/B), placeable PRE or POST the NAM capture (chain decides; this block
// exposes processStack() / processGraphic() entry points).
//
// WHY: NAM captures are static snapshots -- the amp's tone stack is baked in at
// capture settings. This block gives every rig live, authentic Bass/Mid/Treble
// that behaves EXACTLY like the modelled amp's stack, because it IS the
// modelled amp's stack: each model is a component-value netlist of the real
// circuit (schematic-verified values), solved exactly.
//
// HOW (the engine): a tone stack is a passive linear RC network, so its
// transfer function is a low-order rational H(s) whose coefficients depend on
// the pot positions (Yeh & Smith, "Discretization of the '59 Fender Bassman
// Tone Stack", DAFx-06). Instead of hardcoding per-topology symbolic formulas,
// we store the NETLIST and identify H(s) numerically on every control change:
//
//   1. stamp the (<= 8 node) complex nodal matrix and solve it at 2N+3
//      log-spaced frequencies (tiny complex Gaussian solves);
//   2. fit the known-order rational exactly by linear least squares in a
//      frequency-normalised s (this is interpolation, not curve fitting: for a
//      known-order rational system the fit is exact to machine precision);
//   3. bilinear-transform to a direct-form digital filter (c = 2*fs, the
//      Yeh/Smith choice -- exact at DC, warp only near Nyquist).
//
// Adding an amp = adding a table row. The offline harness
// (tests/tonestack_test.cpp) re-solves the same netlists with an independent
// MNA evaluator at non-fitted frequencies and verifies the running filter
// matches the circuit across a dense knob grid.
//
// AUTHENTICITY NOTES (sources logged in docs/tonestack/RESEARCH.md):
//  * FMV family: two real wirings ship. 'Pot' = 5F6-A/Marshall mid pot as
//    potentiometer (matches the DAFx-06 formula EXACTLY -- verified to 4e-15).
//    'Rheo' = blackface-family mid wired as variable resistor (input+wiper
//    jumpered) -- a genuinely different circuit (up to ~28% response delta)
//    that most emulations ignore. All-controls-zero dropout quirk included.
//  * Source impedance is part of each model: cathode-follower fed (~1.3k,
//    tweed Bassman/Marshall family) vs plate fed (~38k, blackface family).
//    Yeh/CAPS assume an ideal source; for plate-fed stacks the source Z
//    audibly reshapes the response, so we put it in the netlist.
//  * VOX TB ships with the CUT control: the real one lives across the phase
//    inverter outputs (250k lin + 4n7 between anti-phase plates); we use the
//    exact 2-node solution of that differential bridge (closed form below).
//  * 5E3 ships the full interactive network: signal enters the volume pot
//    WIPER (it loads, it does not divide), 500p bright cap across the pot's
//    upper section, tone pot bridging the two channel nodes with 5n bleed at
//    its wiper, the unused channel's volume as a third authentic knob
//    ("ghost"), and V2A Miller capacitance (~110p). All five documented
//    interaction quirks reproduce (see harness T-checks).
//  * MARK graphic: five REAL series-RLC branches (the panel labels lie: true
//    centres 88/372/723/1576/4823 Hz), MXR-style op-amp bus, closed-form
//    per-band boost/cut biquads sharing w0 (max +/-17 dB low bands, +/-12 dB
//    top two -- that asymmetry is in the real circuit's Rk). Band-to-band bus
//    interaction is approximated by cascading (documented). MARK-ONLY, like
//    the hardware: processGraphic() self-gates on the model, so the authentic
//    pairing (Mark TMB pre + graphic post) is automatic.
//
// Level: per-model STATIC makeup (computed once per model at prepare/model
// change from the noon-settings response, 300-1200 Hz geometric mean) so
// models land near unity at noon; knob moves then change level authentically.
// No reactive gain riding anywhere.

#include "Blocks.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace nam_rig
{

class TonestackBlock : public MonoBlock
{
public:
    enum Model
    {
        kTweedBassman = 0, // 5F6-A ladder, CF-fed, mid pot   (FMV-pot)
        kBlackface,        // AB763, plate-fed, mid rheostat  (FMV-rheo)
        kTwinAA270,        // '69 Twin, 120p treble cap       (FMV-rheo)
        kMarkTMB,          // Mesa Mark rotary TMB, plate-fed (FMV-rheo)
        kJCM800,           // 2203/2204, CF-fed               (FMV-pot)
        kJTM45,            // JTM45, CF-fed                   (FMV-pot)
        kMajorLead,        // Major Lead 200                  (FMV-pot)
        kSLO,              // Soldano SLO-100                 (FMV-pot)
        kSovtekMig,        // MIG-100H                        (FMV-pot)
        kPeaveyC20,        // classic 20 SS-ish voicing       (FMV-rheo)
        kRolandSS,         // Cube-60 solid-state clean       (FMV-rheo)
        kVoxTB,            // AC30 Top Boost (fixed mid) + CUT(FMV-rheo + cut)
        kJames,            // passive James/Baxandall (Orange Graphic MkII values)
        kTweed5E3,         // 5E3 Deluxe interactive vol/tone/ghost network
        kNumModels
    };

    static constexpr int kMaxOrder = 8; // delta mode fits a RATIO of two
                                        // same-topology responses (worst case
                                        // order 2N); smallest-sufficient-order
                                        // still lands at 3-5 in typical use

    // ---------------- public control surface ----------------
    // The SHIPPING behaviour is relative ("delta") and there is NO user-facing
    // mode: apply H(knobs)/H(ref), ref = the model's default control positions
    // (noon; Cut/Ghost at 0). At the defaults the stack is an EXACT passthrough
    // -- the capture's baked-in stack IS the reference -- and moving a knob
    // applies the circuit-exact DIFFERENCE the real control geometry produces,
    // layered on top of the capture. All knob interaction survives (only a
    // fixed normalisation divides out); insertion loss cancels, so no makeup.
    // setDeltaMode(false) switches to the ABSOLUTE circuit response (static
    // makeup): kept as a harness hook -- tests/tonestack_test.cpp pins the
    // absolute response against the paper + an independent MNA. Product call
    // 2026-07-11: not exposed as a parameter.
    void setDeltaMode(bool d)
    {
        if (d != mDelta) { mDelta = d; mModelDirty = true; mDirty = true; }
    }
    bool deltaMode() const { return mDelta; }

    void setModel(int m)
    {
        int mm = std::min(std::max(m, 0), (int)kNumModels - 1);
        if (mm != mModel) { mModel = mm; mModelDirty = true; mDirty = true; }
    }
    int model() const { return mModel; }

    // CAPTURE CALIBRATION: pack sheets often list the amp's knob positions at
    // capture time (e.g. Amalgam's "B-5 M-6 T-4"). Setting those here moves
    // the flat reference from noon to the CAPTURE settings, so knobs parked at
    // the sheet values are a bit-exact passthrough and any move is the exact
    // retune of the real amp away from its captured state. Values are raw
    // knob rotation 0..1 (a sheet "5" on a 1-10 panel = the same rotation the
    // tone knobs use, so matching numbers is what matters). No smoothing:
    // these are setup values; the change lands via the commit crossfade.
    void setReference(float t, float m, float l, float cut)
    {
        auto cl = [](float v) { return std::min(std::max(v, 0.0f), 1.0f); };
        t = cl(t); m = cl(m); l = cl(l); cut = cl(cut);
        if (t != mRefK1 || m != mRefK2 || l != mRefK3 || cut != mRefCut)
        {
            mRefK1 = t; mRefK2 = m; mRefK3 = l; mRefCut = cut;
            mModelDirty = true; // recompute the cached reference divisor
            mDirty = true;
        }
    }

    // Knob meanings per model:
    //   FMV family : k1 = Treble, k2 = Mid (ignored where fixed), k3 = Bass
    //   VoxTB      : k1 = Treble, k3 = Bass, cut = Cut (authentic: up = darker)
    //   James      : k1 = Treble, k3 = Bass
    //   5E3        : k1 = Tone,   k3 = Volume, ghost = unused channel volume
    void setKnob1(float v) { setP(mK1t, v); }
    void setKnob2(float v) { setP(mK2t, v); }
    void setKnob3(float v) { setP(mK3t, v); }
    void setCut(float v)   { setP(mCutT, v); }
    void setGhost(float v) { setP(mGhostT, v); }

    void setGraphicOn(bool on) { mGraphicOn = on; }
    bool graphicOn() const { return mGraphicOn; }
    // slider in [-1, +1], 0 = flat (centre detent)
    void setGraphicBand(int band, float v)
    {
        if (band < 0 || band >= 5) return;
        float c = std::min(std::max(v, -1.0f), 1.0f);
        if (c != mBandT[band]) { mBandT[band] = c; mGraphicDirty = true; }
    }

    // ---------------- MonoBlock ----------------
    const char *name() const override { return "TONESTACK"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        // pot smoothing ~10 ms
        mSmoothA = std::exp(-1.0 / (0.010 * mFs));
        mModelDirty = true;
        mDirty = true;
        mGraphicDirty = true;
        reset();
    }

    void reset() override
    {
        for (double &z : mZ) z = 0.0;
        for (double &z : mZOld) z = 0.0;
        for (double &z : mZCut) z = 0.0;
        for (double &z : mZCut0) z = 0.0;
        mFadePending = false;
        for (auto &b : mBands) { b.z1 = b.z2 = 0.0; }
        // snap smoothers to targets
        mK1 = mK1t; mK2 = mK2t; mK3 = mK3t; mCut = mCutT; mGhost = mGhostT;
        mDirty = true;
    }

    void process(float *mono, int numSamples) override
    {
        processStack(mono, numSamples);
        processGraphic(mono, numSamples);
    }

    // stack section: place PRE (default, shapes what drives the capture) or
    // POST the amp -- the chain decides where to call this.
    void processStack(float *mono, int numSamples)
    {
        // control updates in <= 64-sample slices so a knob jump glides over
        // ~10 ms instead of stepping once per host block (de-zipper)
        int done = 0;
        while (done < numSamples)
        {
            const int n2 = std::min(numSamples - done, 64);
            updateIfNeeded(n2);
            float *seg = mono + done;
            // The recursion ALWAYS runs at kMaxOrder: unused high slots carry
            // zero coefficients (arithmetically identical to the fitted order),
            // so state slots never change meaning when the fitted order moves
            // during a knob glide -- no state resets, no zipper (T11).
            const int n = kMaxOrder;
            if (!mFadePending)
            {
                for (int i = 0; i < n2; ++i)
                {
                    double x = (double)seg[i] * mMakeup;
                    // transposed direct form II, order n
                    double y = mB[0] * x + mZ[0];
                    for (int k = 1; k <= n; ++k)
                        mZ[k - 1] = mB[k] * x - mA[k] * y + (k < n ? mZ[k] : 0.0);
                    seg[i] = (float)y;
                }
            }
            else
            {
                // coefficient change this chunk: run the snapshot (old) and the
                // live (new) filter in parallel and equal-power-free linear-fade
                // across the chunk -- clickless by construction, and repeated
                // chunk-rate commits during a glide chain into a smooth ramp.
                mFadePending = false;
                const double wStep = 1.0 / (double)n2;
                double w = 0.0;
                for (int i = 0; i < n2; ++i)
                {
                    w += wStep;
                    double x = (double)seg[i] * mMakeup;
                    double yNew = mB[0] * x + mZ[0];
                    for (int k = 1; k <= n; ++k)
                        mZ[k - 1] = mB[k] * x - mA[k] * yNew + (k < n ? mZ[k] : 0.0);
                    double yOld = mBOld[0] * x + mZOld[0];
                    for (int k = 1; k <= n; ++k)
                        mZOld[k - 1] = mBOld[k] * x - mAOld[k] * yOld + (k < n ? mZOld[k] : 0.0);
                    seg[i] = (float)(yOld + w * (yNew - yOld));
                }
            }
            if (mModel == (int)kVoxTB)
            {
                if (mDelta && mCut == mRefCut)
                {
                    // delta + knob at reference: exactly flat, skip both stages
                    for (double &z : mZCut) z = 0.0;
                    for (double &z : mZCut0) z = 0.0;
                }
                else
                {
                    for (int i = 0; i < n2; ++i)
                    {
                        double x = (double)seg[i];
                        double y = mBCut[0] * x + mZCut[0];
                        for (int k = 1; k <= 3; ++k)
                            mZCut[k - 1] = mBCut[k] * x - mACut[k] * y + (k < 3 ? mZCut[k] : 0.0);
                        seg[i] = (float)y;
                    }
                    if (mDelta)
                    {
                        for (int i = 0; i < n2; ++i)
                        {
                            double x = (double)seg[i];
                            double y = mBCut0[0] * x + mZCut0[0];
                            for (int k = 1; k <= 3; ++k)
                                mZCut0[k - 1] = mBCut0[k] * x - mACut0[k] * y + (k < 3 ? mZCut0[k] : 0.0);
                            seg[i] = (float)y;
                        }
                    }
                }
            }
            done += n2;
        }
    }

    // graphic section: authentic position is POST (after the amp), and it
    // only exists where the real hardware has it -- the Mark. Selecting any
    // other model silently disables it (params stay live for preset recall).
    void processGraphic(float *mono, int numSamples)
    {
        if (!mGraphicOn) return;
        if (mModel != (int)kMarkTMB) return;
        if (mGraphicDirty) designGraphic();
        for (int b = 0; b < 5; ++b)
        {
            Band &bd = mBands[b];
            for (int i = 0; i < numSamples; ++i)
            {
                double x = mono[i];
                double y = bd.b0 * x + bd.z1;
                bd.z1 = bd.b1 * x - bd.a1 * y + bd.z2;
                bd.z2 = bd.b2 * x - bd.a2 * y;
                mono[i] = (float)y;
            }
        }
    }

    // ---------------- introspection for the harness ----------------
    // Exact analog reference: |H| of the CIRCUIT (independent MNA solve) at
    // the CURRENT smoothed controls. The test compares the running digital
    // filter against this through the bilinear map.
    std::complex<double> referenceAnalogH(double fHz) const
    {
        Net net;
        buildNet(net, mModel, (double)mK1, (double)mK2, (double)mK3, (double)mGhost);
        return solveNet(net, 2.0 * kPi * fHz);
    }
    std::complex<double> referenceCutH(double fHz) const
    {
        double c[4], d[4];
        cutAnalogCoeffs((double)mCut, c, d);
        std::complex<double> s(0.0, 2.0 * kPi * fHz);
        std::complex<double> num = ((c[0] * s + c[1]) * s + c[2]) * s + c[3];
        std::complex<double> den = ((d[0] * s + d[1]) * s + d[2]) * s + d[3];
        return num / den;
    }
    double makeup() const { return mMakeup; }
    int analogOrder() const { return orderOf(mModel); }
    void analogCoeffs(double *b, double *a) const // normalised s (w0 = 2*pi*1k)
    {
        for (int i = 0; i <= kMaxOrder; ++i) { b[i] = mBs[i]; a[i] = mAs[i]; }
    }
    // graphic band exact analog response (closed form, shared-w0 peaking)
    std::complex<double> referenceBandH(int band, double fHz) const
    {
        double a = 0.5 * (1.0 - (double)mBandT[band]); // slider -> alpha (linear pot)
        a = std::min(std::max(a, 1e-4), 1.0 - 1e-4);
        const BandRLC &q = kBandRLC[band];
        double Rn = kEqBus * (1.0 - a) + q.R + kEqPot * a * (1.0 - a);
        double Rd = kEqBus * a + q.R + kEqPot * a * (1.0 - a);
        std::complex<double> s(0.0, 2.0 * kPi * fHz);
        std::complex<double> LC = q.L * q.C * s * s;
        return (LC + q.C * Rn * s + 1.0) / (LC + q.C * Rd * s + 1.0);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    // ---------------- model tables ----------------
    enum Topo { FmvPot, FmvRheo, JamesTopo, E3Topo };
    enum Taper { Lin, LogA, LogB }; // LogA: 10% @ half, LogB: 15% @ half

    struct Spec
    {
        Topo topo;
        // FMV: C1 C2 C3 R1(treble) R2(bass) R3(mid) R4(slope); James/5E3 ignore
        double C1, C2, C3, R1, R2, R3, R4;
        double Rs, RL;      // source Z feeding the stack, load on its output
        double midFixed;    // >= 0: mid knob pinned (VoxTB) else -1
        Taper tapT, tapM, tapL;
    };

    static const Spec &spec(int m)
    {
        // Values: Duncan-TSC/CAPS lineage cross-checked against schematics and
        // the DAFx-06 paper; provenance per model in docs/tonestack/RESEARCH.md.
        static const Spec kSpecs[kNumModels] = {
        /*Tweed 5F6-A */ {FmvPot , 250e-12,  20e-9, 20e-9, 250e3, 1e6  , 25e3, 56e3 , 1300.0, 1e6  , -1.0, Lin, Lin, LogA},
        /*Blackface   */ {FmvRheo, 250e-12, 100e-9, 47e-9, 250e3, 250e3, 10e3, 100e3, 38e3  , 1e6  , -1.0, LogA, Lin, LogA},
        /*Twin AA270  */ {FmvRheo, 120e-12, 100e-9, 47e-9, 250e3, 250e3, 10e3, 100e3, 38e3  , 1e6  , -1.0, LogA, Lin, LogA},
        /*Mark TMB    */ {FmvRheo, 250e-12, 100e-9, 47e-9, 250e3, 250e3, 25e3, 100e3, 38e3  , 1e6  , -1.0, LogA, Lin, LogA},
        /*JCM800      */ {FmvPot , 470e-12,  22e-9, 22e-9, 220e3, 1e6  , 22e3, 33e3 , 1300.0, 517e3, -1.0, Lin, Lin, LogB},
        /*JTM45       */ {FmvPot , 270e-12,  22e-9, 22e-9, 250e3, 1e6  , 25e3, 33e3 , 1300.0, 517e3, -1.0, Lin, Lin, LogB},
        /*Major Lead  */ {FmvPot , 500e-12,  22e-9, 22e-9, 250e3, 1e6  , 25e3, 33e3 , 1300.0, 517e3, -1.0, Lin, Lin, LogB},
        /*SLO-100     */ {FmvPot , 470e-12,  20e-9, 20e-9, 250e3, 1e6  , 25e3, 47e3 , 1300.0, 1e6  , -1.0, Lin, Lin, LogB},
        /*Sovtek MIG  */ {FmvPot , 470e-12,  22e-9, 22e-9, 500e3, 1e6  , 10e3, 47e3 , 1300.0, 1e6  , -1.0, Lin, Lin, LogB},
        /*Peavey C20  */ {FmvRheo, 270e-12,  22e-9, 22e-9, 250e3, 250e3, 20e3, 68e3 , 100.0 , 1e6  , -1.0, LogA, Lin, LogA},
        /*Roland SS   */ {FmvRheo, 240e-12,  33e-9, 82e-9, 250e3, 250e3, 10e3, 41e3 , 100.0 , 1e6  , -1.0, LogA, Lin, LogA},
        /*Vox TB      */ {FmvRheo,  50e-12,  22e-9, 22e-9, 1e6  , 1e6  , 10e3, 100e3, 38e3  , 1e6  ,  1.0, LogA, Lin, LogA},
        /*James       */ {JamesTopo, 2200e-12, 22e-9, 1500e-12, 0, 0, 0, 0    , 49e3  , 1e6  , -1.0, LogA, Lin, LogA},
        /*Tweed 5E3   */ {E3Topo ,       0,      0,     0, 0, 0, 0, 0        , 20e3  , 1e12 , -1.0, LogA, Lin, LogA},
        };
        return kSpecs[std::min(std::max(m, 0), (int)kNumModels - 1)];
    }

    static int orderOf(int m)
    {
        switch (spec(m).topo)
        {
            case JamesTopo: return 4;
            case E3Topo:    return 5;
            default:        return 3;
        }
    }

    static double taper(Taper tp, double x)
    {
        switch (tp)
        {
            case LogA: return (std::pow(81.0, x) - 1.0) / 80.0;
            case LogB: return (std::pow(32.11, x) - 1.0) / 31.11;
            default:   return x;
        }
    }

    // ---------------- tiny nodal solver ----------------
    // Nets are small: numbered nodes, node 0 = ground, node 1 = source (1V).
    struct Net
    {
        struct El { double val; int a, b; bool isC; };
        El els[20];
        int nEls = 0, nNodes = 0, outNode = 0;
        void R(double v, int a, int b) { els[nEls++] = { std::max(v, 1.0), a, b, false }; }
        void C(double v, int a, int b) { els[nEls++] = { v, a, b, true }; }
    };

    static std::complex<double> solveNet(const Net &net, double w)
    {
        // stamp Y, eliminate ground(0) and source(1); solve for outNode.
        const int n = net.nNodes;              // includes gnd+src
        std::complex<double> Y[10][10] = {};
        std::complex<double> rhs[10] = {};
        const std::complex<double> jw(0.0, w);
        for (int e = 0; e < net.nEls; ++e)
        {
            const Net::El &el = net.els[e];
            std::complex<double> y = el.isC ? jw * el.val
                                            : std::complex<double>(1.0 / el.val, 0.0);
            int a = el.a, b = el.b;
            // every non-ground, non-source endpoint gets the diagonal add;
            // a source endpoint contributes a Norton current y*1V instead.
            if (a > 1) Y[a][a] += y;
            if (b > 1) Y[b][b] += y;
            if (a > 1 && b > 1) { Y[a][b] -= y; Y[b][a] -= y; }
            if (a == 1 && b > 1) rhs[b] += y;
            if (b == 1 && a > 1) rhs[a] += y;
        }
        // Gaussian elimination over nodes 2..n-1 (partial pivot)
        int idx[10]; int u = 0;
        for (int i = 2; i < n; ++i) idx[u++] = i;
        std::complex<double> A[8][9];
        for (int r = 0; r < u; ++r)
        {
            for (int c = 0; c < u; ++c) A[r][c] = Y[idx[r]][idx[c]];
            A[r][u] = rhs[idx[r]];
        }
        for (int col = 0; col < u; ++col)
        {
            int piv = col;
            for (int r = col + 1; r < u; ++r)
                if (std::abs(A[r][col]) > std::abs(A[piv][col])) piv = r;
            if (piv != col)
                for (int c = col; c <= u; ++c) std::swap(A[piv][c], A[col][c]);
            std::complex<double> d = A[col][col];
            if (std::abs(d) < 1e-30) d = 1e-30;
            for (int r = col + 1; r < u; ++r)
            {
                std::complex<double> f = A[r][col] / d;
                if (f == std::complex<double>(0.0, 0.0)) continue;
                for (int c = col; c <= u; ++c) A[r][c] -= f * A[col][c];
            }
        }
        std::complex<double> x[8];
        for (int r = u - 1; r >= 0; --r)
        {
            std::complex<double> acc = A[r][u];
            for (int c = r + 1; c < u; ++c) acc -= A[r][c] * x[c];
            std::complex<double> d = A[r][r];
            if (std::abs(d) < 1e-30) d = 1e-30;
            x[r] = acc / d;
        }
        for (int r = 0; r < u; ++r)
            if (idx[r] == net.outNode) return x[r];
        return 0.0;
    }

    // ---------------- netlists ----------------
    // knobs arrive RAW 0..1; tapers applied inside.
    static void buildNet(Net &net, int model, double k1, double k2, double k3, double ghost)
    {
        const Spec &sp = spec(model);
        const double t = taper(sp.tapT, k1);
        const double m = sp.midFixed >= 0.0 ? sp.midFixed : taper(sp.tapM, k2);
        const double l = taper(sp.tapL, k3);
        net.nEls = 0;
        switch (sp.topo)
        {
        case FmvPot:
        {
            // nodes: 0 gnd, 1 src, 2 in, 3 T, 4 O(out), 5 K, 6 J, 7 M, 8 W
            net.nNodes = 9; net.outNode = 4;
            net.R(sp.Rs, 1, 2);
            net.C(sp.C1, 2, 3);
            net.R((1.0 - t) * sp.R1, 3, 4);
            net.R(t * sp.R1, 4, 5);
            net.R(sp.R4, 2, 6);
            net.C(sp.C2, 6, 5);
            net.R(l * sp.R2, 5, 7);
            net.R((1.0 - m) * sp.R3, 7, 8);
            net.C(sp.C3, 6, 8);
            net.R(m * sp.R3, 8, 0);
            net.R(sp.RL, 4, 0);
            break;
        }
        case FmvRheo:
        {
            // blackface wiring: mid cap + dialled resistance at one node
            // nodes: 0 gnd, 1 src, 2 in, 3 T, 4 O, 5 K, 6 J, 7 M
            net.nNodes = 8; net.outNode = 4;
            net.R(sp.Rs, 1, 2);
            net.C(sp.C1, 2, 3);
            net.R((1.0 - t) * sp.R1, 3, 4);
            net.R(t * sp.R1, 4, 5);
            net.R(sp.R4, 2, 6);
            net.C(sp.C2, 6, 5);
            net.R(l * sp.R2, 5, 7);
            net.C(sp.C3, 6, 7);
            net.R(m * sp.R3, 7, 0);
            net.R(sp.RL, 4, 0);
            break;
        }
        case JamesTopo:
        {
            // Orange Graphic MkII values. b = bass (k3), tr = treble (k1).
            // nodes: 0 gnd, 1 src, 2 IN, 3 A, 4 W, 5 B, 6 OUT, 7 T1, 8 T2
            const double RPot = 1e6, R1j = 100e3, R2j = 22e3, R3j = 100e3;
            const double C4j = 10e-9;
            const double b = l, tr = t;
            net.nNodes = 9; net.outNode = 6;
            net.R(sp.Rs, 1, 2);
            net.R(R1j, 2, 3);
            net.R((1.0 - b) * RPot, 3, 4); net.C(sp.C1, 3, 4);   // upper || C1
            net.R(b * RPot, 4, 5);         net.C(sp.C2, 4, 5);   // lower || C2
            net.R(R2j, 5, 0);
            net.R(R3j, 4, 6);
            net.C(sp.C3, 2, 7);
            net.R((1.0 - tr) * RPot, 7, 6);
            net.R(tr * RPot, 6, 8);
            net.C(C4j, 8, 0);
            net.R(sp.RL, 6, 0);
            break;
        }
        case E3Topo:
        {
            // 5E3: vol = k3, tone = k1, ghost = unused channel volume.
            // Signal INTO the volume pot wiper; CW lug -> shared grid bus.
            // nodes: 0 gnd, 1 src, 2 P1, 3 Y, 4 G(out), 5 P2, 6 X, 7 TW
            const double POT = 1e6, CPL = 0.1e-6, CBR = 500e-12, CTO = 5e-9, CMI = 110e-12;
            const double av = taper(LogA, k3);
            const double at = taper(LogA, k1);
            const double ag = taper(LogA, ghost);
            net.nNodes = 8; net.outNode = 4;
            net.R(sp.Rs, 1, 2);
            net.C(CPL, 2, 3);
            net.R((1.0 - av) * POT, 3, 4);
            net.R(av * POT, 3, 0);
            net.C(CBR, 3, 4);
            net.R(sp.Rs, 0, 5);      // idle channel plate Z (AC-grounded source)
            net.C(CPL, 5, 6);
            net.R((1.0 - ag) * POT, 6, 4);
            net.R(ag * POT, 6, 0);
            net.C(CMI, 4, 0);
            net.R(at * POT, 3, 7);          // tone: bright end at Y ...
            net.R((1.0 - at) * POT, 7, 6);  // ... other end at X (normal node)
            net.C(CTO, 7, 0);               // 5n bleed at the tone wiper
            break;
        }
        }
    }

    // ---------------- VOX CUT closed form ----------------
    // Exact 2-node solve of the differential bridge (derivation:
    // docs/tonestack/RESEARCH.md): plate Zs 35k/38k, 220k grid leaks behind
    // 100n coupling, bridge = x*250k + 4n7 between anti-phase plates. Output =
    // one power-tube grid, normalised so cut fully open ~ unity. Coefficients
    // are LINEAR in the pot resistance fraction x; s in rad/s (descending).
    static void cutAnalogCoeffs(double cutKnob, double *num, double *den)
    {
        // authentic direction: knob UP = darker (more cut) => x = 1 - knob
        const double x = 1.0 - std::min(std::max(cutKnob, 0.0), 1.0);
        num[0] = 6.6693e-7 * x + 6.8244e-9;
        num[1] = 5.6165e-5 * x + 5.682204e-4;
        num[2] = 1.175e-3  * x + 4.78141e-2;
        num[3] = 1.0;
        den[0] = 7.730325e-7 * x + 1.935648e-7;
        den[1] = 6.02775e-5  * x + 6.742466e-4;
        den[2] = 1.175e-3    * x + 5.16431e-2;
        den[3] = 1.0;
    }

    // ---------------- MARK graphic tables ----------------
    struct BandRLC { double R, L, C; };
    static constexpr BandRLC kBandRLC[5] = {
        { 470.0, 1.0,   3.3e-6  },   // "80"   -> 87.6 Hz
        { 470.0, 0.39,  0.47e-6 },   // "240"  -> 371.7 Hz
        { 470.0, 0.22,  0.22e-6 },   // "750"  -> 723.4 Hz
        { 1000.0, 0.068, 0.15e-6 },  // "2200" -> 1575.9 Hz
        { 1000.0, 0.033, 0.033e-6 }, // "6600" -> 4822.9 Hz
    };
    static constexpr double kEqBus = 2890.0; // fitted: +17 dB max boost @ 88 Hz
    static constexpr double kEqPot = 50e3;   // slide pot

    // ---------------- identification + bilinear ----------------
    void setP(float &slot, float v)
    {
        float c = std::min(std::max(v, 0.0f), 1.0f);
        if (c != slot) { slot = c; mDirty = true; }
    }

    void updateIfNeeded(int numSamples)
    {
        // smooth raw knob positions, advanced block-wise by the equivalent of
        // numSamples one-pole steps (10 ms time constant)
        bool moving = false;
        const float a = (float)std::pow(mSmoothA, std::max(numSamples, 1));
        auto step = [&](float &cur, float tgt)
        {
            float nxt = tgt + a * (cur - tgt);
            if (std::fabs(nxt - tgt) < 1e-4f) nxt = tgt;
            if (nxt != cur) { cur = nxt; moving = true; }
        };
        step(mK1, mK1t); step(mK2, mK2t); step(mK3, mK3t);
        step(mCut, mCutT); step(mGhost, mGhostT);
        if (!mDirty && !moving) return;
        mDirty = false;

        if (mModelDirty)
        {
            computeReference();               // delta divisor (also on mode flip)
            if (mDelta) mMakeup = 1.0;        // flat-at-defaults by construction
            else computeMakeup();
            for (double &z : mZ) z = 0.0;
            for (double &z : mZCut) z = 0.0;
            for (double &z : mZCut0) z = 0.0;
            mModelDirty = false;
        }

        // Delta shortcut: at the exact reference settings the ratio is unity,
        // so hand back a true passthrough (enable the stack -> zero change).
        if (mDelta && mK1 == mRefK1 && mK2 == mRefK2 && mK3 == mRefK3 && mGhost == 0.0f)
        {
            double one[kMaxOrder + 1] = {}, id_[kMaxOrder + 1] = {};
            one[0] = 1.0; id_[0] = 1.0;
            commitStack(one, id_, 0);
        }
        else
            identifyStack();
        if (mModel == (int)kVoxTB) designCut();
    }

    // Solve the reference (default-knobs) circuit at the fit + validation
    // frequencies. Cheap (one identify's worth of MNA); model/mode change only.
    void computeReference()
    {
        Net net;
        buildNet(net, mModel, (double)mRefK1, (double)mRefK2, (double)mRefK3, 0.0);
        for (int i = 0; i < kNumFitC; ++i)
            mRefFit[i] = solveNet(net, 2.0 * kPi * kFitFreqs[i]);
        for (int v = 0; v < 4; ++v)
            mRefVal[v] = solveNet(net, 2.0 * kPi * kValidateFreqs[v]);
    }

    // Routh-Hurwitz: true iff all roots of sum a_k s^k (ascending, a0 > 0)
    // are strictly in the left half plane. N <= 5.
    static bool isHurwitz(const double *a, int N)
    {
        // build descending coefficient list c[0] = a_N ... c[N] = a_0
        double c[kMaxOrder + 1];
        for (int k = 0; k <= N; ++k) c[k] = a[N - k];
        if (c[0] == 0.0) return false;
        double row0[kMaxOrder + 1] = {}, row1[kMaxOrder + 1] = {};
        int n0 = 0, n1 = 0;
        for (int k = 0; k <= N; k += 2) row0[n0++] = c[k];
        for (int k = 1; k <= N; k += 2) row1[n1++] = c[k];
        double first = row0[0];
        if (first <= 0.0) return false;
        for (int it = 0; it < N; ++it)
        {
            if (row1[0] == 0.0) return false;
            if ((first > 0.0) != (row1[0] > 0.0)) return false;
            double next[kMaxOrder + 1] = {};
            for (int k2 = 0; k2 + 1 < kMaxOrder + 1; ++k2)
                next[k2] = (row1[0] * row0[k2 + 1] - row0[0] * row1[k2 + 1]) / row1[0];
            for (int k2 = 0; k2 < kMaxOrder + 1; ++k2) { row0[k2] = row1[k2]; row1[k2] = next[k2]; }
            first = row0[0];
        }
        return true;
    }

    void identifyStack()
    {
        // SMALLEST SUFFICIENT ORDER: try ascending orders; commit the first
        // whose fit passes Routh + sanity + held-out validation (< 0.05 dB).
        // A minimal-order fit cannot carry spurious near-cancelling pole/zero
        // pairs (every pole is needed for the band); the full order is used
        // only when the response genuinely requires it. Spurious pairs are
        // what turned TDF2 state into a slow integrator (poles at ~1e-4 Hz)
        // at some 5E3 knob corners.
        //
        // If NO order passes the gate (rare corners where the reduced orders
        // miss by a hair and the full order is numerically poisoned), commit
        // the best-validating SAFE candidate instead -- never the poisoned
        // one. If nothing safe exists, keep the previous coefficients.
        double bestErr = 1e30;
        double bestB[kMaxOrder + 1], bestA[kMaxOrder + 1];
        int bestN = 0;
        // delta mode fits a RATIO of two same-topology responses, which is up
        // to order 2N (shared poles usually cancel, so the ascending search
        // still lands low -- but it must be ALLOWED to climb when they don't).
        const int maxN = mDelta ? std::min(2 * orderOf(mModel), (int)kMaxOrder)
                                : orderOf(mModel);
        for (int N = 1; N <= maxN; ++N)
        {
            double cb[kMaxOrder + 1], ca[kMaxOrder + 1];
            const double err = identifyStackOrder(N, cb, ca);
            if (err < bestErr)
            {
                bestErr = err; bestN = N;
                for (int k = 0; k <= kMaxOrder; ++k) { bestB[k] = cb[k]; bestA[k] = ca[k]; }
            }
            if (err < 0.05) break;
        }
        if (bestN > 0 && bestErr < 1e29)
            commitStack(bestB, bestA, bestN);
    }

    void commitStack(const double *bs, const double *as, int N)
    {
        // snapshot the outgoing filter (coefficients + a state COPY) so the
        // next chunk can crossfade old -> new. The live state array keeps
        // running under the new coefficients (LF state continuity); whatever
        // mismatch transient that causes is masked by the fade-in weight.
        for (int k = 0; k <= kMaxOrder; ++k) { mBOld[k] = mB[k]; mAOld[k] = mA[k]; }
        for (int k = 0; k < kMaxOrder; ++k) mZOld[k] = mZ[k];
        mFadePending = true;

        for (int k = 0; k <= kMaxOrder; ++k) { mBs[k] = bs[k]; mAs[k] = as[k]; }
        const double w0 = 2.0 * kPi * 1000.0;
        bilinear(mBs, mAs, N, 2.0 * mFs / w0, mB, mA);
        for (int k = N + 1; k <= kMaxOrder; ++k) { mB[k] = 0.0; mA[k] = 0.0; }
        mOrder = N; // introspection only; the recursion runs at kMaxOrder

        // State policy: TDF2 state encodes recent input history WEIGHTED BY THE
        // COEFFICIENTS, so it only transfers between NEARBY filters. Keep it for
        // small glide steps (preserves the LF tail; no droop while turning), but
        // start clean on big jumps (model/mode changes, wild automation) where
        // inherited state is garbage at the new coefficient scale and can ring
        // for seconds. The old-snapshot fade carries the audio either way.
        double dist = 0.0, scale = 1e-12;
        for (int k = 0; k <= kMaxOrder; ++k)
        {
            dist += std::abs(mB[k] - mBOld[k]) + std::abs(mA[k] - mAOld[k]);
            scale += std::abs(mB[k]) + std::abs(mA[k]);
        }
        if (dist > 0.05 * scale)
            for (double &z : mZ) z = 0.0;
    }

    // returns worst held-out validation error in dB, or 1e30 for unsafe fits
    double identifyStackOrder(const int N, double *outB, double *outA)
    {
        // 1) sample the circuit
        Net net;
        buildNet(net, mModel, (double)mK1, (double)mK2, (double)mK3, (double)mGhost);
        // (grid + validation freqs are class constants: kFitFreqs reaches below
        // the lowest physical poles -- see the class-scope comment.)
        static constexpr int kNumFit = kNumFitC;
        const double *kFreqs = kFitFreqs;
        std::complex<double> H[kNumFitC];
        for (int i = 0; i < kNumFit; ++i)
        {
            H[i] = solveNet(net, 2.0 * kPi * kFreqs[i]);
            if (mDelta)
                H[i] /= mRefFit[i]; // fit the RATIO -> delta stack
        }

        // 2) exact rational fit in normalised s' = s / w0.
        // Solved as a column-scaled least-squares via modified Gram-Schmidt QR
        // on the real-stacked system (2 rows per frequency). For a rational
        // system of known order this is interpolation: residual ~ machine eps.
        const double w0 = 2.0 * kPi * 1000.0;
        const int nu = 2 * N + 1;          // b0..bN, a1..aN (a0 = 1)
        const int nr = 2 * kNumFit;        // stacked real rows
        double A[2 * kNumFit][17] = {}, rhs[2 * kNumFit] = {};
        for (int i = 0; i < kNumFit; ++i)
        {
            std::complex<double> sp_(0.0, 2.0 * kPi * kFreqs[i] / w0);
            std::complex<double> spow[kMaxOrder + 1];
            spow[0] = 1.0;
            for (int k = 1; k <= N; ++k) spow[k] = spow[k - 1] * sp_;
            const double wgt = 1.0 / std::max(1.0, std::abs(H[i]));
            for (int k = 0; k <= N; ++k)
            {
                A[2 * i][k]     = spow[k].real() * wgt;
                A[2 * i + 1][k] = spow[k].imag() * wgt;
            }
            for (int k = 1; k <= N; ++k)
            {
                const std::complex<double> v = -H[i] * spow[k] * wgt;
                A[2 * i][N + k]     = v.real();
                A[2 * i + 1][N + k] = v.imag();
            }
            const std::complex<double> rv = H[i] * wgt;
            rhs[2 * i] = rv.real();
            rhs[2 * i + 1] = rv.imag();
        }
        // column scaling
        double cscale[17];
        for (int c = 0; c < nu; ++c)
        {
            double nrm = 0.0;
            for (int r = 0; r < nr; ++r) nrm += A[r][c] * A[r][c];
            cscale[c] = nrm > 0.0 ? 1.0 / std::sqrt(nrm) : 1.0;
            for (int r = 0; r < nr; ++r) A[r][c] *= cscale[c];
        }
        // modified Gram-Schmidt QR: A = QR, then back-substitute R x = Q^T rhs
        double Rm[17][17] = {}, qtb[17] = {};
        for (int c = 0; c < nu; ++c)
        {
            for (int p = 0; p < c; ++p)
            {
                double dot = 0.0;
                for (int r = 0; r < nr; ++r) dot += A[r][p] * A[r][c];
                Rm[p][c] = dot;
                for (int r = 0; r < nr; ++r) A[r][c] -= dot * A[r][p];
            }
            double nrm = 0.0;
            for (int r = 0; r < nr; ++r) nrm += A[r][c] * A[r][c];
            nrm = std::sqrt(std::max(nrm, 1e-300));
            Rm[c][c] = nrm;
            for (int r = 0; r < nr; ++r) A[r][c] /= nrm;
        }
        for (int c = 0; c < nu; ++c)
        {
            double dot = 0.0;
            for (int r = 0; r < nr; ++r) dot += A[r][c] * rhs[r];
            qtb[c] = dot;
        }
        double sol[17];
        for (int r = nu - 1; r >= 0; --r)
        {
            double acc = qtb[r];
            for (int c = r + 1; c < nu; ++c) acc -= Rm[r][c] * sol[c];
            sol[r] = acc / Rm[r][r];
        }
        for (int c = 0; c < nu; ++c) sol[c] *= cscale[c];
        double bs[kMaxOrder + 1] = {}, as[kMaxOrder + 1] = {};
        as[0] = 1.0;
        for (int k = 0; k <= N; ++k) bs[k] = sol[k];
        for (int k = 1; k <= N; ++k) as[k] = sol[N + k];
        if (!isHurwitz(as, N))
            return 1e30;
        // no real tone stack has poles below ~0.01 Hz; a giant a1 means the
        // fit parked a spurious pole near DC (numerically poisonous even
        // when Hurwitz-stable)
        if (as[1] > 5e3)   // physical floor: lowest real poles ~1.6 Hz (5E3
            return 1e30;    // coupling caps) put a1 near 1.4e3; 5e3 = 3.5x margin
        // held-out validation: worst error where the fit was NOT sampled
        double worst = 0.0;
        for (int v = 0; v < 4; ++v)
        {
            const double fv = kValidateFreqs[v];
            const std::complex<double> sv(0.0, 2.0 * kPi * fv / w0);
            std::complex<double> num = 0.0, den = 0.0, sp2 = 1.0;
            for (int k = 0; k <= N; ++k) { num += bs[k] * sp2; den += as[k] * sp2; sp2 *= sv; }
            const std::complex<double> Hf = num / den;
            std::complex<double> Hc = solveNet(net, 2.0 * kPi * fv);
            if (mDelta)
                Hc /= mRefVal[v];
            const double ma = std::abs(Hf), mc = std::abs(Hc);
            if (mc > 1e-7)
                worst = std::max(worst, std::abs(20.0 * std::log10(std::max(ma, 1e-12) / mc)));
        }
        // digital sanity: a near-cancelling pole/zero pair can pass every
        // frequency-domain gate yet ring with a huge internal response (the
        // T10 failure mode). Probe the ACTUAL digital impulse response of the
        // candidate and reject ringers outright.
        {
            double Bt[kMaxOrder + 1], At[kMaxOrder + 1];
            bilinear(bs, as, N, 2.0 * mFs / w0, Bt, At);
            double z[kMaxOrder] = {};
            double pk = 0.0;
            for (int i = 0; i < 256; ++i)
            {
                const double x = (i == 0) ? 1.0 : 0.0;
                const double y = Bt[0] * x + z[0];
                for (int k = 1; k <= kMaxOrder; ++k)
                    z[k - 1] = (k <= N ? Bt[k] * x - At[k] * y : 0.0)
                             + (k < kMaxOrder ? z[k] : 0.0);
                pk = std::max(pk, std::abs(y));
            }
            if (pk > 50.0)
                return 1e30;
        }

        for (int k = 0; k <= kMaxOrder; ++k)
        {
            outB[k] = bs[k];
            outA[k] = as[k];
        }
        return worst;
    }

    void designCut()
    {
        double num[4], den[4]; // descending in s (rad/s)
        cutAnalogCoeffs((double)mCut, num, den);
        // convert to ascending arrays for bilinear helper
        double bs[kMaxOrder + 1] = {};
        double as[kMaxOrder + 1] = {};
        bs[0] = num[3]; bs[1] = num[2]; bs[2] = num[1]; bs[3] = num[0];
        as[0] = den[3]; as[1] = den[2]; as[2] = den[1]; as[3] = den[0];
        bilinear(bs, as, 3, 2.0 * mFs, mBCut, mACut);
        if (mDelta)
        {
            // fixed inverse of the REFERENCE response (default: open) so the
            // calibrated cut position is exactly flat: swap num/den.
            double n0[4], d0[4];
            cutAnalogCoeffs((double)mRefCut, n0, d0);
            double bs0[kMaxOrder + 1] = {};
            double as0[kMaxOrder + 1] = {};
            bs0[0] = d0[3]; bs0[1] = d0[2]; bs0[2] = d0[1]; bs0[3] = d0[0];
            as0[0] = n0[3]; as0[1] = n0[2]; as0[2] = n0[1]; as0[3] = n0[0];
            bilinear(bs0, as0, 3, 2.0 * mFs, mBCut0, mACut0);
        }
    }

    static void bilinear(const double *bs, const double *as, int N, double c,
                         double *Bz, double *Az)
    {
        // H(s)=sum bs_k s^k / sum as_k s^k  ->  substitute s = c(1-z^-1)/(1+z^-1).
        // Multiply through by (1+z^-1)^N: each s^k -> c^k (1-z^-1)^k (1+z^-1)^(N-k).
        double num[kMaxOrder + 1] = {}, den[kMaxOrder + 1] = {};
        double poly[kMaxOrder + 1];
        auto accum = [&](double coeff, int k, double *dst)
        {
            // poly = (1-x)^k * (1+x)^(N-k), x = z^-1
            double p1[kMaxOrder + 1] = {}, tmp[kMaxOrder + 1] = {};
            p1[0] = 1.0;
            int deg = 0;
            for (int i = 0; i < k; ++i)
            {
                for (int d2 = 0; d2 <= deg + 1; ++d2) tmp[d2] = 0.0;
                for (int d2 = 0; d2 <= deg; ++d2)
                {
                    tmp[d2] += p1[d2];
                    tmp[d2 + 1] -= p1[d2];
                }
                ++deg;
                for (int d2 = 0; d2 <= deg; ++d2) p1[d2] = tmp[d2];
            }
            for (int i = 0; i < N - k; ++i)
            {
                for (int d2 = 0; d2 <= deg + 1; ++d2) tmp[d2] = 0.0;
                for (int d2 = 0; d2 <= deg; ++d2)
                {
                    tmp[d2] += p1[d2];
                    tmp[d2 + 1] += p1[d2];
                }
                ++deg;
                for (int d2 = 0; d2 <= deg; ++d2) p1[d2] = tmp[d2];
            }
            for (int d2 = 0; d2 <= N; ++d2) dst[d2] += coeff * p1[d2];
            (void)poly;
        };
        double ck = 1.0;
        for (int k = 0; k <= N; ++k)
        {
            if (bs[k] != 0.0) accum(bs[k] * ck, k, num);
            if (as[k] != 0.0) accum(as[k] * ck, k, den);
            ck *= c;
        }
        const double a0 = den[0] != 0.0 ? den[0] : 1e-300;
        for (int k2 = 0; k2 <= kMaxOrder; ++k2)
        {
            Bz[k2] = k2 <= N ? num[k2] / a0 : 0.0;
            Az[k2] = k2 <= N ? den[k2] / a0 : 0.0;
        }
    }

    void computeMakeup()
    {
        // static, per model: unity-ish at reference settings (noon; 5E3: full
        // up), 300-1200 Hz geometric mean. Never reactive.
        Net net;
        const bool is5E3 = (mModel == (int)kTweed5E3);
        buildNet(net, mModel, is5E3 ? 1.0 : 0.5, 0.5, is5E3 ? 1.0 : 0.5, 0.0);
        double acc = 0.0;
        static constexpr double kRef[3] = { 300.0, 600.0, 1200.0 };
        for (double f : kRef)
            acc += std::log(std::max(std::abs(solveNet(net, 2.0 * kPi * f)), 1e-6));
        double g = 1.0 / std::exp(acc / 3.0);
        mMakeup = std::min(g, 8.0); // cap +18 dB
    }

    void designGraphic()
    {
        mGraphicDirty = false;
        for (int b = 0; b < 5; ++b)
        {
            double a = 0.5 * (1.0 - (double)mBandT[b]);
            a = std::min(std::max(a, 1e-4), 1.0 - 1e-4);
            const BandRLC &q = kBandRLC[b];
            const double Rn = kEqBus * (1.0 - a) + q.R + kEqPot * a * (1.0 - a);
            const double Rd = kEqBus * a + q.R + kEqPot * a * (1.0 - a);
            const double w0a = 1.0 / std::sqrt(q.L * q.C);
            // prewarp so the band centre lands exactly at w0a
            const double c = w0a / std::tan(w0a / (2.0 * mFs));
            // analog: (s^2 LC + s C Rn + 1) / (s^2 LC + s C Rd + 1)
            const double LC = q.L * q.C;
            const double c2 = c * c;
            double n0 = LC * c2 + q.C * Rn * c + 1.0;
            double n1 = 2.0 - 2.0 * LC * c2;
            double n2 = LC * c2 - q.C * Rn * c + 1.0;
            double d0 = LC * c2 + q.C * Rd * c + 1.0;
            double d1 = n1;
            double d2 = LC * c2 - q.C * Rd * c + 1.0;
            Band &bd = mBands[b];
            bd.b0 = n0 / d0; bd.b1 = n1 / d0; bd.b2 = n2 / d0;
            bd.a1 = d1 / d0; bd.a2 = d2 / d0;
        }
    }

    // fit grid: reaches below the lowest physical poles (5E3 coupling caps at
    // ~1.6 Hz) -- without LF coverage the fit invents spurious near-DC
    // pole/zero pairs whose imperfect cancellation wrecks the filter state.
    static constexpr int kNumFitC = 13;
    static constexpr double kFitFreqs[kNumFitC] = { 0.5, 1.5, 5.0, 12.5, 31.0,
                                                    78.0, 195.0, 490.0, 1225.0,
                                                    3050.0, 7625.0, 15250.0,
                                                    21000.0 };
    static constexpr double kValidateFreqs[4] = { 1.0, 60.0, 2500.0, 18000.0 };

    // ---------------- state ----------------
    double mFs = 48000.0;
    int mModel = kTweedBassman;
    bool mModelDirty = true, mDirty = true, mGraphicOn = false, mGraphicDirty = true;
    bool mDelta = true; // default: EQ-delta on top of the capture (usability)
    // capture calibration (flat point); defaults = noon, cut open
    float mRefK1 = 0.5f, mRefK2 = 0.5f, mRefK3 = 0.5f, mRefCut = 0.0f;
    std::complex<double> mRefFit[kNumFitC];
    std::complex<double> mRefVal[4];

    // control targets + smoothed values (raw 0..1)
    float mK1t = 0.5f, mK2t = 0.5f, mK3t = 0.5f, mCutT = 0.0f, mGhostT = 0.0f;
    float mK1 = 0.5f, mK2 = 0.5f, mK3 = 0.5f, mCut = 0.0f, mGhost = 0.0f;
    double mSmoothA = 0.999;

    // identified analog (normalised s) + digital coefficients. mB/mA start as
    // an identity filter so the very first commit crossfades from passthrough.
    double mBs[kMaxOrder + 1] = {}, mAs[kMaxOrder + 1] = {};
    double mB[kMaxOrder + 1] = { 1.0 }, mA[kMaxOrder + 1] = { 1.0 };
    double mZ[kMaxOrder] = {};
    double mBOld[kMaxOrder + 1] = { 1.0 }, mAOld[kMaxOrder + 1] = { 1.0 };
    double mZOld[kMaxOrder] = {};
    bool mFadePending = false;
    int mOrder = 3;
    double mMakeup = 1.0;

    // vox cut filter (3rd order) + the FIXED inverse-of-open stage used by
    // delta mode (so Cut at 0 is exactly flat instead of the bridge's ~1 dB
    // open tilt). Inverse denominator = the open-cut numerator; Hurwitz by the
    // cubic Routh condition a2*a1 > a3*a0 (6.2e-4 * 4.9e-2 >> 6.7e-7), checked
    // again by the harness stability sweep.
    double mBCut[kMaxOrder + 1] = {}, mACut[kMaxOrder + 1] = {};
    double mZCut[kMaxOrder] = {};
    double mBCut0[kMaxOrder + 1] = {}, mACut0[kMaxOrder + 1] = {};
    double mZCut0[kMaxOrder] = {};

    // graphic
    struct Band { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
    Band mBands[5];
    float mBandT[5] = { 0, 0, 0, 0, 0 };
};

} // namespace nam_rig
