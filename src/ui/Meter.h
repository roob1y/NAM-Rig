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

    float shownDb() const { return mShownDb; }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        const float radius = juce::jmin(4.0f, b.getWidth() * 0.5f);
        g.setColour(colors::track);
        g.fillRoundedRectangle(b, radius);

        // Full gradient, masked from the top by the unlit portion.
        juce::Graphics::ScopedSaveState ss(g);
        juce::Path clip;
        clip.addRoundedRectangle(b, radius);
        g.reduceClipRegion(clip);
        g.setGradientFill(RigLookAndFeel::meterGradient(b));
        g.fillRect(b);
        const float norm = juce::jlimit(0.0f, 1.0f, (mShownDb - kMinDb) / (0.0f - kMinDb));
        g.setColour(juce::Colour(0xff191c21)); // dark mask over the un-lit top
        g.fillRect(b.withHeight(b.getHeight() * (1.0f - norm)));
    }

private:
    float mShownDb = kMinDb;
};

} // namespace nam_rig::ui
