#pragma once
// ReverbIcon — a small animated glyph giving each reverb CHARACTER a visual
// identity, exactly like ModFxIcon does for the modulation slots. One per
// reverb panel; it paints the character's signature motion (room reflections,
// hall arcs, plate ripple, spring coil, shimmer rising, ambience scatter, bloom
// swell). 100% hand-drawn with juce::Path — no image assets.
//
// Type order matches nam_rig::ReverbBlock::Type:
//   0 Room  1 Hall  2 Plate  3 Spring  4 Shimmer  5 Ambience  6 Bloom

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdint>
#include "RigLookAndFeel.h"

namespace nam_rig::ui
{

class ReverbIcon : public juce::Component, private juce::Timer
{
public:
    ReverbIcon() { startTimerHz(30); }

    void setType(int t) { if (t != mType) { mType = t; repaint(); } }
    void setActive(bool a) { if (a != mActive) { mActive = a; repaint(); } }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(colors::tile);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

        auto a = b.reduced(8.0f, 9.0f);
        const juce::Colour c = mActive ? colors::accent : colors::textDim;
        const float ph = mPhase; // 0..1 animation phase
        const float L = a.getX(), R = a.getRight(), T = a.getY(), B = a.getBottom();
        const float W = a.getWidth(), H = a.getHeight(), cx = a.getCentreX(), cy = a.getCentreY();
        const float k2pi = 6.2831853f;

        auto stroke = [&](const juce::Path &p, float w, float alpha) {
            g.setColour(c.withAlpha(alpha));
            g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        };

        switch (mType)
        {
        case 0: // Room — a source dot and a couple of short early reflections
        {
            g.setColour(c);
            g.fillEllipse(L + W * 0.18f - 2.0f, cy - 2.0f, 4.0f, 4.0f);
            const float pulse = 0.5f + 0.5f * std::sin(ph * k2pi);
            for (int i = 0; i < 2; ++i)
            {
                juce::Path p;
                const float x = L + W * (0.45f + 0.22f * (float)i);
                p.startNewSubPath(x, T + H * 0.20f);
                p.lineTo(x, B - H * 0.20f);
                stroke(p, 1.6f, (i == 0 ? 0.9f : 0.5f) * (0.5f + 0.5f * pulse));
            }
            break;
        }
        case 1: // Hall — concentric expanding arcs (a big space)
        {
            for (int i = 0; i < 3; ++i)
            {
                const float pr = ((float)i + 0.4f + ph);
                const float frac = pr - std::floor(pr);
                const float rad = frac * std::min(W, H) * 0.62f;
                juce::Path p;
                p.addCentredArc(L + W * 0.16f, cy, rad, rad, 0.0f, -1.0f, 1.0f, true);
                stroke(p, 1.7f, (1.0f - frac) * 0.9f);
            }
            break;
        }
        case 2: // Plate — a taut rectangle rippling (horizontal standing waves)
        {
            g.setColour(c.withAlpha(0.35f));
            g.drawRoundedRectangle(L, T + H * 0.12f, W, H * 0.76f, 2.0f, 1.2f);
            for (int i = 0; i < 3; ++i)
            {
                juce::Path p;
                const float yc = T + H * (0.30f + 0.20f * (float)i);
                const int N = 40;
                for (int k = 0; k <= N; ++k)
                {
                    const float t = (float)k / (float)N, x = L + t * W;
                    const float y = yc + std::sin(t * k2pi * 2.0f + ph * k2pi + (float)i) * H * 0.045f;
                    if (k == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
                }
                stroke(p, 1.4f, 0.9f);
            }
            break;
        }
        case 3: // Spring — a horizontal coil that wobbles
        {
            juce::Path p;
            const int N = 96;
            const float loops = 5.0f;
            for (int k = 0; k <= N; ++k)
            {
                const float t = (float)k / (float)N;
                const float x = L + t * W;
                const float y = cy + std::sin(t * k2pi * loops - ph * k2pi) * H * 0.34f
                                * (0.5f + 0.5f * std::sin(t * 3.14159f));
                if (k == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            stroke(p, 1.7f, 1.0f);
            break;
        }
        case 4: // Shimmer — particles rising (octave up)
        {
            for (int i = 0; i < 5; ++i)
            {
                const float base = (float)i / 5.0f;
                float rise = base + ph * 0.9f;
                rise -= std::floor(rise);                       // 0..1 travelling up
                const float x = L + W * (0.12f + 0.76f * ((float)i / 4.0f));
                const float y = B - rise * H;
                const float al = std::sin(rise * 3.14159f);     // fade in/out at ends
                g.setColour(c.withAlpha(al * 0.95f));
                const float s = 1.6f + 1.6f * (1.0f - rise);
                g.fillEllipse(x - s, y - s, 2 * s, 2 * s);
            }
            break;
        }
        case 5: // Ambience — a soft scatter of short, shimmering ticks
        {
            const uint32_t seed = 0x9e3779b9u;
            for (int i = 0; i < 9; ++i)
            {
                uint32_t h = (uint32_t)(i + 1) * seed;
                const float fx = (float)((h >> 9) & 1023) / 1023.0f;
                const float fy = (float)((h >> 19) & 1023) / 1023.0f;
                const float tw = 0.5f + 0.5f * std::sin((ph + fx + fy) * k2pi);
                const float x = L + 4.0f + fx * (W - 8.0f);
                const float y = T + 4.0f + fy * (H - 8.0f);
                g.setColour(c.withAlpha(0.25f + 0.7f * tw));
                g.fillEllipse(x - 1.5f, y - 1.5f, 3.0f, 3.0f);
            }
            break;
        }
        case 6: // Bloom — a slow swelling ring with a soft core
        {
            const float swell = 0.5f + 0.5f * std::sin(ph * k2pi);
            const float rad = std::min(W, H) * (0.20f + 0.28f * swell);
            g.setColour(c.withAlpha(0.9f));
            g.drawEllipse(cx - rad, cy - rad, 2 * rad, 2 * rad, 1.8f);
            g.setColour(c.withAlpha(0.25f + 0.45f * swell));
            const float cr = rad * 0.45f;
            g.fillEllipse(cx - cr, cy - cr, 2 * cr, 2 * cr);
            break;
        }
        default:
            break;
        }
    }

private:
    void timerCallback() override
    {
        mPhase += 0.012f;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
        if (isShowing())
            repaint();
    }

    int mType = 1; // default Hall
    bool mActive = true;
    float mPhase = 0.0f;
};

} // namespace nam_rig::ui
