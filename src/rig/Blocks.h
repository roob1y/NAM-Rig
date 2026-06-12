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

// ---- block homes (each verified by its own measurement suite) ----
// GateBlock.h   (tests/gate_test.cpp)     CompBlock.h (tests/comp_test.cpp)
// EqBlock.h     (tests/eq_test.cpp)       CabBlock.h  (rig_chain_process)
// ModBlock.h    (tests/mod_test.cpp)      DelayBlock.h (tests/delay_test.cpp)
// ReverbBlock.h (tests/reverb_test.cpp)

} // namespace nam_rig
