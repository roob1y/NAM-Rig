#pragma once
// IrGraph — single source of truth for the cab IR response WELL: the magnitude
// curve + tone-zone energy heat map + zone labels + frequency markers. Shared by
// the loaded-cab panel (CabPanel) AND the IR-library preview so a file looks the
// SAME before you drag it onto a cab as it does once loaded (mirrors IrAnalysis.h,
// which already unifies the tone TAG the two show).

#include "ui/RigLookAndFeel.h"
#include "rig/IrAnalysis.h"
#include <cmath>

namespace nam_rig::ui
{

// Draw an IR's mean-centred magnitude response into `well`. `resp` holds
// nam_rig::ir::kResPts points (log-spaced kResFLo..kResFHi); it is read ONLY when
// haveIr is true. When haveIr is false a flat 0 dB "direct" line is drawn with
// `emptyText` centred in the well.
inline void drawIrResponse(juce::Graphics &g, juce::Rectangle<float> well,
                           const float *resp, bool haveIr,
                           const juce::String &emptyText)
{
    RigLookAndFeel::drawWell(g, well);
    auto in = well.reduced(2.0f);
    g.setColour(juce::Colour(0xff262c34));
    g.fillRect(in.getX(), in.getCentreY(), in.getWidth(), 1.0f);

    // Display scale: +/-18 dB across the well half-height.
    constexpr float kSpanDb = 18.0f;
    auto dbToY = [&](float db) {
        return juce::jlimit(in.getY(), in.getBottom(),
                            in.getCentreY() - (db / kSpanDb) * in.getHeight() * 0.5f);
    };

    juce::Path line;
    const int n = juce::jmax(2, (int)in.getWidth());
    const int P = nam_rig::ir::kResPts;
    for (int x = 0; x < n; ++x)
    {
        const float t = (float)x / (float)(n - 1); // 0..1 across the log-f axis
        float db = 0.0f;
        if (haveIr)
        {
            // map x to the response array (log-spaced, same fLo..fHi) + lerp
            const float fp = t * (float)(P - 1);
            const int i0 = juce::jlimit(0, P - 1, (int)fp);
            const int i1 = juce::jmin(P - 1, i0 + 1);
            db = resp[i0] + (resp[i1] - resp[i0]) * (fp - (float)i0);
        }
        const float y = dbToY(db);
        const float px = in.getX() + (float)x;
        if (x == 0) line.startNewSubPath(px, y);
        else line.lineTo(px, y);
    }
    juce::Path area = line;
    area.lineTo(in.getRight(), in.getBottom());
    area.lineTo(in.getX(), in.getBottom());
    area.closeSubPath();

    // Tone-zone energy heat map: each guitar band is tinted by how much energy
    // this IR has there RELATIVE to its own average (the curve is mean-centred),
    // so the cab's character reads at a glance — hot lows = thick, hot presence =
    // bright/cutting, hot fizz = harsh.
    struct Zone { double fLo, fHi; const char *lbl; };
    static const Zone zones[] = {
        {nam_rig::ir::kResFLo, 120.0, "LOWS"}, {120.0, 400.0, "BODY"}, {400.0, 1500.0, "MIDS"},
        {1500.0, 4000.0, "PRESENCE"}, {4000.0, 8000.0, "FIZZ"}};
    const double zfLo = nam_rig::ir::kResFLo, zfHi = nam_rig::ir::kResFHi;
    const double lr = std::log(zfHi / zfLo);
    auto tOf = [&](double f) { return (float)(std::log(f / zfLo) / lr); };
    auto idxOf = [&](double f) {
        return juce::jlimit(0, P - 1, (int)std::round((double)(P - 1) * std::log(f / zfLo) / lr));
    };

    if (haveIr)
    {
        const juce::Colour cold(0xff2f6fae), warm = colors::accent, hot(0xffff5a2a);
        auto blend = [](juce::Colour a, juce::Colour b, float t) {
            return juce::Colour::fromFloatRGBA(
                a.getFloatRed() + (b.getFloatRed() - a.getFloatRed()) * t,
                a.getFloatGreen() + (b.getFloatGreen() - a.getFloatGreen()) * t,
                a.getFloatBlue() + (b.getFloatBlue() - a.getFloatBlue()) * t, 1.0f);
        };
        for (auto &z : zones)
        {
            int c0 = idxOf(z.fLo), c1 = idxOf(z.fHi);
            float zdb = 0.0f;
            for (int i = c0; i <= c1; ++i) zdb += resp[i];
            zdb /= (float)(c1 - c0 + 1);
            const float tt = juce::jlimit(0.0f, 1.0f, (zdb + 9.0f) / 18.0f); // -9..+9 dB
            const juce::Colour c = (tt < 0.5f) ? blend(cold, warm, tt * 2.0f)
                                               : blend(warm, hot, (tt - 0.5f) * 2.0f);
            const float xL = in.getX() + tOf(z.fLo) * in.getWidth();
            const float xR = in.getX() + tOf(z.fHi) * in.getWidth();
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(area);
            g.reduceClipRegion(juce::Rectangle<int>((int)std::floor(xL), (int)std::floor(in.getY()),
                                                    (int)std::ceil(xR - xL), (int)std::ceil(in.getHeight())));
            juce::ColourGradient grad(c.withAlpha(0.46f), 0.0f, in.getY(),
                                      c.withAlpha(0.05f), 0.0f, in.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRect(in);
        }
    }
    else
    {
        juce::ColourGradient fill(colors::accent.withAlpha(0.06f), in.getX(), in.getY(),
                                  colors::accent.withAlpha(0.0f), in.getX(), in.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(area);
    }
    g.setColour(haveIr ? colors::accent : colors::accent.withAlpha(0.35f));
    g.strokePath(line, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));

    // Zone dividers + labels along the top of the well.
    for (auto &z : zones)
    {
        const float xL = in.getX() + tOf(z.fLo) * in.getWidth();
        const float xR = in.getX() + tOf(z.fHi) * in.getWidth();
        if (z.fHi < zfHi - 1.0)
        {
            g.setColour(juce::Colour(0x16ffffff));
            g.fillRect(xR, in.getY(), 1.0f, in.getHeight());
        }
        g.setColour(haveIr ? colors::caption : colors::captionDim);
        g.setFont(fonts::archivo(8.5f, fonts::SemiBold, 0.06f));
        g.drawText(z.lbl, juce::Rectangle<float>(xL, in.getY() + 3.0f, xR - xL, 11.0f),
                   juce::Justification::centred);
    }

    // Zone-boundary frequency markers along the bottom (lined up with the
    // dividers): 40, 120, 400, 1.5k, 4k, 8k.
    {
        g.setColour(juce::Colour(0xff5a616b));
        g.setFont(fonts::mono(8.0f, fonts::SemiBold));
        auto freqStr = [](double f) -> juce::String {
            if (f >= 1000.0)
            {
                const double k = f / 1000.0;
                return (k == std::floor(k)) ? juce::String((int)k) + "k" : juce::String(k, 1) + "k";
            }
            return juce::String((int)(f + 0.5));
        };
        const int nz = (int)(sizeof(zones) / sizeof(zones[0]));
        for (int b = 0; b <= nz; ++b)
        {
            const double f = (b == 0) ? zones[0].fLo : zones[b - 1].fHi;
            const float cx = in.getX() + tOf(f) * in.getWidth();
            const float w = 42.0f;
            const float rx = juce::jlimit(in.getX() + 1.0f, in.getRight() - w - 1.0f, cx - w * 0.5f);
            g.drawText(freqStr(f), juce::Rectangle<float>(rx, in.getBottom() - 13.0f, w, 11.0f),
                       juce::Justification::centred);
        }
    }

    if (!haveIr && emptyText.isNotEmpty())
    {
        g.setColour(colors::captionDim);
        g.setFont(fonts::mono(11.0f));
        g.drawText(emptyText, in.toNearestInt(), juce::Justification::centred);
    }
}

} // namespace nam_rig::ui
