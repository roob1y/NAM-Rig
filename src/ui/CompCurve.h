#pragma once
// CompCurve — input-vs-output transfer graph for the compressor (Advanced view).
//
// Draws the dynamic characteristic straight from the block's single source of
// truth (CompBlock::computeGainDb + thresholdForSustain), so the picture can
// never drift from what the DSP actually does. Shows:
//   - the unity (1:1) reference diagonal,
//   - the soft-knee transfer curve for the current Sustain,
//   - a non-draggable threshold marker that tracks the Sustain knob.
//
// Axes are dBFS, -60..0 on both. The plotted curve is the compression
// characteristic (pre makeup/level/boost) so the knee and ratio read clearly.

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "RigLookAndFeel.h"
#include "rig/CompBlock.h"

namespace nam_rig::ui
{

class CompCurve : public juce::Component
{
public:
    static constexpr float kMinDb = -60.0f;

    // Driven by the panel from the Sustain knob (0..1).
    void setSustain(float s01)
    {
        s01 = juce::jlimit(0.0f, 1.0f, s01);
        if (std::abs(s01 - mSustain) > 1.0e-4f)
        {
            mSustain = s01;
            repaint();
        }
    }

    // Live input level (dBFS) for the operating-point dot; -120 = idle/hidden.
    void setInputDb(float db)
    {
        if (std::abs(db - mInputDb) > 0.1f)
        {
            mInputDb = db;
            repaint();
        }
    }

    void paint(juce::Graphics &g) override
    {
        auto outer = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(colors::panel.darker(0.25f));
        g.fillRoundedRectangle(outer, 4.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(outer, 4.0f, 1.0f);

        auto plot = outer.reduced(6.0f);
        const float L = plot.getX(), R = plot.getRight();
        const float T = plot.getY(), B = plot.getBottom();
        auto xOf = [&](float inDb) {
            return L + juce::jlimit(0.0f, 1.0f, (inDb - kMinDb) / (0.0f - kMinDb)) * (R - L);
        };
        auto yOf = [&](float outDb) {
            return B - juce::jlimit(0.0f, 1.0f, (outDb - kMinDb) / (0.0f - kMinDb)) * (B - T);
        };

        // faint grid every 12 dB
        g.setColour(colors::outline.withAlpha(0.45f));
        for (float d = kMinDb + 12.0f; d < 0.0f; d += 12.0f)
        {
            g.fillRect(xOf(d), T, 1.0f, B - T);
            g.fillRect(L, yOf(d), R - L, 1.0f);
        }

        // unity reference diagonal
        g.setColour(colors::textDim.withAlpha(0.5f));
        g.drawLine(xOf(kMinDb), yOf(kMinDb), xOf(0.0f), yOf(0.0f), 1.0f);

        const float thr = nam_rig::CompBlock::thresholdForSustain(mSustain);

        // threshold marker (non-draggable, follows Sustain)
        g.setColour(colors::accentDim);
        const float tx = xOf(thr);
        g.fillRect(tx, T, 1.0f, B - T);
        g.setColour(colors::textDim);
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        g.drawText("T", juce::Rectangle<float>(tx + 2.0f, T + 1.0f, 12.0f, 10.0f).toNearestInt(),
                   juce::Justification::topLeft);

        // transfer curve: out = in + computeGainDb(in, threshold)
        juce::Path curve;
        bool started = false;
        for (float inDb = kMinDb; inDb <= 0.001f; inDb += 1.0f)
        {
            const float outDb = inDb + nam_rig::CompBlock::computeGainDb(inDb, thr);
            const float px = xOf(inDb), py = yOf(juce::jmax(outDb, kMinDb));
            if (!started)
            {
                curve.startNewSubPath(px, py);
                started = true;
            }
            else
                curve.lineTo(px, py);
        }
        g.setColour(colors::accent);
        g.strokePath(curve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // live operating-point dot: rides the curve at the current input level.
        if (mInputDb > kMinDb + 1.0f)
        {
            const float inDb = juce::jlimit(kMinDb, 0.0f, mInputDb);
            const float outDb = inDb + nam_rig::CompBlock::computeGainDb(inDb, thr);
            const float dx = xOf(inDb), dy = yOf(juce::jmax(outDb, kMinDb));
            g.setColour(colors::text);
            g.fillEllipse(dx - 3.5f, dy - 3.5f, 7.0f, 7.0f);
            g.setColour(colors::accent);
            g.drawEllipse(dx - 3.5f, dy - 3.5f, 7.0f, 7.0f, 1.5f);
        }

        // axis hint
        g.setColour(colors::textDim.withAlpha(0.7f));
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        g.drawText("IN", juce::Rectangle<float>(L, B - 11.0f, R - L, 10.0f).toNearestInt(),
                   juce::Justification::bottomRight);
        g.drawText("OUT", juce::Rectangle<float>(L + 1.0f, T, 40.0f, 10.0f).toNearestInt(),
                   juce::Justification::topLeft);
    }

private:
    float mSustain = 0.5f;
    float mInputDb = kMinDb; // idle (dot hidden) until fed
};

} // namespace nam_rig::ui
