#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

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
    // (LED, knob arcs), second = enclosure tint.
    struct AccentPair { juce::Colour accent, tint; };
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
        g.setGradientFill(cg);
        g.fillEllipse(cap);
        g.setColour(juce::Colours::white.withAlpha(0.05f * ga));
        g.drawEllipse(cap.reduced(0.5f), 1.0f);

        // Glowing pointer from centre to ~0.7×radius.
        const float pinLen = radius * 0.70f;
        const float pinW   = juce::jmax(2.4f, radius * 0.085f);
        const auto tf = juce::AffineTransform::rotation(angle).translated(c.x, c.y);
        const juce::Colour pinCol = enabled ? accentCol : juce::Colour(0xff5a616b);
        if (enabled)
        {
            for (auto m : {3.2f, 2.0f})
            {
                juce::Path glow;
                glow.addRoundedRectangle(-pinW * 0.5f * m, -pinLen, pinW * m, pinLen, pinW * 0.5f * m);
                g.setColour(pinCol.withAlpha(m > 2.5f ? 0.10f : 0.18f));
                g.fillPath(glow, tf);
            }
        }
        juce::Path pin;
        pin.addRoundedRectangle(-pinW * 0.5f, -pinLen, pinW, pinLen, pinW * 0.5f);
        g.setColour(pinCol.withMultipliedAlpha(ga));
        g.fillPath(pin, tf);
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
    juce::Font getPopupMenuFont() override { return fonts::archivo(13.0f); }

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
        g.setGradientFill(grad);
        g.fillRoundedRectangle(r, radius);
        g.setColour(colors::cardBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    }
};

} // namespace nam_rig::ui
