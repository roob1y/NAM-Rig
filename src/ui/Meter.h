#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RigLookAndFeel.h"

namespace nam_rig::ui
{

// Vertical peak meter, -60..0 dB. Fed from the editor timer (push latest peak;
// the displayed bar has its own fall-back so short peaks stay readable).
class PeakMeter : public juce::Component
{
public:
    static constexpr float kMinDb = -60.0f;
    static constexpr float kFallDbPerSec = 36.0f;

    void push(float peakDb, float dtSeconds)
    {
        mShownDb = juce::jmax(peakDb, mShownDb - kFallDbPerSec * dtSeconds);
        mShownDb = juce::jlimit(kMinDb, 6.0f, mShownDb);
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(colors::track);
        g.fillRoundedRectangle(b, 2.0f);

        const float norm = juce::jlimit(0.0f, 1.0f, (mShownDb - kMinDb) / (0.0f - kMinDb));
        if (norm > 0.001f)
        {
            auto fill = b.removeFromBottom(b.getHeight() * norm);
            auto colour = colors::meterLo;
            if (mShownDb > -3.0f)       colour = colors::meterHi;
            else if (mShownDb > -12.0f) colour = colors::meterMid;
            g.setColour(colour);
            g.fillRoundedRectangle(fill, 2.0f);
        }
    }

private:
    float mShownDb = kMinDb;
};

} // namespace nam_rig::ui
