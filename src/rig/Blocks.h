#pragma once
// Block interfaces + passthrough stubs for the NAM Rig fixed serial chain:
//
//   [mono]   gate -> comp/drive -> AMP (NAM AA engine) -> EQ -> CAB (IR)
//   [stereo] -> modulation -> delay -> reverb
//
// Scoping (agreed June 2026): fixed order, first-party blocks only, mono
// through the cab, stereo fan-out AFTER the cab. Every block reports its own
// latency; total PDC = sum (RigChain::latencySamples).
//
// Contract:
//  - prepare() is called before any process(); may allocate.
//  - process() is RT-safe: no allocation, no locks held while blocking.
//  - bypassed blocks are skipped by the chain (latency must be 0 or constant
//    regardless of bypass — latency-bearing blocks stay in the path; see
//    AmpBlock/CabBlock notes).
//  - Each block ships only after it has an offline verification harness
//    (tests/rig_chain_process.cpp runs the same chain code offline).

#include <juce_audio_basics/juce_audio_basics.h>

namespace nam_rig
{

struct BlockContext
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
};

// ---- mono section ----
class MonoBlock
{
public:
    virtual ~MonoBlock() = default;
    virtual const char *name() const = 0;
    virtual void prepare(const BlockContext &) = 0;
    virtual void reset() {}
    virtual void process(float *mono, int numSamples) = 0;
    virtual double latencySamples() const { return 0.0; }

    void setBypassed(bool b) { mBypassed = b; }
    bool isBypassed() const { return mBypassed; }

private:
    bool mBypassed = false;
};

// ---- stereo section ----
class StereoBlock
{
public:
    virtual ~StereoBlock() = default;
    virtual const char *name() const = 0;
    virtual void prepare(const BlockContext &) = 0;
    virtual void reset() {}
    virtual void process(float *left, float *right, int numSamples) = 0;
    virtual double latencySamples() const { return 0.0; }

    void setBypassed(bool b) { mBypassed = b; }
    bool isBypassed() const { return mBypassed; }

private:
    bool mBypassed = false;
};

// ---- v1 stubs (passthrough; implemented incrementally, harness-first) ----

// GateBlock lives in GateBlock.h (real DSP, verified by tests/gate_test.cpp).

// CompBlock lives in CompBlock.h (real DSP, verified by tests/comp_test.cpp).

// EqBlock lives in EqBlock.h (8-band graphic, verified by tests/eq_test.cpp).

class ModBlock : public StereoBlock
{
public:
    const char *name() const override { return "Modulation"; }
    void prepare(const BlockContext &) override {}
    void process(float *, float *, int) override {} // TODO: chorus/flanger/phaser
};

class DelayBlock : public StereoBlock
{
public:
    const char *name() const override { return "Delay"; }
    void prepare(const BlockContext &) override {}
    void process(float *, float *, int) override {} // TODO: stereo delay
};

class ReverbBlock : public StereoBlock
{
public:
    const char *name() const override { return "Reverb"; }
    void prepare(const BlockContext &) override {}
    void process(float *, float *, int) override {} // TODO: reverb
};

} // namespace nam_rig
