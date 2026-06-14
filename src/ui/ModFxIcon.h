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
    ModFxIcon() { startTimerHz(30); }

    void setType(int t)
    {
        if (t != mType) { mType = t; repaint(); }
    }
    void setActive(bool a)
    {
        if (a != mActive) { mActive = a; repaint(); }
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(colors::tile);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

        auto a = b.reduced(8.0f, 9.0f);
        const juce::Colour c = mActive ? colors::accent : colors::textDim;
        const float ph = mPhase;          // 0..1 animation phase
        const float L = a.getX(), R = a.getRight(), T = a.getY(), B = a.getBottom();
        const float W = a.getWidth(), H = a.getHeight(), cy = a.getCentreY(), cx = a.getCentreX();
        const float k2pi = 6.2831853f;

        auto sinePath = [&](float yc, float amp, float cycles, float phase) {
            juce::Path p;
            const int N = 48;
            for (int i = 0; i <= N; ++i)
            {
                const float t = (float)i / (float)N;
                const float x = L + t * W;
                const float y = yc + amp * std::sin(t * k2pi * cycles + phase * k2pi);
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

        switch (mType)
        {
        case 0: // Chorus — layered shimmering sines
            stroke(sinePath(cy, H * 0.22f, 1.5f, ph), 1.8f, 1.0f);
            stroke(sinePath(cy + 3.0f, H * 0.18f, 1.5f, ph + 0.18f), 1.4f, 0.5f);
            stroke(sinePath(cy - 3.0f, H * 0.18f, 1.5f, ph + 0.36f), 1.4f, 0.5f);
            break;
        case 1: // Flanger — jet: a sine and its mirror, sweeping
        {
            const float s = 0.5f + 0.5f * std::sin(ph * k2pi);
            stroke(sinePath(cy, H * (0.12f + 0.20f * s), 2.0f + 2.0f * s, ph), 1.8f, 1.0f);
            stroke(sinePath(cy, H * (0.12f + 0.20f * s), 2.0f + 2.0f * s, ph + 0.5f), 1.4f, 0.45f);
            break;
        }
        case 2: // Phaser — notch response curve sweeping horizontally
        {
            const float sweep = (std::sin(ph * k2pi)) * (W * 0.16f);
            juce::Path p;
            const int N = 60;
            for (int i = 0; i <= N; ++i)
            {
                const float t = (float)i / (float)N, x = L + t * W;
                float dip = 0.0f;
                for (float nc : {0.34f, 0.66f})
                {
                    const float d = (x - (L + nc * W + sweep)) / (W * 0.07f);
                    dip += std::exp(-d * d);
                }
                const float y = T + H * 0.30f + std::min(1.0f, dip) * H * 0.55f;
                if (i == 0) p.startNewSubPath(x, y);
                else p.lineTo(x, y);
            }
            stroke(p, 1.8f, 1.0f);
            break;
        }
        case 3: // Tremolo — pulsing bars
        {
            const int n = 5;
            const float bw = W / (float)n * 0.5f;
            for (int i = 0; i < n; ++i)
            {
                const float frac = 0.32f + 0.68f * (0.5f + 0.5f * std::sin((ph + (float)i * 0.12f) * k2pi));
                const float bx = L + ((float)i + 0.25f) * (W / (float)n);
                const float bh = H * frac;
                g.setColour(c);
                g.fillRoundedRectangle(bx, B - bh, bw, bh, 1.5f);
            }
            break;
        }
        case 4: // Vibrato — single bold sine wobbling (pitch)
            stroke(sinePath(cy, H * 0.30f, 1.25f, ph), 2.2f, 1.0f);
            break;
        case 5: // Rotary — a blade spinning (Leslie)
        {
            const float r = std::min(W, H) * 0.42f;
            g.setColour(c.withAlpha(0.35f));
            g.drawEllipse(cx - r, cy - r, 2 * r, 2 * r, 1.4f);
            const float ang = ph * k2pi;
            for (int s = 0; s < 2; ++s)
            {
                const float aa = ang + (float)s * 3.14159f;
                juce::Path blade;
                blade.startNewSubPath(cx, cy);
                blade.lineTo(cx + r * std::cos(aa), cy + r * std::sin(aa) * 0.55f);
                stroke(blade, 2.2f, s == 0 ? 1.0f : 0.5f);
            }
            g.setColour(c);
            g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
            break;
        }
        case 6: // Uni-Vibe — concentric pulsing rings (photocell glow)
        {
            for (int r = 0; r < 3; ++r)
            {
                const float pr = ((float)r + 0.5f + ph) ;
                const float frac = pr - std::floor(pr); // 0..1 expanding
                const float rad = frac * std::min(W, H) * 0.5f;
                g.setColour(c.withAlpha((1.0f - frac) * 0.9f));
                g.drawEllipse(cx - rad, cy - rad, 2 * rad, 2 * rad, 1.6f);
            }
            break;
        }
        case 7: // Harmonic Tremolo — two bands trading brightness (opposite phase)
        {
            const float s = 0.5f + 0.5f * std::sin(ph * k2pi);
            g.setColour(c.withAlpha(0.25f + 0.75f * s));
            g.fillRoundedRectangle(L, T, W, H * 0.42f, 2.0f);          // high band
            g.setColour(c.withAlpha(0.25f + 0.75f * (1.0f - s)));
            g.fillRoundedRectangle(L, B - H * 0.42f, W, H * 0.42f, 2.0f); // low band
            break;
        }
        default:
            break;
        }
    }

private:
    void timerCallback() override
    {
        mPhase += 0.018f;
        if (mPhase >= 1.0f) mPhase -= 1.0f;
        if (isShowing())
            repaint();
    }

    int mType = 0;
    bool mActive = true;
    float mPhase = 0.0f;
};

} // namespace nam_rig::ui
