#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace nam_rig::ui
{

// Palette for the whole editor. Dark flat: one amber accent, no textures.
namespace colors
{
    inline const juce::Colour bg          {0xff14161a}; // window
    inline const juce::Colour panel       {0xff1d2027}; // block panel body
    inline const juce::Colour tile        {0xff22262d}; // strip tile
    inline const juce::Colour tileSel     {0xff2b3039}; // selected strip tile
    inline const juce::Colour outline     {0xff343a43};
    inline const juce::Colour text        {0xffd8dbe0};
    inline const juce::Colour textDim     {0xff8b919b};
    inline const juce::Colour accent      {0xffffb13d}; // amber: value arcs, selection
    inline const juce::Colour accentDim   {0xff7a5a28};
    inline const juce::Colour ledOff      {0xff3a3f47};
    inline const juce::Colour track       {0xff2e333b}; // knob/slider track
    inline const juce::Colour meterLo     {0xff5fcf6e};
    inline const juce::Colour meterMid    {0xffffb13d};
    inline const juce::Colour meterHi     {0xffe85d4a};
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
    }

    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider &slider) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float lineW = juce::jmax(2.0f, radius * 0.14f);
        const float arcR = radius - lineW * 0.5f;
        const bool enabled = slider.isEnabled();

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(colors::track);
        g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Bipolar params (range spanning 0) fill from 12 o'clock, others from the start.
        const auto &range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        float fillFrom = rotaryStartAngle;
        if (bipolar)
            fillFrom = rotaryStartAngle
                       + (float)((0.0 - range.getStart()) / range.getLength())
                             * (rotaryEndAngle - rotaryStartAngle);

        if (std::abs(angle - fillFrom) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f,
                                juce::jmin(fillFrom, angle), juce::jmax(fillFrom, angle), true);
            g.setColour(enabled ? colors::accent : colors::accentDim);
            g.strokePath(value, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Pointer
        const float innerR = arcR - lineW * 1.4f;
        juce::Path p;
        p.addRoundedRectangle(-lineW * 0.5f, -innerR, lineW, innerR * 0.55f, lineW * 0.4f);
        g.setColour(enabled ? colors::text : colors::textDim);
        g.fillPath(p, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
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

        // Vertical EQ-style slider: centered bipolar fill + bar thumb.
        const float trackW = 5.0f;
        const float cx = (float)x + (float)width * 0.5f;
        const auto track = juce::Rectangle<float>(cx - trackW * 0.5f, (float)y,
                                                  trackW, (float)height);
        g.setColour(colors::track);
        g.fillRoundedRectangle(track, trackW * 0.5f);

        const auto &range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        float fillFromY = (float)y + (float)height; // bottom (unipolar)
        if (bipolar)
            fillFromY = (float)y + (float)height
                        * (float)(range.getEnd() / range.getLength()); // value 0 line

        const auto fill = juce::Rectangle<float>(cx - trackW * 0.5f,
                                                 juce::jmin(sliderPos, fillFromY), trackW,
                                                 std::abs(fillFromY - sliderPos));
        g.setColour(slider.isEnabled() ? colors::accent : colors::accentDim);
        g.fillRoundedRectangle(fill, trackW * 0.5f);

        // Thumb: horizontal bar
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
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 6.0f, 1.0f);
    }

    void drawComboBox(juce::Graphics &g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox &box) override
    {
        auto b = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
        g.setColour(colors::tile);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 6.0f, 1.0f);

        juce::Path arrow;
        const float ax = (float)width - 14.0f, ay = (float)height * 0.5f;
        arrow.addTriangle(ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.5f);
        g.setColour(box.isEnabled() ? colors::textDim : colors::ledOff);
        g.fillPath(arrow);
    }

    juce::Font getComboBoxFont(juce::ComboBox &) override { return withHeight(14.0f); }
    // Reserve the arrow width on BOTH sides so centred text sits in the true middle.
    void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override
    {
        const int m = 22;
        label.setBounds(m, 1, juce::jmax(1, box.getWidth() - m * 2), box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
    juce::Font getTextButtonFont(juce::TextButton &, int) override { return withHeight(14.0f); }
    juce::Font getLabelFont(juce::Label &l) override
    {
        // Label may not be laid out yet — never hand Font a non-positive height.
        return withHeight(juce::jlimit(10.0f, 14.0f, (float)l.getHeight() - 2.0f));
    }

    static juce::Font withHeight(float h)
    {
        return juce::Font(juce::FontOptions{}.withHeight(h));
    }
};

} // namespace nam_rig::ui
