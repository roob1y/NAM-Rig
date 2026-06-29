#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include <cmath>

// Bundled UI typefaces (Archivo + JetBrains Mono, SIL OFL 1.1). Guarded so the
// header still compiles in non-plugin targets that don't link the binary data.
#if __has_include("BinaryData.h")
#include "BinaryData.h"
#define NAM_RIG_HAS_FONTS 1
#endif

namespace nam_rig::ui
{

// Palette for the whole editor. Dark flat: one amber accent, no textures.
// Values lifted from the design handoff (NAM Rig.dc.html design tokens).
namespace colors
{
    inline const juce::Colour bg          {0xff14161a}; // window frame
    inline const juce::Colour deepBg      {0xff08090b}; // backdrop
    inline const juce::Colour headerTop   {0xff171a1f}; // header gradient top
    inline const juce::Colour headerBot   {0xff131519}; // header gradient bottom
    inline const juce::Colour panel       {0xff1d2027}; // block panel body
    inline const juce::Colour tile        {0xff22262d}; // strip tile / control fill
    inline const juce::Colour tileSel     {0xff2b3039}; // selected strip tile
    inline const juce::Colour inset       {0xff1b1e24}; // inset field fill
    inline const juce::Colour outline     {0xff343a43}; // panel + control border
    inline const juce::Colour divider     {0xff262a31}; // panel header rule / subtle
    inline const juce::Colour cardBorder  {0xff2c313a}; // display-well / card border
    inline const juce::Colour wellTop     {0xff14171f}; // display-well gradient top
    inline const juce::Colour wellBot     {0xff0f121a}; // display-well gradient bottom

    inline const juce::Colour text        {0xffd8dbe0}; // values / body
    inline const juce::Colour textBright  {0xffeef0f3}; // wordmark / big values
    inline const juce::Colour text2       {0xff9aa0aa}; // secondary
    inline const juce::Colour textDim     {0xff8b919b}; // labels
    inline const juce::Colour caption     {0xff6c727c}; // captions / axis
    inline const juce::Colour captionDim  {0xff5a616b}; // faint ticks

    inline const juce::Colour accent      {0xffffb13d}; // amber: arcs / active fills
    inline const juce::Colour accentDim   {0xff7a5a28};
    inline const juce::Colour titleAccent {0xffeb9b43}; // panel header titles
    inline const juce::Colour ledOff      {0xff3a3f47};
    inline const juce::Colour track       {0xff2e333b}; // knob/slider/meter track

    inline const juce::Colour meterLo     {0xff5fcf6e}; // green (open/loaded LED)
    inline const juce::Colour meterMid    {0xffffb13d};
    inline const juce::Colour meterHi     {0xffe85d4a}; // red (peak)
    inline const juce::Colour green       {0xff5fcf6e};
    inline const juce::Colour red         {0xffe85d4a};

    inline const juce::Colour scopeBg     {0xff13161f}; // mod-lane scope canvas
    inline const juce::Colour post        {0xffc79be6}; // post-lane (OUT) accent (violet)

    // Per-slot accent for the 3 front mod slots: ties each lane's scope trace to
    // its node on the Cartesian blend pad. slot 0 = amber, 1 = teal, 2 = violet.
    inline juce::Colour laneColour(int slot)
    {
        switch (slot)
        {
        case 0: return juce::Colour(0xffeb9b43);
        case 1: return juce::Colour(0xff45c4b0);
        case 2: return juce::Colour(0xff9a6fd0);
        default: return post; // post lane / fallback
        }
    }

    // Drive-pedal accent / tint pairs (per model family). first = bright accent
    // (knob arcs, footswitch), second = enclosure tint, third = jewel LED colour
    // (defaults to the accent when not given, so the LED tracks the accent unless a
    // model deliberately overrides it -- e.g. Violet Ram is chrome with a violet LED).
    struct AccentPair
    {
        juce::Colour accent, tint, led;
        AccentPair(juce::Colour a = {}, juce::Colour t = {}, juce::Colour l = {})
            : accent(a), tint(t), led(l.isTransparent() ? a : l) {}
    };
    inline AccentPair driveAccent(int kind) // 0 boost,1 od,2 dist,3 fuzz,4 fuzz-alt
    {
        switch (kind)
        {
        case 0: return {juce::Colour(0xfff0d68a), juce::Colour(0xffc79a3e)}; // EP Boost
        case 1: return {juce::Colour(0xff3fd45f), juce::Colour(0xff3f9d57)}; // Green Drive
        case 2: return {juce::Colour(0xffcfd5dd), juce::Colour(0xff3a4049)}; // Black Rodent
        case 3: return {juce::Colour(0xffcaa6f0), juce::Colour(0xff8a5cc6)}; // Round Fuzz
        case 4: return {juce::Colour(0xffff7d6b), juce::Colour(0xffa23d31)}; // Range '65
        default: return {accent, accentDim};
        }
    }

    // Per-MODEL accent / tint / LED, keyed by (drive type, model index). Each real
    // pedal gets its own livery. The v1 stand-ins are gone, so indices are compact:
    // Boost 0 Range '65 / 1 EP Boost; OD 0 Green Drive / 1 Super Drive / 2 Gold Horse
    // / 3 Breaker Drive; Dist 0 Black Rodent; Fuzz 0 Round Fuzz / 1 Violet Ram.
    // type: 1 boost,2 od,3 dist,4 fuzz.
    inline AccentPair driveModelAccent(int type, int model)
    {
        using C = juce::Colour;
        switch (type)
        {
        case 1: // Boost
            return (model == 0)
                       ? AccentPair{C(0xffff7d6b), C(0xffa23d31)}  // Range '65 (warm red)
                       : AccentPair{C(0xfff0d68a), C(0xffc79a3e)}; // EP Boost  (gold)
        case 2: // Overdrive
            switch (model)
            {
            case 1:  return {C(0xfff2c230), C(0xffb8902a)};        // Super Drive   (Boss SD-1, yellow)
            case 2:  return {C(0xffe0b347), C(0xff8a6622)};        // Gold Horse    (Klon, gold)
            case 3:  return {C(0xff5b9bd5), C(0xff2f5d8a)};        // Breaker Drive (Bluesbreaker, blue)
            default: return {C(0xff3fd45f), C(0xff3f9d57)};        // Green Drive   (0)
            }
        case 3: // Distortion: Black Rodent
            return {C(0xffcfd5dd), C(0xff3a4049)};
        case 4: // Fuzz
            return (model == 1)
                       ? AccentPair{C(0xffd7dde2), C(0xff5a626b), C(0xffb06bd8)} // Violet Ram (Big Muff): chrome body, VIOLET LED
                       : AccentPair{C(0xffff5a4d), C(0xffa83229)};               // Round Fuzz (Fuzz Face red)
        default: return {accent, accentDim};
        }
    }
}

// ---------------------------------------------------------------------------
// Bundled fonts. Archivo for all UI text, JetBrains Mono for numeric readouts.
// ---------------------------------------------------------------------------
namespace fonts
{
    enum Weight { Regular = 0, Medium, SemiBold, Bold, ExtraBold };

#if NAM_RIG_HAS_FONTS
    inline juce::Typeface::Ptr archivoFace(Weight w)
    {
        static std::array<juce::Typeface::Ptr, 5> cache;
        auto &slot = cache[(size_t)w];
        if (slot == nullptr)
        {
            switch (w)
            {
            case Regular:   slot = juce::Typeface::createSystemTypefaceFor(BinaryData::ArchivoRegular_ttf,   (size_t)BinaryData::ArchivoRegular_ttfSize); break;
            case Medium:    slot = juce::Typeface::createSystemTypefaceFor(BinaryData::ArchivoMedium_ttf,    (size_t)BinaryData::ArchivoMedium_ttfSize); break;
            case SemiBold:  slot = juce::Typeface::createSystemTypefaceFor(BinaryData::ArchivoSemiBold_ttf,  (size_t)BinaryData::ArchivoSemiBold_ttfSize); break;
            case Bold:      slot = juce::Typeface::createSystemTypefaceFor(BinaryData::ArchivoBold_ttf,      (size_t)BinaryData::ArchivoBold_ttfSize); break;
            case ExtraBold: slot = juce::Typeface::createSystemTypefaceFor(BinaryData::ArchivoExtraBold_ttf, (size_t)BinaryData::ArchivoExtraBold_ttfSize); break;
            }
        }
        return slot;
    }

    inline juce::Typeface::Ptr monoFace(Weight w)
    {
        const int idx = w >= SemiBold ? 2 : (int)w; // mono has Regular/Medium/SemiBold
        static std::array<juce::Typeface::Ptr, 3> cache;
        auto &slot = cache[(size_t)idx];
        if (slot == nullptr)
        {
            switch (idx)
            {
            case 0: slot = juce::Typeface::createSystemTypefaceFor(BinaryData::JetBrainsMonoRegular_ttf,  (size_t)BinaryData::JetBrainsMonoRegular_ttfSize); break;
            case 1: slot = juce::Typeface::createSystemTypefaceFor(BinaryData::JetBrainsMonoMedium_ttf,   (size_t)BinaryData::JetBrainsMonoMedium_ttfSize); break;
            default:slot = juce::Typeface::createSystemTypefaceFor(BinaryData::JetBrainsMonoSemiBold_ttf, (size_t)BinaryData::JetBrainsMonoSemiBold_ttfSize); break;
            }
        }
        return slot;
    }
#else
    inline juce::Typeface::Ptr archivoFace(Weight) { return {}; }
    inline juce::Typeface::Ptr monoFace(Weight) { return {}; }
#endif

    // tracking = extra letter-spacing as a fraction of height (≈ CSS em).
    inline juce::Font archivo(float height, Weight w = Regular, float tracking = 0.0f)
    {
        auto opts = juce::FontOptions{}.withHeight(height);
#if NAM_RIG_HAS_FONTS
        opts = opts.withTypeface(archivoFace(w));
#else
        if (w >= Bold) opts = opts.withStyle("Bold");
#endif
        juce::Font f(opts);
        if (tracking != 0.0f) f.setExtraKerningFactor(tracking);
        return f;
    }

    inline juce::Font mono(float height, Weight w = Regular, float tracking = 0.0f)
    {
        auto opts = juce::FontOptions{}.withHeight(height);
#if NAM_RIG_HAS_FONTS
        opts = opts.withTypeface(monoFace(w));
#else
        opts = opts.withName(juce::Font::getDefaultMonospacedFontName());
#endif
        juce::Font f(opts);
        if (tracking != 0.0f) f.setExtraKerningFactor(tracking);
        return f;
    }
}

// ---------------------------------------------------------------------------
// Gradient dithering. JUCE's software renderer interpolates ColourGradients in
// 8-bit with no dithering, so subtle dark gradients (wells, knob caps, the
// header) show visible stair-step banding. Instead of a noise overlay (which
// reads as grain), we render the gradient ourselves in floating point and add
// an ordered (Bayer) +/-0.5 LSB dither BEFORE quantising to 8-bit -- true
// dithering, so the bands dissolve with no perceptible grain. (Random noise
// dither, as used on the reverb field background, is invisible on near-black
// but reads as faint fuzz on lighter/saturated fills; the ordered pattern does
// not.) Renders are cached
// by geometry + colour, so repainting panels re-blit a prebuilt image instead
// of looping over pixels every frame. Use the fill* helpers in place of
// g.setGradientFill(grad) + g.fill<shape>().
// ---------------------------------------------------------------------------
namespace dither
{
    // Float colour of a gradient's stops at position t in [0,1].
    inline void sampleStops(const juce::ColourGradient &grad, float t,
                            float &r, float &g, float &b, float &a)
    {
        const int n = grad.getNumColours();
        if (n <= 0) { r = g = b = a = 0.0f; return; }
        auto set = [&](juce::Colour c) { r = c.getFloatRed(); g = c.getFloatGreen();
                                         b = c.getFloatBlue(); a = c.getFloatAlpha(); };
        if (t <= (float) grad.getColourPosition(0)) { set(grad.getColour(0)); return; }
        for (int i = 1; i < n; ++i)
        {
            const float p1 = (float) grad.getColourPosition(i);
            if (t <= p1)
            {
                const float p0 = (float) grad.getColourPosition(i - 1);
                const float f  = p1 > p0 ? (t - p0) / (p1 - p0) : 0.0f;
                const auto c0 = grad.getColour(i - 1), c1 = grad.getColour(i);
                r = c0.getFloatRed()   + (c1.getFloatRed()   - c0.getFloatRed())   * f;
                g = c0.getFloatGreen() + (c1.getFloatGreen() - c0.getFloatGreen()) * f;
                b = c0.getFloatBlue()  + (c1.getFloatBlue()  - c0.getFloatBlue())  * f;
                a = c0.getFloatAlpha() + (c1.getFloatAlpha() - c0.getFloatAlpha()) * f;
                return;
            }
        }
        set(grad.getColour(n - 1));
    }

    // Render `grad` into a dithered ARGB image covering `bounds` (pixel (0,0) ->
    // bounds.getTopLeft()). Cached by geometry+colour; juce::Image is a cheap
    // ref-counted handle so returning by value is fine.
    inline juce::Image image(juce::ColourGradient grad, juce::Rectangle<float> bounds)
    {
        const int w = juce::jmax(1, (int) std::ceil(bounds.getWidth()));
        const int h = juce::jmax(1, (int) std::ceil(bounds.getHeight()));

        // Work in local space so the cache key is position-independent.
        grad.point1 -= bounds.getTopLeft();
        grad.point2 -= bounds.getTopLeft();

        struct Entry { int w, h, n; bool radial; float p1x, p1y, p2x, p2y;
                       float sp[6]; juce::uint32 sc[6]; juce::Image img; };
        static std::vector<Entry> cache;

        const int ns = juce::jmin(6, grad.getNumColours());
        const bool radial = grad.isRadial;
        auto matches = [&](const Entry &e)
        {
            if (e.w != w || e.h != h || e.n != ns || e.radial != radial) return false;
            if (e.p1x != grad.point1.x || e.p1y != grad.point1.y
                || e.p2x != grad.point2.x || e.p2y != grad.point2.y) return false;
            for (int i = 0; i < ns; ++i)
                if (e.sp[i] != (float) grad.getColourPosition(i)
                    || e.sc[i] != grad.getColour(i).getARGB()) return false;
            return true;
        };
        for (auto &e : cache)
            if (matches(e)) return e.img;

        juce::Image img(juce::Image::ARGB, w, h, true);
        {
            juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
            const float ax = grad.point2.x - grad.point1.x;
            const float ay = grad.point2.y - grad.point1.y;
            const float len2 = juce::jmax(1.0e-6f, ax * ax + ay * ay);
            const float radius = std::sqrt(len2);
            // Ordered (Bayer 8x8) dither: an evenly-dispersed +/-0.5 LSB offset.
            // Unlike random noise it has no clumps, so it dissolves banding with
            // no perceptible grain (random dither reads as fuzz on mid-tones).
            static constexpr int bayer8[8][8] = {
                {  0, 32,  8, 40,  2, 34, 10, 42 },
                { 48, 16, 56, 24, 50, 18, 58, 26 },
                { 12, 44,  4, 36, 14, 46,  6, 38 },
                { 60, 28, 52, 20, 62, 30, 54, 22 },
                {  3, 35, 11, 43,  1, 33,  9, 41 },
                { 51, 19, 59, 27, 49, 17, 57, 25 },
                { 15, 47,  7, 39, 13, 45,  5, 37 },
                { 63, 31, 55, 23, 61, 29, 53, 21 }};
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const float px = (float) x + 0.5f, py = (float) y + 0.5f;
                    float t;
                    if (radial)
                        t = std::sqrt((px - grad.point1.x) * (px - grad.point1.x)
                                      + (py - grad.point1.y) * (py - grad.point1.y)) / radius;
                    else
                        t = ((px - grad.point1.x) * ax + (py - grad.point1.y) * ay) / len2;
                    t = juce::jlimit(0.0f, 1.0f, t);

                    float r, gg, b, a;
                    sampleStops(grad, t, r, gg, b, a);
                    // Bayer value 0..63 -> centred offset in (-0.5,+0.5) LSB.
                    const float dz = (((float) bayer8[y & 7][x & 7] + 0.5f) / 64.0f - 0.5f) / 255.0f;
                    bd.setPixelColour(x, y, juce::Colour::fromFloatRGBA(
                        juce::jlimit(0.0f, 1.0f, r + dz),
                        juce::jlimit(0.0f, 1.0f, gg + dz),
                        juce::jlimit(0.0f, 1.0f, b + dz),
                        juce::jlimit(0.0f, 1.0f, a + dz)));
                }
        }

        if (cache.size() >= 64) cache.clear(); // bound memory; geometry set is small
        Entry e {}; e.w = w; e.h = h; e.n = ns; e.radial = radial;
        e.p1x = grad.point1.x; e.p1y = grad.point1.y;
        e.p2x = grad.point2.x; e.p2y = grad.point2.y;
        for (int i = 0; i < ns; ++i) { e.sp[i] = (float) grad.getColourPosition(i);
                                       e.sc[i] = grad.getColour(i).getARGB(); }
        e.img = img;
        cache.push_back(e);
        return img;
    }

    // Fill helpers: drop-in for g.setGradientFill(grad) + g.fill<shape>().
    inline void fillPath(juce::Graphics &g, const juce::ColourGradient &grad, const juce::Path &shape)
    {
        const auto b = shape.getBounds();
        const auto img = image(grad, b);
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(shape);
        g.drawImageAt(img, (int) std::floor(b.getX()), (int) std::floor(b.getY()));
    }
    inline void fillRect(juce::Graphics &g, const juce::ColourGradient &grad, juce::Rectangle<float> r)
    {
        const auto img = image(grad, r);
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(r.getSmallestIntegerContainer());
        g.drawImageAt(img, (int) std::floor(r.getX()), (int) std::floor(r.getY()));
    }
    inline void fillRoundedRectangle(juce::Graphics &g, const juce::ColourGradient &grad,
                                     juce::Rectangle<float> r, float radius)
    {
        juce::Path p;
        p.addRoundedRectangle(r, radius);
        fillPath(g, grad, p);
    }
    inline void fillEllipse(juce::Graphics &g, const juce::ColourGradient &grad, juce::Rectangle<float> r)
    {
        juce::Path p;
        p.addEllipse(r);
        fillPath(g, grad, p);
    }
}

// ---------------------------------------------------------------------------
// Soft glows. A real glow is a blurred copy of the lit shape (like a CSS
// box-shadow), which fades off in every direction -- NOT a hard-edged expanded
// shape or a flat translucent disc. Use a wide faint pass + a tight brighter
// pass for a natural bloom. Shared so every glowing element matches.
// ---------------------------------------------------------------------------
namespace fx
{
    inline void glow(juce::Graphics &g, const juce::Path &shape, juce::Colour c,
                     int wideRadius, float wideAlpha, int tightRadius, float tightAlpha)
    {
        juce::DropShadow(c.withAlpha(wideAlpha),  juce::jmax(1, wideRadius),  {}).drawForPath(g, shape);
        juce::DropShadow(c.withAlpha(tightAlpha), juce::jmax(1, tightRadius), {}).drawForPath(g, shape);
    }
    inline void glowEllipse(juce::Graphics &g, juce::Rectangle<float> area, juce::Colour c,
                            int wideRadius, float wideAlpha, int tightRadius, float tightAlpha)
    {
        juce::Path p;
        p.addEllipse(area);
        glow(g, p, c, wideRadius, wideAlpha, tightRadius, tightAlpha);
    }
}

class RigLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RigLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, colors::bg);
        setColour(juce::Label::textColourId, colors::text);
        setColour(juce::Slider::textBoxTextColourId, colors::textDim);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextButton::buttonColourId, colors::tile);
        setColour(juce::TextButton::textColourOffId, colors::text);
        setColour(juce::ComboBox::backgroundColourId, colors::tile);
        setColour(juce::ComboBox::textColourId, colors::text);
        setColour(juce::ComboBox::outlineColourId, colors::outline);
        setColour(juce::ComboBox::arrowColourId, colors::textDim);
        // Opaque -> JUCE makes the popup window opaque (no transparent-corner / drop-
        // shadow-box games). These standard PopupMenus are drawn SQUARE + opaque by
        // drawPopupMenuBackground below, so there's no ring. (The fancy ROUNDED drive
        // picker is a custom in-canvas component, not a PopupMenu -- see DrivePickerOverlay.)
        setColour(juce::PopupMenu::backgroundColourId, colors::panel);
        setColour(juce::PopupMenu::textColourId, colors::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, colors::tileSel);
        setColour(juce::PopupMenu::highlightedTextColourId, colors::text);
        setColour(juce::TooltipWindow::backgroundColourId, colors::panel);
        setColour(juce::TooltipWindow::textColourId, colors::text);
        setColour(juce::TooltipWindow::outlineColourId, colors::outline);
    }

    // Resolve any non-explicit Font (Labels, combos, tooltips) to Archivo.
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font &f) override
    {
#if NAM_RIG_HAS_FONTS
        return fonts::archivoFace(f.isBold() ? fonts::Bold : fonts::Regular);
#else
        return LookAndFeel_V4::getTypefaceForFont(f);
#endif
    }

    // ---- Knob: glow pointer + outer fill ring (design "Glow Pointer + fill") ----
    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                          float sliderPos, float /*startAngle*/, float /*endAngle*/,
                          juce::Slider &slider) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const bool enabled = slider.isEnabled();
        const float ga = enabled ? 1.0f : 0.45f; // whole-knob opacity when disabled

        // 280° sweep starting at 220° (gap centred at the bottom).
        const float kStart = juce::degreesToRadians(220.0f);
        const float kEnd   = juce::degreesToRadians(500.0f);
        const float angle  = kStart + sliderPos * (kEnd - kStart);

        // Outer value-ring band (~ outer 26% of the radius).
        const float bandW = juce::jmax(3.0f, radius * 0.26f);
        const float ringR = radius - bandW * 0.5f - 1.0f;

        juce::Path track;
        track.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, kStart, kEnd, true);
        g.setColour(colors::track.withMultipliedAlpha(ga));
        g.strokePath(track, juce::PathStrokeType(bandW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::butt));

        const juce::Colour accentCol = slider.isColourSpecified(juce::Slider::rotarySliderFillColourId)
                                           ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                                           : colors::accent;

        const auto &range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        float fillFrom = kStart;
        if (bipolar)
            fillFrom = kStart + (float)((0.0 - range.getStart()) / range.getLength())
                                    * (kEnd - kStart);

        if (std::abs(angle - fillFrom) > 0.004f)
        {
            juce::Path value;
            value.addCentredArc(c.x, c.y, ringR, ringR, 0.0f,
                                juce::jmin(fillFrom, angle), juce::jmax(fillFrom, angle), true);
            g.setColour((enabled ? accentCol : juce::Colour(0xff3a414c)).withMultipliedAlpha(ga));
            g.strokePath(value, juce::PathStrokeType(bandW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::butt));
        }

        // Dark dial cap, inset ~17%.
        const float capR = radius * 0.83f;
        auto cap = juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(c);
        juce::ColourGradient cg(juce::Colour(0xff262b33).withMultipliedAlpha(ga), cap.getTopLeft(),
                                juce::Colour(0xff1b1f25).withMultipliedAlpha(ga), cap.getBottomRight(), false);
        dither::fillEllipse(g, cg, cap);
        g.setColour(juce::Colours::white.withAlpha(0.05f * ga));
        g.drawEllipse(cap.reduced(0.5f), 1.0f);

        // Glowing pointer from centre to ~0.7×radius.
        const float pinLen = radius * 0.70f;
        const float pinW   = juce::jmax(2.4f, radius * 0.085f);
        const auto tf = juce::AffineTransform::rotation(angle).translated(c.x, c.y);
        const juce::Colour pinCol = enabled ? accentCol : juce::Colour(0xff5a616b);
        juce::Path pin;
        pin.addRoundedRectangle(-pinW * 0.5f, -pinLen, pinW, pinLen, pinW * 0.5f);
        pin.applyTransform(tf);
        if (enabled)
            fx::glow(g, pin, pinCol,
                     juce::roundToInt(juce::jmax(7.0f, radius * 0.42f)), 0.28f,
                     juce::roundToInt(juce::jmax(3.0f, radius * 0.16f)), 0.48f);
        g.setColour(pinCol.withMultipliedAlpha(ga));
        g.fillPath(pin);
    }

    // Toggle: "pill" property -> filled/outlined pill with centred text; caption-
    // less -> small centred box; else a standard checkbox.
    void drawToggleButton(juce::Graphics &g, juce::ToggleButton &b,
                          bool highlighted, bool down) override
    {
        const bool pill = b.getProperties()["pill"].equals(true);
        if (!pill && b.getButtonText().isEmpty())
        {
            auto a = b.getLocalBounds().toFloat();
            const float sz = juce::jmin(15.0f, a.getWidth() - 2.0f, a.getHeight() - 2.0f);
            auto box = juce::Rectangle<float>(sz, sz).withCentre(a.getCentre());
            const bool on = b.getToggleState();
            g.setColour(on ? colors::accent : colors::tile);
            g.fillRoundedRectangle(box, 3.0f);
            g.setColour(on ? colors::accent : colors::outline);
            g.drawRoundedRectangle(box, 3.0f, 1.0f);
            return;
        }
        if (!pill)
        {
            LookAndFeel_V4::drawToggleButton(g, b, highlighted, down);
            return;
        }
        auto r = b.getLocalBounds().toFloat().reduced(1.0f);
        const bool on = b.getToggleState();
        const juce::Colour onCol = b.isColourSpecified(juce::TextButton::buttonOnColourId)
                                       ? b.findColour(juce::TextButton::buttonOnColourId)
                                       : colors::accent;
        if (on)
        {
            g.setColour(highlighted ? onCol.brighter(0.08f) : onCol);
            g.fillRoundedRectangle(r, 7.0f);
            g.setColour(onCol);
            g.drawRoundedRectangle(r, 7.0f, 1.0f);
        }
        else
        {
            g.setColour(highlighted ? colors::tileSel : colors::tile);
            g.fillRoundedRectangle(r, 7.0f);
            g.setColour(colors::outline);
            g.drawRoundedRectangle(r, 7.0f, 1.0f);
        }
        g.setColour(on ? colors::bg : colors::textDim);
        g.setFont(fonts::archivo(12.0f, fonts::SemiBold));
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }

    void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider &slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                             minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const float trackW = 5.0f;
        const float cx = (float)x + (float)width * 0.5f;
        const auto trk = juce::Rectangle<float>(cx - trackW * 0.5f, (float)y, trackW, (float)height);
        g.setColour(colors::track);
        g.fillRoundedRectangle(trk, trackW * 0.5f);

        const auto &range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        float fillFromY = (float)y + (float)height;
        if (bipolar)
            fillFromY = (float)y + (float)height * (float)(range.getEnd() / range.getLength());

        const auto fill = juce::Rectangle<float>(cx - trackW * 0.5f, juce::jmin(sliderPos, fillFromY),
                                                 trackW, std::abs(fillFromY - sliderPos));
        g.setColour(slider.isEnabled() ? colors::accent : colors::accentDim);
        g.fillRoundedRectangle(fill, trackW * 0.5f);

        const float thumbW = juce::jmin((float)width, 26.0f);
        g.setColour(colors::text);
        g.fillRoundedRectangle(cx - thumbW * 0.5f, sliderPos - 2.5f, thumbW, 5.0f, 2.5f);
    }

    void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                              const juce::Colour &, bool highlighted, bool down) override
    {
        auto b = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = colors::tile;
        if (down) fill = colors::tileSel.brighter(0.06f);
        else if (highlighted) fill = colors::tileSel;
        g.setColour(fill);
        g.fillRoundedRectangle(b, 7.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 7.0f, 1.0f);
    }

    void drawComboBox(juce::Graphics &g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox &box) override
    {
        auto b = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
        g.setColour(colors::inset);
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 8.0f, 1.0f);

        g.setColour(box.isEnabled() ? colors::caption : colors::ledOff);
        g.setFont(fonts::archivo((float)height * 0.5f));
        g.drawText(juce::String::fromUTF8("▾"), // ▾
                   juce::Rectangle<int>(width - 20, 0, 16, height), juce::Justification::centred);
    }

    juce::Font getComboBoxFont(juce::ComboBox &) override { return fonts::archivo(13.0f, fonts::SemiBold); }
    void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override
    {
        label.setBounds(13, 1, juce::jmax(1, box.getWidth() - 30), box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
    juce::Font getTextButtonFont(juce::TextButton &, int) override { return fonts::archivo(13.0f, fonts::SemiBold); }
    juce::Font getLabelFont(juce::Label &l) override
    {
        return fonts::archivo(juce::jlimit(9.0f, 14.0f, (float)l.getHeight() - 2.0f));
    }
    juce::Font getPopupMenuFont() override { return fonts::archivo(14.0f, fonts::SemiBold); }

    // ComboBox popups drop BELOW the box instead of overlaying the selected item on
    // top of it, so the box (button) stays visible. Omitting withItemThatMustBeVisible
    // is what stops JUCE from aligning the chosen item over the target.
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox &box,
                                                            juce::Label &label) override
    {
        return juce::PopupMenu::Options()
            .withTargetComponent(&box)
            .withInitiallySelectedItem(box.getSelectedId())
            .withMinimumWidth(box.getWidth())
            .withMaximumNumColumns(1)
            .withStandardItemHeight(label.getHeight());
    }

    // ---- Styled dropdown menus (matches the drive picker design) -------------
    // Every PopupMenu + ComboBox dropdown in the editor uses this LookAndFeel, so
    // styling it here gives the whole plugin the one rounded dark menu look.

    // Inner margin so the rounded border + corners have room around the items.
    int getPopupMenuBorderSize() override { return 6; }

    // Rounded dark panel with a soft border (the popup window is transparent on
    // Windows, so the corners outside the rounded rect stay clear).
    void drawPopupMenuBackground(juce::Graphics &g, int width, int height) override
    {
        // Standard PopupMenus only (combos, delay, presets). Opaque SQUARE fill edge-
        // to-edge -> no white/dark ring, the OS gives a normal drop shadow. Rounded
        // corners on an opaque popup window are impossible without a corner ring, so
        // rounding lives only in the custom DrivePickerOverlay (a real component).
        juce::ColourGradient grad(juce::Colour(0xff232830), 0.0f, 0.0f,
                                  juce::Colour(0xff171a21), 0.0f, (float)height, false);
        dither::fillRect(g, grad, juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height));
    }

    // Comfortable rows; section headers and separators get their own heights.
    void getIdealPopupMenuItemSize(const juce::String &text, bool isSeparator,
                                   int /*standardMenuItemHeight*/,
                                   int &idealWidth, int &idealHeight) override
    {
        if (isSeparator)
        {
            idealWidth = 60;
            idealHeight = 11;
            return;
        }
        const auto f = getPopupMenuFont();
        idealWidth = (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, text)) + 60;
        idealHeight = 32;
    }

    // "CHOOSE DRIVE"-style header: small tracked caption, left aligned.
    void drawPopupMenuSectionHeader(juce::Graphics &g, const juce::Rectangle<int> &area,
                                    const juce::String &sectionName) override
    {
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.14f));
        g.drawText(sectionName.toUpperCase(), area.reduced(14, 0).withTrimmedTop(4),
                   juce::Justification::bottomLeft, true);
    }

    void drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String &text,
                           const juce::String &shortcutKeyText,
                           const juce::Drawable * /*icon*/,
                           const juce::Colour *textColour) override
    {
        if (isSeparator)
        {
            auto s = area.reduced(12, 0);
            g.setColour(colors::divider);
            g.fillRect(s.withSizeKeepingCentre(s.getWidth(), 1));
            return;
        }

        auto row = area.toFloat().reduced(5.0f, 1.5f);
        if (isHighlighted && isActive)
        {
            g.setColour(colors::tileSel);
            g.fillRoundedRectangle(row, 7.0f);
        }

        const float ga = isActive ? 1.0f : 0.4f;
        auto txt = area.reduced(14, 0);

        if (isTicked) // current selection -> accent dot at the left
        {
            auto dot = juce::Rectangle<float>(6.0f, 6.0f)
                           .withCentre({(float)area.getX() + 11.0f, (float)area.getCentreY()});
            g.setColour(colors::accent);
            g.fillEllipse(dot);
            txt = txt.withTrimmedLeft(10);
        }

        juce::Colour col = textColour != nullptr
                               ? *textColour
                               : (isTicked ? colors::textBright
                                           : (isHighlighted ? colors::textBright : colors::text));
        g.setColour(col.withMultipliedAlpha(ga));
        g.setFont(getPopupMenuFont());
        g.drawText(text, txt, juce::Justification::centredLeft, true);

        if (hasSubMenu) // right chevron ">"
        {
            const float h = (float)area.getHeight();
            const float x = (float)area.getRight() - 16.0f;
            const float cy = (float)area.getCentreY();
            const float s = juce::jmin(4.5f, h * 0.18f);
            juce::Path p;
            p.startNewSubPath(x - s * 0.5f, cy - s);
            p.lineTo(x + s * 0.5f, cy);
            p.lineTo(x - s * 0.5f, cy + s);
            g.setColour(colors::caption.withMultipliedAlpha(ga));
            g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        }
        else if (shortcutKeyText.isNotEmpty())
        {
            g.setColour(colors::caption.withMultipliedAlpha(ga));
            g.setFont(fonts::mono(11.0f));
            g.drawText(shortcutKeyText, area.reduced(14, 0),
                       juce::Justification::centredRight, true);
        }
    }

    // Back-compat helper used throughout the UI; resolves to Archivo via the
    // typeface override (bold flag picks Archivo-Bold).
    static juce::Font withHeight(float h)
    {
        return juce::Font(juce::FontOptions{}.withHeight(h));
    }

    // Vertical level-meter gradient (green -> amber -> red, anchored at bottom).
    static juce::ColourGradient meterGradient(juce::Rectangle<float> r)
    {
        juce::ColourGradient grad(colors::meterLo, r.getBottomLeft(),
                                  colors::meterHi, r.getTopLeft(), false);
        grad.addColour(0.55, colors::meterLo);
        grad.addColour(0.80, colors::accent);
        return grad;
    }

    // Display-well background (meters / graphs): vertical gradient + card border.
    static void drawWell(juce::Graphics &g, juce::Rectangle<float> r, float radius = 11.0f)
    {
        juce::ColourGradient grad(colors::wellTop, r.getTopLeft(),
                                  colors::wellBot, r.getBottomLeft(), false);
        dither::fillRoundedRectangle(g, grad, r, radius);
        g.setColour(colors::cardBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    }
};

} // namespace nam_rig::ui
