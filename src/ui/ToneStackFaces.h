#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/RigLookAndFeel.h"
#include <cmath>
#include <vector>
#include <utility>

// ============================================================================
// Per-model AMP FACES for the tone stack section.
//
// The tone stack of each amp is drawn as the control panel of the amp whose
// circuit it models -- the same idea as the pedalboard's per-model pedal faces
// (DrivePedal). This header carries:
//
//   * FaceSpec table (14 models, order = TonestackBlock::Model) -- the livery
//     (jewel lens, label colour) + which knob painter each model wears.
//   * paintFace() -- draws one model's panel background + furniture (screws,
//     gloss, jewel pilot lamp, brand-free plate) into a rectangle, honouring
//     the tone-On (jewel lit / face dimmed) and CAL (amber dashed ring) states.
//   * AmpKnobLnF -- a LookAndFeel whose drawRotarySlider paints one of five
//     amp-knob styles (chicken-head / skirted / top-hat / pointer / modern),
//     configured per model. Assigned to the tone LabeledKnob sliders.
//   * pictogram() -- Graphic '72's speaker/wave glyphs above its knobs.
//
// All gradients go through dither::fill* (never a noise overlay). Fonts are
// Archivo throughout (the silverface "script" is an italic serif here).
// ============================================================================
namespace nam_rig::ui::ampface
{

// The five knob painters (DrivePedal-face pattern: one enum keyed per model).
enum Painter { Chicken, Skirt, TopHat, Pointer, Modern };

// Knob variant within a painter:
//  Chicken : 0 black · 1 cream · 2 black + white pointer line
//  Skirt   : 0 cream · 1 silver
//  TopHat  : 0 gold  · 1 dark
//  Pointer : 0 cream · 1 black · 2 brown
//  Modern  : 0
struct FaceSpec
{
    Painter painter;
    int     knobVar;
    bool    numbers;         // skirted 1..10 digits (the two Fender-family faces)
    juce::Colour jewel;      // pilot-lamp lens colour
    juce::Colour label;      // knob caption colour on this panel
    fonts::Weight labelWeight;
    float   labelTrack;      // knob-caption tracking (em)
};

// order = TonestackBlock::Model (Tweed 59 .. Tweed 57)
inline const FaceSpec &spec(int model)
{
    using juce::Colour;
    static const FaceSpec table[14] = {
        /* 0  Tweed 59  */ { Chicken, 0, false, Colour(0xffe8552f), Colour(0xff23262b), fonts::Bold,      0.13f },
        /* 1  Black 65  */ { Skirt,   0, true,  Colour(0xffffb13d), Colour(0xffeceff2), fonts::Bold,      0.13f },
        /* 2  Silver 69 */ { Skirt,   1, true,  Colour(0xffe8552f), Colour(0xff2b4a8b), fonts::Bold,      0.13f },
        /* 3  Cali Lead */ { Pointer, 0, false, Colour(0xffe8452f), Colour(0xffe9e9e6), fonts::Bold,      0.16f },
        /* 4  Brit 800  */ { TopHat,  1, false, Colour(0xffff4a3a), Colour(0xffffffff), fonts::Bold,      0.13f },
        /* 5  Brit 45   */ { TopHat,  0, false, Colour(0xffe83a2a), Colour(0xff241a05), fonts::Bold,      0.13f },
        /* 6  Brit Major*/ { TopHat,  1, false, Colour(0xffff4a3a), Colour(0xfff2ead2), fonts::Bold,      0.13f },
        /* 7  Solo 100  */ { Chicken, 2, false, Colour(0xffb6a4e8), Colour(0xfff2f2f4), fonts::Bold,      0.18f },
        /* 8  Red Star  */ { Chicken, 0, false, Colour(0xffff3b30), Colour(0xffe6e2d0), fonts::ExtraBold, 0.20f },
        /* 9  Classic 20*/ { Chicken, 1, false, Colour(0xffffb13d), Colour(0xff23262b), fonts::Bold,      0.13f },
        /* 10 Solid Clean*/{ Modern,  0, false, Colour(0xff7ddc84), Colour(0xffcfd4da), fonts::Bold,      0.14f },
        /* 11 Top Boost */ { Chicken, 0, false, Colour(0xffff3b30), Colour(0xfff6efe2), fonts::Bold,      0.13f },
        /* 12 Graphic 72*/ { Pointer, 1, false, Colour(0xffffb13d), Colour(0xff241f10), fonts::Bold,      0.13f },
        /* 13 Tweed 57  */ { Pointer, 2, false, Colour(0xffe8a03a), Colour(0xff23262b), fonts::Bold,      0.13f },
    };
    return table[juce::jlimit(0, 13, model)];
}

// Some faces shift their controls sideways to make room for face art
// (Red Star's star, Cali Lead's left rule). Returns extra left padding, in px,
// the section should apply to the knob group for this model.
inline int controlsLeftPad(int model)
{
    if (model == 3) return 16; // Cali Lead: left white rule
    return 0;                  // Red Star: knobs stay centred (star sits behind)
}

// The knob's visible TOP, as a fraction of the dial size below the dial's top edge,
// used to vertically centre the knob+label as one unit. Chicken-heads protrude a
// beak above the dome (that beak IS the top of the knob); the other painters top
// out at the dome edge.
inline float knobTopFrac(int model)
{
    return spec(model).painter == Chicken ? 0.047f : 0.14f;
}

// ---------------------------------------------------------------------------
// small paint helpers
// ---------------------------------------------------------------------------
namespace detail
{
    // Tweed panel: 45° four-tone diagonal stripes (3px each, 12px repeat) exactly
    // as the reference <pattern>, plus a top->bottom varnish wash (lacquer). The
    // four tones cycle s0,s1,s2,base across the perpendicular of the "/" bands.
    inline void tweed(juce::Graphics &g, juce::Rectangle<float> r,
                      juce::Colour s0, juce::Colour s1, juce::Colour s2, juce::Colour base,
                      juce::Colour vTop, juce::Colour vBot)
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(r.getSmallestIntegerContainer());
        g.setColour(base);
        g.fillRect(r);
        const juce::Colour pal[4] = {s0, s1, s2, base};
        const float step = 4.243f; // 3px perpendicular between band centres
        int k = 0;
        // "\" bands: constant (x - y), matching the reference's rotate(45) weave.
        for (float d = r.getX() - r.getBottom(); d <= r.getRight() - r.getY(); d += step, ++k)
        {
            const float xa = juce::jlimit(r.getX(), r.getRight(), r.getY() + d);
            const float xb = juce::jlimit(r.getX(), r.getRight(), r.getBottom() + d);
            g.setColour(pal[k & 3]);
            g.drawLine(xa, xa - d, xb, xb - d, 3.2f);
        }
        juce::ColourGradient vg(vTop, r.getX(), r.getY(), vBot, r.getX(), r.getBottom(), false);
        g.setGradientFill(vg);
        g.fillRect(r);
    }

    // A brushed chrome bar (exact reference stops: light top, hot mid-dark, light base).
    // Uses a solid opaque base + plain gradient fill (NOT the dithered image path,
    // which comes out slightly translucent on some renderers and let the tweed show
    // through the plate).
    inline void chromeBar(juce::Graphics &g, juce::Rectangle<float> r)
    {
        g.setColour(juce::Colour(0xffc3c9cf)); // opaque floor
        g.fillRoundedRectangle(r, 7.0f);
        juce::ColourGradient cg(juce::Colour(0xfff2f4f6), r.getX(), r.getY(),
                                juce::Colour(0xffcdd3d9), r.getX(), r.getBottom(), false);
        cg.addColour(0.42, juce::Colour(0xffc3c9cf));
        cg.addColour(0.55, juce::Colour(0xff969ca4));
        g.setGradientFill(cg);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(juce::Colour(0xff71767d));
        g.drawRoundedRectangle(r, 7.0f, 1.0f);
    }

    inline void screw(juce::Graphics &g, float cx, float cy)
    {
        auto s = juce::Rectangle<float>(8.0f, 8.0f).withCentre({cx, cy});
        juce::Path sp;
        sp.addEllipse(s);
        juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 2, {0, 1}).drawForPath(g, sp);
        juce::ColourGradient sg(juce::Colour(0xff9aa0a8), s.getX() + 2.5f, s.getY() + 2.0f,
                                juce::Colour(0xff17191c), s.getRight(), s.getBottom(), true);
        sg.addColour(0.65, juce::Colour(0xff3c4046));
        dither::fillEllipse(g, sg, s);
        g.setColour(juce::Colour(0xff14161a));
        auto c = s.getCentre();
        const float a = juce::degreesToRadians(38.0f), rr = 2.6f;
        g.drawLine(c.x - rr * std::cos(a), c.y - rr * std::sin(a),
                   c.x + rr * std::cos(a), c.y + rr * std::sin(a), 1.0f);
    }
}

// ---------------------------------------------------------------------------
// paintFace: draw one model's panel into `bounds`.
//   on  : tone stack engaged -> jewel lit; else the whole face dims.
//   cal : CAL (capture-EQ) mode -> amber dashed ring + "CAPTURE EQ" tag.
// ---------------------------------------------------------------------------
inline void paintFace(juce::Graphics &g, juce::Rectangle<int> boundsI,
                      int model, bool on, bool cal)
{
    using juce::Colour;
    const auto r = boundsI.toFloat();
    const float rad = 10.0f;
    const auto &sp = spec(model);

    juce::Path clip;
    clip.addRoundedRectangle(r, rad);

    // ---- panel background (clipped to the rounded face) ----
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(clip);

        auto fill = [&](Colour a, Colour b) {
            g.setColour(a); g.fillRect(r); // opaque floor (panels must not show the lane through them)
            juce::ColourGradient cg(a, r.getX(), r.getY(), b, r.getX(), r.getBottom(), false);
            dither::fillRect(g, cg, r);
        };
        auto fill3 = [&](Colour a, Colour m, Colour b, float mp) {
            g.setColour(a); g.fillRect(r);
            juce::ColourGradient cg(a, r.getX(), r.getY(), b, r.getX(), r.getBottom(), false);
            cg.addColour(mp, m);
            dither::fillRect(g, cg, r);
        };

        switch (model)
        {
        case 0: // Tweed 59 -- lacquered tweed + chrome bar
            detail::tweed(g, r, Colour(0xffcfa76b), Colour(0xffdcb87e), Colour(0xffa67c40), Colour(0xffb8904e),
                          Colour(0xff693c0f).withAlpha(0.30f), Colour(0xff3c1e05).withAlpha(0.38f));
            detail::chromeBar(g, juce::Rectangle<float>(r.getX() + 12, r.getY() + 29, r.getWidth() - 24, 92));
            break;
        case 9: // Classic 20 -- darker/tighter tweed + chrome bar
            detail::tweed(g, r, Colour(0xffb98a52), Colour(0xffc69a60), Colour(0xff8a6130), Colour(0xff9c7038),
                          Colour(0xff50280a).withAlpha(0.38f), Colour(0xff2d1405).withAlpha(0.44f));
            detail::chromeBar(g, juce::Rectangle<float>(r.getX() + 12, r.getY() + 29, r.getWidth() - 24, 92));
            break;
        case 13: // Tweed 57 -- aged tweed + chrome bar (same size as Tweed 59)
            detail::tweed(g, r, Colour(0xffc39a5e), Colour(0xffcfa768), Colour(0xff93672f), Colour(0xffa67c42),
                          Colour(0xff5a320c).withAlpha(0.42f), Colour(0xff321906).withAlpha(0.50f));
            detail::chromeBar(g, juce::Rectangle<float>(r.getX() + 12, r.getY() + 29, r.getWidth() - 24, 92));
            break;
        case 1: // Black 65 -- black panel + white keyline
            fill3(Colour(0xff1e1e20), Colour(0xff101012), Colour(0xff151517), 0.7f);
            g.setColour(Colour(0xfff0f0f0).withAlpha(0.6f));
            g.drawRoundedRectangle(r.reduced(11.0f), 5.0f, 1.0f);
            break;
        case 2: // Silver 69 -- brushed alu + blue rails
        {
            fill3(Colour(0xffd7dbdf), Colour(0xffc4cad0), Colour(0xff9aa0a7), 0.55f);
            juce::Graphics::ScopedSaveState s2(g); // vertical brush grain
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            for (float x = r.getX(); x < r.getRight(); x += 3.0f) g.drawVerticalLine((int)x, r.getY(), r.getBottom());
            g.setColour(Colour(0xff3a5f9e).withAlpha(0.75f));
            g.fillRect(r.getX(), r.getY() + 9.0f, r.getWidth(), 2.0f);
            g.fillRect(r.getX(), r.getBottom() - 11.0f, r.getWidth(), 2.0f);
            break;
        }
        case 3: // Cali Lead -- black anodised + top rule
            fill(Colour(0xff17181b), Colour(0xff0c0d0f));
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.fillRect(r.getX() + 14, r.getY() + 14, r.getWidth() - 28, 1.0f);
            break;
        case 4: // Brit 800 -- brushed gold
            fill3(Colour(0xffd3ad58), Colour(0xffb28c3c), Colour(0xff8f6d26), 0.55f);
            g.setColour(juce::Colours::black.withAlpha(0.33f));
            g.fillRect(r.getX(), r.getBottom() - 20.0f, r.getWidth(), 20.0f);
            break;
        case 5: // Brit 45 -- warm plexi gold (glossy)
            fill3(Colour(0xffe2bd6b), Colour(0xffc49c45), Colour(0xffa67e2e), 0.5f);
            break;
        case 6: // Brit Major -- bronze
            fill3(Colour(0xff8f6f2c), Colour(0xff6b511c), Colour(0xff4e3a12), 0.55f);
            break;
        case 7: // Solo 100 -- jet black + white/violet rails
            fill(Colour(0xff101013), Colour(0xff070709));
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.fillRect(r.getX() + 14, r.getY() + 13, r.getWidth() - 28, 1.0f);
            g.setColour(Colour(0xff9a6fd0).withAlpha(0.7f));
            g.fillRect(r.getX() + 14, r.getBottom() - 14, r.getWidth() - 28, 1.0f);
            break;
        case 8: // Red Star -- olive steel + scanline + star
        {
            fill3(Colour(0xff4c4f3c), Colour(0xff34372a), Colour(0xff2b2d22), 0.6f);
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            for (float y = r.getY(); y < r.getBottom(); y += 4.0f) g.drawHorizontalLine((int)y, r.getX(), r.getRight());
            break;
        }
        case 10: // Solid Clean -- slate SS + silver top / orange base rail
            fill3(Colour(0xff2b2e33), Colour(0xff1c1f23), Colour(0xff17191d), 0.6f);
            {
                juce::ColourGradient tg(Colour(0xff7c828a), r.getX(), r.getY(),
                                        Colour(0xff4a4f56), r.getX(), r.getY() + 7.0f, false);
                g.setGradientFill(tg);
                g.fillRect(r.getX(), r.getY(), r.getWidth(), 7.0f);
            }
            g.setColour(Colour(0xffff7a1a));
            g.fillRect(r.getX(), r.getBottom() - 4.0f, r.getWidth(), 4.0f);
            break;
        case 11: // Top Boost -- Vox AC30 panel (#A05244) + diamond lattice hint
        {
            fill3(Colour(0xffb96a5b), Colour(0xffa05244), Colour(0xff713a2e), 0.55f);
            juce::Graphics::ScopedSaveState s3(g);
            g.reduceClipRegion(juce::Rectangle<int>((int)r.getX(), (int)(r.getBottom() - 16),
                                                    (int)r.getWidth(), 16));
            g.setColour(juce::Colours::black.withAlpha(0.26f));
            for (float x = r.getX() - 16; x < r.getRight(); x += 7.0f)
            {
                g.drawLine(x, r.getBottom() - 16, x + 16, r.getBottom(), 1.0f);
                g.drawLine(x, r.getBottom(), x + 16, r.getBottom() - 16, 1.0f);
            }
            break;
        }
        case 12: // Graphic 72 -- orange tolex + cream pictogram panel
            fill(Colour(0xffcf6820), Colour(0xffb34e12));
            {
                auto inset = r.reduced(11.0f);
                juce::ColourGradient cg(Colour(0xfff2ead2), inset.getX(), inset.getY(),
                                        Colour(0xffe4d9b8), inset.getX(), inset.getBottom(), false);
                dither::fillRoundedRectangle(g, cg, inset, 6.0f);
                g.setColour(Colour(0xffb8a87c));
                g.drawRoundedRectangle(inset, 6.0f, 1.0f);
            }
            break;
        default:
            fill(Colour(0xff22262d), Colour(0xff17191d));
            break;
        }

        // ---- gloss overlay (white top -> black bottom), on every face ----
        {
            juce::ColourGradient gl(juce::Colours::white.withAlpha(0.09f), r.getX(), r.getY(),
                                    juce::Colours::black.withAlpha(0.18f), r.getX(), r.getBottom(), false);
            gl.addColour(0.30, juce::Colours::white.withAlpha(0.0f));
            dither::fillRect(g, gl, r);
        }

        // (corner screws removed per design direction)

        // ---- model-specific face art ----
        if (model == 8) // Red Star: red star + vertical plate
        {
            g.setColour(Colour(0xffc8322a));
            g.setFont(fonts::archivo(30.0f, fonts::Bold));
            g.drawText(juce::String::fromUTF8("\xE2\x9C\xB6"),
                       juce::Rectangle<int>((int)r.getX() + 12, (int)r.getCentreY() - 18, 30, 36),
                       juce::Justification::centred);
        }
        if (model == 6) // Brit Major: "200" watermark
        {
            g.setColour(juce::Colours::black.withAlpha(0.22f));
            g.setFont(fonts::archivo(22.0f, fonts::ExtraBold, 0.05f));
            g.drawText("200", juce::Rectangle<int>((int)r.getRight() - 96, (int)r.getBottom() - 34, 60, 28),
                       juce::Justification::centredRight);
        }

        // ---- brand-free plate (model script in the amp's typography) ----
        {
            // position rectangle + text per model
            juce::Rectangle<int> pr;
            juce::Colour pc = sp.label;
            juce::Font pf = fonts::archivo(9.0f, fonts::Bold, 0.2f);
            juce::String txt;
            auto j = juce::Justification::centredLeft;
            switch (model)
            {
            case 0:  txt = juce::String::fromUTF8("TWEED \xC2\xB7 FIFTY NINE"); pr = {(int)r.getX()+16,(int)r.getY()+6,220,14};
                     pc = Colour(0xff3f2a10); pf = fonts::archivo(8.0f, fonts::ExtraBold, 0.22f); break;
            case 9:  txt = juce::String::fromUTF8("CLASSIC \xC2\xB7 TWENTY"); pr = {(int)r.getX()+16,(int)r.getY()+6,220,14};
                     pc = Colour(0xff3f2a10); pf = fonts::archivo(8.0f, fonts::ExtraBold, 0.22f); break;
            case 13: txt = juce::String::fromUTF8("TWEED \xC2\xB7 FIFTY SEVEN"); pr = {(int)r.getX()+16,(int)r.getY()+6,220,14};
                     pc = Colour(0xff3f2a10); pf = fonts::archivo(8.0f, fonts::ExtraBold, 0.22f); break;
            case 1:  txt = "black sixty-five"; pr = {(int)r.getRight()-190,(int)r.getBottom()-24,150,16};
                     pc = Colour(0xffdfe3e8); pf = fonts::archivo(11.0f, fonts::SemiBold, 0.04f).italicised();
                     j = juce::Justification::centredRight; break;
            case 2:  txt = "silver sixty-nine"; pr = {(int)r.getRight()-210,(int)r.getBottom()-27,180,20};
                     // reference uses Dancing Script ~16px; fallback = Archivo bold-italic.
                     pc = Colour(0xff2b4a8b); pf = fonts::archivo(16.0f, fonts::Bold, 0.0f).italicised();
                     j = juce::Justification::centredRight; break;
            case 3:  txt = juce::String::fromUTF8("CALI \xC2\xB7 LEAD"); pr = {(int)r.getX()+18,(int)r.getBottom()-22,160,14};
                     pc = Colour(0xffcfcfcb); pf = fonts::archivo(9.0f, fonts::ExtraBold, 0.3f); break;
            case 4:  txt = juce::String::fromUTF8("brit \xC2\xB7 eight hundred"); pr = {(int)r.getX()+18,(int)r.getY()+8,220,14};
                     pc = Colour(0xff2c2005); pf = fonts::archivo(10.0f, fonts::ExtraBold, 0.1f).italicised(); break;
            case 5:  txt = "brit 45"; pr = {(int)r.getX()+18,(int)r.getY()+8,160,16};
                     pc = Colour(0xff241a05); pf = fonts::archivo(11.0f, fonts::Bold, 0.04f).italicised(); break;
            case 6:  txt = juce::String::fromUTF8("brit \xC2\xB7 major"); pr = {(int)r.getX()+18,(int)r.getY()+8,200,14};
                     pc = Colour(0xfff2ead2); pf = fonts::archivo(10.0f, fonts::ExtraBold, 0.12f).italicised(); break;
            case 7:  txt = juce::String::fromUTF8("SOLO \xC2\xB7 ONE HUNDRED"); pr = {(int)r.getX()+18,(int)r.getBottom()-26,220,14};
                     pc = Colour(0xff8f8f96); pf = fonts::archivo(9.0f, fonts::ExtraBold, 0.32f); break;
            case 10: txt = juce::String::fromUTF8("SOLID \xC2\xB7 CLEAN"); pr = {(int)r.getRight()-200,(int)r.getY()+12,160,14};
                     pc = Colour(0xffff7a1a); pf = fonts::archivo(9.0f, fonts::ExtraBold, 0.26f);
                     j = juce::Justification::centredRight; break;
            case 11: txt = "top boost"; pr = {(int)r.getX()+18,(int)r.getY()+8,180,16};
                     pc = Colour(0xfff6efe2); pf = fonts::archivo(12.0f, fonts::ExtraBold, 0.06f).italicised(); break;
            case 12: txt = juce::String::fromUTF8("GRAPHIC \xE2\x80\x99" "72"); pr = {(int)r.getX()+26,(int)r.getY()+16,180,14};
                     pc = Colour(0xff241f10); pf = fonts::archivo(9.0f, fonts::ExtraBold, 0.2f); break;
            case 8:  txt = "RED STAR"; break; // drawn vertically below
            default: break;
            }
            if (model == 8)
            {
                juce::Graphics::ScopedSaveState sv(g);
                g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi,
                                                               r.getX() + 52, r.getCentreY()));
                g.setColour(Colour(0xffc8b98e));
                g.setFont(fonts::archivo(9.0f, fonts::ExtraBold, 0.24f));
                g.drawText("RED STAR", juce::Rectangle<int>((int)r.getX() + 52 - 40, (int)r.getCentreY() - 7, 80, 14),
                           juce::Justification::centred);
            }
            else if (model == 5) // Brit 45: "brit" dark 700, "45" heavier + red
            {
                const int x0 = (int)r.getX() + 18, y0 = (int)r.getY() + 8;
                auto f1 = fonts::archivo(11.0f, fonts::Bold, 0.04f).italicised();
                auto f2 = fonts::archivo(11.0f, fonts::ExtraBold, 0.04f).italicised();
                g.setFont(f1);
                g.setColour(Colour(0xff241a05));
                g.drawText("brit ", juce::Rectangle<int>(x0, y0, 60, 16), juce::Justification::centredLeft);
                const int w1 = (int)std::ceil(juce::GlyphArrangement::getStringWidth(f1, "brit "));
                g.setFont(f2);
                g.setColour(Colour(0xffa3241a));
                g.drawText("45", juce::Rectangle<int>(x0 + w1, y0, 40, 16), juce::Justification::centredLeft);
            }
            else if (txt.isNotEmpty())
            {
                g.setColour(pc);
                g.setFont(pf);
                g.drawText(txt, pr, j);
            }
        }

        // ---- jewel pilot lamp: 25px from the right edge, vertically centred.
        // Flat translucent halo (r13) + lens (r8.5) + white highlight (r2.6), per
        // the reference. Lit when the tone stack is On, otherwise dim.
        {
            const float jcx = r.getRight() - 25.0f, jcy = r.getCentreY();
            auto at = [&](float rad) { return juce::Rectangle<float>(rad * 2.0f, rad * 2.0f).withCentre({jcx, jcy}); };
            if (on)
            {
                // soft LED bloom (mock: box-shadow 0 0 14px jglow) — a real glow,
                // not the flat disc the SVG uses to fake it.
                fx::glowEllipse(g, at(8.5f), sp.jewel, 15, 0.75f, 7, 0.6f);
                g.setColour(sp.jewel);
                g.fillEllipse(at(8.5f));
                // inner shadow (inset 0 0 4px rgba(0,0,0,.5)) + dark bezel outline.
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.drawEllipse(at(8.5f).reduced(1.2f), 2.4f);
                g.setColour(juce::Colours::black.withAlpha(0.55f));
                g.drawEllipse(at(8.5f), 1.4f);
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.fillEllipse(juce::Rectangle<float>(5.2f, 5.2f).withCentre({jcx - 2.5f, jcy - 2.7f}));
            }
            else
            {
                g.setColour(sp.jewel.withMultipliedSaturation(0.4f).darker(0.7f));
                g.fillEllipse(at(8.5f));
                g.setColour(juce::Colours::black.withAlpha(0.5f));
                g.drawEllipse(at(8.5f), 2.0f);
            }
        }
    } // end clipped region

    // ---- OFF: darken the whole face ~28% (mock: brightness .72). A lighter wash
    // than before so the chrome/panel reads as dimmed, not see-through. ----
    if (!on)
    {
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.fillPath(clip);
    }

    // ---- CAL: amber dashed inset ring + "CAPTURE EQ" tag ----
    if (cal)
    {
        juce::Path ring, dashed;
        ring.addRoundedRectangle(r.reduced(3.0f), 8.0f);
        const float dl[2] = {5.0f, 4.0f};
        juce::PathStrokeType(1.5f).createDashedStroke(dashed, ring, dl, 2);
        g.setColour(colors::accent.withAlpha(0.85f));
        g.fillPath(dashed);

        auto tag = juce::Rectangle<float>(78.0f, 15.0f).withCentre({r.getCentreX(), r.getY() + 9.0f});
        g.setColour(colors::accent);
        g.fillRoundedRectangle(tag, 7.0f);
        g.setColour(Colour(0xff1a1a1a));
        g.setFont(fonts::archivo(8.5f, fonts::ExtraBold, 0.2f));
        g.drawText("CAPTURE EQ", tag.toNearestInt(), juce::Justification::centred);
    }
}

// ---------------------------------------------------------------------------
// Graphic '72 pictograms drawn above its knobs (0 = treble, 1 = bass).
// ---------------------------------------------------------------------------
inline void pictogram(juce::Graphics &g, juce::Rectangle<int> boxI, int which, juce::Colour col)
{
    auto b = boxI.toFloat();
    const float u = juce::jmin(b.getWidth(), b.getHeight()) / 15.0f;
    const float ox = b.getCentreX() - 11.0f * u, oy = b.getCentreY() - 7.5f * u;
    auto P = [&](float x, float y) { return juce::Point<float>(ox + x * u, oy + y * u); };

    // speaker glyph (shared)
    juce::Path spk;
    spk.startNewSubPath(P(2, 9.5f)); spk.lineTo(P(6, 9.5f)); spk.lineTo(P(10, 13));
    spk.lineTo(P(10, 2)); spk.lineTo(P(6, 5.5f)); spk.lineTo(P(2, 5.5f)); spk.closeSubPath();
    g.setColour(col);
    g.fillPath(spk);

    g.setColour(col);
    juce::Path waves;
    auto qcurve = [&](float sx, float sy, float cx, float cy, float ex, float ey) {
        waves.startNewSubPath(P(sx, sy));
        waves.quadraticTo(P(cx, cy), P(ex, ey));
    };
    float sw;
    if (which == 0) // treble: three vertical arcs bulging right (rising)
    {
        qcurve(13.0f, 5.0f, 14.6f, 7.5f, 13.0f, 10.0f);
        qcurve(16.0f, 3.5f, 18.4f, 7.5f, 16.0f, 11.5f);
        qcurve(19.0f, 2.0f, 22.2f, 7.5f, 19.0f, 13.0f);
        sw = 1.3f;
    }
    else // bass: one arc bulging up
    {
        qcurve(12.5f, 7.5f, 16.5f, -0.5f, 20.5f, 7.5f);
        sw = 1.6f;
    }
    g.strokePath(waves, juce::PathStrokeType(sw * u, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

// ===========================================================================
// AmpKnobLnF -- paints the tone knobs in one of five amp styles. Configured
// per model (configure()) and assigned to the tone LabeledKnob sliders. The
// rotation is -135..+135 for the 0..10 value (noon = 5), matching real amps.
// ===========================================================================
class AmpKnobLnF : public juce::LookAndFeel_V4
{
public:
    void configure(int model)
    {
        const auto &sp = spec(model);
        mPainter = sp.painter;
        mVar     = sp.knobVar;
        mNumbers = sp.numbers;
    }

    void drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h,
                          float pos, float, float, juce::Slider &s) override
    {
        using juce::Colour;
        using Stops = std::vector<std::pair<float, juce::Colour>>;
        auto b = juce::Rectangle<int>(x, y, w, h).toFloat();
        // Reference knob radius is 27 in a 54px dial; inset to 0.72 of the slider's
        // half-extent leaves headroom for the chicken-head beak (it protrudes ~0.26R
        // above the dome, and JUCE clips painting to the slider bounds).
        const float R = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f * 0.72f;
        const auto c = b.getCentre();
        const float u = R / 27.0f;                 // reference knob radius = 27
        const bool en = s.isEnabled();
        const float ga = en ? 1.0f : 0.5f;
        const float ang = juce::degreesToRadians(-135.0f + pos * 270.0f);

        // A domed disc of radius `rad` (px). Radial highlight at cx0.40/cy0.32,
        // gradient radius 0.75 of the box, stops per the reference gradient defs.
        auto dome = [&](float rad, const Stops &stops) {
            auto dd = juce::Rectangle<float>(rad * 2.0f, rad * 2.0f).withCentre(c);
            const float hx = c.x - 0.20f * rad, hy = c.y - 0.36f * rad;
            // Opaque floor first: the knob is a solid object and must never let the
            // panel show through it. When disabled (ga<1) the gradient sits translucent
            // over this floor, which reads as a DARKENED knob, not a transparent one.
            g.setColour(stops.back().second);
            g.fillEllipse(dd);
            juce::ColourGradient cg(stops.front().second.withMultipliedAlpha(ga), hx, hy,
                                    stops.back().second.withMultipliedAlpha(ga),
                                    hx + 1.5f * rad, hy, true);
            for (size_t i = 1; i + 1 < stops.size(); ++i)
                cg.addColour(stops[i].first, stops[i].second.withMultipliedAlpha(ga));
            dither::fillEllipse(g, cg, dd);
            // inset top rim highlight (mock: inset 0 1px 1px rgba(255,255,255,.18)).
            g.setColour(juce::Colours::white.withAlpha(0.11f * ga));
            g.drawEllipse(dd.reduced(0.6f), 1.0f);
            return dd;
        };
        // A pointer bar: rect centred on x, from y=c.y+yTop down `len`, width `wdt`,
        // corner `rx` (all in reference px), rotated by the value angle about c.
        auto bar = [&](float yTop, float len, float wdt, float rx, Colour col) {
            juce::Path p;
            p.addRoundedRectangle(c.x - wdt * 0.5f * u, c.y + yTop * u, wdt * u, len * u, rx * u);
            p.applyTransform(juce::AffineTransform::rotation(ang, c.x, c.y));
            g.setColour(col.withMultipliedAlpha(ga));
            g.fillPath(p);
        };

        // Soft drop shadow under the knob so it sits on the panel with depth
        // (mock: box-shadow 0 3px 7px rgba(0,0,0,.55)).
        {
            juce::Path sh;
            sh.addEllipse(juce::Rectangle<float>(2.0f * R, 2.0f * R).withCentre(c));
            juce::DropShadow(juce::Colours::black.withAlpha(en ? 0.5f : 0.28f),
                             juce::roundToInt(7.0f * u),
                             juce::Point<int>(0, juce::roundToInt(3.0f * u))).drawForPath(g, sh);
        }

        switch (mPainter)
        {
        case Chicken:
        {
            const bool cream = (mVar == 1), wline = (mVar == 2);
            dome(R, cream ? Stops{{0, Colour(0xfff7efd8)}, {0.6f, Colour(0xffd9cba4)}, {1, Colour(0xffa8925e)}}
                          : Stops{{0, Colour(0xff454545)}, {0.55f, Colour(0xff181818)}, {1, Colour(0xff000000)}});
            // solid 5-sided wedge beak, points (rel centre) exactly per reference.
            juce::Path wedge;
            wedge.startNewSubPath(c.x, c.y - 34 * u);
            wedge.lineTo(c.x + 6.5f * u, c.y - 24 * u);
            wedge.lineTo(c.x + 3.8f * u, c.y + 2 * u);
            wedge.lineTo(c.x - 3.8f * u, c.y + 2 * u);
            wedge.lineTo(c.x - 6.5f * u, c.y - 24 * u);
            wedge.closeSubPath();
            wedge.applyTransform(juce::AffineTransform::rotation(ang, c.x, c.y));
            if (cream)
            {
                // soft gradient fill + gentle self-coloured edge (no hard black line).
                juce::ColourGradient wg(Colour(0xfffbf4e0), c.x, c.y - R,
                                        Colour(0xffcbbd94), c.x, c.y + R * 0.4f, false);
                wg.multiplyOpacity(ga);
                g.setGradientFill(wg);
                g.fillPath(wedge);
                g.setColour(Colour(0xff9a8a5e).withAlpha(0.18f * ga));
                g.strokePath(wedge, juce::PathStrokeType(0.6f * u));
            }
            else
            {
                g.setColour(Colour(0xff1a1a1a).withMultipliedAlpha(ga));
                g.fillPath(wedge);
                g.setColour(juce::Colours::black.withAlpha(0.35f * ga));
                g.strokePath(wedge, juce::PathStrokeType(0.7f * u));
            }
            if (wline) bar(-31, 20, 2.0f, 1.0f, Colour(0xffe8e8e8));
            break;
        }
        case Skirt:
        {
            const bool si = (mVar == 1);
            const Stops sk = si ? Stops{{0, Colour(0xffeef0f2)}, {0.5f, Colour(0xffc9ced4)}, {0.78f, Colour(0xff8f959c)}, {1, Colour(0xff585d64)}}
                                : Stops{{0, Colour(0xfff3ecd6)}, {0.5f, Colour(0xffe6dcbe)}, {0.78f, Colour(0xffcfc39d)}, {1, Colour(0xffb3a578)}};
            dome(R, sk);
            // The 1..10 numbers + their ticks are printed on the knob and turn WITH
            // it; a fixed index at 12 o'clock reads the value. Number i sits at the
            // top exactly when the value is i (screen angle = (135-27i)deg + value angle).
            const Colour tk = si ? Colour(0xff111111) : Colour(0x55000000);
            const Colour nc = si ? Colour(0xffe8eaee) : Colour(0xff3a3323);
            g.setFont(fonts::archivo(6.5f * u, fonts::Bold));
            // Ten digits spanning the full travel: number 1 at the minimum (−135°),
            // 10 at the maximum (+135°), 30° apart — so the dial reads 1..10.
            for (int i = 1; i <= 10; ++i)
            {
                const float sa = juce::degreesToRadians(135.0f - 30.0f * (i - 1)) + ang;
                const float sx = std::sin(sa), sy = -std::cos(sa);
                g.setColour(tk.withMultipliedAlpha(ga));
                g.drawLine(c.x + 16 * u * sx, c.y + 16 * u * sy,
                           c.x + 22 * u * sx, c.y + 22 * u * sy, 1.0f * u);
                if (mNumbers)
                {
                    g.setColour(nc.withMultipliedAlpha(ga));
                    auto nb = juce::Rectangle<float>(11 * u, 9 * u).withCentre({c.x + 24 * u * sx, c.y + 24 * u * sy});
                    g.drawText(juce::String(i), nb, juce::Justification::centred);
                }
            }
            dome(10.0f * u, sk); // raised centre cap
            // fixed index triangle ABOVE the knob (points down at the top number);
            // coloured to contrast the panel (Black 65 = light index, Silver 69 = dark).
            juce::Path mk;
            mk.addTriangle(c.x - 3.0f * u, c.y - R - 5.0f * u,
                           c.x + 3.0f * u, c.y - R - 5.0f * u, c.x, c.y - R + 1.0f * u);
            g.setColour((si ? Colour(0xff26262a) : Colour(0xffe6e9ec)).withMultipliedAlpha(ga));
            g.fillPath(mk);
            break;
        }
        case TopHat:
        {
            const bool dk = (mVar == 1);
            dome(R, dk ? Stops{{0, Colour(0xff3d3d40)}, {0.55f, Colour(0xff1a1a1c)}, {1, Colour(0xff050505)}}
                       : Stops{{0, Colour(0xfff2d891)}, {0.52f, Colour(0xffc49b3f)}, {1, Colour(0xff8a6820)}});
            // ribbed rim: a dashed ring at r24.
            {
                juce::Path ring, dash;
                ring.addEllipse(juce::Rectangle<float>(48 * u, 48 * u).withCentre(c));
                const float dl[2] = {1.5f * u, 2.2f * u};
                juce::PathStrokeType(3.0f * u).createDashedStroke(dash, ring, dl, 2);
                g.setColour(juce::Colours::black.withAlpha(0.13f * ga));
                g.fillPath(dash);
            }
            dome(16.0f * u, dk ? Stops{{0, Colour(0xff4a4a4e)}, {1, Colour(0xff232326)}}
                               : Stops{{0, Colour(0xfff7e5ac)}, {1, Colour(0xffcfa848)}});
            bar(-23, 17, 2.5f, 1.2f, dk ? Colour(0xffe8c877) : Colour(0xff241a05));
            break;
        }
        case Pointer:
        {
            Stops st; Colour line;
            switch (mVar)
            {
            case 1:  st = {{0, Colour(0xff3f3f42)}, {0.6f, Colour(0xff151517)}, {1, Colour(0xff000000)}}; line = Colour(0xffe9e9e9); break;
            case 2:  st = {{0, Colour(0xff7a5638)}, {0.6f, Colour(0xff4a2f1a)}, {1, Colour(0xff241207)}}; line = Colour(0xfff0e2c4); break;
            default: st = {{0, Colour(0xfff6eed9)}, {0.6f, Colour(0xffdccfa8)}, {1, Colour(0xffb09a66)}}; line = Colour(0xff2c2415); break;
            }
            dome(R, st);
            bar(-24, 24, 3.0f, 1.4f, line); // full-length davies pointer
            break;
        }
        case Modern:
        default:
        {
            auto dd = dome(R, Stops{{0, Colour(0xff26292e)}, {0.65f, Colour(0xff141619)}, {1, Colour(0xff0a0b0d)}});
            g.setColour(Colour(0xff383e46).withMultipliedAlpha(ga));
            g.drawEllipse(dd, 1.0f);
            bar(-22, 14, 3.0f, 1.4f, Colour(0xffff7a1a));
            break;
        }
        }
    }

private:
    Painter mPainter = Chicken;
    int  mVar = 0;
    bool mNumbers = false;
};

// ===========================================================================
// GraphicEqLnF -- the Cali Lead 5-band graphic slider: a 5px black slot track
// with a faint white outline + centre detent, and a cream scribed cap (19x11)
// that rides the value. Matches 03_cali.svg.
// ===========================================================================
class GraphicEqLnF : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics &g, int x, int y, int w, int h,
                          float sliderPos, float, float,
                          juce::Slider::SliderStyle, juce::Slider &s) override
    {
        const float ga = s.isEnabled() ? 1.0f : 0.5f;
        const float cx = x + w * 0.5f;
        // slot track (5px), full height, black with a hairline white outline.
        auto track = juce::Rectangle<float>(5.0f, (float)h).withCentre({cx, y + h * 0.5f});
        g.setColour(juce::Colour(0xff000000).withMultipliedAlpha(ga));
        g.fillRoundedRectangle(track, 2.5f);
        g.setColour(juce::Colours::white.withAlpha(0.10f * ga));
        g.drawRoundedRectangle(track, 2.5f, 1.0f);
        // centre detent line.
        g.setColour(juce::Colours::white.withAlpha(0.28f * ga));
        g.fillRect(juce::Rectangle<float>(cx - 5.5f, y + h * 0.5f - 0.5f, 11.0f, 1.0f));
        // cream cap + scribed groove at the value position.
        auto cap = juce::Rectangle<float>(19.0f, 11.0f).withCentre({cx, sliderPos});
        g.setColour(juce::Colour(0xffede6d2).withMultipliedAlpha(ga));
        g.fillRoundedRectangle(cap, 2.5f);
        g.setColour(juce::Colour(0xff8a8371).withMultipliedAlpha(ga));
        g.drawRoundedRectangle(cap, 2.5f, 0.6f);
        g.setColour(juce::Colour(0xff3a3428).withMultipliedAlpha(ga));
        g.fillRect(juce::Rectangle<float>(cap.getCentreX() - 7.5f, cap.getCentreY() - 0.5f, 15.0f, 1.0f));
    }
};

} // namespace nam_rig::ui::ampface
