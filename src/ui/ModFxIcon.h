#pragma once
// ModFxIcon — a small animated glyph that gives each mod slot a visual identity.
// One per slot; it paints the effect's signature motion (chorus shimmer, phaser
// notch sweep, tremolo pulse, ...) so the user reads what's loaded at a glance.
// 100% hand-drawn with juce::Path — no image assets, no icon libraries.

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "RigLookAndFeel.h"

namespace nam_rig::ui
{

class ModFxIcon : public juce::Component, private juce::Timer
{
public:
    ModFxIcon() {} // static glyphs (the lane scope carries the live motion now)

    void setType(int t)
    {
        if (t != mType) { mType = t; repaint(); }
    }
    void setActive(bool a)
    {
        if (a != mActive) { mActive = a; repaint(); }
    }
    // Per-lane tint for the glyph + box border (defaults to the global accent).
    void setAccent(juce::Colour c)
    {
        if (c != mAccent) { mAccent = c; repaint(); }
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(colors::tile);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(mActive ? mAccent.withAlpha(0.7f) : colors::outline);
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

        auto a = b.reduced(9.0f, 10.0f);
        const juce::Colour c = mActive ? mAccent : colors::textDim;
        const float L = a.getX(), W = a.getWidth(), H = a.getHeight();
        const float cy = a.getCentreY(), cx = a.getCentreX();
        const float k2pi = 6.2831853f;

        // Clean single-stroke sine (static), matching the HTML lane icons.
        auto sinePath = [&](float yc, float amp, float cycles, float phase) {
            juce::Path p;
            const int N = 44;
            for (int i = 0; i <= N; ++i)
            {
                const float t = (float)i / (float)N;
                const float x = L + t * W;
                const float y = yc - amp * std::sin(t * k2pi * cycles + phase * k2pi);
                if (i == 0) p.startNewSubPath(x, y);
                else p.lineTo(x, y);
            }
            return p;
        };
        auto stroke = [&](const juce::Path &p, float w, float alpha) {
            g.setColour(c.withAlpha(alpha));
            g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        };

        // A curve drawn from a per-x function (for the chirp / notch shapes).
        auto curve = [&](auto fy, float w, float alpha) {
            juce::Path p;
            const int N = 60;
            for (int i = 0; i <= N; ++i)
            {
                const float t = (float)i / (float)N;
                const float x = L + t * W, y = fy(t);
                if (i == 0) p.startNewSubPath(x, y);
                else p.lineTo(x, y);
            }
            stroke(p, w, alpha);
        };

        switch (mType)
        {
        case 0: // Chorus — two parallel even ripples (whole cycles -> balanced)
            stroke(sinePath(cy - 3.4f, H * 0.13f, 2.0f, 0.0f), 1.8f, 1.0f);
            stroke(sinePath(cy + 3.4f, H * 0.13f, 2.0f, 0.0f), 1.8f, 0.6f);
            break;
        case 1: // Flanger — a frequency sweep (chirp): waves tighten left->right
            curve([&](float t) { return cy - H * 0.24f * std::sin(k2pi * (0.8f * t + 1.7f * t * t)); },
                  1.9f, 1.0f);
            break;
        case 2: // Phaser — swept notches: a high line dipping at two points (comb)
            curve([&](float t) {
                float y = cy - H * 0.20f;
                for (float nc : {0.32f, 0.70f}) { const float d = (t - nc) / 0.10f; y += H * 0.46f * std::exp(-d * d); }
                return y;
            }, 1.9f, 1.0f);
            break;
        case 3: // Tremolo — amplitude bars
        {
            const int n = 4;
            const float bw = 2.6f, step = W / (float)n;
            const float frac[4] = {0.5f, 1.0f, 0.55f, 0.85f};
            for (int i = 0; i < n; ++i)
            {
                const float bx = L + ((float)i + 0.5f) * step - bw * 0.5f;
                const float bh = H * frac[i];
                g.setColour(c);
                g.fillRoundedRectangle(bx, cy - bh * 0.5f, bw, bh, 1.3f);
            }
            break;
        }
        case 4: // Vibrato — one deep even sine (pitch)
            stroke(sinePath(cy, H * 0.30f, 2.0f, 0.0f), 2.2f, 1.0f);
            break;
        case 5: // Rotary — a disc (record): outer ring + hub
        {
            const float r = std::min(W, H) * 0.46f;
            g.setColour(c);
            g.drawEllipse(cx - r, cy - r, 2 * r, 2 * r, 1.8f);
            g.drawEllipse(cx - r * 0.34f, cy - r * 0.34f, r * 0.68f, r * 0.68f, 1.4f);
            g.fillEllipse(cx - 1.5f, cy - 1.5f, 3.0f, 3.0f);
            break;
        }
        case 6: // Uni-Vibe — circle with a centre dot (photocell eye)
        {
            const float r = std::min(W, H) * 0.42f;
            g.setColour(c);
            g.drawEllipse(cx - r, cy - r, 2 * r, 2 * r, 1.8f);
            g.fillEllipse(cx - 2.8f, cy - 2.8f, 5.6f, 5.6f);
            break;
        }
        case 7: // Harmonic Tremolo — two stacked bands (high / low)
            g.setColour(c);
            g.fillRoundedRectangle(L, cy - H * 0.30f, W, H * 0.22f, 2.0f);
            g.setColour(c.withAlpha(0.5f));
            g.fillRoundedRectangle(L, cy + H * 0.08f, W, H * 0.22f, 2.0f);
            break;
        case 8: // Bi-Phase — two even sines in opposite phase (crossing)
            stroke(sinePath(cy, H * 0.18f, 2.0f, 0.0f), 1.8f, 1.0f);
            stroke(sinePath(cy, H * 0.18f, 2.0f, 0.5f), 1.8f, 0.6f);
            break;
        default:
            break;
        }
    }

private:
    void timerCallback() override {} // static glyphs: no animation

    int mType = 0;
    bool mActive = true;
    juce::Colour mAccent = colors::accent;
};

} // namespace nam_rig::ui
