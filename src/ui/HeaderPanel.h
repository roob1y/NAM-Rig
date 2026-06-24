#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RigLookAndFeel.h"

namespace nam_rig::ui
{

// Small custom button: a 3-bar "hamburger" in a rounded tile. Used for the
// header Settings menu. Highlights on hover / when its menu is open.
class HamburgerButton : public juce::Component
{
public:
    std::function<void()> onClick;
    bool open = false;

    void mouseEnter(const juce::MouseEvent &) override { mHot = true; repaint(); }
    void mouseExit(const juce::MouseEvent &) override { mHot = false; repaint(); }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (onClick && getLocalBounds().contains(e.getPosition()))
            onClick();
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour((open || mHot) ? colors::tileSel : colors::tile);
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour((open || mHot) ? colors::accent.withAlpha(0.6f) : colors::outline);
        g.drawRoundedRectangle(b, 8.0f, 1.0f);

        const float w = 15.0f, cx = b.getCentreX(), cy = b.getCentreY();
        g.setColour(juce::Colour(0xffc2c7cf));
        for (int i = -1; i <= 1; ++i)
            g.fillRoundedRectangle(cx - w * 0.5f, cy + (float)i * 5.0f - 1.0f, w, 2.0f, 1.0f);
    }

private:
    bool mHot = false;
};

// The header band: gradient background, wordmark (logo + "NAM RIG"), and the
// loaded-capture rows (Rig A/B amp model names with coloured tags). Interactive
// widgets (preset bar, I/O knobs, hamburger) are positioned over it by the
// editor; this component paints the static chrome at rects the editor passes in.
class HeaderPanel : public juce::Component
{
public:
    void setLayout(juce::Rectangle<int> wordmark, juce::Rectangle<int> captures)
    {
        mWordmark = wordmark;
        mCaptures = captures;
        repaint();
    }

    void setCaptures(const juce::String &a, bool aLoaded, const juce::String &b, bool bLoaded)
    {
        if (a != mNameA || b != mNameB || aLoaded != mLoadedA || bLoaded != mLoadedB)
        {
            mNameA = a; mNameB = b; mLoadedA = aLoaded; mLoadedB = bLoaded;
            repaint();
        }
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        juce::ColourGradient grad(colors::headerTop, b.getTopLeft(),
                                  colors::headerBot, b.getBottomLeft(), false);
        dither::fillRect(g, grad, b);
        g.setColour(juce::Colour(0xff23262d));
        g.fillRect(0.0f, b.getBottom() - 1.0f, b.getWidth(), 1.0f);

        paintWordmark(g);
        paintCaptures(g);
    }

private:
    void paintWordmark(juce::Graphics &g)
    {
        auto r = mWordmark;
        if (r.isEmpty()) return;

        // Logo: rounded amber tile with an open-ring "gauge" glyph.
        auto logo = juce::Rectangle<float>(32.0f, 32.0f)
                        .withCentre({(float)r.getX() + 16.0f, (float)r.getCentreY()});
        juce::ColourGradient lg(colors::accent, logo.getTopLeft(),
                                juce::Colour(0xffc77a1e), logo.getBottomRight(), false);
        dither::fillRoundedRectangle(g, lg, logo, 8.0f);

        juce::Path ring;
        const float rad = 6.5f;
        ring.addCentredArc(logo.getCentreX(), logo.getCentreY(), rad, rad, 0.0f,
                           juce::degreesToRadians(20.0f), juce::degreesToRadians(320.0f), true);
        g.setColour(colors::bg);
        g.strokePath(ring, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        g.setColour(colors::textBright);
        g.setFont(fonts::archivo(21.0f, fonts::ExtraBold, 0.02f));
        g.drawText("NAM RIG", r.withTrimmedLeft(42), juce::Justification::centredLeft);
    }

    void paintCaptures(juce::Graphics &g)
    {
        auto r = mCaptures;
        if (r.isEmpty()) return;

        const int rowH = juce::jmin(22, r.getHeight() / 2);
        auto rowsArea = r.withSizeKeepingCentre(r.getWidth(), rowH * 2 + 5);
        struct Cap { juce::String name; bool loaded; juce::Colour col; const char *tag; };
        const Cap caps[] = {
            {mNameA, mLoadedA, colors::titleAccent, "A"},
            {mNameB, mLoadedB, colors::laneColour(1), "B"},
        };
        for (int i = 0; i < 2; ++i)
        {
            auto row = rowsArea.removeFromTop(rowH);
            if (i == 0) rowsArea.removeFromTop(5);
            auto tag = juce::Rectangle<float>(17.0f, 17.0f)
                           .withCentre({(float)row.getX() + 8.5f, (float)row.getCentreY()});
            g.setColour(caps[i].col);
            g.fillRoundedRectangle(tag, 4.0f);
            g.setColour(colors::bg);
            g.setFont(fonts::archivo(11.0f, fonts::ExtraBold));
            g.drawText(caps[i].tag, tag, juce::Justification::centred);

            auto txt = row.withTrimmedLeft(26);
            g.setColour(caps[i].loaded ? juce::Colour(0xffc2c7cf) : colors::caption);
            g.setFont(fonts::mono(13.0f, fonts::Medium));
            g.drawText(caps[i].name, txt, juce::Justification::centredLeft, true);
        }
    }

    juce::Rectangle<int> mWordmark, mCaptures;
    juce::String mNameA = "No model", mNameB = "No model";
    bool mLoadedA = false, mLoadedB = false;
};

} // namespace nam_rig::ui
