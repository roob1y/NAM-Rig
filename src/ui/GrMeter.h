#pragma once
// GrMeter — vertical gain-reduction meter: fills DOWNWARD from the top as
// reduction grows (standard GR convention). Fed from the editor timer with
// instant attack and a timed release, like PeakMeter.

#include <juce_gui_basics/juce_gui_basics.h>
#include "RigLookAndFeel.h"

namespace nam_rig::ui
{

class GrMeter : public juce::Component
{
public:
    static constexpr float kMaxDb = 40.0f;        // display range
    static constexpr float kFallDbPerSec = 60.0f; // release ballistics

    void push(float grDb, float dtSeconds)
    {
        grDb = juce::jlimit(0.0f, kMaxDb, grDb);
        const float fallen = juce::jmax(mShown - kFallDbPerSec * dtSeconds, 0.0f);
        const float next = juce::jmax(grDb, fallen);
        if (std::abs(next - mShown) > 0.05f)
            repaint();
        mShown = next;
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(colors::track);
        g.fillRoundedRectangle(b, 2.0f);
        const float norm = mShown / kMaxDb;
        if (norm > 0.002f)
        {
            g.setColour(colors::accent);
            g.fillRoundedRectangle(b.removeFromTop(b.getHeight() * norm), 2.0f);
        }
    }

private:
    float mShown = 0.0f;
};

} // namespace nam_rig::ui
