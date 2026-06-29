#pragma once
#include "PluginProcessor.h"
#include "ui/RigLookAndFeel.h"
#include "ui/Meter.h"
#include "ui/GrMeter.h"
#include "ui/CompMeter.h"
#include "ui/CompCurve.h"
#include "ui/ModFxIcon.h"
#include "ui/ReverbIcon.h"

namespace nam_rig::ui
{

// Compact, non-selectable menu section header ("CHOOSE DRIVE" etc). JUCE's
// addSectionHeader reserves a tall row with the label bottom-aligned, which left
// a big dead gap at the top of the menu; this custom item is short and draws the
// caption with minimal padding.
class MenuSectionHeader : public juce::PopupMenu::CustomComponent
{
public:
    explicit MenuSectionHeader(juce::String text)
        : juce::PopupMenu::CustomComponent(false), mText(std::move(text).toUpperCase()) {}

    void getIdealSize(int &w, int &h) override
    {
        const auto f = fonts::archivo(10.0f, fonts::SemiBold, 0.14f);
        w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, mText)) + 34;
        h = 22;
    }
    void paint(juce::Graphics &g) override
    {
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.14f));
        g.drawText(mText, getLocalBounds().reduced(14, 0), juce::Justification::centredLeft, true);
    }

private:
    juce::String mText;
};

// Rotary knob with a caption above and a mono value readout below, attached to
// one APVTS parameter. Caption + value are drawn by the component (the rotary
// itself comes from the LookAndFeel); the readout turns accent while dragging.
class LabeledKnob : public juce::Component, private juce::Slider::Listener
{
public:
    LabeledKnob(juce::AudioProcessorValueTreeState &apvts, const juce::String &paramId,
                const juce::String &caption)
        : mCaption(caption.toUpperCase())
    {
        mSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        mSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(mSlider);
        mAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, mSlider);
        if (auto *param = apvts.getParameter(paramId)) // double-click = default
        {
            mSlider.setDoubleClickReturnValue(
                true, param->convertFrom0to1(param->getDefaultValue()));
            mUnit = param->getLabel(); // unit suffix (dB/ms/Hz/s/...) for the value readout
        }
        mSlider.addListener(this);
    }

    void paint(juce::Graphics &g) override
    {
        const juce::Colour acc = mSlider.isColourSpecified(juce::Slider::rotarySliderFillColourId)
                                     ? mSlider.findColour(juce::Slider::rotarySliderFillColourId)
                                     : colors::accent;
        const float ga = isEnabled() ? 1.0f : 0.45f;

        g.setColour(colors::textDim.withMultipliedAlpha(ga));
        g.setFont(fonts::archivo(juce::jmin(10.0f, (float)mCaptionH - 2.0f), fonts::SemiBold, 0.08f));
        g.drawText(mCaption, mCaptionRect, juce::Justification::centred);

        if (mShowValue)
        {
            juce::String txt = mSlider.getTextFromValue(mSlider.getValue());
            if (!mRotationReadout && mUnit.isNotEmpty()) // pedal-style 0..10 knobs stay unitless
                txt << ' ' << mUnit;
            if (!mValueMenu.isEmpty()) // clickable value -> dropdown
                txt << juce::String::fromUTF8(" \xE2\x96\xBE");
            g.setColour((mDragging ? acc : colors::text2).withMultipliedAlpha(ga));
            g.setFont(fonts::mono(11.0f, fonts::Medium));
            g.drawText(txt, mValueRect, juce::Justification::centred);
        }
    }

    // Make the value readout a clickable dropdown of discrete choices (the knob
    // can still be turned to step). Used for the delay Sync division knobs. An
    // optional header titles the styled menu ("Sync", "Mode", ...).
    void setValueMenu(juce::StringArray items, juce::String header = {})
    {
        mValueMenu = std::move(items);
        mValueMenuHeader = std::move(header);
        repaint();
    }

    void mouseDown(const juce::MouseEvent &e) override
    {
        if (mValueMenu.isEmpty() || !mShowValue || !isEnabled()
            || !mValueRect.contains(e.getPosition()))
            return;
        juce::PopupMenu m;
        if (mValueMenuHeader.isNotEmpty())
            m.addCustomItem(-1, std::make_unique<MenuSectionHeader>(mValueMenuHeader), nullptr, {});
        const int cur = (int)std::lround(mSlider.getValue());
        for (int i = 0; i < mValueMenu.size(); ++i)
            m.addItem(i + 1, mValueMenu[i], true, i == cur);
        m.setLookAndFeel(&getLookAndFeel());
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [this](int r) {
                            if (r > 0) mSlider.setValue(r - 1, juce::sendNotificationSync);
                        });
    }

    void resized() override
    {
        auto b = getLocalBounds();
        mCaptionRect = b.removeFromTop(mCaptionH);
        if (mShowValue)
            mValueRect = b.removeFromBottom(mValueH);
        const int d = juce::jmin(b.getWidth(), b.getHeight());
        mSlider.setBounds(b.withSizeKeepingCentre(d, d));
    }

    // JUCE doesn't repaint on setEnabled(), so force it -> the caption, value readout
    // and rotary all grey out together when disabled (e.g. the delay Time knob once a
    // Sync division owns the time).
    void enablementChanged() override { repaint(); }

    // Shrink the caption row so longer captions fit narrower mod-lane knobs.
    void setCaptionHeight(int h) { mCaptionH = h; resized(); repaint(); }

    juce::Slider &slider() { return mSlider; }
    void setCaption(const juce::String &c)
    {
        mCaption = c.toUpperCase();
        repaint();
    }

    // Tint the value arc (per-lane mod colour). The LookAndFeel reads this colour
    // id and falls back to the global accent when it isn't set.
    void setAccent(juce::Colour c) { mSlider.setColour(juce::Slider::rotarySliderFillColourId, c); }

    // Drop the numeric readout (the rotary then fills the freed space).
    void hideValue() { mShowValue = false; resized(); repaint(); }

    // Read the value box as a 0..top reading of the knob's ROTATION (pedal-style,
    // "everything goes to 10") instead of raw parameter units. Display only.
    void setRotationReadout(double top = 10.0)
    {
        mRotationReadout = true; // 0..10 rotation display -> suppress the unit suffix
        auto *s = &mSlider;
        mSlider.textFromValueFunction = [s, top](double v) {
            return juce::String(s->valueToProportionOfLength(v) * top, 1);
        };
        mSlider.valueFromTextFunction = [s, top](const juce::String &t) {
            return s->proportionOfLengthToValue(juce::jlimit(0.0, 1.0, t.getDoubleValue() / top));
        };
        mSlider.updateText();
        repaint();
    }
    void updateReadout() { mSlider.updateText(); repaint(); }

    // Fully custom value readout: the function returns the COMPLETE string (incl.
    // any unit), so the auto unit suffix is suppressed and kept suppressed across
    // rebind(). Used by the delay Time knob to show the measured ms in both Free
    // and tempo-synced modes.
    void setReadoutFn(std::function<juce::String(double)> fn)
    {
        mReadoutFn = true;
        mSlider.textFromValueFunction = std::move(fn);
        mUnit.clear();
        mSlider.updateText();
        repaint();
    }

    // Re-point this knob at a different parameter (drive pedals rebind per TYPE).
    void rebind(juce::AudioProcessorValueTreeState &apvts, const juce::String &paramId)
    {
        mAtt.reset(); // destroy the old attachment before making the new one
        mAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, mSlider);
        if (auto *param = apvts.getParameter(paramId))
        {
            mSlider.setDoubleClickReturnValue(
                true, param->convertFrom0to1(param->getDefaultValue()));
            if (!mReadoutFn) mUnit = param->getLabel(); // custom readout supplies its own unit
        }
        repaint();
    }

private:
    void sliderValueChanged(juce::Slider *) override { repaint(); }
    void sliderDragStarted(juce::Slider *) override { mDragging = true; repaint(); }
    void sliderDragEnded(juce::Slider *) override { mDragging = false; repaint(); }

    juce::String mCaption;
    juce::Slider mSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mAtt;
    juce::Rectangle<int> mCaptionRect, mValueRect;
    juce::String mUnit;
    juce::StringArray mValueMenu; // when set, the value readout is a click-to-pick dropdown
    juce::String mValueMenuHeader; // optional title for the click-to-pick menu
    int mCaptionH = 15, mValueH = 16;
    bool mShowValue = true, mDragging = false, mRotationReadout = false, mReadoutFn = false;
};

// Horizontal knob in a rounded bordered box: knob on the left, caption + value
// readout (0..10 rotation, pedal-style) stacked on the right. Used for the
// section Dry/Wet so it reads as one tidy control beside the blend pad.
class HKnob : public juce::Component, private juce::Slider::Listener
{
public:
    HKnob(juce::AudioProcessorValueTreeState &apvts, const juce::String &paramId,
          const juce::String &caption, juce::Colour accent = colors::accent)
        : mCaption(caption)
    {
        mSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        mSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        mSlider.setColour(juce::Slider::rotarySliderFillColourId, accent);
        addAndMakeVisible(mSlider);
        mAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramId, mSlider);
        if (auto *p = apvts.getParameter(paramId))
            mSlider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        mSlider.addListener(this);
    }

    void paint(juce::Graphics &g) override
    {
        auto box = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(colors::tile);
        g.fillRoundedRectangle(box, 8.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(box, 8.0f, 1.0f);

        auto txt = mTextArea;
        g.setColour(colors::textDim);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.06f));
        auto cap = txt.removeFromTop(txt.getHeight() * 0.5f);
        g.drawText(mCaption.toUpperCase(), cap.toNearestInt(), juce::Justification::centred);
        // Value box: fixed width, centred under the caption.
        const float vbw = juce::jmin(txt.getWidth(), 48.0f);
        auto vb = juce::Rectangle<float>(vbw, juce::jmin(txt.getHeight(), 18.0f)).withCentre(txt.getCentre());
        g.setColour(colors::scopeBg);
        g.fillRoundedRectangle(vb, 4.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(vb, 4.0f, 1.0f);
        g.setColour(colors::text);
        g.setFont(fonts::mono(11.0f, fonts::Medium));
        const double v = mSlider.valueToProportionOfLength(mSlider.getValue()) * 10.0;
        g.drawText(juce::String(v, 1), vb.toNearestInt(), juce::Justification::centred);
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced(8, 6);
        const int kw = juce::jmin(inner.getHeight(), inner.getWidth() / 2);
        mSlider.setBounds(inner.removeFromLeft(kw));
        inner.removeFromLeft(8);
        mTextArea = inner.toFloat();
    }

private:
    void sliderValueChanged(juce::Slider *) override { repaint(); }
    juce::String mCaption;
    juce::Slider mSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mAtt;
    juce::Rectangle<float> mTextArea;
};

// Segmented mode selector (design "pill" buttons): one accent-filled active pill
// + outlined inactive pills, bound to a choice parameter via a hidden ComboBox so
// it tracks automation / presets. Content-sized; ask idealWidth() to lay it out.
class SegmentedControl : public juce::Component
{
public:
    std::function<void(int)> onChange;

    SegmentedControl(juce::AudioProcessorValueTreeState &apvts, const juce::String &paramId,
                     juce::StringArray options)
        : mOptions(std::move(options))
    {
        mCombo.addItemList(mOptions, 1);
        addChildComponent(mCombo); // invisible: just the parameter bridge
        mAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, paramId, mCombo);
        mCombo.onChange = [this] { repaint(); if (onChange) onChange(index()); };
    }

    // Manual mode (no parameter attachment): the owner drives the highlight via
    // setActive() and handles clicks via onChange(). Use this when the control
    // maps to PART of a parameter's range — e.g. the Mix A/B selector over the
    // 3-choice rigMode, where a plain attachment would mis-scale (2 items vs 3).
    explicit SegmentedControl(juce::StringArray options)
        : mOptions(std::move(options)), mManual(true) {}

    int index() const
    {
        return mManual ? juce::jmax(0, mManualIndex) : juce::jmax(0, mCombo.getSelectedItemIndex());
    }
    void setActive(int i) { if (i != mManualIndex) { mManualIndex = i; repaint(); } }
    void setEnabledIndex(int i) { mCombo.setSelectedItemIndex(i); }

    int idealWidth() const
    {
        auto f = fonts::archivo(12.0f, fonts::SemiBold);
        int w = 0;
        for (auto &o : mOptions)
            w += (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, o)) + 26 + mGap;
        return juce::jmax(0, w - mGap);
    }

    // Widest single button -- used to size the vertical stacked layout (all
    // buttons are made this same size).
    int idealCellWidth() const
    {
        auto f = fonts::archivo(12.0f, fonts::SemiBold);
        int w = 0;
        for (auto &o : mOptions)
            w = juce::jmax(w, (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, o)));
        return w + 16;
    }

    // Stack the buttons vertically (equal size) instead of in a horizontal row.
    void setVertical(bool v) { if (mVertical != v) { mVertical = v; repaint(); } }

    // Override the active-segment highlight colour (defaults to the global accent);
    // drive pedals set this to the pedal's own colour so Range/Gate match the LED.
    void setAccent(juce::Colour c) { if (mAccent != c) { mAccent = c; repaint(); } }

    void paint(juce::Graphics &g) override
    {
        auto f = fonts::archivo(12.0f, fonts::SemiBold);
        g.setFont(f);
        // -1 (no highlight) when the value isn't one of this control's segments —
        // e.g. the Mix A/B selector while the rig is in Dual.
        const int active = mManual ? mManualIndex : mCombo.getSelectedItemIndex();
        const int n = juce::jmax(1, mOptions.size());
        const bool en = isEnabled(); // greyed (no accent) when the host disables it

        if (mVertical) // equal-size buttons stacked top-to-bottom, touching
        {
            const float bh = (float)getHeight() / (float)n;
            const float rad = 7.0f;
            for (int i = 0; i < mOptions.size(); ++i)
            {
                auto r = juce::Rectangle<float>(0.0f, i * bh, (float)getWidth(), bh).reduced(0.5f);
                const bool top = (i == 0), bot = (i == n - 1);
                juce::Path cell; // round only the stack's outer corners
                cell.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                                         rad, rad, top, top, bot, bot);
                const bool on = i == active;
                g.setColour(on ? (en ? mAccent : colors::tileSel) : colors::tile);
                g.fillPath(cell);
                g.setColour((on && en) ? mAccent : colors::outline);
                g.strokePath(cell, juce::PathStrokeType(1.0f));
                g.setColour(on ? (en ? colors::bg : colors::textDim) : (en ? colors::textDim : colors::captionDim));
                g.drawText(mOptions[i], r, juce::Justification::centred);
            }
            return;
        }

        int x = 0;
        for (int i = 0; i < mOptions.size(); ++i)
        {
            const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, mOptions[i])) + 26;
            auto r = juce::Rectangle<float>((float)x, 0.0f, (float)w, (float)getHeight());
            const bool on = i == active;
            g.setColour(on ? (en ? mAccent : colors::tileSel) : colors::tile);
            g.fillRoundedRectangle(r, 7.0f);
            g.setColour((on && en) ? mAccent : colors::outline);
            g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);
            g.setColour(on ? (en ? colors::bg : colors::textDim) : (en ? colors::textDim : colors::captionDim));
            g.drawText(mOptions[i], r, juce::Justification::centred);
            x += w + mGap;
        }
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!isEnabled()) return; // bypassed control: ignore clicks
        const int n = juce::jmax(1, mOptions.size());
        if (mVertical)
        {
            const float bh = (float)getHeight() / (float)n;
            for (int i = 0; i < mOptions.size(); ++i)
                if (e.y >= i * bh && e.y < (i + 1) * bh) { pick(i); return; }
            return;
        }
        auto f = fonts::archivo(12.0f, fonts::SemiBold);
        int x = 0;
        for (int i = 0; i < mOptions.size(); ++i)
        {
            const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, mOptions[i])) + 26;
            if (e.x >= x && e.x < x + w) { pick(i); return; }
            x += w + mGap;
        }
    }

private:
    void pick(int i)
    {
        if (mManual) { mManualIndex = i; repaint(); if (onChange) onChange(i); }
        else mCombo.setSelectedItemIndex(i);
    }

    juce::StringArray mOptions;
    juce::ComboBox mCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mAtt;
    int mGap = 8;
    bool mVertical = false, mManual = false;
    int mManualIndex = -1;
    juce::Colour mAccent { colors::accent };
};

// A small toggle "switch" (rounded track + sliding knob), bound to a bool param.
// Optional left caption is drawn by the host; this is just the switch.
class ToggleSwitch : public juce::Component
{
public:
    std::function<void(bool)> onChange;

    ToggleSwitch(juce::AudioProcessorValueTreeState &apvts, const juce::String &paramId)
    {
        mBtn.setClickingTogglesState(true);
        addChildComponent(mBtn);
        mAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, paramId, mBtn);
        mBtn.onClick = [this] { repaint(); if (onChange) onChange(mBtn.getToggleState()); };
    }

    bool state() const { return mBtn.getToggleState(); }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (getLocalBounds().contains(e.getPosition()))
            mBtn.setToggleState(!mBtn.getToggleState(), juce::sendNotificationSync);
    }

    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre(
            juce::jmin((float)getWidth(), 42.0f), juce::jmin((float)getHeight(), 24.0f));
        const bool on = mBtn.getToggleState();
        g.setColour(on ? colors::accent : colors::tile);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(on ? colors::accent : colors::outline);
        g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
        const float d = r.getHeight() - 4.0f;
        const float kx = on ? r.getRight() - d - 2.0f : r.getX() + 2.0f;
        g.setColour(on ? colors::bg : colors::text2);
        g.fillEllipse(kx, r.getY() + 2.0f, d, d);
    }

private:
    juce::ToggleButton mBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mAtt;
};

// Common panel chrome: rounded #1d2027 body + 46px header row (title left, an
// optional right-aligned caption, bottom divider). Content laid out by subclass.
// Bypass veil: when a block is off, this sits on top of the whole panel, draws the
// dim "BYPASSED" scrim over the body, and swallows every mouse event so nothing —
// including the header buttons next to the title — can be clicked.
class BypassVeil : public juce::Component
{
public:
    BypassVeil() { setInterceptsMouseClicks(true, false); }

    void parentSizeChanged() override
    {
        if (auto *p = getParentComponent()) setBounds(p->getLocalBounds());
    }

    bool hitTest(int, int) override { return true; } // block the entire panel, header included

    void paint(juce::Graphics &g) override
    {
        // Light dim across the whole panel so the header buttons next to the
        // title also read as bypassed (the amber title stays legible through it).
        g.setColour(colors::panel.withAlpha(0.45f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 11.0f);

        // Heavier scrim over the body.
        auto body = getLocalBounds().withTrimmedTop(46).toFloat().reduced(2.0f);
        g.setColour(colors::panel.withAlpha(0.62f));
        g.fillRoundedRectangle(body, 10.0f);
        // Soft elliptical vignette behind the word — wider than it is tall — so it
        // lifts off the scrim without a box; fades to nothing at the edges.
        auto ctr = getLocalBounds().getCentre().toFloat();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.addTransform(juce::AffineTransform::scale(1.0f, 0.4f, ctr.x, ctr.y)); // squash height
            juce::ColourGradient halo(juce::Colours::black.withAlpha(0.42f), ctr.x, ctr.y,
                                      juce::Colours::black.withAlpha(0.0f), ctr.x + 180.0f, ctr.y, true);
            halo.addColour(0.55, juce::Colours::black.withAlpha(0.16f));
            g.setGradientFill(halo);
            g.fillRect(getLocalBounds());
        }

        // Clean bright label, centred in the FULL panel height.
        g.setColour(colors::textBright.withAlpha(0.97f));
        g.setFont(fonts::archivo(16.0f, fonts::ExtraBold, 0.28f));
        g.drawText("BYPASSED", getLocalBounds(), juce::Justification::centred);
    }
};

class BlockPanel : public juce::Component
{
public:
    static constexpr int kHeaderH = 46;

    explicit BlockPanel(const juce::String &title) : mTitle(title) { addChildComponent(mVeil); }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(colors::panel);
        g.fillRoundedRectangle(b, 11.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 11.0f, 1.0f);

        auto header = getLocalBounds().removeFromTop(kHeaderH);
        // Header bottom divider.
        g.setColour(colors::divider);
        g.fillRect(header.getX() + 1, header.getBottom() - 1, header.getWidth() - 2, 1);

        // Panel title: amber spaced caps, vertically centred in the header band.
        g.setColour(colors::titleAccent);
        g.setFont(fonts::archivo(13.0f, fonts::Bold, 0.15f));
        g.drawText(mTitle.toUpperCase(), header.reduced(22, 0), juce::Justification::centredLeft);

        if (mHeaderRight.isNotEmpty())
        {
            g.setColour(colors::caption);
            g.setFont(fonts::mono(11.0f, fonts::Medium));
            g.drawText(mHeaderRight, header.reduced(22, 0), juce::Justification::centredRight);
        }
    }

    void setTitle(const juce::String &t) { mTitle = t; repaint(); }
    void setHeaderRight(const juce::String &t)
    {
        if (mHeaderRight != t) { mHeaderRight = t; repaint(); }
    }

    // Block on/off. When bypassed, the veil covers the whole body (up to the
    // title) with a dim "BYPASSED" scrim AND blocks all input so nothing in the
    // panel can be modified. The title bar stays live.
    void setBypassed(bool b)
    {
        if (b == mBypassed) return;
        mBypassed = b;
        mVeil.setBounds(getLocalBounds());
        mVeil.setVisible(b);
        if (b) mVeil.toFront(false); // sit above controls added by the subclass
        onBypassChanged(b);
        repaint();
    }
    bool isBypassed() const { return mBypassed; }

protected:
    // Subclasses override to freeze internal animations when bypass toggles.
    virtual void onBypassChanged(bool /*bypassed*/) {}

    bool mBypassed = false;
    BypassVeil mVeil;

    // 46px header band, inset by the standard 22px horizontal padding.
    juce::Rectangle<int> headerArea() const
    {
        return getLocalBounds().removeFromTop(kHeaderH).reduced(22, 0);
    }
    // Everything below the header (subclasses apply their own padding).
    juce::Rectangle<int> bodyArea() const { return getLocalBounds().withTrimmedTop(kHeaderH); }
    // Legacy default content rect (header + standard padding).
    juce::Rectangle<int> contentArea() const { return bodyArea().reduced(24, 14); }

private:
    juce::String mTitle, mHeaderRight;
};

//==============================================================================
// Horizontal reduction analyser: a luminous area that fills DOWNWARD from the
// top as reduction grows, over a faint dB grid, with a live readout pill. Used
// for the compressor GR display and the gate's live-activity meter.
class GrAnalyser : public juce::Component
{
public:
    static constexpr int N = 170; // shorter history -> trace scrolls a bit faster

    void setAccent(juce::Colour c) { mAccent = c; repaint(); }
    void setLabel(const juce::String &l) { mLabel = l; repaint(); }
    void setSpanDb(float d) { mMaxDb = d; }

    void push(float grDb, float dt)
    {
        grDb = juce::jlimit(0.0f, mMaxDb, grDb);
        // Smooth the stored trace so transients don't make it chatter. Fast on
        // the way up (catch the grab), gentler on the way down.
        const float coef = grDb > mPushSm ? 0.55f : 0.30f;
        mPushSm += coef * (grDb - mPushSm);
        mHist[(size_t)mW] = mPushSm;
        mW = (mW + 1) % N;
        mCur = juce::jmax(grDb, mCur - 60.0f * dt);
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat();
        RigLookAndFeel::drawWell(g, r.reduced(0.5f));
        auto in = r.reduced(2.0f);

        const int step = (int)(mMaxDb / 4.0f);
        for (int d = step; d < (int)mMaxDb; d += step)
        {
            const float y = in.getY() + ((float)d / mMaxDb) * in.getHeight();
            g.setColour(juce::Colours::white.withAlpha(0.045f));
            g.fillRect(in.getX(), y, in.getWidth(), 1.0f);
            auto lbl = juce::Rectangle<float>(in.getX() + 6.0f, y - 6.0f, 30.0f, 12.0f);
            g.setColour(juce::Colour(0xff0f121a).withAlpha(0.55f));
            g.fillRoundedRectangle(lbl, 3.0f);
            g.setColour(colors::textDim);
            g.setFont(fonts::mono(8.5f, fonts::SemiBold));
            g.drawText("-" + juce::String(d), lbl, juce::Justification::centred);
        }

        juce::Path line;
        for (int k = 0; k < N; ++k)
        {
            const float v = mHist[(size_t)((mW + k) % N)];
            const float x = in.getX() + ((float)k / (float)(N - 1)) * in.getWidth();
            const float y = in.getY() + (v / mMaxDb) * in.getHeight();
            if (k == 0) line.startNewSubPath(x, y);
            else        line.lineTo(x, y);
        }
        auto smooth = line.createPathWithRoundedCorners(7.0f); // round kinks for a fluid trace
        juce::Path area = smooth;
        area.lineTo(in.getRight(), in.getY());
        area.lineTo(in.getX(), in.getY());
        area.closeSubPath();
        juce::ColourGradient fill(mAccent.withAlpha(0.40f), in.getX(), in.getY(),
                                  mAccent.withAlpha(0.02f), in.getX(), in.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(area);
        g.setColour(mAccent);
        g.strokePath(smooth, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        auto pill = juce::Rectangle<float>(86.0f, 20.0f).withPosition(in.getRight() - 92.0f, in.getY() + 7.0f);
        g.setColour(juce::Colour(0xff0f121a).withAlpha(0.72f));
        g.fillRoundedRectangle(pill, 6.0f);
        g.setColour(colors::cardBorder);
        g.drawRoundedRectangle(pill, 6.0f, 1.0f);
        auto pc = pill.reduced(9.0f, 0.0f);
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(9.0f, fonts::Bold, 0.12f));
        g.drawText(mLabel, pc, juce::Justification::centredLeft);
        g.setColour(mAccent);
        g.setFont(fonts::mono(11.0f, fonts::Medium));
        g.drawText((mCur < 0.05f ? juce::String("0.0") : "-" + juce::String(mCur, 1)) + " dB",
                   pc, juce::Justification::centredRight);
    }

private:
    std::array<float, N> mHist{};
    int mW = 0;
    float mCur = 0.0f, mMaxDb = 14.0f, mPushSm = 0.0f;
    juce::Colour mAccent = colors::accent;
    juce::String mLabel = "GR";
};

//==============================================================================
// Gate waveform scope: draws the live (or captured) INPUT envelope as a faint
// grey area and the GATED OUTPUT envelope as a luminous accent area + line, plus
// a dashed THRESHOLD line. This is what makes the gate's dynamics visible — you
// watch the input cross the threshold and the output open/close behind it.
class GateScope : public juce::Component
{
public:
    static constexpr int N = 132; // history length — kept short so features stay wide & readable

    void setAccent(juce::Colour c) { mAccent = c; repaint(); }
    void setThreshold01(float t) { mThr01 = juce::jlimit(0.0f, 1.0f, t); }
    void setBypassed(bool b) { if (b != mBypassed) { mBypassed = b; repaint(); } }

    // Live feed: one (input, output) pair per UI tick, each already mapped to 0..1.
    void push(float in01, float out01)
    {
        mIn[(size_t)mW] = juce::jlimit(0.0f, 1.0f, in01);
        mOut[(size_t)mW] = juce::jlimit(0.0f, 1.0f, out01);
        mW = (mW + 1) % N;
        if (mFill < N) ++mFill;
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        RigLookAndFeel::drawWell(g, getLocalBounds().toFloat().reduced(0.5f));
        auto in = getLocalBounds().toFloat().reduced(9.0f);

        if (mBypassed)
            return; // keep the well blank; the panel draws the BYPASSED scrim

        if (mFill > 1)
        {
            const int n = mFill;
            const int start = (mFill < N) ? 0 : mW; // oldest sample index
            auto at = [&](const std::array<float, N> &buf, int k) { return buf[(size_t)((start + k) % N)]; };
            auto xOf = [&](int k) { return in.getX() + (n > 1 ? (float)k / (float)(n - 1) : 0.0f) * in.getWidth(); };
            auto yOf = [&](float v) { return in.getY() + (1.0f - juce::jlimit(0.0f, 1.0f, v)) * in.getHeight(); };

            // INPUT envelope — faint grey filled area (the live dynamics coming in).
            juce::Path inArea;
            inArea.startNewSubPath(in.getX(), in.getBottom());
            for (int k = 0; k < n; ++k) inArea.lineTo(xOf(k), yOf(at(mIn, k)));
            inArea.lineTo(in.getRight(), in.getBottom());
            inArea.closeSubPath();
            g.setColour(colors::captionDim.withAlpha(0.20f));
            g.fillPath(inArea);

            // OUTPUT (gated) envelope — luminous accent area + stroke.
            juce::Path outLine;
            for (int k = 0; k < n; ++k)
            {
                const float x = xOf(k), y = yOf(at(mOut, k));
                if (k == 0) outLine.startNewSubPath(x, y); else outLine.lineTo(x, y);
            }
            juce::Path outArea = outLine;
            outArea.lineTo(in.getRight(), in.getBottom());
            outArea.lineTo(in.getX(), in.getBottom());
            outArea.closeSubPath();
            juce::ColourGradient grad(mAccent.withAlpha(0.36f), in.getX(), in.getY(),
                                      mAccent.withAlpha(0.02f), in.getX(), in.getBottom(), false);
            g.setGradientFill(grad);
            g.fillPath(outArea);
            g.setColour(mAccent);
            g.strokePath(outLine, juce::PathStrokeType(2.0f));

            // THRESHOLD — dashed line + "THR" tag.
            const float ty = yOf(mThr01);
            g.setColour(colors::textDim.withAlpha(0.85f));
            for (float x = in.getX(); x < in.getRight(); x += 8.0f)
                g.fillRect(x, ty - 0.5f, juce::jmin(4.0f, in.getRight() - x), 1.0f);
            g.setColour(colors::text2.withAlpha(0.9f));
            g.setFont(fonts::mono(9.0f, fonts::SemiBold));
            g.drawText("THR", juce::Rectangle<float>(in.getRight() - 30.0f, ty - 14.0f, 28.0f, 11.0f),
                       juce::Justification::centredRight);
        }
    }

private:
    std::array<float, N> mIn{}, mOut{};
    int mW = 0, mFill = 0;
    float mThr01 = 0.5f;
    bool mBypassed = false;
    juce::Colour mAccent = colors::green;
};

//==============================================================================
class GatePanel : public BlockPanel
{
public:
    explicit GatePanel(juce::AudioProcessorValueTreeState &apvts) : BlockPanel("NOISE GATE")
    {
        const std::pair<const char *, const char *> defs[] = {
            {"gateThresh", "Threshold"}, {"gateRange", "Range"},
            {"gateAttack", "Attack"},    {"gateHold", "Hold"},
            {"gateRelease", "Release"},  {"gateLook", "Lookahead"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }

        mLive.setAccent(colors::green);
        addAndMakeVisible(mLive);
    }

    // Editor timer feed. inDb = detector input level, gainDb = gate gain (<=0 dB),
    // thrDb = open threshold. Drives the live scope and the OPEN/CLOSED pill.
    void pushActivity(float inDb, float gainDb, float thrDb, float dt)
    {
        juce::ignoreUnused(dt);
        mCurDb = -gainDb; // attenuation (>=0) for the state pill

        mLive.setThreshold01(map01(thrDb));
        mLive.push(map01(inDb), map01(inDb + gainDb));

        const bool nowOpen = mCurDb < 1.5f;
        if (nowOpen != mShownOpen) { mShownOpen = nowOpen; repaint(); }
    }

    void setLowLatency(bool ll)
    {
        if (mLowLat == ll) return;
        mLowLat = ll;
        if (mKnobs.size() >= 6) mKnobs[5]->setEnabled(!ll);
        repaint();
    }

    // gateOn off: freeze/blank the scope so it doesn't show the forced-open
    // passthrough as a live "OPEN" gate (the base draws the BYPASSED scrim).
    void onBypassChanged(bool b) override { mLive.setBypassed(b); }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        // State pill (header-right). Fixed min-width so the dot never moves;
        // label centred in the remaining slot. Reads OPEN / CLOSED / BYP.
        const bool open = !mBypassed && mCurDb < 1.5f;
        const bool active = !mBypassed;
        auto pill = mStatePill.toFloat();
        g.setColour(open ? colors::green.withAlpha(0.14f) : colors::tile);
        g.fillRoundedRectangle(pill, 7.0f);
        g.setColour(open ? colors::green.withAlpha(0.5f) : colors::outline);
        g.drawRoundedRectangle(pill, 7.0f, 1.0f);
        auto pc = pill.reduced(11.0f, 0.0f);
        auto dot = juce::Rectangle<float>(7.0f, 7.0f).withCentre({pc.getX() + 3.5f, pc.getCentreY()});
        g.setColour(open ? colors::green : (active ? colors::caption : colors::captionDim));
        g.fillEllipse(dot);
        g.setColour(open ? colors::green : (active ? colors::textDim : colors::captionDim));
        g.setFont(fonts::archivo(10.0f, fonts::Bold, 0.12f));
        g.drawText(mBypassed ? "BYP" : (open ? "OPEN" : "CLOSED"),
                   pc.withTrimmedLeft(10), juce::Justification::centred);
    }

    void resized() override
    {
        auto hr = headerArea();
        const int pillW = 90;
        mStatePill = hr.removeFromRight(pillW).withSizeKeepingCentre(pillW, 24);

        auto area = bodyArea().reduced(24, 14);
        auto knobRow = area.removeFromBottom(118);
        area.removeFromBottom(12);
        mLive.setBounds(area); // full-width live scope

        auto row = knobRow.withSizeKeepingCentre(
            juce::jmin(knobRow.getWidth(), 86 * (int)mKnobs.size()), knobRow.getHeight());
        const int w = row.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
            k->setBounds(row.removeFromLeft(w).reduced(7, 10));
    }

private:
    // Map a dB level onto the scope's 0..1 vertical axis (-90 dB floor .. 0 dB top).
    static float map01(float db) { return juce::jlimit(0.0f, 1.0f, (db + 90.0f) / 90.0f); }

    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    GateScope mLive;
    juce::Rectangle<int> mStatePill;
    float mCurDb = 0.0f;
    bool mLowLat = false, mShownOpen = true;
};

//==============================================================================
class CompPanel : public BlockPanel
{
public:
    explicit CompPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("COMPRESSOR / BOOST")
    {
        const std::pair<const char *, const char *> defs[] = {
            {"compSustain", "Sustain"}, {"compAttack", "Attack"},
            {"compLevel", "Level"},     {"compBoost", "Boost"},
            {"compCharacter", "Character"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }

        addAndMakeVisible(mGr);
        addAndMakeVisible(mIn);
        addAndMakeVisible(mOut);
        addChildComponent(mCurve); // DETAIL view only

        // Sustain knob drives the curve's threshold marker.
        mKnobs[0]->slider().onValueChange = [this] {
            mCurve.setSustain((float)mKnobs[0]->slider().getValue());
        };
        mCurve.setSustain((float)mKnobs[0]->slider().getValue());

        // Voicing selector (Clean / OTA / Opto / FET) -> compMode param. A dropdown
        // (the box stays visible; the menu opens below it via the global combo options).
        mModeBox.addItemList({"Clean", "OTA", "Opto", "FET"}, 1);
        mModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "compMode", mModeBox);
        mModeBox.onChange = [this] { updateCurveShape(); };
        addAndMakeVisible(mModeBox);
        updateCurveShape();

        mDetail.setButtonText("DETAIL");
        mDetail.getProperties().set("pill", true);
        mDetail.setClickingTogglesState(true);
        mDetail.onClick = [this] { setDetail(mDetail.getToggleState()); };
        addAndMakeVisible(mDetail);

        setDetail(false);
    }

    // Editor timer feeds: GR + IN + OUT.
    void pushGr(float grDb, float dt) { mGr.push(grDb, dt); }
    void pushIn(float db, float dt) { mIn.push(db, dt); mCurve.setInputDb(db); }
    void pushOut(float db, float dt) { mOut.push(db, dt); }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);
        // IN / OUT captions + dB readouts around the mini-meters.
        auto drawMeterLabels = [&](juce::Rectangle<int> col, const char *name, float db)
        {
            g.setColour(colors::caption);
            g.setFont(fonts::archivo(9.0f, fonts::SemiBold, 0.08f));
            g.drawText(name, col.removeFromTop(12), juce::Justification::centred);
            g.setColour(colors::textDim);
            g.setFont(fonts::mono(9.5f, fonts::Medium));
            g.drawText(juce::String(db, 1), col.removeFromBottom(12), juce::Justification::centred);
        };
        drawMeterLabels(mInLabelCol, "IN", mIn.shownDb());
        drawMeterLabels(mOutLabelCol, "OUT", mOut.shownDb());
    }

    void resized() override
    {
        // Header-right: mode pills + DETAIL toggle.
        auto hr = headerArea();
        mDetail.setBounds(hr.removeFromRight(62).withSizeKeepingCentre(62, 26));
        hr.removeFromRight(10);
        const int mw = juce::jmin(120, hr.getWidth());
        mModeBox.setBounds(hr.removeFromRight(mw).withSizeKeepingCentre(mw, 26));

        auto area = bodyArea().reduced(24, 14);

        // Bottom: knob row (centre) + IN/OUT meters (right).
        auto bottom = area.removeFromBottom(118);
        auto ioCol = bottom.removeFromRight(96);
        ioCol.removeFromLeft(18); // gap + (border drawn by divider look)
        auto inC = ioCol.removeFromLeft(ioCol.getWidth() / 2);
        mInLabelCol = inC.reduced(2, 0);
        mIn.setBounds(inC.withSizeKeepingCentre(9, 74).translated(0, -1));
        mOutLabelCol = ioCol.reduced(2, 0);
        mOut.setBounds(ioCol.withSizeKeepingCentre(9, 74).translated(0, -1));

        auto row = bottom.withSizeKeepingCentre(
            juce::jmin(bottom.getWidth(), 104 * (int)mKnobs.size()), bottom.getHeight());
        const int w = row.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
            k->setBounds(row.removeFromLeft(w).reduced(5, 0));

        area.removeFromBottom(12);
        // Top: optional transfer-curve well + GR analyser.
        if (mDetail.getToggleState())
        {
            mCurve.setBounds(area.removeFromLeft(224));
            area.removeFromLeft(16);
        }
        mGr.setBounds(area);
    }

private:
    void setDetail(bool on)
    {
        mCurve.setVisible(on);
        mDetail.setToggleState(on, juce::dontSendNotification);
        resized();
    }

    void updateCurveShape()
    {
        const int idx = juce::jmax(0, mModeBox.getSelectedItemIndex());
        const auto v = nam_rig::CompBlock::voicingFor((nam_rig::CompBlock::Mode)idx);
        mCurve.setShape(v.ratio, v.kneeDb);
    }

    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    GrAnalyser mGr;
    PeakMeter mIn, mOut;
    juce::Rectangle<int> mInLabelCol, mOutLabelCol;
    CompCurve mCurve;
    juce::ComboBox mModeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mModeAtt;
    juce::TextButton mDetail;
};

//==============================================================================
// Metal footswitch: a hex-nut housing + chrome cap, with a soft accent glow
// disc drawn BEHIND the housing when engaged (so the opaque housing never
// covers it). Component is sized ~80px to give the glow room.
class Footswitch : public juce::Button
{
public:
    Footswitch() : juce::Button({}) { setClickingTogglesState(true); }
    void setAccent(juce::Colour c) { mAccent = c; repaint(); }
    void setLit(bool l) { if (mLit != l) { mLit = l; repaint(); } }

    void paintButton(juce::Graphics &g, bool, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();

        // Hex-nut housing (pointy-top hexagon).
        const float hs = 52.0f;
        auto hb = juce::Rectangle<float>(hs, hs).withCentre(c);
        auto pt = [&](float fx, float fy) { return juce::Point<float>(hb.getX() + fx * hs, hb.getY() + fy * hs); };
        juce::Path hex;
        hex.startNewSubPath(pt(0.50f, 0.0f));
        hex.lineTo(pt(0.93f, 0.25f)); hex.lineTo(pt(0.93f, 0.75f));
        hex.lineTo(pt(0.50f, 1.0f));  hex.lineTo(pt(0.07f, 0.75f));
        hex.lineTo(pt(0.07f, 0.25f)); hex.closeSubPath();
        juce::ColourGradient hg(juce::Colour(0xff454c57), hb.getX(), hb.getY(),
                                juce::Colour(0xff1b1f25), hb.getRight(), hb.getBottom(), false);
        dither::fillPath(g, hg, hex);

        // Chrome cap geometry (needed before the glow so the cap area can be
        // clipped OUT of the glow -- guarantees the glow only touches the hex).
        const float cs = down ? 32.0f : 34.0f;
        auto cap = juce::Rectangle<float>(cs, cs).withCentre(c);

        // Accent bloom around the cap. Clip = whole component MINUS the cap
        // (even-odd), so the glow can bloom out across the hex and beyond, but
        // physically cannot land on the chrome cap.
        if (getToggleState() && mLit)
        {
            juce::Graphics::ScopedSaveState s(g);
            juce::Path ring;
            ring.addRectangle(b);
            ring.addEllipse(cap);
            ring.setUsingNonZeroWinding(false); // even-odd -> component minus cap
            g.reduceClipRegion(ring);
            auto disc = juce::Rectangle<float>(50.0f, 50.0f).withCentre(c);
            fx::glowEllipse(g, disc, mAccent, 32, 0.32f, 16, 0.46f);
        }

        // Chrome cap.
        juce::ColourGradient cg(juce::Colour(0xffd8dce2), c.x, cap.getY() + cs * 0.32f,
                                juce::Colour(0xff565d67), c.x, cap.getBottom(), true);
        dither::fillEllipse(g, cg, cap);
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.drawEllipse(cap.reduced(0.5f), 1.0f);
    }

private:
    juce::Colour mAccent = colors::accent;
    bool mLit = false;
};

// Monochrome silkscreen art glyph per drive family, drawn from the design's
// 64x48-viewBox paths, scaled/centred into box.
// Per-MODEL silkscreen art, one motif per real pedal. The category
// representatives keep the exact glyphs shipped today (sparkle, hill, rat, round
// wave); the off-category models get their own. All art is authored on a shared
// 64x48 viewBox. type: 1 boost, 2 od, 3 dist, 4 fuzz.
inline void paintDriveGlyph(juce::Graphics &g, int type, int model,
                            juce::Rectangle<float> box, juce::Colour col)
{
    enum Motif { None, Spark, Hill, Rat, RoundWave, Germanium, Sd1, Klon, Bluesbreaker, BigMuff };
    Motif motif = None;
    switch (type)
    {
    case 1: motif = (model == 0) ? Germanium : Spark; break;                       // Range '65 / EP Boost
    case 2: motif = (model == 1) ? Sd1 : (model == 2) ? Klon
                  : (model == 3) ? Bluesbreaker : Hill; break;                     // SD-1 / Klon / BB / Green Drive
    case 3: motif = Rat; break;                                                    // Black Rodent
    case 4: motif = (model == 1) ? BigMuff : RoundWave; break;                     // Violet Ram / Round Fuzz
    default: return;
    }

    const float s = juce::jmin(box.getWidth() / 64.0f, box.getHeight() / 48.0f);
    const auto xf = juce::AffineTransform::scale(s)
                        .translated(box.getCentreX() - 32.0f * s, box.getCentreY() - 24.0f * s);
    g.setColour(col);

    // ---- multi-part motifs (mixed fill + stroke): drawn directly with the xf ----
    if (motif == Bluesbreaker) // combo amp + speaker
    {
        juce::Path lines;
        lines.addRoundedRectangle(15.0f, 9.0f, 34.0f, 30.0f, 3.0f);           // cabinet
        lines.startNewSubPath(15, 16); lines.lineTo(49, 16);                  // control-panel divider
        lines.addEllipse(24.5f, 19.5f, 15.0f, 15.0f);                         // speaker (cx32 cy27 r7.5)
        g.strokePath(lines, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), xf);
        juce::Path dots;                                                      // 3 panel knobs + speaker dust-cap
        dots.addEllipse(21.0f - 1.1f, 12.5f - 1.1f, 2.2f, 2.2f);
        dots.addEllipse(27.0f - 1.1f, 12.5f - 1.1f, 2.2f, 2.2f);
        dots.addEllipse(33.0f - 1.1f, 12.5f - 1.1f, 2.2f, 2.2f);
        dots.addEllipse(32.0f - 1.8f, 27.0f - 1.8f, 3.6f, 3.6f);
        g.fillPath(dots, xf);
        return;
    }
    if (motif == Sd1) // Super Drive -> Robbie's vector design (Super-Drive Design 2.svg): outline shield + FILLED bolt
    {
        juce::Path shield; // path2 "Shield" -- stroked outline
        shield.startNewSubPath(31.69f, 3.36f);
        shield.cubicTo(33.22f, 3.27f, 45.80f, 7.37f, 45.80f, 7.37f);
        shield.cubicTo(46.69f, 7.63f, 49.80f, 8.38f, 50.09f, 9.13f);
        shield.cubicTo(50.44f, 9.78f, 49.62f, 17.81f, 49.62f, 17.81f);
        shield.cubicTo(49.15f, 22.96f, 47.54f, 30.48f, 44.75f, 34.83f);
        shield.cubicTo(43.27f, 37.13f, 41.01f, 39.34f, 38.85f, 41.01f);
        shield.cubicTo(37.72f, 41.89f, 33.65f, 44.60f, 32.36f, 44.77f);
        shield.cubicTo(31.44f, 44.89f, 25.76f, 41.48f, 23.85f, 39.74f);
        shield.cubicTo(22.15f, 38.20f, 20.77f, 36.77f, 19.52f, 34.83f);
        shield.cubicTo(16.39f, 29.96f, 15.29f, 23.24f, 14.60f, 17.58f);
        shield.cubicTo(14.60f, 17.58f, 13.81f, 9.81f, 14.18f, 9.13f);
        shield.cubicTo(14.49f, 8.33f, 17.56f, 7.62f, 18.47f, 7.34f);
        shield.cubicTo(18.47f, 7.34f, 31.69f, 3.36f, 31.69f, 3.36f);
        shield.closeSubPath();
        g.strokePath(shield, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), xf);

        juce::Path bolt; // path1 "Bolt" -- filled solid with the accent
        bolt.startNewSubPath(34.60f, 8.62f);
        bolt.lineTo(34.15f, 12.21f);
        bolt.lineTo(32.81f, 19.38f);
        bolt.lineTo(38.63f, 20.05f);
        bolt.lineTo(32.90f, 30.35f);
        bolt.lineTo(29.45f, 35.95f);
        bolt.lineTo(31.69f, 23.86f);
        bolt.lineTo(25.41f, 23.18f);
        bolt.lineTo(28.46f, 18.26f);
        bolt.closeSubPath();
        g.fillPath(bolt, xf);
        return;
    }

    // ---- single-path motifs: pre-transform the path (keeps the existing glyphs
    //      pixel-for-pixel) then fill or stroke ----
    juce::Path p;
    bool fill = false;
    float w = 2.0f * s + 0.4f;
    switch (motif)
    {
    case Spark: // EP Boost -> twin sparkle (filled)
        p.startNewSubPath(28, 7);  p.quadraticTo(28, 23, 44, 23); p.quadraticTo(28, 23, 28, 39);
        p.quadraticTo(28, 23, 12, 23); p.quadraticTo(28, 23, 28, 7); p.closeSubPath();
        p.startNewSubPath(47, 9);  p.quadraticTo(47, 16, 54, 16); p.quadraticTo(47, 16, 47, 23);
        p.quadraticTo(47, 16, 40, 16); p.quadraticTo(47, 16, 47, 9); p.closeSubPath();
        fill = true; break;
    case Hill: // Green Drive -> smooth mid-hump
        p.startNewSubPath(8, 33); p.cubicTo(19, 33, 23, 15, 32, 15); p.cubicTo(41, 15, 45, 33, 56, 33);
        break;
    case Rat: // Black Rodent -> rodent
        p.startNewSubPath(53, 27); p.cubicTo(46, 21, 36, 20, 27, 22); p.cubicTo(18, 24, 12, 26, 12, 30);
        p.cubicTo(12, 34, 19, 36, 28, 34); p.cubicTo(38, 33, 47, 32, 53, 27); p.closeSubPath();
        p.startNewSubPath(36, 21); p.cubicTo(33, 13, 39, 8, 42, 10); p.cubicTo(45, 12, 46, 16, 44, 21);
        p.startNewSubPath(14, 31); p.cubicTo(8, 33, 3, 31, 3, 25); p.cubicTo(3, 21, 5, 18, 8, 17);
        break;
    case RoundWave: // Round Fuzz -> hard-clipped round wave
        p.startNewSubPath(4, 24); p.cubicTo(5, 14, 9, 13, 13, 13); p.lineTo(20, 13);
        p.cubicTo(24, 13, 25, 35, 29, 35); p.lineTo(36, 35); p.cubicTo(40, 35, 41, 13, 45, 13);
        p.lineTo(52, 13); p.cubicTo(56, 13, 58, 19, 60, 24);
        p.applyTransform(juce::AffineTransform(0.82f, 0.0f, 5.76f, 0.0f, 1.0f, 0.0f));
        break;
    case Germanium: // Range '65 -> germanium transistor (TO can + 3 legs), from range-65.svg
        p.startNewSubPath(22, 27); p.lineTo(22, 19); p.quadraticTo(22, 13, 28, 13);
        p.lineTo(36, 13); p.quadraticTo(42, 13, 42, 19); p.lineTo(42, 27); p.closeSubPath(); // can body
        p.startNewSubPath(27, 27); p.lineTo(25, 39);                                          // leg
        p.startNewSubPath(32, 27); p.lineTo(32, 39);                                          // leg
        p.startNewSubPath(37, 27); p.lineTo(39, 39);                                          // leg
        break;
    case Klon: // Gold Horse -> simple bow & arrow (archer motif, minimal line art)
        p.startNewSubPath(40, 9); p.quadraticTo(14, 16, 14, 24); p.quadraticTo(14, 32, 40, 39); // bow limb
        p.startNewSubPath(40, 9); p.lineTo(40, 39);                                              // bowstring
        p.startNewSubPath(16, 24); p.lineTo(54, 24);                                             // arrow shaft
        p.startNewSubPath(54, 24); p.lineTo(48, 20); p.startNewSubPath(54, 24); p.lineTo(48, 28);// arrowhead
        break;
    case BigMuff: // Violet Ram -> pi symbol (a touch bolder)
        p.startNewSubPath(13, 16); p.cubicTo(22, 14, 42, 14, 51, 16);
        p.startNewSubPath(24, 16); p.cubicTo(24, 25, 23, 31, 20, 35);
        p.startNewSubPath(41, 16); p.lineTo(41, 32); p.cubicTo(41, 35, 43, 35, 46, 34);
        w = 3.0f * s + 0.4f; break;
    default: return;
    }
    p.applyTransform(xf);
    if (fill)
        g.fillPath(p);
    else
        g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

// Fully custom drive picker -- a real Component (NOT juce::PopupMenu), so it has no
// OS window and therefore none of PopupMenu's window-level headaches (opaque/white
// clear, drop-shadow box, corner ring, 1x scale). It's added to the SCALED content
// canvas so it scales with the plugin automatically, draws its own rounded panels,
// and shows a two-pane cascade: categories on the left, the hovered category's
// models (two-line: bold name + mono descriptor) on the right.
class DrivePickerOverlay : public juce::Component
{
public:
    struct Model { juce::String name, sub; juce::Colour led; };
    struct Cat   { juce::String name; std::vector<Model> models; };

    std::function<void(int /*type 0..4*/, int /*model*/)> onPick;
    std::function<void()> onDismiss;

    DrivePickerOverlay(std::vector<Cat> cats, int curType, int curModel)
        : mCats(std::move(cats)), mCurType(curType), mCurModel(curModel)
    {
        setWantsKeyboardFocus(true);
        // Open the current category's models by default (so the selection is visible).
        mOpenCat = (curType >= 1 && curType <= (int)mCats.size()) ? curType - 1 : -1;
    }

    // Pill rect expressed in THIS overlay's coordinate space; anchors the menu.
    void setAnchor(juce::Rectangle<int> a) { mAnchor = a; layout(); repaint(); }

    void resized() override { layout(); }
    void mouseMove(const juce::MouseEvent &e) override { updateHover(e.getPosition()); }
    void mouseDrag(const juce::MouseEvent &e) override { updateHover(e.getPosition()); }

    void mouseDown(const juce::MouseEvent &e) override
    {
        const auto p = e.getPosition();
        if (mOpenCat >= 0 && mModelPanel.contains(p))
        {
            const int idx = modelIndexAt(p);
            if (idx >= 0) pick(mOpenCat + 1, idx);
            return; // click on panel padding: keep open
        }
        if (mCatPanel.contains(p))
        {
            const int row = catRowAt(p);
            if (row == kOffRow) { pick(0, 0); return; }
            if (row >= 0)
            {
                if ((int)mCats[(size_t)row].models.size() <= 1) pick(row + 1, 0);
                else { mOpenCat = row; mHoverModel = -1; layout(); repaint(); }
            }
            return;
        }
        dismiss(); // click anywhere outside the panels closes the picker
    }

    bool keyPressed(const juce::KeyPress &k) override
    {
        if (k == juce::KeyPress::escapeKey) { dismiss(); return true; }
        return false;
    }

    void paint(juce::Graphics &g) override
    {
        paintPanel(g, mCatPanel);
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.14f));
        g.drawText("CHOOSE DRIVE",
                   juce::Rectangle<int>(mCatPanel.getX() + 14, mCatPanel.getY() + kPad,
                                        mCatPanel.getWidth() - 28, kHeaderH),
                   juce::Justification::centredLeft);

        paintRow(g, offRowRect(), "Off", mCurType == 0, juce::Colour(), false,
                 mHoverCatRow == kOffRow);
        auto sep = juce::Rectangle<int>(mCatPanel.getX() + 12, catRowY(0) - kSep / 2 - 1,
                                        mCatPanel.getWidth() - 24, 1);
        g.setColour(colors::divider);
        g.fillRect(sep);

        for (int i = 0; i < (int)mCats.size(); ++i)
            paintCatRow(g, i);

        if (mOpenCat >= 0)
        {
            paintPanel(g, mModelPanel);
            const auto &ms = mCats[(size_t)mOpenCat].models;
            for (int i = 0; i < (int)ms.size(); ++i)
                paintModelRow(g, i);
        }
    }

private:
    static constexpr int kHeaderH = 22, kRowH = 38, kModelH = 46, kSep = 13, kPad = 6;
    static constexpr int kCatW = 200;
    static constexpr int kOffRow = -2;

    void layout()
    {
        const int n = (int)mCats.size();
        const int h = kPad + kHeaderH + n * kRowH + kSep + kRowH + kPad;
        int x = mAnchor.getX();
        int y = mAnchor.getY() - h - 4; // float ABOVE the pill so the button stays visible
        if (auto *par = getParentComponent())
        {
            if (y < 4) y = mAnchor.getBottom() + 4; // no room above -> drop below
            x = juce::jlimit(4, juce::jmax(4, par->getWidth() - kCatW - 4), x);
            y = juce::jlimit(4, juce::jmax(4, par->getHeight() - h - 4), y);
        }
        mCatPanel = {x, y, kCatW, h};

        if (mOpenCat >= 0 && mOpenCat < (int)mCats.size())
        {
            const auto &ms = mCats[(size_t)mOpenCat].models;
            int mw = 160;
            for (auto &m : ms)
            {
                mw = juce::jmax(mw, (int)std::ceil(juce::GlyphArrangement::getStringWidth(
                                        fonts::archivo(15.0f, fonts::Bold), m.name)) + 44);
                mw = juce::jmax(mw, (int)std::ceil(juce::GlyphArrangement::getStringWidth(
                                        fonts::mono(11.0f), m.sub)) + 44);
            }
            const int mh = kPad * 2 + (int)ms.size() * kModelH;
            int mx = mCatPanel.getRight(); // flush against the category panel (no gap)
            int my = catRowY(mOpenCat) - kPad;
            if (auto *par = getParentComponent())
            {
                if (mx + mw + 4 > par->getWidth()) mx = mCatPanel.getX() - mw; // flip to the left, still flush
                my = juce::jlimit(4, juce::jmax(4, par->getHeight() - mh - 4), my);
            }
            mModelPanel = {mx, my, mw, mh};
        }
        else
            mModelPanel = {};
    }

    // Order top-to-bottom: header, Off, divider, then the category rows.
    int offRowY() const { return mCatPanel.getY() + kPad + kHeaderH; }
    int catRowY(int i) const
    {
        return mCatPanel.getY() + kPad + kHeaderH + kRowH + kSep + i * kRowH;
    }
    juce::Rectangle<int> catRowRect(int i) const
    {
        return {mCatPanel.getX() + 4, catRowY(i), mCatPanel.getWidth() - 8, kRowH};
    }
    juce::Rectangle<int> offRowRect() const
    {
        return {mCatPanel.getX() + 4, offRowY(), mCatPanel.getWidth() - 8, kRowH};
    }
    juce::Rectangle<int> modelRowRect(int i) const
    {
        return {mModelPanel.getX() + 4, mModelPanel.getY() + kPad + i * kModelH,
                mModelPanel.getWidth() - 8, kModelH};
    }
    int catRowAt(juce::Point<int> p) const
    {
        for (int i = 0; i < (int)mCats.size(); ++i)
            if (catRowRect(i).contains(p)) return i;
        if (offRowRect().contains(p)) return kOffRow;
        return -1;
    }
    int modelIndexAt(juce::Point<int> p) const
    {
        if (mOpenCat < 0) return -1;
        const auto &ms = mCats[(size_t)mOpenCat].models;
        for (int i = 0; i < (int)ms.size(); ++i)
            if (modelRowRect(i).contains(p)) return i;
        return -1;
    }

    void updateHover(juce::Point<int> p)
    {
        const int row = mCatPanel.contains(p) ? catRowAt(p) : -1;
        if (row >= 0 && row != mOpenCat && !mCats[(size_t)row].models.empty())
        {
            mOpenCat = row;
            layout();
        }
        mHoverCatRow = row;
        mHoverModel = (mOpenCat >= 0 && mModelPanel.contains(p)) ? modelIndexAt(p) : -1;
        repaint();
    }

    void pick(int type, int model)
    {
        if (onPick) onPick(type, model);
        dismiss();
    }
    void dismiss()
    {
        if (mDismissing) return;
        mDismissing = true;
        juce::Component::SafePointer<DrivePickerOverlay> self(this);
        juce::MessageManager::callAsync([self]() mutable {
            if (self == nullptr) return;
            auto cb = self->onDismiss; // local copy: invoking it deletes the overlay
            if (cb) cb();              // (and thus the member std::function) -- safe via the copy
        });
    }

    void paintPanel(juce::Graphics &g, juce::Rectangle<int> r)
    {
        auto b = r.toFloat();
        const float rad = 9.0f;
        g.setColour(juce::Colour(0xff1c2027)); // opaque base
        g.fillRoundedRectangle(b, rad);
        juce::ColourGradient grad(juce::Colour(0xff232830), b.getTopLeft(),
                                  juce::Colour(0xff171a21), b.getBottomLeft(), false);
        dither::fillRoundedRectangle(g, grad, b, rad);
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.drawRoundedRectangle(b.reduced(0.5f), rad, 1.0f);
    }

    void drawChevron(juce::Graphics &g, juce::Rectangle<int> row)
    {
        const float x = (float)row.getRight() - 14.0f, cy = (float)row.getCentreY(), s = 4.0f;
        juce::Path p;
        p.startNewSubPath(x - s * 0.5f, cy - s);
        p.lineTo(x + s * 0.5f, cy);
        p.lineTo(x - s * 0.5f, cy + s);
        g.setColour(colors::caption);
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    // Single-line row (categories + Off). dot = accent dot when selected.
    void paintRow(juce::Graphics &g, juce::Rectangle<int> r, const juce::String &name,
                  bool selected, juce::Colour, bool hasChevron, bool hovered)
    {
        if (hovered)
        {
            g.setColour(colors::tileSel);
            g.fillRoundedRectangle(r.toFloat().reduced(1.0f), 7.0f);
        }
        auto tx = r.reduced(14, 0);
        if (selected)
        {
            auto dot = juce::Rectangle<float>(6.0f, 6.0f)
                           .withCentre({(float)r.getX() + 11.0f, (float)r.getCentreY()});
            g.setColour(colors::accent);
            g.fillEllipse(dot);
            tx = tx.withTrimmedLeft(10);
        }
        g.setColour(colors::textBright);
        g.setFont(fonts::archivo(14.0f, fonts::SemiBold));
        g.drawText(name, tx, juce::Justification::centredLeft);
        if (hasChevron) drawChevron(g, r);
    }

    void paintCatRow(juce::Graphics &g, int i)
    {
        paintRow(g, catRowRect(i), mCats[(size_t)i].name, mCurType == i + 1, juce::Colour(),
                 true, mHoverCatRow == i || mOpenCat == i);
    }

    void paintModelRow(juce::Graphics &g, int i)
    {
        const auto &m = mCats[(size_t)mOpenCat].models[(size_t)i];
        auto r = modelRowRect(i);
        if (mHoverModel == i)
        {
            g.setColour(colors::tileSel);
            g.fillRoundedRectangle(r.toFloat().reduced(1.0f), 7.0f);
        }
        auto tx = r.reduced(14, 0);
        const bool sel = (mCurType == mOpenCat + 1 && mCurModel == i);
        if (sel)
        {
            auto dot = juce::Rectangle<float>(6.0f, 6.0f)
                           .withCentre({(float)r.getX() + 11.0f, (float)r.getCentreY()});
            g.setColour(m.led);
            g.fillEllipse(dot);
            tx = tx.withTrimmedLeft(10);
        }
        auto top = tx.removeFromTop(tx.getHeight() / 2 + 3);
        g.setColour(colors::textBright);
        g.setFont(fonts::archivo(15.0f, fonts::Bold));
        g.drawText(m.name, top, juce::Justification::bottomLeft);
        g.setColour(colors::caption);
        g.setFont(fonts::mono(11.0f));
        g.drawText(m.sub, tx.withTrimmedTop(1), juce::Justification::topLeft);
    }

    std::vector<Cat> mCats;
    int mCurType, mCurModel;
    int mOpenCat = -1, mHoverCatRow = -1, mHoverModel = -1;
    bool mDismissing = false;
    juce::Rectangle<int> mAnchor, mCatPanel, mModelPanel;
};

// One drive "stomp" in the pedalboard. Top-to-bottom: Type (category), a model
// selector (when the category has several models), the model's descriptive
// subtitle, the authentic knobs, the model's mode switch (treble-boost Range),
// then the footswitch. Only the controls that pedal actually has are shown.
class DrivePedal : public juce::Component
{
public:
    DrivePedal(juce::AudioProcessorValueTreeState &apvts, int slot)
        : mApvts(apvts), mSlot(slot)
    {
        const juce::String p = "drv" + juce::String(slot + 1);

        // Hidden parameter bridges (the pill + menu drive these; they keep the
        // params synced with host automation / presets).
        mType.addItemList({"Off", "Boost", "Overdrive", "Distortion", "Fuzz"}, 1);
        mTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, p + "Type", mType);
        mType.onChange = [this] { refresh(); };
        addChildComponent(mType);

        mModel.onChange = [this] { onModelPicked(); };
        addChildComponent(mModel);

        // Boost-only tonal Range (Treble/Mid/Full), shown when the model has one.
        mRangeSeg = std::make_unique<SegmentedControl>(apvts, p + "bRange",
                                                       juce::StringArray{"Treble", "Mid", "Full"});
        addChildComponent(*mRangeSeg);

        // Fuzz-only bias-starved Gate (Off/Gate), shown when the model has a gate.
        mGateSeg = std::make_unique<SegmentedControl>(apvts, p + "fGate",
                                                      juce::StringArray{"Off", "Gate"});
        addChildComponent(*mGateSeg);

        mOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "On", mOn);
        mOn.onClick = [this] { refresh(); };
        addAndMakeVisible(mOn);

        mDrive = std::make_unique<LabeledKnob>(apvts, p + "oDrive", "Drive"); // rebound per type
        mTone  = std::make_unique<LabeledKnob>(apvts, p + "oTone", "Tone");
        mLevel = std::make_unique<LabeledKnob>(apvts, p + "oLevel", "Level");
        for (auto *k : {mDrive.get(), mTone.get(), mLevel.get()})
        {
            k->setRotationReadout(10.0);
            k->setCaptionHeight(13);
            addAndMakeVisible(*k);
        }

        refresh();
    }

    void refresh()
    {
        const juce::String pid = "drv" + juce::String(mSlot + 1);
        // Type is read from the param (authoritative), not the hidden bridge combo,
        // so a fresh selection always takes effect immediately. Keep the combo in
        // sync for the APVTS attachment, without re-triggering onChange.
        const int type = curTypeIndex();
        if (mType.getSelectedItemIndex() != type)
            mType.setSelectedItemIndex(type, juce::dontSendNotification);
        const bool on = mApvts.getRawParameterValue(pid + "On")->load() >= 0.5f;
        if (type != mLastType)
            populateModels(type);
        const int count = nam_rig::DriveBlock::modelCount((nam_rig::DriveBlock::Kind)type);
        int model = (count > 1) ? (int)mApvts.getRawParameterValue(pid + "bModel")->load() : 0;
        if (count > 1 && model >= count) { model = 0; setModelParam(0); }
        if (count > 1 && mModel.getSelectedItemIndex() != model)
            mModel.setSelectedItemIndex(juce::jmax(0, model), juce::dontSendNotification);
        mActive = (type != 0) && on;
        mOn.setLit(mActive); // keep the footswitch glow in sync every tick
        if (type == mLastType && on == mLastOn && model == mLastModel) { repaint(); return; }
        mLastType = type; mLastOn = on; mLastModel = model;
        configure();
        resized();
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        const auto pair = colors::driveModelAccent(mLastType, mLastModel);
        const juce::Colour tint = mActive ? pair.tint : juce::Colour(0xff343a43);

        // Enclosure: neutral base + tint wash, pre-composited into one opaque
        // gradient so the whole fill is dithered in a single pass (a separate
        // translucent wash on top would re-introduce 8-bit banding).
        const juce::Colour encTop = juce::Colour(0xff262b33).overlaidWith(tint.withAlpha(0.20f));
        const juce::Colour encBot = juce::Colour(0xff15181d).overlaidWith(tint.withAlpha(0.05f));
        juce::ColourGradient base(encTop, 0.0f, b.getY(),
                                  encBot, 0.0f, b.getBottom(), false);
        dither::fillRoundedRectangle(g, base, b, 16.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawRoundedRectangle(b.reduced(0.5f).withTrimmedBottom(b.getHeight() - 2.0f), 16.0f, 1.0f);
        g.setColour(tint.withAlpha(mActive ? 0.55f : 0.34f));
        g.drawRoundedRectangle(b, 16.0f, 1.5f);

        // Header: kind label (left) + jewel LED (right).
        g.setColour(mActive ? pair.accent : colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::Bold, 0.13f));
        g.drawText(mKindStr, mHeaderRect, juce::Justification::centredLeft);
        auto jewel = juce::Rectangle<float>(13.0f, 13.0f).withCentre(
            {(float)mHeaderRect.getRight() - 6.5f, (float)mHeaderRect.getCentreY()});
        // Jewel uses the model's LED colour (= accent for every pedal except those
        // that override it, e.g. Violet Ram = chrome body with a violet LED).
        if (mActive) fx::glowEllipse(g, jewel, pair.led, 13, 0.6f, 6, 0.55f);
        g.setColour(mActive ? pair.led : juce::Colour(0xff2a2f37));
        g.fillEllipse(jewel);

        // Silkscreen art glyph (the Fuzz Gate toggle now lives up in the knob row,
        // so the art always shows for an active pedal type).
        if (mLastType != 0)
            paintDriveGlyph(g, mLastType, mLastModel, mGlyphRect.toFloat(),
                            mActive ? pair.led.withAlpha(0.85f) : juce::Colour(0xff5a616b));

        // Model-name pill + sub.
        auto pill = mPillRect.toFloat();
        g.setColour(mActive ? juce::Colours::white.withAlpha(0.06f) : juce::Colour(0xff22262d));
        g.fillRoundedRectangle(pill, 12.0f);
        g.setColour(tint.withAlpha(0.45f));
        g.drawRoundedRectangle(pill, 12.0f, 1.0f);
        g.setColour(colors::textBright);
        g.setFont(fonts::archivo(23.0f, fonts::Bold));
        g.drawText(mModelStr, pill, juce::Justification::centred);
        g.setColour(juce::Colour(0xff7a808a));
        g.setFont(fonts::mono(11.5f));
        g.drawText(mSubStr, mSubRect, juce::Justification::centred);
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (mPillRect.contains(e.getPosition()))
            showMenu();
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced(16, 18);
        mHeaderRect = a.removeFromTop(16);
        a.removeFromTop(14);
        auto knobRow = a.removeFromTop(96);
        a.removeFromTop(6);

        auto fs = a.removeFromBottom(80);
        mOn.setBounds(fs.withSizeKeepingCentre(120, 120)); // larger: room for the glow to disperse
        a.removeFromBottom(4);

        a.removeFromTop(8); // drop the model name down a touch
        auto pillRow = a.removeFromTop(40);
        const int pw = juce::jlimit(90, getWidth() - 24,
            (int)std::ceil(juce::GlyphArrangement::getStringWidth(
                fonts::archivo(23.0f, fonts::Bold), mModelStr.isEmpty() ? "Off" : mModelStr)) + 34);
        mPillRect = pillRow.withSizeKeepingCentre(pw, 38);
        a.removeFromTop(5);
        mSubRect = a.removeFromTop(16);

        // Glyph centred vertically in the space left between the subtitle bottom
        // and the footswitch top.
        const int gh = juce::jlimit(40, 64, a.getHeight());
        mGlyphRect = a.withSizeKeepingCentre(a.getWidth(), juce::jmin(gh, a.getHeight()))
                         .translated(0, 14); // nudge down toward the footswitch

        LabeledKnob *ks[3] = {mDrive.get(), mTone.get(), mLevel.get()};
        int nVis = 0;
        for (auto *k : ks) if (k->isVisible()) ++nVis;
        // Round Fuzz's Off/Gate toggle rides up in the knob row as a trailing
        // column (instead of sitting down in the silkscreen area), so the art shows.
        const bool gateInRow = mGateSeg->isVisible();
        const int nSlots = nVis + (gateInRow ? 1 : 0);
        if (nSlots > 0)
        {
            // Non-gate pedals keep their exact shipped spacing (2 knobs = 16, else 0);
            // only Round Fuzz's extra gate column introduces a gap at 3 slots.
            const int gap = gateInRow ? 8 : ((nVis == 2) ? 16 : 0);
            const int kw = juce::jmin(78, juce::jmax(1, (knobRow.getWidth() - gap * (nSlots - 1)) / nSlots));
            auto grp = knobRow.withSizeKeepingCentre(kw * nSlots + gap * (nSlots - 1), knobRow.getHeight());
            bool first = true;
            for (auto *k : ks)
                if (k->isVisible())
                {
                    if (!first) grp.removeFromLeft(gap);
                    k->setBounds(grp.removeFromLeft(kw).reduced(3, 0));
                    first = false;
                }
            if (gateInRow) // Off/Gate as a vertical 2-cell column, level with the knob dials
            {
                if (!first) grp.removeFromLeft(gap);
                auto cell = grp.removeFromLeft(kw);
                mGateSeg->setVertical(true);
                const int sw = juce::jlimit(36, mGateSeg->idealCellWidth(), kw);
                const int sh = 58;
                int dialCy = knobRow.getCentreY();
                for (auto *k : ks)
                    if (k->isVisible())
                        dialCy = k->getBounds().getY() + k->slider().getBounds().getCentreY();
                mGateSeg->setBounds(cell.getCentreX() - sw / 2, dialCy - sh / 2, sw, sh);
            }
        }

        // Range '65 (Boost): Treble/Mid/Full stacked vertically (equal size) to
        // the right of the Boost knob, without moving the knob. Width clamps to
        // the room available so it always fits inside the pedal.
        if (mRangeSeg->isVisible())
        {
            mRangeSeg->setVertical(true);
            auto kb = mDrive->getBounds();
            const int dialCy = kb.getY() + mDrive->slider().getBounds().getCentreY(); // dial centre
            const int avail = (getWidth() - 16) - (kb.getRight() + 10);
            const int sw = juce::jlimit(36, mRangeSeg->idealCellWidth(), avail);
            const int sh = 58;
            mRangeSeg->setBounds(kb.getRight() + 10, dialCy - sh / 2, sw, sh);
        }
        // (Fuzz Off/Gate toggle is now positioned up in the knob row above.)
    }

private:
    int curModel() const
    {
        return juce::jmax(0,
            (int)mApvts.getRawParameterValue("drv" + juce::String(mSlot + 1) + "bModel")->load());
    }

    void showMenu()
    {
        using DB = nam_rig::DriveBlock;
        if (mPicker != nullptr) { mPicker.reset(); return; } // click pill again = close

        // Build the category/model tree for the custom picker.
        std::vector<DrivePickerOverlay::Cat> cats;
        for (int t = 1; t <= 4; ++t)
        {
            static const char *names[] = {"Off", "Boost", "Overdrive", "Distortion", "Fuzz"};
            const auto cat = (DB::Kind)t;
            DrivePickerOverlay::Cat c;
            c.name = names[t];
            const int n = DB::modelCount(cat);
            for (int i = 0; i < n; ++i)
                c.models.push_back({DB::modelName(cat, i), DB::modelSub(cat, i),
                                    colors::driveModelAccent(t, i).led});
            cats.push_back(std::move(c));
        }

        // Host the overlay on the SCALED content canvas so it scales with the plugin
        // and can position anywhere without clipping. The editor scales mContent via an
        // AffineTransform, so the canvas is the nearest ancestor with a non-identity
        // transform. At 1:1 zoom that transform is identity, so fall back to our
        // grandparent (DrivePedal -> DrivePanel -> mContent), which is the same canvas.
        juce::Component *host = nullptr;
        for (auto *c = getParentComponent(); c != nullptr; c = c->getParentComponent())
            if (!c->getTransform().isIdentity()) { host = c; break; }
        if (host == nullptr)
        {
            host = getParentComponent();                       // DrivePanel
            if (host != nullptr) host = host->getParentComponent(); // mContent
        }
        if (host == nullptr) return; // safety

        auto overlay = std::make_unique<DrivePickerOverlay>(std::move(cats), curTypeIndex(), curModel());
        auto *ov = overlay.get();
        ov->onPick = [this](int t, int m) { setTypeModel(t, m); };
        ov->onDismiss = [this] { mPicker.reset(); };
        mPicker = std::move(overlay);

        host->addAndMakeVisible(*ov);
        ov->setBounds(host->getLocalBounds());
        ov->setAnchor(host->getLocalArea(this, mPillRect)); // pill rect in host coords
        ov->grabKeyboardFocus();
    }

    // Selection is param-authoritative (mirrors the modulation panel's
    // ParameterAttachment fix): write the APVTS params directly with a full change
    // gesture and let refresh() read them back, so a re-pick can never get "stuck"
    // on a hidden ComboBox that didn't fire its change.
    void setTypeModel(int type, int model)
    {
        const juce::String p = "drv" + juce::String(mSlot + 1);
        setChoiceParam(p + "Type", type);
        if (nam_rig::DriveBlock::modelCount((nam_rig::DriveBlock::Kind)type) > 1)
            setChoiceParam(p + "bModel", model);
        refresh();
    }
    void setChoiceParam(const juce::String &id, int idx)
    {
        if (auto *prm = mApvts.getParameter(id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1((float)juce::jmax(0, idx)));
            prm->endChangeGesture();
        }
    }
    int curTypeIndex() const
    {
        return juce::jlimit(0, 4,
            (int)mApvts.getRawParameterValue("drv" + juce::String(mSlot + 1) + "Type")->load());
    }

    void populateModels(int type)
    {
        const auto cat = (nam_rig::DriveBlock::Kind)type;
        const int n = nam_rig::DriveBlock::modelCount(cat);
        mModel.clear(juce::dontSendNotification);
        for (int i = 0; i < n; ++i)
            mModel.addItem(nam_rig::DriveBlock::modelName(cat, i), i + 1);
    }
    void onModelPicked()
    {
        setModelParam(mModel.getSelectedItemIndex());
        refresh();
    }
    void setModelParam(int idx)
    {
        if (auto *prm = mApvts.getParameter("drv" + juce::String(mSlot + 1) + "bModel"))
            prm->setValueNotifyingHost(prm->convertTo0to1((float)juce::jmax(0, idx)));
    }
    void configure()
    {
        static const char *names[] = {"DRIVE", "BOOST", "OVERDRIVE", "DISTORTION", "FUZZ"};
        const juce::String p = "drv" + juce::String(mSlot + 1);
        const int type = mType.getSelectedItemIndex();
        const int model = juce::jmax(0, mModel.getSelectedItemIndex());
        const auto cat = (nam_rig::DriveBlock::Kind)type;
        switch (type)
        {
        case 1:
            mDrive->rebind(mApvts, p + "bDrive"); mDrive->setCaption("Boost");
            mDrive->setVisible(true); mTone->setVisible(false); mLevel->setVisible(false);
            break;
        case 2:
        {
            // Per-model control names match each real pedal: Gold Horse (Klon,
            // model 2) = Gain/Treble/Output; Breaker Drive (Bluesbreaker, model 3)
            // = Gain/Tone/Volume; the TS-family OD models (Green Drive / Super Drive)
            // keep Drive/Tone/Level. Only the captions differ -- the params
            // (oDrive/oTone/oLevel) are unchanged, so presets/automation are intact.
            const bool klon = (model == 2);
            const bool bb   = (model == 3); // Breaker Drive (Marshall Bluesbreaker)
            mDrive->rebind(mApvts, p + "oDrive"); mDrive->setCaption((klon || bb) ? "Gain" : "Drive");
            mTone->rebind(mApvts, p + "oTone");   mTone->setCaption(klon ? "Treble" : "Tone");
            mLevel->rebind(mApvts, p + "oLevel"); mLevel->setCaption(klon ? "Output" : (bb ? "Volume" : "Level"));
            mDrive->setVisible(true); mTone->setVisible(true); mLevel->setVisible(true);
            break;
        }
        case 3:
            mDrive->rebind(mApvts, p + "dDrive"); mDrive->setCaption("Dist");
            mTone->rebind(mApvts, p + "dTone");   mTone->setCaption("Filter");
            mLevel->rebind(mApvts, p + "dLevel"); mLevel->setCaption("Volume");
            mDrive->setVisible(true); mTone->setVisible(true); mLevel->setVisible(true);
            break;
        case 4:
        {
            // Round Fuzz = Fuzz / Volume (no tone). The Big Muff (Violet Ram, model 1)
            // is filed under Fuzz but exposes its own Sustain / Tone / Volume
            // (the only fuzz with a Tone knob -- bound to fTone, shown only here).
            const bool muff = (model == 1);
            mDrive->rebind(mApvts, p + "fDrive"); mDrive->setCaption(muff ? "Sustain" : "Fuzz");
            mTone->rebind(mApvts, p + "fTone");   mTone->setCaption("Tone");
            mLevel->rebind(mApvts, p + "fLevel"); mLevel->setCaption("Volume");
            mDrive->setVisible(true); mTone->setVisible(muff); mLevel->setVisible(true);
            break;
        }
        default:
            mDrive->setVisible(false); mTone->setVisible(false); mLevel->setVisible(false);
            break;
        }
        // Knob value-ring colour follows the pedal: accent when engaged, neutral
        // grey when bypassed (so the colour drains like the rest of the pedal).
        const juce::Colour knobAcc = mActive ? colors::driveModelAccent(type, model).led
                                             : juce::Colour(0xff5a616b);
        for (auto *k : {mDrive.get(), mTone.get(), mLevel.get()})
            k->setAccent(knobAcc);
        mModelStr = type == 0 ? juce::String("Off")
                              : juce::String(nam_rig::DriveBlock::modelName(cat, model));
        mKindStr = names[juce::jlimit(0, 4, type)];
        mSubStr = type == 0 ? juce::String("select a pedal")
                            : juce::String(nam_rig::DriveBlock::modelSub(cat, model));
        // Range/Gate segmented controls take the pedal's own accent (the LED colour),
        // so e.g. Round Fuzz's Off/Gate is red, not the global amber. They grey
        // themselves via setEnabled(false) when the pedal is bypassed.
        const juce::Colour segAcc = colors::driveModelAccent(type, model).led;
        mRangeSeg->setAccent(segAcc);
        mRangeSeg->setVisible(nam_rig::DriveBlock::modelHasRange(cat, model));
        mRangeSeg->setEnabled(mActive); // drains its colour when the pedal is bypassed
        mGateSeg->setAccent(segAcc);
        mGateSeg->setVisible(nam_rig::DriveBlock::modelHasGate(cat, model)); // Round Fuzz only
        mGateSeg->setEnabled(mActive);
        mOn.setAccent(colors::driveModelAccent(type, model).led); // footswitch glow tracks the LED (= accent, except Violet Ram = violet)
        mOn.setLit(mActive);
    }

    juce::AudioProcessorValueTreeState &mApvts;
    int mSlot;
    juce::ComboBox mType, mModel;
    std::unique_ptr<SegmentedControl> mRangeSeg;
    std::unique_ptr<SegmentedControl> mGateSeg; // fuzz bias-starved gate (Off/Gate)
    Footswitch mOn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mTypeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mOnAtt;
    std::unique_ptr<LabeledKnob> mDrive, mTone, mLevel;
    juce::String mModelStr{"Off"}, mKindStr{"DRIVE"}, mSubStr{"select a pedal"};
    juce::Rectangle<int> mHeaderRect, mGlyphRect, mPillRect, mSubRect;
    int mLastType = -1, mLastModel = -1;
    bool mLastOn = true, mActive = false;
    std::unique_ptr<DrivePickerOverlay> mPicker; // custom drive picker (in-canvas, not a PopupMenu)
};

//==============================================================================
// DrivePanel - the drive section as a left-to-right PEDALBOARD: three stomps in
// series (IN left, OUT right) joined by patch cables, contrasting with the
// rack-style (top-down) amp + mod sections.
class DrivePanel : public BlockPanel
{
public:
    explicit DrivePanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("DRIVE")
    {
        for (int s = 0; s < nam_rig::DriveBlock::kSlots; ++s)
        {
            mPedals[(size_t)s] = std::make_unique<DrivePedal>(apvts, s);
            addAndMakeVisible(*mPedals[(size_t)s]);
        }
        mAutoGain.getProperties().set("pill", true);
        mAutoGain.setButtonText("Auto Gain");
        addAndMakeVisible(mAutoGain);
        mAutoGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, "driveAutoGain", mAutoGain);
    }

    void refresh()
    {
        for (auto &p : mPedals)
            if (p) p->refresh();
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);
        // Simple joiner lines between pedals (mid-height).
        g.setColour(juce::Colour(0xff3a414c));
        for (auto &c : mJoiners)
            g.fillRect(c);
    }

    void resized() override
    {
        // Auto-Gain pill in the header-right.
        auto hr = headerArea();
        mAutoGain.setBounds(hr.removeFromRight(96).withSizeKeepingCentre(96, 26));

        auto area = bodyArea().reduced(26, 24);
        const int n = nam_rig::DriveBlock::kSlots;
        const int joiner = 40;
        const int pedalW = juce::jmin(268, (area.getWidth() - joiner * (n - 1)) / n);
        const int total = pedalW * n + joiner * (n - 1);
        area = area.withSizeKeepingCentre(total, area.getHeight());
        const float midY = (float)area.getCentreY();
        mJoiners.clear();
        for (int s = 0; s < n; ++s)
        {
            mPedals[(size_t)s]->setBounds(area.removeFromLeft(pedalW));
            if (s < n - 1)
            {
                auto gap = area.removeFromLeft(joiner);
                mJoiners.push_back(juce::Rectangle<float>((float)gap.getX() + 6.0f, midY - 1.0f,
                                                          (float)gap.getWidth() - 12.0f, 2.0f));
            }
        }
    }

private:
    std::array<std::unique_ptr<DrivePedal>, (size_t)nam_rig::DriveBlock::kSlots> mPedals;
    juce::ToggleButton mAutoGain;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mAutoGainAtt;
    std::vector<juce::Rectangle<float>> mJoiners;
};

//==============================================================================
// CalPanel - global INPUT calibration, opened from the header INPUT button.
// Sets the level of the incoming signal (your interface's dBu at 0 dBFS) so the
// shared pre-amp section (drive rack, gate, comp) is driven consistently; each
// amp still lands on its own capture level automatically (CalNorm.h splits the
// correction into this global stage + a per-rig residual). Shown as an overlay
// over the block-panel area; Close (or selecting any block) hides it.
class CalPanel : public BlockPanel
{
public:
    std::function<void()> onClose;

    explicit CalPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("INPUT CALIBRATION")
    {
        mEnable.setButtonText("Calibrate input level");
        addAndMakeVisible(mEnable);
        mEnableAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, "calEnable", mEnable);

        mDbu.setSliderStyle(juce::Slider::LinearHorizontal);
        mDbu.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
        mDbu.setTextValueSuffix(" dBu");
        addAndMakeVisible(mDbu);
        mDbuAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, "calDbu", mDbu);

        mHint.setJustificationType(juce::Justification::topLeft);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        mHint.setText("Tells the rig what your interface sends at 0 dBFS so the drive pedals "
                      "and both amps are driven at a consistent level. Each amp still hits its "
                      "own captured sweet spot automatically. Reference is 12 dBu, so the "
                      "default leaves your sound unchanged - raise or lower it to match a "
                      "hotter or quieter interface.",
                      juce::dontSendNotification);
        addAndMakeVisible(mHint);

        mClose.setButtonText("Close");
        mClose.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(mClose);
    }

    void resized() override
    {
        auto area = contentArea();
        auto bottom = area.removeFromBottom(30);
        mClose.setBounds(bottom.removeFromRight(96).reduced(0, 3));
        mEnable.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        mDbu.setBounds(area.removeFromTop(28).removeFromLeft(340));
        area.removeFromTop(14);
        mHint.setBounds(area.removeFromTop(96));
    }

private:
    juce::ToggleButton mEnable;
    juce::Slider mDbu;
    juce::Label mHint;
    juce::TextButton mClose;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mEnableAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mDbuAtt;
};

//==============================================================================
class AmpPanel : public BlockPanel
{
public:
    AmpPanel(NamRigProcessor &proc, int rig)
        : BlockPanel(rig == 0 ? "AMP A - NEURAL MODEL" : "AMP B - NEURAL MODEL"),
          mProc(proc), mRig(rig)
    {
        setHeaderRight(rig == 0 ? "RIG A" : "RIG B");

        mModelName.setFont(fonts::archivo(22.0f, fonts::Bold));
        mModelName.setColour(juce::Label::textColourId, colors::textBright);
        mModelName.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(mModelName);

        mInfo.setColour(juce::Label::textColourId, colors::textDim);
        mInfo.setFont(fonts::mono(12.0f));
        mInfo.setInterceptsMouseClicks(false, false);
        mInfo.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(mInfo);

        auto initCombo = [this](juce::ComboBox &box, const juce::StringArray &items,
                                const char *paramId,
                                std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> &att)
        {
            box.addItemList(items, 1); // must match the parameter StringArray order
            addAndMakeVisible(box);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                mProc.apvts, paramId, box);
        };
        initCombo(mLiveAa, {"Off", "2x", "4x", "8x", "16x", "32x"},
                  rig == 0 ? "oversample" : "oversampleB", mLiveAtt);
        initCombo(mOfflineAa, {"Same as live", "8x", "16x", "32x"},
                  rig == 0 ? "offlineAA" : "offlineAAB", mOfflineAtt);

        mNorm = std::make_unique<ToggleSwitch>(mProc.apvts, "normalize");
        addAndMakeVisible(*mNorm);
    }

    // Called from the editor timer.
    void refresh()
    {
        const bool loaded = mProc.isModelLoaded(mRig);
        const bool a2 = loaded && mProc.isA2Model(mRig);
        if (loaded != mLoaded) { mLoaded = loaded; repaint(); }

        // "Capped at 4x" note when Low Latency holds back a higher live AA setting.
        const bool ll = mProc.apvts.getRawParameterValue("lowLatency")->load() >= 0.5f;
        const int liveChoice = (int)mProc.apvts.getRawParameterValue(
            mRig == 0 ? "oversample" : "oversampleB")->load();
        const bool capped = ll && liveChoice > 2; // >4x
        if (capped != mAaCapped) { mAaCapped = capped; repaint(); }
        mModelName.setText(loaded ? mProc.getModelName(mRig) : "No model loaded",
                           juce::dontSendNotification);

        const bool aaAvailable = !loaded || a2;
        mLiveAa.setEnabled(aaAvailable);
        mOfflineAa.setEnabled(aaAvailable);
        mNorm->setEnabled(mProc.hasLoudness());

        juce::String info;
        if (!loaded)
            info = "Load a .nam model to bring the amp online.";
        else if (!a2)
            info = "Standard model - anti-aliasing needs an A2 model.";
        else
        {
            const int engaged = mProc.engagedFactor(mRig);
            info = engaged > 0 ? "Engaged at " + juce::String(engaged) + "x"
                               : "Passthrough";
            info << "  |  PDC " << mProc.getLatencySamples() << " smp";
        }
        const float calDb = mProc.calibrationGainDb(mRig);
        if (calDb != 0.0f)
            info << "  |  cal " << (calDb > 0 ? "+" : "") << juce::String(calDb, 1) << " dB";
        const float normDb = mProc.normalizationGainDb(mRig);
        mNormalized = (normDb != 0.0f);
        if (mNormalized)
            info << "  |  norm " << (normDb > 0 ? "+" : "") << juce::String(normDb, 1) << " dB";
        // Self-heal flag: a block hit a non-finite sample and was auto-reset. Shows
        // it happened (and which block) so a silent NaN never goes unnoticed.
        const auto nanN = mProc.nanRecoveries();
        if (nanN > 0)
            info << "  |  (!) recovered NaN x" << (int)nanN
                 << " (" << mProc.lastNanBlock() << ")";
        if (info != mInfo.getText())
            mInfo.setText(info, juce::dontSendNotification);
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!mLoaderRect.contains(e.getPosition()))
            return;
        mChooser = std::make_unique<juce::FileChooser>("Select a NAM model", juce::File{}, "*.nam");
        mChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser &fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      mProc.loadModel(fc.getResult(), mRig);
                              });
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);
        auto sub = [&](juce::Rectangle<int> r, const juce::String &t)
        {
            g.setColour(colors::caption);
            g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
            g.drawText(t, r, juce::Justification::topLeft);
        };

        // Left column: caption + loader pill.
        sub(mCaptionL, "NEURAL AMP MODEL");
        auto lp = mLoaderRect.toFloat();
        juce::ColourGradient lg(juce::Colour(0xff23272e), lp.getTopLeft(),
                                juce::Colour(0xff1b1f25), lp.getBottomLeft(), false);
        dither::fillRoundedRectangle(g, lg, lp, 9.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(lp, 9.0f, 1.0f);
        auto dot = juce::Rectangle<float>(9.0f, 9.0f).withCentre({lp.getX() + 18.0f, lp.getCentreY()});
        if (mLoaded) fx::glowEllipse(g, dot, colors::green, 9, 0.5f, 4, 0.4f);
        g.setColour(mLoaded ? colors::green : colors::caption);
        g.fillEllipse(dot);
        g.setColour(colors::text);
        g.setFont(fonts::archivo(13.0f, fonts::SemiBold));
        g.drawText(juce::String::fromUTF8("Load NAM model\xE2\x80\xA6"), lp.withTrimmedLeft(32).toNearestInt(),
                   juce::Justification::centredLeft);

        // Left column: tag pills.
        auto tagPill = [&](juce::Rectangle<int> &row, const juce::String &t)
        {
            const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(fonts::mono(11.0f), t)) + 22;
            auto r = row.removeFromLeft(w).toFloat();
            row.removeFromLeft(7);
            g.setColour(juce::Colour(0xff191c21));
            g.fillRoundedRectangle(r, 6.0f);
            g.setColour(colors::cardBorder);
            g.drawRoundedRectangle(r, 6.0f, 1.0f);
            g.setColour(juce::Colour(0xff7a808a));
            g.setFont(fonts::mono(11.0f));
            g.drawText(t, r, juce::Justification::centred);
        };
        auto tags = mTagsRect;
        if (mLoaded) tagPill(tags, mProc.isA2Model(mRig) ? "A2 model" : "standard");
        if (mNormalized) tagPill(tags, "normalized");

        // Right column: divider + caption + AA labels + normalize label.
        g.setColour(colors::divider);
        g.fillRect(mDivX, mCaptionR.getY(), 1, getHeight() - mCaptionR.getY() - 24);
        sub(mCaptionR, juce::String::fromUTF8("ANTI-ALIAS \xC2\xB7 QUALITY"));
        g.setColour(colors::textDim);
        g.setFont(fonts::archivo(11.0f));
        g.drawText("Live AA Oversampling", mLiveLabel, juce::Justification::centredLeft);
        g.drawText("Offline (render) AA", mOffLabel, juce::Justification::centredLeft);
        if (mAaCapped)
        {
            g.setColour(colors::accent);
            g.setFont(fonts::mono(10.0f, fonts::Medium));
            g.drawText(juce::String::fromUTF8("Capped at 4\xC3\x97 \xC2\xB7 Low Latency on"),
                       juce::Rectangle<int>(mLiveAa.getX(), mLiveAa.getBottom() + 3,
                                            mLiveAa.getWidth(), 13),
                       juce::Justification::centredLeft);
        }
        g.setColour(colors::text);
        g.setFont(fonts::archivo(12.5f));
        g.drawText(juce::String::fromUTF8("Normalize output \xC2\xB7 \xE2\x88\x92" "18 LUFS"), mNormLabel,
                   juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = bodyArea().reduced(26, 26);
        auto left = area.removeFromLeft((int)(area.getWidth() * 0.52f));
        area.removeFromLeft(34);
        mDivX = area.getX() - 17;
        auto right = area;

        // Left column.
        mCaptionL = left.removeFromTop(14);
        left.removeFromTop(12);
        mLoaderRect = left.removeFromTop(44).removeFromLeft(208);
        left.removeFromTop(18);
        mModelName.setBounds(left.removeFromTop(30));
        left.removeFromTop(6);
        mInfo.setBounds(left.removeFromTop(40));
        mTagsRect = left.removeFromBottom(26);

        // Right column.
        mCaptionR = right.removeFromTop(14);
        right.removeFromTop(16);
        mLiveLabel = right.removeFromTop(16);
        right.removeFromTop(6);
        mLiveAa.setBounds(right.removeFromTop(36));
        right.removeFromTop(18);
        mOffLabel = right.removeFromTop(16);
        right.removeFromTop(6);
        mOfflineAa.setBounds(right.removeFromTop(36));
        right.removeFromTop(22);
        auto normRow = right.removeFromTop(24);
        mNorm->setBounds(normRow.removeFromLeft(42));
        normRow.removeFromLeft(12);
        mNormLabel = normRow;
    }

private:
    NamRigProcessor &mProc;
    int mRig = 0;
    juce::Label mModelName, mInfo;
    juce::ComboBox mLiveAa, mOfflineAa;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mLiveAtt, mOfflineAtt;
    std::unique_ptr<ToggleSwitch> mNorm;
    std::unique_ptr<juce::FileChooser> mChooser;
    juce::Rectangle<int> mCaptionL, mLoaderRect, mTagsRect, mCaptionR, mLiveLabel, mOffLabel, mNormLabel;
    int mDivX = 0;
    bool mLoaded = false, mNormalized = false, mAaCapped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpPanel)
};

//==============================================================================
class EqPanel : public BlockPanel
{
public:
    EqPanel(juce::AudioProcessorValueTreeState &apvts, int rig)
        : BlockPanel(rig == 0 ? "GRAPHIC EQ A - PRE-CAB" : "GRAPHIC EQ B - PRE-CAB"),
          mApvts(apvts), mRigB(rig != 0)
    {
        static const char *idsA[] = {"eq62", "eq125", "eq250", "eq500",
                                     "eq1k", "eq2k", "eq4k", "eq8k"};
        static const char *idsB[] = {"rigBeq62", "rigBeq125", "rigBeq250", "rigBeq500",
                                     "rigBeq1k", "rigBeq2k", "rigBeq4k", "rigBeq8k"};
        const char *const *ids = (rig == 0) ? idsA : idsB;
        for (int b = 0; b < 8; ++b)
            mBandIds[b] = ids[b]; // remembered for the Flat button
        static const char *captions[] = {"62.5", "125", "250", "500",
                                         "1k", "2k", "4k", "8k"};
        for (int b = 0; b < 8; ++b)
        {
            auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearVertical,
                                                         juce::Slider::NoTextBox);
            slider->onValueChange = [this] { repaint(); }; // redraw response curve
            slider->setDoubleClickReturnValue(true, 0.0); // double-click = flat
            addChildComponent(*slider); // invisible: the nodes are the UI
            mAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, ids[b], *slider));
            mSliders.push_back(std::move(slider));
            mCaptions[b] = captions[b];
        }

        addAndMakeVisible(mFlatBtn);
        mFlatBtn.onClick = [this]
        {
            for (const char *id : mBandIds)
                if (auto *p = mApvts.getParameter(id))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->getDefaultValue());
                    p->endChangeGesture();
                }
        };
    }

    void resized() override
    {
        auto hr = headerArea();
        mFlatBtn.setBounds(hr.removeFromRight(64).withSizeKeepingCentre(64, 30));
        hr.removeFromRight(14);
        setHeaderRight(juce::String("RIG ") + (mRigB ? "B" : "A"));

        mGraph = bodyArea().reduced(26, 18);
    }

    float bandX(int b) const
    {
        return (float)mGraph.getX()
               + ((float)b + 0.5f) / (float)mSliders.size() * (float)mGraph.getWidth();
    }

    void mouseDown(const juce::MouseEvent &e) override { dragBand(e); }
    void mouseDrag(const juce::MouseEvent &e) override { dragBand(e); }

    void dragBand(const juce::MouseEvent &e)
    {
        if (mDrag < 0)
        {
            // pick the nearest band by x within the graph.
            float best = 1.0e9f;
            for (int b = 0; b < (int)mSliders.size(); ++b)
            {
                const float dx = std::abs((float)e.x - bandX(b));
                if (dx < best) { best = dx; mDrag = b; }
            }
            if (best > (float)mGraph.getWidth() / (float)mSliders.size()) { mDrag = -1; return; }
        }
        if (mDrag < 0) return;
        const float prop = juce::jlimit(0.0f, 1.0f,
            (float)(mGraph.getBottom() - e.y) / (float)mGraph.getHeight());
        auto &s = *mSliders[(size_t)mDrag];
        s.setValue(s.proportionOfLengthToValue(prop), juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent &) override { mDrag = -1; }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);
        RigLookAndFeel::drawWell(g, mGraph.toFloat());
        auto in = mGraph.toFloat().reduced(1.0f);

        // ONE dB->y mapping shared by grid, curve and nodes so the curve always
        // meets the dots. The box spans exactly +/-12 dB (the band range), which
        // is the same full-height scale the draggable nodes already use — so the
        // curve moves to the points, the points don't move.
        const float centreY = in.getCentreY();
        const float pxPer12 = in.getHeight() * 0.5f;
        auto dbToY = [&](double db)
        {
            return juce::jlimit(in.getY(), in.getBottom(),
                                centreY - (float)(db / 12.0) * pxPer12);
        };

        // dB grid: +/-12 frame the edges, 0 dB centre is labelled, +/-6 are
        // faint unlabelled guide lines.
        struct Grid { float db; juce::Colour c; const char *lbl; };
        const Grid grid[] = {{ 12.0f, juce::Colour(0xff21262d), "+12"},
                             {  6.0f, juce::Colour(0xff1b2027), nullptr},
                             {  0.0f, juce::Colour(0xff363d47), "0 dB"},
                             { -6.0f, juce::Colour(0xff1b2027), nullptr},
                             {-12.0f, juce::Colour(0xff21262d), "-12"}};
        for (auto &gr : grid)
        {
            const float y = juce::jlimit(in.getY() + 0.5f, in.getBottom() - 0.5f, dbToY(gr.db));
            g.setColour(gr.c);
            g.fillRect(in.getX(), y, in.getWidth(), 1.0f);
            if (gr.lbl != nullptr)
            {
                g.setColour(juce::Colour(0xff5a616b));
                g.setFont(fonts::mono(8.5f, fonts::SemiBold));
                const float ly = juce::jlimit(in.getY() + 1.0f, in.getBottom() - 13.0f, y - 7.0f);
                g.drawText(gr.lbl, juce::Rectangle<float>(in.getX() + 6.0f, ly, 32.0f, 14.0f),
                           juce::Justification::centredLeft);
            }
        }

        // Response curve from the SAME RBJ designs the DSP runs.
        const double fs = 48000.0;
        std::array<Biquad, EqBlock::kNumBands> filters;
        for (int b = 0; b < EqBlock::kNumBands; ++b)
            filters[(size_t)b] = Biquad::peaking(fs, EqBlock::kBandHz[(size_t)b], EqBlock::kQ,
                                                 mSliders[(size_t)b]->getValue());
        // Frequency axis warped so each band centre lands exactly under its
        // node slot (octave-spaced bands -> kNumBands octaves across the width,
        // band 0 at the first slot). This is what makes the curve peaks sit
        // under the dots horizontally; the dots themselves don't move.
        juce::Path curve;
        const double fBase = EqBlock::kBandHz[0];
        const double octaves = (double)EqBlock::kNumBands;
        const int n = juce::jmax(2, (int)in.getWidth());
        for (int x = 0; x < n; ++x)
        {
            const double frac = (double)x / (double)(n - 1);
            const double f = fBase * std::pow(2.0, octaves * frac - 0.5);
            double db = 0.0;
            for (auto &bi : filters)
                if (!bi.isIdentity())
                    db += 20.0 * std::log10(std::max(bi.magnitudeAt(fs, f), 1.0e-6));
            const float y = dbToY(db);
            const float px = in.getX() + (float)x;
            if (x == 0) curve.startNewSubPath(px, y);
            else curve.lineTo(px, y);
        }

        // Center-anchored bipolar fill: the glow lives between the curve and the
        // 0 dB centre line (boosts fill up, cuts fill down) rather than flooding
        // the whole well to the bottom. Brightest at the curve, fading to nothing
        // at the centre line via a symmetric top/centre/bottom gradient.
        juce::Path fillPath = curve;
        fillPath.lineTo(in.getRight(), centreY);
        fillPath.lineTo(in.getX(), centreY);
        fillPath.closeSubPath();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(fillPath);
            juce::ColourGradient grad(colors::accent.withAlpha(0.36f), in.getX(), in.getY(),
                                      colors::accent.withAlpha(0.36f), in.getX(), in.getBottom(), false);
            grad.addColour(0.5, colors::accent.withAlpha(0.03f));
            g.setGradientFill(grad);
            g.fillRect(in);
        }
        g.setColour(colors::accent);
        g.strokePath(curve, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));

        // Node handles + value + frequency labels. Smaller dots, no ring — the
        // accent glow is the halo.
        for (int b = 0; b < (int)mSliders.size(); ++b)
        {
            const float x = bandX(b);
            const float db = (float)mSliders[(size_t)b]->getValue();
            const float y = dbToY(db);
            g.setColour(colors::text);
            g.setFont(fonts::mono(9.0f, fonts::SemiBold));
            g.drawText((db >= 0 ? "+" : "") + juce::String(db, 1),
                       juce::Rectangle<float>(x - 24.0f, y - 24.0f, 48.0f, 12.0f),
                       juce::Justification::centred);
            auto dot = juce::Rectangle<float>(9.0f, 9.0f).withCentre({x, y});
            fx::glowEllipse(g, dot, colors::accent, 11, 0.55f, 5, 0.5f);
            g.setColour(colors::accent);
            g.fillEllipse(dot);
            g.setColour(colors::caption);
            g.setFont(fonts::archivo(9.5f, fonts::SemiBold, 0.03f));
            g.drawText(mCaptions[b], juce::Rectangle<float>(x - 26.0f, in.getBottom() - 16.0f, 52.0f, 12.0f),
                       juce::Justification::centred);
        }
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    const char *mBandIds[8]{};
    juce::String mCaptions[8];
    bool mRigB = false;
    std::vector<std::unique_ptr<juce::Slider>> mSliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> mAtts;
    juce::TextButton mFlatBtn{"Flat"};
    juce::Rectangle<int> mGraph;
    int mDrag = -1;
};

//==============================================================================
// One cab "lane" inside the combined CAB panel (no box/title of its own — the
// parent draws the single "CAB" frame). Holds the IR name/tag, response graph and
// the per-rig Low/High cut. Dims + disables when its cab is bypassed/out.
class CabPanel : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    CabPanel(NamRigProcessor &proc, int rig)
        : mProc(proc), mRig(rig)
    {
        // Post-cab cuts live with their cab (per rig).
        mHpf = std::make_unique<LabeledKnob>(mProc.apvts, rig == 0 ? "cabHpf" : "rigBcabHpf", "Low Cut");
        mLpf = std::make_unique<LabeledKnob>(mProc.apvts, rig == 0 ? "cabLpf" : "rigBcabLpf", "High Cut");
        addAndMakeVisible(*mHpf);
        addAndMakeVisible(*mLpf);

        // Per-cab on/off so each cab can be bypassed independently from the panel
        // (the strip's CAB tile toggles both at once). Stays live when dimmed.
        mOn.setButtonText("On");
        mOn.getProperties().set("pill", true);
        addAndMakeVisible(mOn);
        mOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            mProc.apvts, rig == 0 ? "cabOn" : "cabOnB", mOn);
    }

    // Dim the IR graph when the cab's convolution is bypassed or its rig is out.
    // The Low/High cut knobs stay live (they're always-on output filters), so a
    // baked-in-speaker NAM with no IR can still use the cuts.
    void setBypassed(bool b)
    {
        if (b == mDim) return;
        mDim = b;
        repaint();
    }

    void refresh()
    {
        const bool loaded = mProc.isIrLoaded(mRig);
        auto name = loaded ? mProc.getIrName(mRig) : juce::String::fromUTF8("No IR \xC2\xB7 amp runs direct");
        if (loaded != mLoaded || name != mIrName)
        {
            mLoaded = loaded;
            mIrName = name;
            mCharacter = {};
            if (loaded)
            {
                float resp[nam_rig::CabBlock::kResPts];
                if (mProc.getCabResponseDb(resp, mRig))
                    mCharacter = classifyCharacter(resp);
            }
            repaint();
        }
    }

    // Auto tone descriptor (1-2 words) from the IR's per-zone energy, relative to
    // its own average. Pure function of the smoothed response — same data the
    // heat map shows, just named.
    static juce::String classifyCharacter(const float *resp) { return nam_rig::ir::classify(resp); }

    void resized() override
    {
        // Vertical lane: rig label + IR name on top, response well in the middle,
        // Low/High cut knobs at the bottom.
        auto area = getLocalBounds().reduced(14, 10);
        auto top = area.removeFromTop(24);
        mOn.setBounds(top.removeFromRight(46).withSizeKeepingCentre(46, 20));
        top.removeFromRight(8);
        mNameRect = top;

        auto knobs = area.removeFromBottom(88);
        const int kw = 104, gap = 16;
        auto krow = knobs.withSizeKeepingCentre(kw * 2 + gap, knobs.getHeight());
        mHpf->setBounds(krow.removeFromLeft(kw));
        krow.removeFromLeft(gap);
        mLpf->setBounds(krow.removeFromLeft(kw));

        area.removeFromTop(6);
        mRespRect = area.withTrimmedBottom(4);
    }

    void paint(juce::Graphics &g) override
    {
        paintNameRow(g);
        paintResponse(g);
        if (mDim) // IR bypassed / rig out: dim the graph, but the cuts stay live
        {
            g.setColour(colors::panel.withAlpha(0.62f));
            auto s = getLocalBounds();
            s.setBottom(mRespRect.getBottom() + 4);
            g.fillRect(s);
        }
    }

    // Drag an IR straight from the OS file manager onto the cab to load it.
    bool isInterestedInFileDrag(const juce::StringArray &files) override
    {
        for (auto &f : files)
        {
            const auto l = f.toLowerCase();
            if (l.endsWith(".wav") || l.endsWith(".aif") || l.endsWith(".aiff")) return true;
        }
        return false;
    }
    void filesDropped(const juce::StringArray &files, int, int) override
    {
        for (auto &f : files)
        {
            const auto l = f.toLowerCase();
            if (l.endsWith(".wav") || l.endsWith(".aif") || l.endsWith(".aiff"))
            {
                mProc.loadIr(juce::File(f), mRig);
                break;
            }
        }
    }

private:
    void paintNameRow(juce::Graphics &g)
    {
        auto r = mNameRect;
        // Rig tag on the left (A amber, B lane colour) so the unified CAB box
        // still tells the two apart.
        auto tagR = r.removeFromLeft(40);
        g.setColour(mRig == 0 ? colors::titleAccent : colors::laneColour(1));
        g.setFont(fonts::archivo(11.0f, fonts::Bold, 0.08f));
        g.drawText(mRig == 0 ? "A" : "B", tagR, juce::Justification::centredLeft);
        r.removeFromLeft(2);

        if (!mLoaded)
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(11.0f));
            g.drawText(juce::String::fromUTF8("No IR \xC2\xB7 cuts only"), r,
                       juce::Justification::centredLeft);
            return;
        }
        if (mCharacter.isNotEmpty()) // auto tone tag, as a pill on the right
        {
            const int tw = juce::jlimit(40, r.getWidth() / 2, 18 + mCharacter.length() * 7);
            auto tag = r.removeFromRight(tw);
            g.setColour(colors::accent.withAlpha(0.14f));
            g.fillRoundedRectangle(tag.toFloat().reduced(0.0f, 4.0f), 5.0f);
            g.setColour(colors::accent);
            g.setFont(fonts::mono(10.0f, fonts::SemiBold));
            g.drawText(mCharacter, tag, juce::Justification::centred);
            r.removeFromRight(8);
        }
        g.setColour(colors::text);
        g.setFont(fonts::archivo(13.0f, fonts::SemiBold));
        g.drawText(mIrName, r, juce::Justification::centredLeft, true);
    }

    void paintResponse(juce::Graphics &g)
    {
        RigLookAndFeel::drawWell(g, mRespRect.toFloat());
        auto in = mRespRect.toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff262c34));
        g.fillRect(in.getX(), in.getCentreY(), in.getWidth(), 1.0f);

        // The loaded IR's actual magnitude response (centred on its own mean).
        // No IR -> a flat "direct" line at 0 dB.
        float resp[nam_rig::CabBlock::kResPts];
        const bool haveIr = mLoaded && mProc.getCabResponseDb(resp, mRig);

        // Display scale: +/-18 dB across the well half-height.
        constexpr float kSpanDb = 18.0f;
        auto dbToY = [&](float db) {
            return juce::jlimit(in.getY(), in.getBottom(),
                                in.getCentreY() - (db / kSpanDb) * in.getHeight() * 0.5f);
        };

        juce::Path line;
        const int n = juce::jmax(2, (int)in.getWidth());
        const int P = nam_rig::CabBlock::kResPts;
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

        // Tone-zone energy heat map: each guitar band is tinted by how much
        // energy this IR has there RELATIVE to its own average (the curve is
        // mean-centred), so the cab's character reads at a glance — hot lows =
        // thick, hot presence = bright/cutting, hot fizz = harsh.
        struct Zone { double fLo, fHi; const char *lbl; };
        static const Zone zones[] = {
            {40.0, 120.0, "LOWS"}, {120.0, 400.0, "BODY"}, {400.0, 1500.0, "MIDS"},
            {1500.0, 4000.0, "PRESENCE"}, {4000.0, 8000.0, "FIZZ"}};
        const double zfLo = nam_rig::CabBlock::kResFLo, zfHi = nam_rig::CabBlock::kResFHi;
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

        if (!haveIr)
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(11.0f));
            g.drawText(juce::String::fromUTF8("No IR loaded \xC2\xB7 amp runs direct"), in.toNearestInt(),
                       juce::Justification::centred);
        }
    }

    NamRigProcessor &mProc;
    int mRig = 0;
    juce::String mIrName{juce::String::fromUTF8("No IR \xC2\xB7 amp runs direct")};
    juce::String mCharacter; // auto tone descriptor (e.g. "Dark, thick")
    std::unique_ptr<LabeledKnob> mHpf, mLpf; // post-cab Low/High cut
    juce::ToggleButton mOn;                  // per-cab bypass (cabOn / cabOnB)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mOnAtt;
    juce::Rectangle<int> mNameRect, mRespRect;
    bool mLoaded = false;
    bool mDim = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabPanel)
};

//==============================================================================
// One "CAB" box holding both rigs side by side (A left, B right) with a divider
// and a single Browse button. Per-rig dimming (not a whole-panel veil) shows each
// cab's bypass independently. The chain strip's CAB A and CAB B tiles both open it.
class CombinedCabPanel : public BlockPanel
{
public:
    std::function<void()> onBrowse; // open the IR library

    explicit CombinedCabPanel(NamRigProcessor &proc) : BlockPanel("CAB"), mA(proc, 0), mB(proc, 1)
    {
        addAndMakeVisible(mA);
        addAndMakeVisible(mB);
        mBrowseBtn.setButtonText(juce::String::fromUTF8("Browse IRs\xE2\x80\xA6"));
        mBrowseBtn.getProperties().set("pill", true);
        mBrowseBtn.onClick = [this] { if (onBrowse) onBrowse(); };
        addAndMakeVisible(mBrowseBtn);
    }

    void resized() override
    {
        auto hr = headerArea();
        mBrowseBtn.setBounds(hr.removeFromRight(120).withSizeKeepingCentre(120, 28));

        auto body = bodyArea().reduced(12, 8);
        const int gap = 18;
        auto left = body.removeFromLeft((body.getWidth() - gap) / 2);
        auto sep = body.removeFromLeft(gap);
        mDivX = sep.getCentreX();
        mA.setBounds(left);
        mB.setBounds(body);
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);
        auto body = bodyArea().reduced(12, 14);
        g.setColour(colors::divider);
        g.fillRect((float)mDivX, (float)body.getY(), 1.0f, (float)body.getHeight());
    }

    void refresh() { mA.refresh(); mB.refresh(); }
    CabPanel &cabA() { return mA; }
    CabPanel &cabB() { return mB; }

private:
    CabPanel mA, mB;
    juce::TextButton mBrowseBtn;
    int mDivX = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombinedCabPanel)
};

//==============================================================================
// Live per-lane modulation scope. Reads the slot's params on a 30 Hz timer and
// animates the LFO/effect motion so the visual shows WHAT THE KNOBS DO: Rate ->
// scroll speed, Depth -> height, Wave/Type -> shape, Feedback -> resonant
// ripple. It calls the same voicing rules as the DSP (authenticWave/depthMax/
// maxRateHz) so the picture matches the sound (chorus can't show flanger speed,
// only tremolo honours Shape, etc.). In Parallel the owner scales mBright by the
// blend-pad weight so dragging the puck visibly turns lanes up and down.
class LaneScope : public juce::Component, private juce::Timer
{
public:
    LaneScope(juce::AudioProcessorValueTreeState &apvts, juce::String prefix, juce::Colour colour)
        : mApvts(apvts), mPrefix(std::move(prefix)), mColour(colour)
    {
        mLastMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(30);
    }
    void setBrightness(float b) { mBright = juce::jlimit(0.0f, 1.0f, b); }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(colors::scopeBg);
        g.fillRoundedRectangle(b, 5.0f);
        g.setColour(colors::outline.withAlpha(0.6f));
        g.drawRoundedRectangle(b, 5.0f, 1.0f);
        auto r = b.reduced(4.0f, 3.0f);
        const float midY = r.getCentreY();
        g.setColour(colors::outline.withAlpha(0.5f));
        g.drawHorizontalLine((int)midY, r.getX(), r.getRight());

        const int type = (int)raw("Type");
        const bool on = raw("On") >= 0.5f;
        juce::Colour col = mColour.withMultipliedAlpha(0.30f + 0.70f * mBright);
        if (!on) col = col.withMultipliedAlpha(0.35f); // bypassed -> faint

        if (type == 5) // Rotary: two orbiting rotors (horn + drum), not an LFO line
        {
            const float cx = r.getCentreX(), cy = midY, rad = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f - 2.0f;
            g.setColour(colors::outline.withAlpha(0.6f));
            g.drawEllipse(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, 1.0f);
            const float aH = (float)(mScroll * 2.0 * 3.14159265);          // horn (outer, faster)
            const float aD = (float)(mScroll * 2.0 * 3.14159265 * 0.5);    // drum (inner, slower)
            g.setColour(col);
            g.fillEllipse(cx + std::cos(aH) * rad - 3.0f, cy + std::sin(aH) * rad - 3.0f, 6.0f, 6.0f);
            g.setColour(colors::post.withMultipliedAlpha(0.30f + 0.70f * mBright));
            const float ri = rad * 0.55f;
            g.fillEllipse(cx + std::cos(aD) * ri - 2.5f, cy + std::sin(aD) * ri - 2.5f, 5.0f, 5.0f);
            return;
        }

        const int wave = (type == 3) ? (int)raw("Wave")                                    // tremolo honours Shape
                                     : nam_rig::ModVoice::authenticWave((nam_rig::ModVoice::Type)type);
        float amp = (type == 2) ? 0.7f                                                       // phaser: fixed sweep
                                : raw("Depth") * nam_rig::ModVoice::depthMax((nam_rig::ModVoice::Type)type);
        amp = juce::jlimit(0.05f, 1.0f, amp) * (r.getHeight() * 0.5f - 2.0f);
        const float fb = (type == 1 || type == 2 || type == 8) ? raw("Feedback") : 0.0f;     // flanger/phaser/bi-phase ripple
        const float skew = (type == 6) ? 0.55f : 0.0f;                                       // uni-vibe: lopsided sweep

        // Density tracks rate (fixed time-window scope): a faster rate packs more
        // cycles across the scope, so turning Rate up visibly tightens the wave --
        // not just a faster scroll. Capped per effect (chorus stays slow, etc.).
        const float rateHz = juce::jlimit(0.03f,
                                          (float)nam_rig::ModVoice::maxRateHz((nam_rig::ModVoice::Type)type),
                                          raw("Rate"));
        const float W = r.getWidth();
        const float cycles = juce::jlimit(0.6f, 10.0f, rateHz * 0.9f);
        juce::Path path;
        for (int px = 0; px <= (int)W; ++px)
        {
            float xc = (px / W) * cycles + (float)mScroll;
            float v = shape(wave, xc, skew) + fb * 0.30f * std::sin(2.0f * 3.14159265f * 3.0f * xc);
            v = juce::jlimit(-1.2f, 1.2f, v);
            const float y = midY - v * amp;
            if (px == 0) path.startNewSubPath(r.getX() + px, y);
            else path.lineTo(r.getX() + px, y);
        }
        g.setColour(col);
        g.strokePath(path, juce::PathStrokeType(1.8f));
    }

private:
    float raw(const char *suffix) const
    {
        auto *p = mApvts.getRawParameterValue(mPrefix + suffix);
        return p ? p->load() : 0.0f;
    }
    static float hashStep(int n) // deterministic -1..1 per integer step (Sample & Hold)
    {
        unsigned h = (unsigned)n * 2654435761u + 1013904223u;
        return ((float)((h >> 8) & 0xffffu) / 32768.0f) - 1.0f;
    }
    static float shape(int wave, float x, float skew) // -1..1 at cycle position x
    {
        if (wave == 2) { float f = x - std::floor(x); return f < 0.5f ? 1.0f : -1.0f; }   // Square
        if (wave == 3) return hashStep((int)std::floor(x));                               // Sample & Hold
        if (wave == 1) { float f = x - std::floor(x); return 1.0f - 4.0f * std::abs(f - 0.5f); } // Triangle
        float f = x - std::floor(x);
        if (skew > 0.0f) // warp the phase for the uni-vibe's asymmetric swell
            f = (f < skew) ? 0.5f * f / skew : 0.5f + 0.5f * (f - skew) / (1.0f - skew);
        return std::sin(2.0f * 3.14159265f * f);
    }
    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dt = juce::jlimit(0.0, 0.1, (now - mLastMs) / 1000.0);
        mLastMs = now;
        const int type = (int)raw("Type");
        double hz;
        if (type == 5) // rotary: slow/fast toggle drives the orbit speed
            hz = raw("RotFast") >= 0.5f ? 6.6 : 0.72;
        else
        {
            const double cap = (double)nam_rig::ModVoice::maxRateHz((nam_rig::ModVoice::Type)type);
            hz = juce::jlimit(0.03, cap, (double)raw("Rate"));
        }
        mScroll += hz * dt;                       // scroll in cycles, full rate (density carries speed too)
        if (mScroll > 1.0e6) mScroll -= 1.0e6;    // keep the accumulator bounded
        if (isVisible() && getWidth() > 0)
            repaint();
    }

    juce::AudioProcessorValueTreeState &mApvts;
    juce::String mPrefix;
    juce::Colour mColour;
    double mScroll = 0.0, mLastMs = 0.0;
    float mBright = 1.0f;
};

// One slot's controls. Shows only the controls the selected effect actually
// has: universal Rate/Sync/Mix/Width/On always; Depth (all but Phaser);
// Feedback (Flanger/Phaser); Shape waveform (Tremolo). Everything else is
// hardwired in ModVoice, so there are no bad-sound knobs.
// One slot as a horizontal lane: number+LED, animated effect icon, Type/Sync,
// a live scope, only the effect's real knobs, Shape (tremolo), and an On toggle.
// All three lanes are shown at once (no tabs) so the section reads at a glance.
class ModSlotLane : public juce::Component
{
public:
    // prefix = APVTS id prefix ("mod1".."mod3" for front slots, "post" for the
    // post block). soloSlot = 0-based slot for the solo button, or -1 for the
    // post lane (no solo). label = the small lane badge ("1".."3" or "P").
    ModSlotLane(juce::AudioProcessorValueTreeState &apvts, juce::String prefix,
                juce::String label, int soloSlot)
        : mApvts(apvts), mPrefix(std::move(prefix)), mSoloSlot(soloSlot)
    {
        const juce::String p = mPrefix;
        const bool isFront = (soloSlot >= 0);

        mBadgeText = label;                       // drawn as a coloured badge in paint()
        const juce::Colour laneCol = colors::laneColour(soloSlot);
        mLaneCol = laneCol;

        // Explicit ON pill (new design): replaces the old click-the-icon toggle.
        mOn.setButtonText("ON");
        mOn.setClickingTogglesState(true);
        mOn.getProperties().set("pill", true);
        addAndMakeVisible(mOn);
        mOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "On", mOn);
        mOn.onClick = [this] { refresh(); }; // relayout dim state when toggled

        mScope = std::make_unique<LaneScope>(apvts, p, laneCol);
        addAndMakeVisible(*mScope); // live LFO/effect motion for this slot

        // Filtered list: front lanes show only the 6 front effects, the post lane
        // only the 3 amp/speaker effects (item id = enum + 1, carrying the
        // ModVoice::Type). Bound to the full 9-choice Type param via a
        // juce::ParameterAttachment -- event-driven and message-thread-marshalled,
        // so it's robust (the old timer-polled hand-sync caused the intermittent
        // "won't select" bug). The 9-choice param is unchanged, so presets are safe.
        if (isFront)
        {
            mType.addItem("Chorus", 1); mType.addItem("Flanger", 2); mType.addItem("Phaser", 3);
            mType.addItem("Vibrato", 5); mType.addItem("Uni-Vibe", 7); mType.addItem("Bi-Phase", 9);
            mType.addItem("Ring Mod", 10);
        }
        else
        {
            mType.addItem("Tremolo", 4); mType.addItem("Rotary", 6); mType.addItem("Harm Trem", 8);
        }
        addAndMakeVisible(mType);
        if (auto *prm = apvts.getParameter(p + "Type"))
        {
            mTypeParamAtt = std::make_unique<juce::ParameterAttachment>(
                *prm,
                [this](float v) { // param -> combo (delivered on the message thread)
                    const int id = (int)std::lround(v) + 1;
                    if (mType.getSelectedId() != id)
                        mType.setSelectedId(id, juce::dontSendNotification);
                    applyType();
                });
            mType.onChange = [this] { // combo -> param (full gesture)
                const int id = mType.getSelectedId();
                if (id > 0 && mTypeParamAtt)
                    mTypeParamAtt->setValueAsCompleteGesture((float)(id - 1));
            };
            // NOTE: sendInitialUpdate() is deferred to the END of the ctor -- its
            // callback runs applyType(), which touches the knobs created below.
        }

        mSync.addItemList({"Off", "1/1", "1/2", "1/4", "1/4.", "1/4T",
                           "1/8", "1/8.", "1/8T", "1/16"}, 1);
        addAndMakeVisible(mSync);
        mSyncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, p + "Sync", mSync);
        mSync.onChange = [this] { refresh(); };

        mWave.addItemList({"Sine", "Triangle", "Square", "S&H"}, 1);
        addChildComponent(mWave); // Tremolo only
        mWaveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, p + "Wave", mWave);

        // (No On button: the slot is toggled by clicking its icon, wired above.)

        // Solo = momentary dial-in (front slots only; NOT an APVTS param).
        if (isFront)
        {
            mSolo.setButtonText("S");
            mSolo.setClickingTogglesState(true);
            mSolo.getProperties().set("pill", true);
            mSolo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe0b53d)); // solo = warm yellow
            addAndMakeVisible(mSolo);
            mSolo.onClick = [this] { if (onSolo) onSolo(mSoloSlot, mSolo.getToggleState()); };
        }

        mRate = std::make_unique<LabeledKnob>(apvts, p + "Rate", "Rate");
        mDepth = std::make_unique<LabeledKnob>(apvts, p + "Depth", "Depth");
        mFeedback = std::make_unique<LabeledKnob>(apvts, p + "Feedback", "Fdbk");
        mMix = std::make_unique<LabeledKnob>(apvts, p + "Mix", "Mix");
        mWidth = std::make_unique<LabeledKnob>(apvts, p + "Width", "Width");
        addAndMakeVisible(*mRate);
        addChildComponent(*mDepth);
        addChildComponent(*mFeedback);
        addChildComponent(*mMix); // chorus only (others hardwire their blend)
        addAndMakeVisible(*mWidth);

        mDrive = std::make_unique<LabeledKnob>(apvts, p + "Drive", "Drive");
        addChildComponent(*mDrive); // rotary only (Leslie tube amp)
        mHornDrum = std::make_unique<LabeledKnob>(apvts, p + "HornDrum", "Drum/Horn");
        addChildComponent(*mHornDrum); // rotary only (horn<->drum balance)
        mRotFast.setButtonText("Fast");
        addChildComponent(mRotFast); // rotary only (slow/fast rotor)
        mRotFastAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "RotFast", mRotFast);

        mManual = std::make_unique<LabeledKnob>(apvts, p + "Manual", "Manual");
        addChildComponent(*mManual); // flanger only (M-126 static comb position)

        mP2Ratio = std::make_unique<LabeledKnob>(apvts, p + "P2Ratio", "Swp 2");
        addChildComponent(*mP2Ratio); // bi-phase only (Sweep Gen 2 rate ratio)
        // Series + Extreme kept (per design note) but restyled as small accent
        // pills in the lane's right control stack, matching ON / S.
        mSeries.setButtonText("Series");
        mSeries.setClickingTogglesState(true);
        mSeries.getProperties().set("pill", true);
        addChildComponent(mSeries); // bi-phase only (series/parallel routing)
        mSeriesAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "Series", mSeries);

        mExtreme.setButtonText("Extreme");
        mExtreme.setClickingTogglesState(true);
        mExtreme.getProperties().set("pill", true);
        addChildComponent(mExtreme); // chorus/flanger/phaser/vibrato/uni-vibe/bi-phase (unlocks wild ranges)
        mExtremeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "Extreme", mExtreme);

        // Every knob in the lane reads 0..10 by rotation (pedal-style), so the
        // mixed underlying params (Speed in Hz, the rest 0..1, Sweep 2 a ratio)
        // all show on one consistent scale instead of exposing raw units.
        for (LabeledKnob *k : {mRate.get(), mDepth.get(), mFeedback.get(), mMix.get(),
                               mWidth.get(), mDrive.get(), mManual.get(), mP2Ratio.get(),
                               mHornDrum.get()})
        {
            k->setRotationReadout(10.0);
            k->setAccent(laneCol);    // value arc tinted to the lane colour
            k->hideValue();           // no number box -> a bigger knob
            k->setCaptionHeight(14);  // smaller caption so longer names don't truncate
        }

        // Now that every control exists, sync the combo to the current param value
        // (its callback runs applyType(), which touches the knobs above).
        if (mTypeParamAtt)
            mTypeParamAtt->sendInitialUpdate();
        refresh();
    }

    // Momentary solo: reports clicks to the owner; the owner pushes the live state
    // back so the button reflects it after an editor reopen / external change.
    std::function<void(int, bool)> onSolo;
    void setSoloState(bool on) { mSolo.setToggleState(on, juce::dontSendNotification); }

    // Parallel blend feedback: scale this lane's scope by its pad weight (1 in
    // series). The owner (ModPanel) pushes this from the live pad position.
    void setScopeBrightness(float b) { if (mScope) mScope->setBrightness(b); }

    // The Type combo is driven by a standard ComboBoxAttachment now, so refresh()
    // just re-applies the layout for the current params (no combo hand-sync).
    void refresh() { applyType(); }

    // Section routing: in PARALLEL the per-lane Mix is overridden (the lane runs
    // full-wet, Mod Mix owns dry/wet), so grey the knob. It stays live in SERIES.
    void setSectionParallel(bool p)
    {
        if (mSectionParallel == p) return;
        mSectionParallel = p;
        if (mMix) mMix->setEnabled(!p && mMix->isVisible());
    }

    // Show/relayout the controls for the current Type/Sync/On (reads the params;
    // never touches the combo, so it's safe to call from a fresh user selection).
    void applyType()
    {
        const juce::String p = mPrefix;
        const int type = (int)mApvts.getRawParameterValue(p + "Type")->load();
        const int sync = (int)mApvts.getRawParameterValue(p + "Sync")->load();
        const bool on = mApvts.getRawParameterValue(p + "On")->load() >= 0.5f;
        if (type == mLastType && sync == mLastSync && on == mLastOn)
            return;
        // Only a TYPE change alters which controls are shown / their bounds. Sync
        // and On changes (incl. the type-reset zeroing Sync) must NOT relayout, or
        // the resulting resized() can land on a subsequent dropdown click and eat
        // it (the intermittent "won't select" bug).
        const bool typeChanged = (type != mLastType);
        mLastType = type;
        mLastSync = sync;
        mLastOn = on;
        if (typeChanged)
        {
            const bool rotary = (type == 5);
            mDepth->setVisible(type != 2);                 // phaser: no depth knob
            mFeedback->setVisible(type == 1 || type == 2 || type == 8); // flanger/phaser/bi-phase
            mMix->setVisible(nam_rig::ModVoice::mixExposed((nam_rig::ModVoice::Type)type)); // chorus + flanger
            mMix->setEnabled(!mSectionParallel && mMix->isVisible()); // greyed in parallel (Mod Mix owns dry/wet)
            mWave.setVisible(type == 3);                   // tremolo shape
            mRate->setVisible(!rotary);                    // rotary: slow/fast toggle, not a rate
            if (!rotary) // knob ends exactly at this effect's rate ceiling (no dead travel past the internal cap)
            {
                mRate->slider().setNormalisableRange(
                    {0.03, (double)nam_rig::ModVoice::maxRateHz((nam_rig::ModVoice::Type)type), 0.01, 0.35});
                mRate->updateReadout(); // re-evaluate the 0..10 text against the new range
            }
            mSync.setVisible(!rotary && type != 9);         // ring mod: carrier is audio-rate, no tempo sync
            mDrive->setVisible(rotary);                    // rotary: Leslie tube drive
            mHornDrum->setVisible(rotary);                 // rotary: horn<->drum balance
            mRotFast.setVisible(rotary);
            mManual->setVisible(type == 1);                // flanger: static comb position
            // Per-effect knob naming. M-126 flanger: the sweep-amount knob is
            // "Sweep" (the M-126 "Width" term reads as the stereo control here, so
            // call the sweep "Sweep") and the stereo knob is "Spread" (so there
            // aren't two "Width"s). Uni-Vibe uses the authentic vibe terms: Rate ->
            // "Speed", Depth -> "Intensity". Rotary: Depth drives the swirl
            // intensity (doppler + directional pulse + drum throb) -> "Wom".
            const bool flanger = (type == 1);
            const bool uniVibe = (type == 6);
            const bool ring = (type == 9);                  // ring mod: Rate->carrier Freq, Depth->Amount
            mRate->setCaption(uniVibe ? "Speed" : ring ? "Freq" : "Rate");
            mDepth->setCaption(flanger ? "Sweep" : uniVibe ? "Intensity" : rotary ? "Wom" : ring ? "Amount" : "Depth");
            mWidth->setCaption(flanger ? "Spread" : "Width");
            mP2Ratio->setVisible(type == 8);               // bi-phase: Sweep Gen 2 ratio
            mSeries.setVisible(type == 8);                  // bi-phase: series/parallel
            const bool extremeable = (type == 0 || type == 1 || type == 2 || type == 4 || type == 6 || type == 8); // chorus/flanger/phaser/vibrato/uni-vibe/bi-phase
            mExtreme.setVisible(extremeable);               // unlock the wild ranges
        }
        mRate->setEnabled(sync == 0 || type == 9); // rate greyed when synced; ring mod ignores sync so its Freq stays live
        repaint();
        if (typeChanged)
            resized();
    }

    void paint(juce::Graphics &g) override
    {
        const bool on = mLastOn;
        auto b = getLocalBounds().toFloat().reduced(0.5f);

        // New-design lane chip: dark inset fill, per-lane coloured border, 10px
        // radius. The whole chip dims when the slot is off (mirrors the mockup's
        // opacity treatment).
        juce::Graphics::ScopedSaveState ss(g);
        if (!on)
            g.setOpacity(0.55f);
        g.setColour(juce::Colour(0xff191c21)); // lane chip surface
        g.fillRoundedRectangle(b, 10.0f);
        g.setColour(on ? mLaneCol.withAlpha(0.85f) : colors::outline);
        g.drawRoundedRectangle(b, 10.0f, 1.0f);

        // Coloured number/letter badge (lane colour fill, near-black digit).
        if (!mBadge.isEmpty())
        {
            auto badge = mBadge.toFloat();
            g.setColour(mLaneCol);
            g.fillRoundedRectangle(badge, 7.0f);
            g.setColour(colors::bg);
            g.setFont(fonts::archivo(13.0f, fonts::ExtraBold));
            g.drawText(mBadgeText, mBadge, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(13, 8);

        // Coloured number/letter badge (drawn in paint()).
        mBadge = area.removeFromLeft(26).withSizeKeepingCentre(26, 26);
        area.removeFromLeft(14);

        // Type dropdown chip + (below it) the Sync / rotary slow-fast row.
        auto meta = area.removeFromLeft(104).withSizeKeepingCentre(104, juce::jmin(area.getHeight(), 58));
        mType.setBounds(meta.removeFromTop(28));
        meta.removeFromTop(5);
        auto row2 = meta.removeFromTop(24);
        mSync.setBounds(row2.withWidth(104));               // non-rotary
        mRotFast.setBounds(row2.removeFromLeft(56));        // rotary: slow/fast
        area.removeFromLeft(14);

        // Right control stack (mockup idiom): ON over S, as accent pills. Series /
        // Extreme pills (kept per design note) sit just left of it when shown;
        // Tremolo's wave-shape selector takes the same right slot on the post lane.
        {
            auto col = area.removeFromRight(46);
            const int btnH = 23, gap = 5;
            const bool front = (mSoloSlot >= 0);
            const int groupH = front ? btnH * 2 + gap : btnH;
            auto stack = col.withSizeKeepingCentre(42, juce::jmin(area.getHeight() - 4, groupH));
            mOn.setBounds(stack.removeFromTop(btnH));
            if (front)
            {
                stack.removeFromTop(gap);
                mSolo.setBounds(stack.removeFromTop(btnH));
            }
            area.removeFromRight(8);
        }
        if (mWave.isVisible())
        {
            mWave.setBounds(area.removeFromRight(84).withSizeKeepingCentre(84, 24));
            area.removeFromRight(8);
        }
        if (mSeries.isVisible() || mExtreme.isVisible())
        {
            auto col = area.removeFromRight(64);
            area.removeFromRight(8);
            const int btnH = 21, gap = 5;
            const int n = (mExtreme.isVisible() ? 1 : 0) + (mSeries.isVisible() ? 1 : 0);
            auto stack = col.withSizeKeepingCentre(62, n * btnH + (n - 1) * gap);
            if (mExtreme.isVisible())
            {
                mExtreme.setBounds(stack.removeFromTop(btnH));
                if (mSeries.isVisible()) stack.removeFromTop(gap);
            }
            if (mSeries.isVisible())
                mSeries.setBounds(stack.removeFromTop(btnH));
        }

        // Knob order (left -> right), one consistent rule across every effect:
        // Rate/Speed (motion) first, then the amount + character controls (Manual,
        // Depth, Feedback, Sweep 2, Drive, Horn/Drum), then the "output" controls
        // Mix (blend) and Width (stereo) last. Filtered per effect by visibility.
        std::vector<juce::Component *> vis;
        for (juce::Component *k : {(juce::Component *)mRate.get(), (juce::Component *)mManual.get(),
                                   (juce::Component *)mDepth.get(), (juce::Component *)mFeedback.get(),
                                   (juce::Component *)mP2Ratio.get(), (juce::Component *)mDrive.get(),
                                   (juce::Component *)mHornDrum.get(), (juce::Component *)mMix.get(),
                                   (juce::Component *)mWidth.get()})
            if (k->isVisible())
                vis.push_back(k);
        const int nk = (int)vis.size();
        if (mScope) // scope gets a guaranteed slice FIRST; the knobs share the rest
        {
            // Reserve the scope, leaving at least ~44px per knob so even the
            // densest effects (Flanger/Bi-Phase = 6 knobs) still show the waveform.
            int scopeW = juce::jmin(area.getWidth() - nk * 44 - 12, 132);
            if (scopeW >= 70)
            {
                mScope->setVisible(true);
                mScope->setBounds(area.removeFromLeft(scopeW)
                                      .withSizeKeepingCentre(scopeW, juce::jmin(area.getHeight(), 40)));
                area.removeFromLeft(12);
            }
            else
                mScope->setVisible(false);
        }
        if (nk > 0)
        {
            const int w = juce::jmin(74, area.getWidth() / nk);
            auto row = area.withSizeKeepingCentre(w * nk, juce::jmin(area.getHeight(), 78));
            for (auto *k : vis)
                k->setBounds(row.removeFromLeft(w).reduced(3, 0));
        }
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    juce::String mPrefix;
    int mSoloSlot = -1; // 0-based front slot for solo; -1 = post lane (no solo)
    int mLastType = -1, mLastSync = -1;
    bool mLastOn = true;
    bool mSectionParallel = false; // SERIES = per-lane Mix live; PARALLEL = greyed
    juce::Rectangle<int> mBadge;   // coloured number/letter badge (drawn in paint)
    juce::String mBadgeText;       // "1".."3" or "P"
    juce::Colour mLaneCol;         // per-lane accent (badge fill + chip border)
    std::unique_ptr<LaneScope> mScope;
    juce::ComboBox mType, mWave, mSync;
    std::unique_ptr<juce::ParameterAttachment> mTypeParamAtt; // robust binding for the filtered Type list
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mWaveAtt, mSyncAtt;
    juce::ToggleButton mOn;   // explicit on/off pill (new design)
    juce::ToggleButton mSolo; // momentary dial-in (not APVTS-attached)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mOnAtt;
    std::unique_ptr<LabeledKnob> mRate, mDepth, mFeedback, mMix, mWidth, mDrive, mManual, mP2Ratio, mHornDrum;
    juce::ToggleButton mRotFast, mSeries, mExtreme;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mRotFastAtt, mSeriesAtt, mExtremeAtt;
};

// Draggable blend pad for PARALLEL routing. The puck sets the slot weights
// (barycentric, same geometry as ModBlock::padWeights). Backed by two hidden
// sliders attached to modPadX/modPadY, so host automation and saved state work
// for free: dragging writes the params; external moves repaint the puck.
//
// The geometry follows the ENABLED slots: 3 on = triangle (node1 top-centre /
// node2 bottom-left / node3 bottom-right); 2 on = a line crossfading those two;
// 1 on = a point. The puck is always CONSTRAINED to that geometry, so it can't
// wander into dead square corners.
class BlendPad : public juce::Component, private juce::Slider::Listener
{
public:
    explicit BlendPad(juce::AudioProcessorValueTreeState &apvts)
    {
        for (auto *s : {&mX, &mY})
        {
            s->setRange(0.0, 1.0, 0.0);
            addChildComponent(*s); // hidden: the pad is the visible control
            s->addListener(this);
        }
        mXAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "modPadX", mX);
        mYAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "modPadY", mY);
    }

    // Which slots are enabled -> drives triangle/line/point geometry. Re-projects
    // the puck onto the new shape when the set changes (so it never sits off it).
    // When a slot is DISABLED the puck position is remembered; if the slot comes
    // back before the puck is dragged, that original position is restored.
    void setActiveSlots(bool a0, bool a1, bool a2)
    {
        if (a0 == mActive[0] && a1 == mActive[1] && a2 == mActive[2])
            return;
        const int oldN = (mActive[0] ? 1 : 0) + (mActive[1] ? 1 : 0) + (mActive[2] ? 1 : 0);
        const int newN = (a0 ? 1 : 0) + (a1 ? 1 : 0) + (a2 ? 1 : 0);
        mActive[0] = a0;
        mActive[1] = a1;
        mActive[2] = a2;

        float px = (float)mX.getValue(), py = (float)mY.getValue();
        if (newN < oldN) // a slot was disabled -> remember the puck to restore later
        {
            if (!mHasSaved) { mSavedX = px; mSavedY = py; mSavedCount = oldN; mHasSaved = true; }
        }
        else if (newN > oldN && mHasSaved && newN >= mSavedCount) // back, puck not moved -> restore
        {
            px = mSavedX;
            py = mSavedY;
            mHasSaved = false;
        }
        const auto p = constrain(px, py); // clamp onto the (new) shape
        setPuck(p.x, p.y);
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        g.setColour(colors::caption); // header (mirrors the rack's "CHAIN ORDER")
        g.setFont(fonts::archivo(9.0f, fonts::SemiBold));
        g.drawText("PARALLEL BLEND", getLocalBounds().removeFromTop(13), juce::Justification::centred);
        auto box = getLocalBounds().toFloat(); // bordered container (matches the rack)
        box.removeFromTop(15.0f);
        box = box.reduced(1.0f);
        g.setColour(colors::scopeBg);
        g.fillRoundedRectangle(box, 8.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(box, 8.0f, 1.0f);

        const auto r = padRect();
        int act[3], n = 0;
        for (int i = 0; i < 3; ++i)
            if (mActive[i]) act[n++] = i;

        // Current blend weights for node size + the puck's blended colour.
        float w[3];
        nam_rig::ModBlock::padWeights((float)mX.getValue(), (float)mY.getValue(), w);
        const auto pk = scr(r, (float)mX.getValue(), (float)mY.getValue());

        if (n >= 3 || n == 0) // triangle (n==0 shouldn't happen; show the full shape)
        {
            juce::Path tri;
            tri.startNewSubPath(scr(r, 0));
            tri.lineTo(scr(r, 1));
            tri.lineTo(scr(r, 2));
            tri.closeSubPath();
            g.setColour(colors::scopeBg);
            g.fillPath(tri);
            g.setColour(colors::outline);
            g.strokePath(tri, juce::PathStrokeType(1.2f));
        }
        else if (n == 2) // crossfade line between the two enabled nodes
        {
            g.setColour(colors::outline);
            g.drawLine(juce::Line<float>(scr(r, act[0]), scr(r, act[1])), 1.4f);
        }

        for (int i = 0; i < 3; ++i) // puck->node tethers, coloured + weighted by blend
            if (mActive[i])
            {
                const auto p = scr(r, i);
                g.setColour(colors::laneColour(i).withAlpha(0.20f + 0.55f * w[i]));
                g.drawLine(juce::Line<float>(pk, p), 1.0f);
            }

        g.setFont(fonts::archivo(9.5f, fonts::Bold));
        for (int i = 0; i < 3; ++i)
        {
            const auto p = scr(r, i);
            const float rad = 3.5f + 4.0f * (mActive[i] ? w[i] : 0.0f);
            const juce::Colour lc = colors::laneColour(i);
            // Node is ALWAYS drawn in its lane colour (dim when the slot is off) so
            // each corner has identity even before anything is blended in.
            g.setColour(mActive[i] ? lc : lc.withAlpha(0.30f));
            g.fillEllipse(p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);

            // Persistent slot-number label ("1".."3" in the lane colour); the live
            // blend weight is appended as a % once the slot is active.
            juce::String lbl = juce::String(i + 1);
            if (mActive[i])
                lbl += "  " + juce::String(juce::roundToInt(w[i] * 100.0f)) + "%";
            const float ly = (i == 0) ? p.y - 15.0f - rad : p.y + 5.0f + rad;
            g.setColour(mActive[i] ? lc : lc.withAlpha(0.60f));
            g.drawText(lbl, (int)(p.x - 26.0f), (int)ly, 52, 11, juce::Justification::centred);
        }

        // Puck takes the blended colour of the three lane accents by weight.
        float rr = 0, gg = 0, bb = 0;
        for (int i = 0; i < 3; ++i)
        {
            const auto c = colors::laneColour(i);
            rr += w[i] * c.getFloatRed();
            gg += w[i] * c.getFloatGreen();
            bb += w[i] * c.getFloatBlue();
        }
        g.setColour(juce::Colour::fromFloatRGBA(juce::jlimit(0.0f, 1.0f, rr),
                                                juce::jlimit(0.0f, 1.0f, gg),
                                                juce::jlimit(0.0f, 1.0f, bb), 1.0f));
        g.fillEllipse(pk.x - 6.0f, pk.y - 6.0f, 12.0f, 12.0f);
        g.setColour(colors::scopeBg);
        g.drawEllipse(pk.x - 6.0f, pk.y - 6.0f, 12.0f, 12.0f, 1.5f);
    }

    void mouseDown(const juce::MouseEvent &e) override { drag(e); }
    void mouseDrag(const juce::MouseEvent &e) override { drag(e); }

private:
    void sliderValueChanged(juce::Slider *) override
    {
        // An EXTERNAL move (host automation / preset load) invalidates the saved
        // restore position; our own moves set mInternalSet and are exempt.
        if (!mInternalSet)
            mHasSaved = false;
        repaint();
    }
    void setPuck(float x, float y) // write both params as an internal move
    {
        mInternalSet = true;
        mX.setValue(x, juce::sendNotificationSync);
        mY.setValue(y, juce::sendNotificationSync);
        mInternalSet = false;
    }

    // Node positions in (padX, padY) parameter space (matches ModBlock::padWeights).
    static juce::Point<float> node(int i)
    {
        if (i == 0) return {0.5f, 1.0f};
        if (i == 1) return {0.0f, 0.0f};
        return {1.0f, 0.0f};
    }
    juce::Rectangle<float> padRect() const
    {
        auto b = getLocalBounds().toFloat();
        b.removeFromTop(15.0f); // "MOD MIX" header
        // Fit an EQUILATERAL triangle (height = base * sqrt(3)/2), centred in the
        // box. Margins leave room for the weight-% labels (which clear the nodes).
        b = b.reduced(24.0f, 26.0f);
        const float k = 0.8660254f; // equilateral height / base
        float w = b.getWidth(), h = w * k;
        if (h > b.getHeight()) { h = b.getHeight(); w = h / k; } // height-limited
        return juce::Rectangle<float>(w, h).withCentre(b.getCentre());
    }
    juce::Point<float> scr(juce::Rectangle<float> r, float x, float y) const
    {
        return {r.getX() + x * r.getWidth(), r.getY() + (1.0f - y) * r.getHeight()};
    }
    juce::Point<float> scr(juce::Rectangle<float> r, int nodeIdx) const
    {
        const auto v = node(nodeIdx);
        return scr(r, v.x, v.y);
    }
    // Project a raw (0..1) point onto the active geometry (triangle/line/point).
    juce::Point<float> constrain(float x, float y) const
    {
        int act[3], n = 0;
        for (int i = 0; i < 3; ++i)
            if (mActive[i]) act[n++] = i;
        if (n >= 3 || n == 0)
        {
            float w[3];
            nam_rig::ModBlock::padWeights(x, y, w); // clamp + renormalise into the triangle
            return {0.5f * w[0] + w[2], w[0]};      // barycentric point back to (x,y)
        }
        if (n == 1)
            return node(act[0]);
        const auto A = node(act[0]), B = node(act[1]); // n == 2: project onto the segment
        const auto AB = B - A;
        const float denom = AB.x * AB.x + AB.y * AB.y;
        float t = denom > 1.0e-9f ? ((x - A.x) * AB.x + (y - A.y) * AB.y) / denom : 0.0f;
        t = juce::jlimit(0.0f, 1.0f, t);
        return {A.x + t * AB.x, A.y + t * AB.y};
    }
    void drag(const juce::MouseEvent &e)
    {
        const auto r = padRect();
        const float rx = juce::jlimit(0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        const float ry = juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - r.getY()) / r.getHeight());
        const auto p = constrain(rx, ry);
        mHasSaved = false; // user moved the puck -> don't restore an old position
        setPuck(p.x, p.y);
    }

    juce::Slider mX, mY;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mXAtt, mYAtt;
    bool mActive[3] = {true, true, true};
    // Remember the puck when a slot is disabled, to restore it if the slot comes
    // back before the puck is moved.
    float mSavedX = 0.5f, mSavedY = 1.0f / 3.0f;
    int mSavedCount = 0;
    bool mHasSaved = false;
    bool mInternalSet = false; // true while WE move the puck (vs host/preset)
};

// Draggable chain-order rack for SERIES routing -- the twin of the BlendPad.
// Shows the three front slots stacked IN->OUT in processing order; drag a chip
// up/down to reorder the chain. Reads/writes the single modChainOrder choice
// param (the six permutations), so order saves + automates like any param. The
// rack fills the right strip in Series, exactly where the pad sits in Parallel.
class ChainRack : public juce::Component
{
public:
    explicit ChainRack(juce::AudioProcessorValueTreeState &apvts) : mApvts(apvts)
    {
        readOrder();
    }

    // Pull the order from the param (called on the editor timer). Skipped mid-drag
    // so a timer tick can't fight the user's drag.
    void refresh()
    {
        if (mDragging)
            return;
        readOrder();
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        // Header + bordered container (matches the blend pad's framing).
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(9.0f, fonts::SemiBold));
        g.drawText("CHAIN ORDER", getLocalBounds().removeFromTop(13),
                   juce::Justification::centred);
        const auto box = boxRect();
        g.setColour(colors::scopeBg);
        g.fillRoundedRectangle(box, 8.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(box, 8.0f, 1.0f);

        const auto inner = innerRect();
        const float cx = inner.getCentreX();
        g.setColour(colors::outline); // spine behind the chips
        g.drawLine(cx, box.getY() + 14.0f, cx, box.getBottom() - 14.0f, 1.5f);
        g.setColour(colors::textDim);
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        g.drawText("IN", (int)cx - 13, (int)box.getY() + 2, 26, 11, juce::Justification::centred);
        g.drawText("OUT", (int)cx - 13, (int)box.getBottom() - 13, 26, 11, juce::Justification::centred);

        for (int pos = 0; pos < nam_rig::ModBlock::kSlots - 1; ++pos) // flow arrows between chips
            if (!mDragging)
            {
                const float ay = (chipRect(pos).getBottom() + chipRect(pos + 1).getY()) * 0.5f;
                g.setColour(colors::textDim);
                juce::Path tri;
                tri.addTriangle(cx - 3.0f, ay - 2.5f, cx + 3.0f, ay - 2.5f, cx, ay + 3.0f);
                g.fillPath(tri);
            }

        for (int pos = 0; pos < nam_rig::ModBlock::kSlots; ++pos)
        {
            const int slot = mOrder[pos];
            const bool drag = (mDragging && slot == mDragSlot);
            const juce::Colour laneCol = colors::laneColour(slot);
            auto chip = chipRect(pos);
            if (drag)
                chip = chip.withY(juce::jlimit(inner.getY(), inner.getBottom() - chip.getHeight(),
                                               mDragY - chip.getHeight() * 0.5f));
            g.setColour(drag ? colors::panel.brighter(0.10f) : colors::panel.brighter(0.03f));
            g.fillRoundedRectangle(chip, 7.0f);
            g.setColour(laneCol.withAlpha(drag ? 1.0f : 0.85f));
            g.drawRoundedRectangle(chip, 7.0f, drag ? 1.6f : 1.2f);

            auto row = chip.reduced(7.0f, 0.0f);
            g.setColour(colors::textDim); // grip dots (2x3)
            const float gx = row.removeFromLeft(6.0f).getX();
            for (int d = 0; d < 3; ++d)
                for (int e = 0; e < 2; ++e)
                    g.fillEllipse(gx + e * 3.0f, row.getCentreY() - 4.0f + d * 4.0f, 1.5f, 1.5f);
            row.removeFromLeft(4.0f);
            drawGlyph(g, row.removeFromLeft(16.0f), slotType(slot), laneCol);
            row.removeFromLeft(7.0f);
            g.setColour(laneCol); // slot number on the right
            g.setFont(RigLookAndFeel::withHeight(10.0f));
            g.drawText(juce::String(slot + 1), row.removeFromRight(12.0f).toNearestInt(),
                       juce::Justification::centredRight);
            g.setColour(colors::text); // effect name
            g.setFont(RigLookAndFeel::withHeight(11.5f));
            g.drawText(effectName(slotType(slot)), row.toNearestInt(), juce::Justification::centredLeft);
        }
    }

    void mouseDown(const juce::MouseEvent &e) override
    {
        const int pos = posAtY(e.position.y);
        if (pos < 0)
            return;
        mDragging = true;
        mDragSlot = mOrder[pos];
        mDragY = e.position.y;
        repaint();
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (!mDragging)
            return;
        mDragY = e.position.y;
        const auto b = innerRect();
        const float step = b.getHeight() / (float)nam_rig::ModBlock::kSlots;
        int tgt = (int)std::floor((mDragY - b.getY()) / juce::jmax(1.0f, step));
        tgt = juce::jlimit(0, nam_rig::ModBlock::kSlots - 1, tgt);
        if (tgt != posOfSlot(mDragSlot)) // rebuild the order with the dragged slot at tgt
        {
            int rest[nam_rig::ModBlock::kSlots], n = 0;
            for (int p = 0; p < nam_rig::ModBlock::kSlots; ++p)
                if (mOrder[p] != mDragSlot) rest[n++] = mOrder[p];
            int w = 0;
            for (int p = 0; p < nam_rig::ModBlock::kSlots; ++p)
                mOrder[p] = (p == tgt) ? mDragSlot : rest[w++];
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        if (!mDragging)
            return;
        mDragging = false;
        writeOrder();
        repaint();
    }

private:
    static const int *perm(int i) // row i of the six permutations of {0,1,2}
    {
        static const int P[6][3] = {
            {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        return P[juce::jlimit(0, 5, i)];
    }
    void readOrder()
    {
        const int oi = juce::jlimit(0, 5, (int)mApvts.getRawParameterValue("modChainOrder")->load());
        const int *p = perm(oi);
        for (int k = 0; k < nam_rig::ModBlock::kSlots; ++k) mOrder[k] = p[k];
    }
    void writeOrder()
    {
        int idx = 0;
        for (int i = 0; i < 6; ++i)
        {
            const int *p = perm(i);
            if (p[0] == mOrder[0] && p[1] == mOrder[1] && p[2] == mOrder[2]) { idx = i; break; }
        }
        if (auto *prm = mApvts.getParameter("modChainOrder"))
        {
            const float norm = prm->convertTo0to1((float)idx);
            if (std::abs(prm->getValue() - norm) > 1.0e-6f)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost(norm);
                prm->endChangeGesture();
            }
        }
    }
    int slotType(int slot) const
    {
        return (int)mApvts.getRawParameterValue("mod" + juce::String(slot + 1) + "Type")->load();
    }
    static juce::String effectName(int type)
    {
        static const char *kNames[] = {"Chorus", "Flanger", "Phaser",   "Tremolo", "Vibrato",
                                       "Rotary", "Uni-Vibe", "Harm Trem", "Bi-Phase", "Ring Mod"};
        return (type >= 0 && type < 10) ? kNames[type] : "-";
    }
    int posOfSlot(int slot) const
    {
        for (int p = 0; p < nam_rig::ModBlock::kSlots; ++p)
            if (mOrder[p] == slot) return p;
        return 0;
    }
    juce::Rectangle<float> boxRect() const // bordered container, below the header
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        b.removeFromTop(15.0f); // "SIGNAL FLOW" header
        return b;
    }
    juce::Rectangle<float> innerRect() const // chip area inside the box (IN/OUT reserved)
    {
        auto b = boxRect().reduced(9.0f, 8.0f);
        b.removeFromTop(11.0f);    // IN
        b.removeFromBottom(11.0f); // OUT
        return b;
    }
    juce::Rectangle<float> chipRect(int pos) const
    {
        auto b = innerRect();
        const float step = b.getHeight() / (float)nam_rig::ModBlock::kSlots;
        const float h = juce::jmin(30.0f, step - 8.0f);
        const float cyc = b.getY() + step * pos + step * 0.5f;
        return juce::Rectangle<float>(b.getX(), cyc - h * 0.5f, b.getWidth(), h);
    }
    int posAtY(float y) const
    {
        for (int p = 0; p < nam_rig::ModBlock::kSlots; ++p)
            if (chipRect(p).getY() - 4.0f <= y && y <= chipRect(p).getBottom() + 4.0f)
                return p;
        return -1;
    }
    // A tiny effect glyph for the chip (rotary = ring+dot, tremolo = bars, else a
    // sine squiggle), tinted to the lane colour.
    static void drawGlyph(juce::Graphics &g, juce::Rectangle<float> r, int type, juce::Colour c)
    {
        g.setColour(c);
        const float cx = r.getCentreX(), cy = r.getCentreY();
        if (type == 5) // rotary
        {
            const float rad = juce::jmin(r.getWidth(), r.getHeight()) * 0.42f;
            g.drawEllipse(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, 1.2f);
            g.fillEllipse(cx - 1.4f, cy - rad - 1.4f, 2.8f, 2.8f);
        }
        else if (type == 3) // tremolo: pulsing bars
        {
            for (int i = 0; i < 3; ++i)
            {
                const float bx = r.getX() + 1.0f + (float)i * (r.getWidth() - 2.0f) / 3.0f;
                const float h = r.getHeight() * (0.5f + 0.16f * (float)((i + 1) % 2 ? 1 : -1) + 0.34f);
                g.fillRoundedRectangle(bx, cy - h * 0.5f, (r.getWidth() - 2.0f) / 3.0f - 1.6f, h, 1.0f);
            }
        }
        else // sine squiggle
        {
            juce::Path p;
            const int N = 18;
            for (int i = 0; i <= N; ++i)
            {
                const float t = (float)i / (float)N;
                const float x = r.getX() + t * r.getWidth();
                const float y = cy - std::sin(t * 6.2831853f * 1.5f) * r.getHeight() * 0.34f;
                if (i == 0) p.startNewSubPath(x, y);
                else p.lineTo(x, y);
            }
            g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved));
        }
    }

    juce::AudioProcessorValueTreeState &mApvts;
    int mOrder[nam_rig::ModBlock::kSlots] = {0, 1, 2};
    bool mDragging = false;
    int mDragSlot = 0;
    float mDragY = 0.0f;
};

class ModPanel : public BlockPanel
{
public:
    explicit ModPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("MODULATION"), mApvts(apvts)
    {
        for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
        {
            mLanes[(size_t)s] = std::make_unique<ModSlotLane>(
                apvts, "mod" + juce::String(s + 1), juce::String(s + 1), s);
            addAndMakeVisible(*mLanes[(size_t)s]);
            mLanes[(size_t)s]->onSolo = [this](int slot, bool on) {
                if (!onSetSolo) return;
                // Exclusive solo: turning one on clears the others (radio style);
                // clicking the lit one again clears it. Audio only, blend untouched.
                if (on)
                    for (int k = 0; k < nam_rig::ModBlock::kSlots; ++k)
                        onSetSolo(k, k == slot);
                else
                    onSetSolo(slot, false);
                if (getSolo)
                    for (int k = 0; k < nam_rig::ModBlock::kSlots; ++k)
                        if (mLanes[(size_t)k]) mLanes[(size_t)k]->setSoloState(getSolo(k));
            };
        }
        // Dedicated POST lane: runs at the END of the section (rotary/tremolo/
        // harm-trem). No solo; only the post effects are selectable.
        mPostLane = std::make_unique<ModSlotLane>(apvts, "post", "P", -1);
        addAndMakeVisible(*mPostLane);

        mRouting = std::make_unique<SegmentedControl>(
            apvts, "modRouting", juce::StringArray{"Series", "Parallel"});
        addAndMakeVisible(*mRouting);
        mRouting->onChange = [this](int) { refreshRouting(); };

        mPad = std::make_unique<BlendPad>(apvts);
        addChildComponent(*mPad); // parallel only

        mModMix = std::make_unique<HKnob>(apvts, "modMix", "Dry / Wet");
        addChildComponent(*mModMix); // parallel only

        mRack = std::make_unique<ChainRack>(apvts);
        addChildComponent(*mRack); // series only (fills the strip where the pad sits in parallel)

        refreshRouting();
    }

    // Editor wires these to the processor's momentary solo (not an APVTS param).
    std::function<void(int, bool)> onSetSolo; // -> processor.setModSolo
    std::function<bool(int)> getSolo;         // -> processor.getModSolo

    void refresh()
    {
        for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
            if (mLanes[(size_t)s])
            {
                mLanes[(size_t)s]->refresh();
                if (getSolo) mLanes[(size_t)s]->setSoloState(getSolo(s)); // reflect live solo
            }
        if (mPostLane) mPostLane->refresh();
        if (mRack) mRack->refresh();
        updatePadActive();
        updateScopeBrightness();
        refreshRouting();
    }

    // Pad geometry follows the ENABLED (On) slots only. Solo is momentary and
    // must NOT move the puck / rewrite the blend, so it is deliberately excluded
    // here -- soloing leaves the blend exactly as the user set it.
    void updatePadActive()
    {
        auto enabled = [&](int s) {
            return mApvts.getRawParameterValue("mod" + juce::String(s + 1) + "On")->load() >= 0.5f;
        };
        mPad->setActiveSlots(enabled(0), enabled(1), enabled(2));
    }

    // In Parallel, dim each lane's scope by its blend weight (normalised so the
    // strongest lane is full), so the puck visibly turns lanes up and down. In
    // Series every lane is fully in the chain -> full brightness.
    void updateScopeBrightness()
    {
        if (!mParallel)
        {
            for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
                if (mLanes[(size_t)s]) mLanes[(size_t)s]->setScopeBrightness(1.0f);
            return;
        }
        float w[3];
        nam_rig::ModBlock::padWeights(mApvts.getRawParameterValue("modPadX")->load(),
                                      mApvts.getRawParameterValue("modPadY")->load(), w);
        for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s) // zero bypassed slots, then normalise to the max
            if (mApvts.getRawParameterValue("mod" + juce::String(s + 1) + "On")->load() < 0.5f)
                w[s] = 0.0f;
        const float mx = juce::jmax(1.0e-4f, w[0], w[1], w[2]);
        for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
            if (mLanes[(size_t)s]) mLanes[(size_t)s]->setScopeBrightness(w[s] / mx);
    }

    // Show/hide the routing-dependent right-strip controls and relayout when the
    // Series/Parallel routing changes (from the toggle or host automation). The
    // strip holds the blend pad + Mod Mix in Parallel and the chain-order rack in
    // Series -- the two faces of "how the slots combine".
    void refreshRouting()
    {
        const bool par = (mRouting && mRouting->index() == 1);
        mPad->setVisible(par);
        mModMix->setVisible(par);
        mRack->setVisible(!par);
        for (auto &l : mLanes) // grey each lane's Mix knob in parallel (Mod Mix owns dry/wet)
            if (l) l->setSectionParallel(par);
        if (par == mParallel)
            return; // visibility refreshed; layout already matches the mode
        mParallel = par;
        resized();
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g); // panel body + "MODULATION" title
        if (mSpine.getHeight() <= 0)
            return;
        const float x = (float)mSpine.getCentreX();
        const float top = (float)mSpine.getY();
        const float bot = mPostLaneY - 7.0f; // line ends just above the OUT label
        g.setColour(colors::outline);
        g.drawLine(x, top, x, bot, 1.5f);
        g.setColour(colors::accentDim);
        if (mParallel)
        {
            // Parallel: the input bus feeds each lane in parallel (right-pointing
            // triangles, matching the series ones).
            for (float yC : mLaneCenters)
            {
                juce::Path tri;
                tri.addTriangle(x - 2.0f, yC - 3.0f, x - 2.0f, yC + 3.0f, x + 4.0f, yC);
                g.fillPath(tri);
            }
        }
        else
        {
            // Series: signal flows lane to lane (down-arrows between slots).
            for (float fy : mArrowYs)
            {
                juce::Path tri;
                tri.addTriangle(x - 3.0f, fy - 2.0f, x + 3.0f, fy - 2.0f, x, fy + 3.0f);
                g.fillPath(tri);
            }
        }
        g.setColour(colors::textDim);
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        g.drawText("IN", mSpine.getX() - 3, mSpine.getY() - 13, 26, 11, juce::Justification::centred);
        g.drawText("OUT", mSpine.getX() - 3, (int)mPostLaneY - 5, 26, 11, juce::Justification::centred);

        // POST · SPEAKER STAGE divider sitting just above the post lane: a centred
        // caption flanked by two thin rules (mockup idiom).
        if (!mPostDivider.isEmpty())
        {
            auto d = mPostDivider.toFloat();
            auto f = fonts::archivo(9.0f, fonts::SemiBold);
            const juce::String label = juce::String::fromUTF8("POST \xC2\xB7 SPEAKER STAGE");
            const float tw = juce::GlyphArrangement::getStringWidth(f, label) + 4.0f;
            const float cy = d.getCentreY();
            const float cx = d.getCentreX();
            g.setColour(colors::divider);
            g.fillRect(d.getX(), cy, cx - tw * 0.5f - d.getX() - 4.0f, 1.0f);
            g.fillRect(cx + tw * 0.5f + 4.0f, cy, d.getRight() - (cx + tw * 0.5f + 4.0f), 1.0f);
            g.setColour(colors::caption);
            g.setFont(f);
            g.drawText(label, d, juce::Justification::centred);
        }
    }

    void resized() override
    {
        // Routing toggle (Series/Parallel) sits in the header's top-right corner,
        // vertically centred in the same band as the title.
        auto hdr = getLocalBounds().removeFromTop(contentArea().getY()).reduced(16, 0);
        const int rw = mRouting ? mRouting->idealWidth() : 120;
        mRouting->setBounds(hdr.removeFromRight(rw).withSizeKeepingCentre(rw, 26));

        auto area = contentArea();
        const int n = nam_rig::ModBlock::kSlots, gap = 8;

        // Spine column spans the FULL height: IN at the top, down through the
        // slots, to the OUT (post) stage at the bottom.
        mSpine = area.removeFromLeft(20).withTrimmedTop(14).withTrimmedBottom(16);
        area.removeFromLeft(4);

        // POST lane spans the full remaining width at the bottom (under the OUT
        // divider), so the right-hand rack/pad does NOT sit beside it.
        auto postRegion = area.removeFromBottom(area.getHeight() / 4);
        mPostDivider = postRegion.removeFromTop(16);
        mPostLaneY = (float)postRegion.getCentreY(); // OUT label sits next to the post lane
        if (mPostLane) mPostLane->setBounds(postRegion.reduced(0, 2));
        area.removeFromBottom(gap);

        // Right strip -- now only as tall as the 3 front lanes, and wider, to give
        // the signal-flow rack / Cartesian pad more room. Routing toggle on top;
        // pad + Mod Mix in parallel, chain rack in series.
        auto strip = area.removeFromRight(150);
        area.removeFromRight(10);
        if (mParallel)
        {
            mModMix->setBounds(strip.removeFromBottom(52).reduced(4, 0)); // horizontal Dry/Wet
            strip.removeFromBottom(8);
            mPad->setBounds(strip); // fill the strip -> a taller blend box
        }
        else
        {
            mRack->setBounds(strip); // chain-order rack fills the series strip
        }

        // Front lanes fill the rest of the upper region.
        const int laneH = (area.getHeight() - gap * (n - 1)) / n;
        mArrowYs.clear();
        for (int s = 0; s < n; ++s)
        {
            auto lane = area.removeFromTop(laneH);
            mLanes[(size_t)s]->setBounds(lane);
            mLaneCenters[(size_t)s] = (float)lane.getCentreY(); // for the parallel spine taps
            if (s < n - 1)
                mArrowYs.push_back((float)area.removeFromTop(gap).getCentreY());
        }
    }

private:
    std::array<std::unique_ptr<ModSlotLane>, (size_t)nam_rig::ModBlock::kSlots> mLanes;
    std::unique_ptr<ModSlotLane> mPostLane;
    juce::Rectangle<int> mSpine, mPostDivider;
    std::vector<float> mArrowYs;
    std::array<float, (size_t)nam_rig::ModBlock::kSlots> mLaneCenters{}; // parallel spine taps
    float mPostLaneY = 0.0f;                                             // OUT label / line end
    std::unique_ptr<SegmentedControl> mRouting;
    std::unique_ptr<BlendPad> mPad;
    std::unique_ptr<ChainRack> mRack;
    std::unique_ptr<HKnob> mModMix;
    juce::AudioProcessorValueTreeState &mApvts;
    bool mParallel = false;
};

//==============================================================================
// Echo-taps visualiser (new design "ECHO TAPS" well): a left accent bar = the dry
// hit, then feedback echoes marching right, each shorter by Feedback, spread up/
// down by Width, alternating L/R when Ping-Pong is on. Tap spacing tracks Time.
// Reads the live params; the panel repaints it from the editor timer.
class DelayTaps : public juce::Component
{
public:
    explicit DelayTaps(juce::AudioProcessorValueTreeState &apvts) : mApvts(apvts) {}

    // Effective sync-resolved delay time in ms per side. When set, used instead of
    // the raw Free-time param so synced spacing tracks the tempo division. The L/R
    // pair lets the visualiser show the dual rhythm (dotted-1/8 + 1/4, etc.).
    std::function<float()> getTimeMs, getTimeMsR;

    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient grad(colors::wellTop, r.getX(), r.getY(),
                                  colors::wellBot, r.getX(), r.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(r, 11.0f);
        g.setColour(colors::cardBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 11.0f, 1.0f);

        auto in = r.reduced(13.0f, 11.0f);

        // Space Tape: show the three heads and their time differences (a HEADS/TIME
        // table + the heads drawn at their true relative spacing) instead of the
        // generic single echo train. Inactive heads (per the Mode) are dimmed.
        if ((int)mApvts.getRawParameterValue("delayCharacter")->load() == 2)
        { paintSpaceTape(g, in); return; }

        const float H = in.getHeight(), top = in.getY();
        const float gutter = 16.0f;            // left margin reserved for the L / R labels
        const float left = in.getX() + gutter; // dry bar + taps start just inside it
        const float W = in.getRight() - left;
        const float cy = top + H * 0.5f;

        g.setColour(juce::Colour(0xff262c34)); // centre (L/R) axis
        g.fillRect(in.getX(), cy - 0.5f, in.getWidth(), 1.0f);

        const float rawTime = mApvts.getRawParameterValue("delayTime")->load();
        const float timeL = getTimeMs  ? getTimeMs()  : rawTime;
        const float timeR = getTimeMsR ? getTimeMsR() : timeL;
        const float fb     = mApvts.getRawParameterValue("delayFeedback")->load();
        const float width  = mApvts.getRawParameterValue("delayWidth")->load();

        // Tap spacing is PROPORTIONAL to delay time (ref: a 500 ms tap ~ 0.13 of
        // the width), so musical ratios read true -- a 1/2 lane lands exactly twice
        // the spacing of a 1/4 lane. Clamped only at the extremes so a very long
        // delay keeps its first echoes on-screen and a very short one stays legible.
        auto spacingFor = [&](float t) {
            // Floor low enough that every musical division stays proportional (1/16
            // must read exactly half of 1/8); it only clamps sub-~45 ms Free times,
            // where bars would otherwise overlap.
            return juce::jlimit(0.012f, 0.55f, t * (0.13f / 500.0f));
        };

        // Dry reference bar at the far left.
        g.setColour(colors::accent.withAlpha(0.5f));
        g.fillRoundedRectangle(left, top, 3.0f, H, 1.5f);

        // Mode: Dual (R unlinked) shows two independent trains; Ping-Pong shows ONE
        // train alternating L/R; Single shows the linked pair. (DSP: Dual overrides ping.)
        // Tape characters are mono: the DSP forces ping-pong/dual off, so the visualiser
        // shows a single linked train for them regardless of the stored params.
        const bool tapeChar = (int)mApvts.getRawParameterValue("delayCharacter")->load() != 0;
        const bool dual = !tapeChar && mApvts.getRawParameterValue("delaySyncR")->load() > 0.5f;
        const bool ping = !tapeChar && !dual && mApvts.getRawParameterValue("delayPingPong")->load() >= 0.5f;

        // L lane sits above the centre axis, R below; their separation opens with
        // Width (at Width 0 they collapse to the centre = mono, matching the DSP).
        const float sep = (0.06f + 0.20f * width) * H;
        auto drawTap = [&](float x, float laneCy, float h) {
            const float halfH = (0.04f + h * 0.16f) * H; // decays with feedback (floored so far taps stay visible)
            const float bx = left + x * W;
            g.setColour(colors::accent.withAlpha(juce::jlimit(0.1f, 1.0f, 0.25f + h * 0.75f)));
            g.fillRoundedRectangle(bx - 3.0f, laneCy - halfH, 6.0f, 2.0f * halfH, 3.0f);
        };
        // First echo sits ONE full delay-interval after the dry line (time zero) so the
        // dry->echo1 gap matches the echo->echo gap; the train runs to the right edge
        // (short/tight delays still fill the width), bars fading with feedback.
        auto drawLane = [&](float t, float laneCy) {
            const float sp = spacingFor(t);
            float x = sp, h = 1.0f;
            // cap high enough that even the minimum tap spacing fills the well to the
            // right edge (short Free times were stopping ~57% across at the old 48 cap).
            for (int i = 0; i < 96 && x < 0.97f; ++i) { drawTap(x, laneCy, h); h *= fb; x += sp; }
        };

        if (ping) // one train, hopping top<->bottom each repeat = the L/R bounce
        {
            const float sp = spacingFor(timeL);
            float x = sp, h = 1.0f;
            for (int i = 0; i < 96 && x < 0.97f; ++i)
            {
                drawTap(x, (i % 2 == 0) ? (cy - sep) : (cy + sep), h);
                h *= fb;
                x += sp;
            }
        }
        else if (dual) // Dual = independent L/R times -> two distinct trains
        {
            drawLane(timeL, cy - sep);
            drawLane(timeR, cy + sep);
        }
        else // Single (Clean) or Tape Echo = MONO -> a single centred train
        {
            drawLane(timeL, cy);
        }

        if (dual || ping) // only the stereo modes have L/R lanes -> only they get labels
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(9.0f, fonts::SemiBold));
            g.drawText("L", (int)in.getX(), (int)top + 1, 14, 11, juce::Justification::centred);
            g.drawText("R", (int)in.getX(), (int)in.getBottom() - 12, 14, 11, juce::Justification::centred);
        }
    }

    // Space Tape head-time display. Left: a HEADS/TIME table (head index + its delay,
    // shown as the musical division when synced or ms when free). Right: the three
    // head taps at their TRUE relative spacing so the time differences read at a
    // glance. Heads not active in the current Mode are dimmed (still shown as ghosts).
    void paintSpaceTape(juce::Graphics &g, juce::Rectangle<float> in)
    {
        const int   syncIdx = (int)mApvts.getRawParameterValue("delaySync")->load();
        const bool  sync    = syncIdx > 0;
        const int   mode    = juce::jlimit(0, 11, (int)mApvts.getRawParameterValue("delayHeadMode")->load());
        const int   mask    = kStHeadMaskUI[(size_t)mode];
        const float base    = getTimeMs ? getTimeMs()
                                        : mApvts.getRawParameterValue("delayTime")->load(); // head 1 ms
        const float *ratio  = sync ? kHeadRatioSyncUI : kHeadRatioFreeUI;
        const float headMs[3] = { base * ratio[0], base * ratio[1], base * ratio[2] };

        // Keep the table + taps in a vertically-centred band so a tall well doesn't
        // spread the content out with big empty margins top and bottom.
        in = in.withSizeKeepingCentre(in.getWidth(), juce::jmin(in.getHeight(), 132.0f));
        const float rowH = in.getHeight() / 3.0f;

        // --- left: HEADS / TIME table (three rows) ---
        const float tableW = juce::jmin(120.0f, in.getWidth() * 0.42f);
        auto table = in.removeFromLeft(tableW);
        for (int h = 0; h < 3; ++h)
        {
            const bool on = (mask & (1 << h)) != 0;
            auto row = table.removeFromTop(rowH);
            const juce::Colour col = on ? colors::accent : colors::captionDim;

            auto chip = row.removeFromLeft(24.0f).reduced(2.0f, rowH * 0.24f);
            g.setColour(col.withAlpha(on ? 0.22f : 0.10f));
            g.fillRoundedRectangle(chip, 3.0f);
            g.setColour(col.withAlpha(on ? 1.0f : 0.6f));
            g.setFont(fonts::mono(11.0f, fonts::SemiBold));
            g.drawText(juce::String(h + 1), chip.toNearestInt(), juce::Justification::centred);

            const juce::String lab = sync ? headSyncLabel(h, syncIdx, headMs[h])
                                          : juce::String(juce::roundToInt(headMs[h])) + " ms";
            g.setColour(col.withAlpha(on ? 1.0f : 0.55f));
            g.setFont(fonts::archivo(12.0f, fonts::SemiBold, 0.04f));
            g.drawText(lab, row.reduced(8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        }

        // --- right: head taps at true relative spacing (auto-fit so it always reads) ---
        auto lane = in.reduced(6.0f, 6.0f);
        const float cy = lane.getCentreY();
        g.setColour(juce::Colour(0xff262c34));
        g.fillRect(lane.getX(), cy - 0.5f, lane.getWidth(), 1.0f);
        g.setColour(colors::accent.withAlpha(0.5f));
        g.fillRoundedRectangle(lane.getX(), lane.getY(), 3.0f, lane.getHeight(), 1.5f); // dry hit

        const float maxMs = juce::jmax(1.0f, headMs[2]); // head 3 is the longest
        const float x0 = lane.getX() + 4.0f, W = lane.getWidth() - 8.0f;
        const float scale = 0.72f / maxMs;
        auto tapAt = [&](float ms, bool on, float alpha) {
            const float x = x0 + ms * scale * W;
            const float hh = (on ? 0.28f : 0.15f) * lane.getHeight();
            g.setColour(colors::accent.withAlpha(alpha));
            g.fillRoundedRectangle(x - 2.5f, cy - hh, 5.0f, 2.0f * hh, 2.5f);
        };
        // the heads (inactive = faint ghost so the spacing still reads), numbered
        for (int h = 0; h < 3; ++h)
        {
            const bool on = (mask & (1 << h)) != 0;
            tapAt(headMs[h], on, on ? 0.95f : 0.18f);
            const float x = x0 + headMs[h] * scale * W;
            g.setColour((on ? colors::accent : colors::captionDim).withAlpha(on ? 0.9f : 0.7f));
            g.setFont(fonts::mono(8.0f, fonts::SemiBold));
            g.drawText(juce::String(h + 1), (int)(x - 8.0f), (int)lane.getY() - 1, 16, 10,
                       juce::Justification::centred);
        }
    }

    // Musical label for a head when synced: head 1 = the selected division; heads 2/3 =
    // that division x the sync head ratio, mapped to the nearest named value (ms fallback
    // for the few odd products, e.g. the x8/3 of a triplet base).
    juce::String headSyncLabel(int h, int syncIdx, float ms) const
    {
        if (h == 0) return juce::String(kSyncNamesUI[juce::jlimit(0, 13, syncIdx)]);
        const double beats = kSyncBeatsUI[(size_t)juce::jlimit(0, 13, syncIdx)]
                           * (double)kHeadRatioSyncUI[h];
        // Snap the LABEL to the nearest musical division. Head 3's 8/3 ratio
        // gallops off-grid for the triplet bases, but they land close enough to
        // name; the tap lane on the right still renders the true spacing.
        const char *best = kNoteByBeats[0].name;
        double bestErr = 1.0e9;
        for (const auto &nb : kNoteByBeats)
        {
            const double e = std::abs(beats - nb.beats);
            if (e < bestErr) { bestErr = e; best = nb.name; }
        }
        // The 1/1 base pushes head 3 (×8/3 ≈ 10.7 beats) well past the longest
        // named division — there's no clean note, and "2/1" would read a third
        // short — so show ms for that one case rather than a misleading division.
        if (syncIdx == 1 && bestErr > 0.01)
            return juce::String(juce::roundToInt(ms)) + " ms";
        return juce::String(best);
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;

    // --- mirrored from DelayBlock for the display only (keep in sync with the engine) ---
    static constexpr int   kStHeadMaskUI[12] =
        {0b001, 0b010, 0b100, 0b110, 0b001, 0b010, 0b100, 0b011, 0b110, 0b101, 0b111, 0b000};
    static constexpr float kHeadRatioFreeUI[3] = {1.0f, 1.95f, 2.79f};
    static constexpr float kHeadRatioSyncUI[3] = {1.0f, 2.0f, 8.0f / 3.0f};
    static constexpr double kSyncBeatsUI[14] =
        {0.0, 4.0, 3.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0};
    static constexpr const char *kSyncNamesUI[14] =
        {"Free", "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T"};
    struct NoteBeats { double beats; const char *name; };
    static constexpr NoteBeats kNoteByBeats[17] = {
        {8.0, "2/1"}, {6.0, "1/1."}, {16.0 / 3.0, "2/1T"}, {4.0, "1/1"}, {3.0, "1/2."},
        {8.0 / 3.0, "1/1T"}, {2.0, "1/2"}, {1.5, "1/4."}, {4.0 / 3.0, "1/2T"}, {1.0, "1/4"},
        {0.75, "1/8."}, {2.0 / 3.0, "1/4T"}, {0.5, "1/8"}, {0.375, "1/16."}, {1.0 / 3.0, "1/8T"},
        {0.25, "1/16"}, {1.0 / 6.0, "1/16T"}};
};

//==============================================================================
// Vertical on/off switch: a pill track with a round handle that slides UP (on) /
// DOWN (off), and an adjacent label that names the current state. Used for the
// delay's Free/Sync switch beside the Time knob.
class VToggle : public juce::Component
{
public:
    std::function<void(bool)> onChange; // fired with the new state (true = up/"on")

    VToggle(juce::String onLabel, juce::String offLabel)
        : mOnLabel(std::move(onLabel)), mOffLabel(std::move(offLabel)) {}

    void setState(bool on) { if (on != mState) { mState = on; repaint(); } }
    bool state() const { return mState; }

    void mouseUp(const juce::MouseEvent &) override
    {
        if (!isEnabled()) return;
        mState = !mState;
        repaint();
        if (onChange) onChange(mState);
    }

    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat();
        const float trackW = 20.0f, trackH = 38.0f;
        // Track on the RIGHT; both labels sit to its left and stay visible. The handle
        // position (not colour) shows the state — no accent highlight at all.
        auto track = juce::Rectangle<float>(r.getRight() - trackW, r.getCentreY() - trackH * 0.5f,
                                            trackW, trackH);
        g.setColour(colors::tile);
        g.fillRoundedRectangle(track, trackW * 0.5f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(track, trackW * 0.5f, 1.0f);

        const float hd = trackW - 6.0f; // handle diameter
        const float hx = track.getCentreX() - hd * 0.5f;
        const float hy = mState ? track.getY() + 3.0f : track.getBottom() - 3.0f - hd;
        g.setColour(colors::text2); // neutral handle, no accent
        g.fillEllipse(hx, hy, hd, hd);

        const float labW = track.getX() - r.getX() - 6.0f;
        g.setFont(fonts::archivo(11.0f, fonts::SemiBold));
        g.setColour(mState ? colors::textBright : colors::textDim); // top label = "on" (up)
        g.drawText(mOnLabel, juce::Rectangle<float>(r.getX(), track.getY() - 3.0f, labW, 15.0f),
                   juce::Justification::centredRight);
        g.setColour(mState ? colors::textDim : colors::textBright); // bottom label = "off" (down)
        g.drawText(mOffLabel, juce::Rectangle<float>(r.getX(), track.getBottom() - 12.0f, labW, 15.0f),
                   juce::Justification::centredRight);
    }

private:
    juce::String mOnLabel, mOffLabel;
    bool mState = false;
};

class DelayPanel : public BlockPanel
{
public:
    explicit DelayPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("DELAY"), mApvts(apvts)
    {
        // Tempo divisions for the Time / Sync R dropdowns (index = choice value; 0 is
        // Free/Link, never picked because the synced knobs clamp to 1..13).
        const juce::StringArray divsL{"Free", "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4",
                                      "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T"};
        mDivNames = divsL;

        // SYNC as a vertical 2-way toggle (Free / Sync) sitting next to the Time knob.
        // Sync -> the Time knob steps tempo divisions (driving delaySync) and shows the
        // resolved ms; Free -> it sets free ms (delayTime). refresh() mirrors its state
        // from delaySync; mLastDiv remembers the division across a Free trip.
        mSyncToggle = std::make_unique<VToggle>("Sync", "Free"); // up = Sync, down = Free
        addAndMakeVisible(*mSyncToggle);
        mSyncToggle->onChange = [this](bool sync) {
            if (sync) setParamIndex("delaySync", mLastDiv > 0 ? mLastDiv : 6); // -> last div (def 1/4)
            else      setParamIndex("delaySync", 0);                           // -> Free
            refresh();
        };

        // Sync R is the R side in Dual. Like the Time knob it does double duty: Free ->
        // free R ms (delayTimeR); Sync -> R division (delaySyncR). It always shows the
        // measured R ms; refresh() rebinds it with the Free/Sync toggle. (Created on the
        // free param to match the initial Free state.)
        mSyncRKnob = std::make_unique<LabeledKnob>(apvts, "delayTimeR", "Sync R");
        mSyncRKnob->setReadoutFn([this](double v) {
            const float ms = mTimeMsRProvider ? mTimeMsRProvider() : (float)v;
            return juce::String(juce::roundToInt(ms)) + " ms";
        });
        mSyncRKnob->slider().onValueChange = [this] { refresh(); };
        addAndMakeVisible(*mSyncRKnob);

        // Space Tape MODE dial — the multi-head tape echo's 11 echo modes + Reverb-only. Stepped
        // knob with click-to-pick menu (like Sync). Shown only for Space Tape
        // (replaces Sync R). Modes 5-11/Reverb auto-engage the rig Spring.
        const juce::StringArray headNames{"1 Head 1", "2 Head 2", "3 Head 3", "4 Heads 2+3",
                                          "5 Head 1 +Rev", "6 Head 2 +Rev", "7 Head 3 +Rev",
                                          "8 Heads 1+2 +Rev", "9 Heads 2+3 +Rev",
                                          "10 Heads 1+3 +Rev", "11 All +Rev", "12 Reverb Only"};
        mHeadKnob = std::make_unique<LabeledKnob>(apvts, "delayHeadMode", "Mode");
        // Readout under the knob = just the mode number (12 = "Reverb"); the full
        // descriptive names live in the click-to-pick menu (setValueMenu) below.
        mHeadKnob->slider().textFromValueFunction =
            [](double v) {
                const int i = juce::jlimit(0, 11, (int)std::lround(v));
                return i == 11 ? juce::String("Reverb") : juce::String(i + 1);
            };
        mHeadKnob->slider().updateText();
        mHeadKnob->setValueMenu(headNames, "Echo Mode");
        addChildComponent(*mHeadKnob); // visibility toggled in refresh()

        // Stereo MODE selector as a dropdown (Single / Dual / Ping-Pong). Single =
        // one linked time; Dual = independent L/R divisions (Sync R active);
        // Ping-Pong = L/R bounce. It drives the underlying delaySyncR (Link) +
        // delayPingPong states the DSP already routes from, so no new params.
        mModeBox.addItemList({"Single", "Dual", "Ping-Pong"}, 1);
        addAndMakeVisible(mModeBox);
        mModeBox.onChange = [this] {
            const int i = mModeBox.getSelectedItemIndex();
            if (i == 1) // Dual: ping off; if R is Linked, match it to L (fallback 1/4 if L is Free)
            {
                setParamIndex("delayPingPong", 0);
                if ((int)mApvts.getRawParameterValue("delaySyncR")->load() == 0)
                {
                    const int l = (int)mApvts.getRawParameterValue("delaySync")->load();
                    setParamIndex("delaySyncR", l >= 1 ? l : 6);
                }
            }
            else if (i == 2) // Ping-Pong: R Linked + bounce on
            {
                setParamIndex("delaySyncR", 0);
                setParamIndex("delayPingPong", 1);
            }
            else // Single: R Linked, no bounce
            {
                setParamIndex("delaySyncR", 0);
                setParamIndex("delayPingPong", 0);
            }
            refresh();
        };

        // Guitar delay presets: a quick menu that snapshots ONLY the delay params
        // (the rest of the rig is untouched). Fast path to a usable sound.
        mPresetBtn.setButtonText(juce::String::fromUTF8("Presets  \xE2\x96\xBE"));
        addAndMakeVisible(mPresetBtn);
        mPresetBtn.onClick = [this] {
            juce::PopupMenu m;
            m.addCustomItem(-1, std::make_unique<MenuSectionHeader>("Delay Presets"), nullptr, {});
            const auto &ps = delayPresets();
            for (int i = 0; i < (int)ps.size(); ++i)
                m.addItem(i + 1, ps[(size_t)i].name);
            m.setLookAndFeel(&getLookAndFeel());
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mPresetBtn),
                            [this](int r) { if (r > 0) applyDelayPreset(r - 1); });
        };

        // Delay CHARACTER selector (Clean / Tape Echo) -> delayCharacter param.
        // Tape engages the tape-echo voicing (saturation, bass/HF roll-off,
        // wow/flutter + drift, tape glide); Clean is the transparent engine.
        mCharacter = std::make_unique<SegmentedControl>(
            apvts, "delayCharacter", juce::StringArray{"Clean", "Tape Echo", "Space Tape"});
        addChildComponent(*mCharacter); // invisible param bridge; the left-column cards drive it
        mCharacter->onChange = [this](int) { refresh(); resized(); repaint(); }; // relayout + card highlight

        mTapsCaption.setText("ECHO TAPS", juce::dontSendNotification);
        mTapsCaption.setColour(juce::Label::textColourId, colors::caption);
        mTapsCaption.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        addAndMakeVisible(mTapsCaption);

        mTaps = std::make_unique<DelayTaps>(apvts);
        addAndMakeVisible(*mTaps);

        // Wow/Flutter is tape character -> it belongs to the future tape delay, not
        // the clean one. The DSP engine keeps it (mod amount stays 0 here).
        const std::pair<const char *, const char *> defs[] = {
            {"delayTime", "Time"},        {"delayFeedback", "Feedback"},
            {"delayLowCut", "Low Cut"},   {"delayTone", "High Cut"},
            {"delayWidth", "Width"},      {"delayMix", "Mix"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }
        // Mix (last knob) reads pedal-style 0-10 off the knob ROTATION, so the skew
        // stays invisible -- the dial just feels smooth; ears, not a percentage.
        mKnobs.back()->setRotationReadout(10.0);

        // Time knob (knob 0) always shows the MEASURED ms: the processor's
        // sync-resolved time when synced, or the free ms otherwise. refresh() rebinds
        // it between delayTime (Free) and delaySync (Sync, steps divisions).
        mKnobs[0]->setReadoutFn([this](double v) {
            const float ms = mTimeMsProvider ? mTimeMsProvider() : (float)v;
            return juce::String(juce::roundToInt(ms)) + " ms";
        });
        mKnobs[0]->slider().onValueChange = [this] { refresh(); }; // sync change -> redraw taps + readout
        refresh();
    }

    // Wire the visualiser to the processor's sync-resolved delay times (per side)
    // so synced tap spacing follows the tempo divisions, not just the Free knob. The
    // L provider also feeds the Time knob's measured-ms readout.
    void setTimeProvider(std::function<float()> f)
    {
        mTimeMsProvider = f;
        if (mTaps) mTaps->getTimeMs = std::move(f);
        if (!mKnobs.empty()) mKnobs[0]->updateReadout();
    }
    void setTimeProviderR(std::function<float()> f)
    {
        mTimeMsRProvider = f;
        if (mTaps) mTaps->getTimeMsR = std::move(f);
        if (mSyncRKnob) mSyncRKnob->updateReadout();
    }

    void refresh() // time knob is owned by the sync division when sync != Free; header shows time + FB
    {
        const int sync = (int)mApvts.getRawParameterValue("delaySync")->load();
        const int chr = (int)mApvts.getRawParameterValue("delayCharacter")->load();
        const bool space = chr == 2;   // Space Tape (multi-head)
        const bool tape = chr != 0;    // any tape character = MONO
        // Ping-pong + dual (independent L/R) are STEREO digital-delay tricks; the tape
        // characters are authentically mono and the DSP forces them off for a tape
        // character, so mirror that here -- treat Sync R / ping as Link/off for the
        // header + mode badge, hide the stereo Mode selector + Sync R, and grey Width.
        const int syncR = tape ? 0 : (int)mApvts.getRawParameterValue("delaySyncR")->load();
        const bool ping = !tape && mApvts.getRawParameterValue("delayPingPong")->load() >= 0.5f;

        // SYNC button reflects the free/sync state; remember a live division so the
        // toggle can restore it. The Time knob does double duty: bound to delayTime in
        // Free (sets ms), or to delaySync when synced (steps divisions, ms still shown).
        const bool synced = sync > 0;
        if (synced) mLastDiv = sync;
        if (mSyncToggle) mSyncToggle->setState(synced);
        if (synced != mWasSynced)
        {
            mWasSynced = synced;
            if (synced)
            {
                mKnobs[0]->rebind(mApvts, "delaySync");
                mKnobs[0]->slider().setRange(1.0, 13.0, 1.0); // exclude Free (0): the toggle owns that
                mKnobs[0]->setValueMenu(mDivNames, "Sync Division"); // click the readout -> pick a division
                // Sync R follows: its own R division (can't reach Link -> stays Dual).
                mSyncRKnob->rebind(mApvts, "delaySyncR");
                mSyncRKnob->slider().setRange(1.0, 13.0, 1.0);
                mSyncRKnob->setValueMenu(mDivNames, "Sync R Division");
            }
            else
            {
                mKnobs[0]->rebind(mApvts, "delayTime");
                mKnobs[0]->setValueMenu({});                  // free ms: no division menu
                mSyncRKnob->rebind(mApvts, "delayTimeR");     // free R ms
                mSyncRKnob->setValueMenu({});
            }
            mKnobs[0]->updateReadout();
            mSyncRKnob->updateReadout();
        }

        // No header time/FB readout on any character -- the knobs (and the Space Tape
        // HEADS/TIME well) already show it; the duplicate summary was just clutter.
        setHeaderRight(juce::String());

        // Mode identifier: Dual if R unlinked, else Ping-Pong if bouncing, else Single
        // (matches the DSP, where Dual overrides ping). Sync R is only live in Dual.
        const bool dual = syncR > 0;
        mModeBox.setSelectedItemIndex(dual ? 1 : (ping ? 2 : 0), juce::dontSendNotification);
        if (mSyncRKnob) mSyncRKnob->setEnabled(dual);
        // Width is an M/S spread on the wet -> only meaningful when there's stereo
        // delay structure (Dual or Ping-Pong); in Single it does nothing, so grey it.
        if (mKnobs.size() > 4) mKnobs[4]->setEnabled(dual || ping); // Width (now 5th knob)

        // Tape characters are MONO: hide the stereo Mode selector + Sync R (Clean-only),
        // grey Width. Space Tape additionally swaps in the multi-head Mode knob.
        if (mHeadKnob) mHeadKnob->setVisible(space);
        if (mSyncRKnob) mSyncRKnob->setVisible(!tape && dual); // only live in Dual
        mModeBox.setVisible(!tape);
        if (tape && mKnobs.size() > 4) mKnobs[4]->setEnabled(false); // Width n/a (mono tape)

        // Space Tape repurposes the echo-taps well as the per-head time display.
        mTapsCaption.setText(space ? "HEADS / TIME" : "ECHO TAPS", juce::dontSendNotification);

        if (mTaps) mTaps->repaint();
    }

    // Set a choice/bool parameter to an integer value (UI -> param).
    void setParamIndex(const char *id, int idx)
    {
        if (auto *p = mApvts.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)idx));
            p->endChangeGesture();
        }
    }

    // Set any param to a real-world value (convertTo0to1 handles range/skew).
    void setParamReal(const char *id, double val)
    {
        if (auto *p = mApvts.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)val));
            p->endChangeGesture();
        }
    }

    // Guitar delay presets — full snapshots of the delay block only.
    struct DPreset { const char *name; std::vector<std::pair<const char *, double>> kv; };
    static const std::vector<DPreset> &delayPresets()
    {
        static const std::vector<DPreset> p = {
            {"Slapback",       {{"delayOn",1},{"delaySync",0},{"delaySyncR",0},{"delayTime",100},{"delayFeedback",0.00},{"delayTone",8000},{"delayLowCut",200},{"delayWidth",1.00},{"delayMix",0.25},{"delayPingPong",0}}},
            {"Quarter",        {{"delayOn",1},{"delaySync",6},{"delaySyncR",0},{"delayTime",500},{"delayFeedback",0.30},{"delayTone",8000},{"delayLowCut",200},{"delayWidth",1.00},{"delayMix",0.28},{"delayPingPong",0}}},
            {"Dotted Lead",    {{"delayOn",1},{"delaySync",8},{"delaySyncR",0},{"delayTime",375},{"delayFeedback",0.40},{"delayTone",6000},{"delayLowCut",270},{"delayWidth",1.00},{"delayMix",0.40},{"delayPingPong",0}}},
            {"Gallop",         {{"delayOn",1},{"delaySync",8},{"delaySyncR",6},{"delayTime",375},{"delayFeedback",0.30},{"delayTone",5000},{"delayLowCut",250},{"delayWidth",1.00},{"delayMix",0.38},{"delayPingPong",0}}},
            {"Ambient Wash",   {{"delayOn",1},{"delaySync",6},{"delaySyncR",0},{"delayTime",500},{"delayFeedback",0.62},{"delayTone",5000},{"delayLowCut",220},{"delayWidth",1.00},{"delayMix",0.40},{"delayPingPong",0}}},
            {"Dub Echo",       {{"delayOn",1},{"delaySync",6},{"delaySyncR",0},{"delayTime",500},{"delayFeedback",0.21},{"delayTone",1500},{"delayLowCut",250},{"delayWidth",1.00},{"delayMix",0.50},{"delayPingPong",1}}},
        };
        return p;
    }
    void applyDelayPreset(int idx)
    {
        const auto &ps = delayPresets();
        if (idx < 0 || idx >= (int)ps.size()) return;
        for (const auto &kv : ps[(size_t)idx].kv)
            setParamReal(kv.first, kv.second);
        refresh();
    }

    // Character display names (index = delayCharacter choice value).
    static const char *kCharName(int i)
    {
        static const char *N[3] = {"Clean", "Tape Echo", "Space Tape"};
        return N[juce::jlimit(0, 2, i)];
    }

    // A glyph that DESCRIBES each character:
    //   0 Clean      -> 3 decaying echo spikes (precise digital repeats)
    //   1 Tape Echo  -> a tape reel (rim + hub + spokes)
    //   2 Space Tape -> a rocket ship (the "space" tape echo)
    static void drawCharGlyph(juce::Graphics &g, int i, juce::Rectangle<float> gly,
                              juce::Colour col)
    {
        g.setColour(col);
        const auto c = gly.getCentre();
        if (i == 0) // CLEAN: a clean train of 3 decaying echo spikes (narrow cluster)
        {
            const float span = gly.getWidth() * 0.55f; // tighter than the full box
            const float x0 = c.x - span * 0.5f;
            for (int b = 0; b < 3; ++b)
            {
                const float f = (float)b / 2.0f;
                const float bx = x0 + f * span;
                const float hh = gly.getHeight() * (0.5f - 0.36f * f);
                g.fillRect(bx, c.y - hh, 1.8f, 2.0f * hh);
            }
            return;
        }

        if (i == 1) // TAPE ECHO: a tape reel
        {
            const float R = gly.getWidth() * 0.46f;
            g.drawEllipse(c.x - R, c.y - R, 2.0f * R, 2.0f * R, 1.6f); // rim
            const float rh = R * 0.30f;
            g.fillEllipse(c.x - rh, c.y - rh, 2.0f * rh, 2.0f * rh);   // hub
            for (int s = 0; s < 3; ++s)                                // spokes
            {
                const float a = (float)s * juce::MathConstants<float>::twoPi / 3.0f + 0.5f;
                g.drawLine(c.x + rh * std::cos(a), c.y + rh * std::sin(a),
                           c.x + (R - 1.5f) * std::cos(a), c.y + (R - 1.5f) * std::sin(a), 1.1f);
            }
            return;
        }

        // SPACE TAPE: a rocket ship.
        const float bw = 3.4f;                       // body half-width
        const float topY = gly.getY() + 1.0f;        // nose tip
        const float bodyTop = gly.getY() + 6.0f;     // nose joins the body
        const float botY = gly.getBottom() - 6.0f;   // body base (room for fins/flame)
        juce::Path rocket;                            // nose + body outline
        rocket.startNewSubPath(c.x, topY);
        rocket.lineTo(c.x + bw, bodyTop);
        rocket.lineTo(c.x + bw, botY);
        rocket.lineTo(c.x - bw, botY);
        rocket.lineTo(c.x - bw, bodyTop);
        rocket.closeSubPath();
        g.strokePath(rocket, juce::PathStrokeType(1.3f));
        juce::Path fins;                              // two swept fins
        fins.startNewSubPath(c.x - bw, botY - 4.0f);
        fins.lineTo(c.x - bw - 3.0f, botY + 1.5f);
        fins.lineTo(c.x - bw, botY);
        fins.startNewSubPath(c.x + bw, botY - 4.0f);
        fins.lineTo(c.x + bw + 3.0f, botY + 1.5f);
        fins.lineTo(c.x + bw, botY);
        g.strokePath(fins, juce::PathStrokeType(1.2f));
        g.fillEllipse(c.x - 1.5f, bodyTop + 2.5f, 3.0f, 3.0f); // window
        juce::Path flame;                             // exhaust
        flame.startNewSubPath(c.x - 2.0f, botY + 1.0f);
        flame.lineTo(c.x, botY + 5.0f);
        flame.lineTo(c.x + 2.0f, botY + 1.0f);
        g.strokePath(flame, juce::PathStrokeType(1.1f));
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        // Left column: "CHARACTER" caption + selectable character cards, laid out
        // exactly like the Reverb panel's character stack (glyph + name + highlight).
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        g.drawText("CHARACTER", mCharCap, juce::Justification::topLeft);

        const int sel = juce::jlimit(0, 2, (int)mApvts.getRawParameterValue("delayCharacter")->load());
        for (int i = 0; i < (int)mCardRects.size(); ++i)
        {
            auto r = mCardRects[(size_t)i].toFloat();
            const bool on = (i == sel);
            g.setColour(on ? colors::accent.withAlpha(0.12f) : juce::Colour(0xff181b21));
            g.fillRoundedRectangle(r, 10.0f);
            g.setColour(on ? colors::accent.withAlpha(0.55f) : colors::cardBorder);
            g.drawRoundedRectangle(r, 10.0f, 1.0f);
            // Per-character glyph that describes the sound (see drawCharGlyph).
            auto gly = juce::Rectangle<float>(22.0f, 22.0f).withCentre(
                {r.getX() + 26.0f, r.getCentreY()});
            drawCharGlyph(g, i, gly, on ? colors::accent : colors::caption);
            g.setColour(on ? colors::textBright : colors::text2);
            g.setFont(fonts::archivo(14.0f, fonts::SemiBold));
            // Left-aligned. The icon box sits 15px in from the card's left edge
            // (centre +26, half-width 11 -> left 15, right 37), so start the label
            // 15px past the icon's right edge to match that padding: 37 + 15 = 52.
            g.drawText(kCharName(i), r.withTrimmedLeft(52), juce::Justification::centredLeft);
        }

        // Right column: big character name header (mirrors the Reverb name line).
        g.setColour(colors::textBright);
        g.setFont(fonts::archivo(18.0f, fonts::Bold));
        g.drawText(kCharName(sel), mNameRect, juce::Justification::centredLeft);

        // Faint grouped tile behind the sync module (its edge is the fence).
        if (!mModuleTile.isEmpty())
        {
            auto t = mModuleTile.toFloat();
            g.setColour(juce::Colour(0xff181b21));
            g.fillRoundedRectangle(t, 12.0f);
            g.setColour(colors::cardBorder);
            g.drawRoundedRectangle(t, 12.0f, 1.0f);
        }
        if (!mSyncSep.isEmpty())
        {
            g.setColour(colors::divider);
            g.fillRect(mSyncSep);
        }

        // Caption centred over the Stereo Mode dropdown (the Free/Sync toggle labels
        // itself, so it needs no separate title).
        if (mModeBox.isVisible())
        {
            g.setColour(colors::caption);
            g.setFont(fonts::archivo(9.0f, fonts::SemiBold, 0.10f));
            g.drawText("STEREO MODE", mModeCap, juce::Justification::centred);
        }
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        for (int i = 0; i < (int)mCardRects.size(); ++i)
            if (mCardRects[(size_t)i].contains(e.getPosition()))
            {
                if (mCharacter) mCharacter->setEnabledIndex(i); // drives delayCharacter + onChange
                return;
            }
    }

    void resized() override
    {
        const int chr = (int)mApvts.getRawParameterValue("delayCharacter")->load();
        const bool space = chr == 2; // Space Tape (multi-head)
        const bool tape = chr != 0;  // any tape character = mono (no Sync R / Mode)

        // Header-right: delay-only Presets menu (where Reverb keeps Freeze).
        auto hr = headerArea();
        mPresetBtn.setBounds(hr.removeFromRight(118).withSizeKeepingCentre(118, 30));

        auto body = bodyArea().reduced(24, 18);
        auto left = body.removeFromLeft(166);
        body.removeFromLeft(20);
        auto right = body;
        right.setBottom(bodyArea().getBottom() - 6); // reclaim the bottom padding for the knob band

        // --- left column: CHARACTER caption + 3 character cards ---------------
        mCharCap = left.removeFromTop(14);
        left.removeFromTop(8);
        const int n = 3, gap = 8;
        // Card height is the SAME as a Reverb card: the left column is identical, so
        // we divide it by the reverb character count (kNumShipped) instead of our 3.
        // A Delay card == a Reverb card; with fewer characters the column's lower
        // space is simply left empty rather than stretching the cards taller.
        const int rvN = nam_rig::ReverbBlock::kNumShipped;
        const int ch = juce::jmax(34, (left.getHeight() - gap * (rvN - 1)) / rvN);
        mCardRects.clear();
        for (int i = 0; i < n; ++i)
        {
            mCardRects.push_back(left.removeFromTop(ch));
            if (i < n - 1) left.removeFromTop(gap);
        }

        // --- right column: name header --------------------------------------
        mNameRect = right.removeFromTop(24);
        right.removeFromTop(8);

        // "ECHO TAPS" / "HEADS / TIME" caption above the visualiser.
        auto capRow = right.removeFromTop(16);
        mTapsCaption.setBounds(capRow.removeFromLeft(140).withSizeKeepingCentre(140, 16));
        right.removeFromTop(4);

        // --- bottom band: effect knobs | divider | sync module ----------------
        // Every dial shares ONE baseline (anchored to the bottom of the band) so the
        // whole row reads uniformly. The sync module on the right is only as wide as
        // its content: the Sync toggle + Stereo Mode dropdown (equal width) sit above
        // the Sync R knob, which lines up with the main dials.
        auto bottom = right.removeFromBottom(162); // taller band -> uses the reclaimed bottom space
        right.removeFromBottom(10);
        mTaps->setBounds(right);

        const int knobH = 92;                 // shared knob-cell height (caption+dial+value)
        const int moduleW = 120;              // grouped sync tile

        // Right sync module, inside a faint grouped tile (the tile edge is the fence,
        // so there's no separate divider line).
        auto moduleCol = bottom.removeFromRight(moduleW);
        bottom.removeFromRight(16);           // gap between the knobs and the module
        mModuleTile = moduleCol;
        mSyncSep = {};                        // tile replaces the standalone divider

        auto tileFull = moduleCol; // the whole tile, before carving out the dropdown

        // Stereo Mode dropdown anchored at the TOP of the tile -> it stays put whether
        // or not the Sync R knob is showing below.
        moduleCol.removeFromTop(8);
        mModeCap = moduleCol.removeFromTop(12);
        moduleCol.removeFromTop(2);
        auto ctlRow = moduleCol.removeFromTop(28);
        mModeBox.setBounds(ctlRow.withSizeKeepingCentre(juce::jmin(96, moduleW), 28));

        // Knob slot: Clean's Sync R centres in the space below the dropdown; Space
        // Tape has no dropdown, so its Mode knob centres in the WHOLE tile.
        auto knobArea = space ? tileFull : moduleCol;
        auto srKnob = knobArea.withSizeKeepingCentre(juce::jmin(96, moduleW),
                                                     juce::jmin(knobArea.getHeight(), knobH));
        if (!tape && mSyncRKnob) mSyncRKnob->setBounds(srKnob);
        if (space && mHeadKnob) mHeadKnob->setBounds(srKnob);

        // Main row: a Free/Sync toggle leads (next to Time), then the six effect knobs.
        // They fill the FULL band height so the left side isn't bottom-heavy under the
        // tall sync tile on the right.
        auto mainRow = bottom; // use the whole band, not just the bottom knob strip
        auto togCell = mainRow.removeFromLeft(80);
        if (mSyncToggle)
        {
            auto tb = togCell.withSizeKeepingCentre(66, 56);
            tb.setX(togCell.getX());        // nudge the toggle to the left of its cell
            mSyncToggle->setBounds(tb);
        }
        const int cellW = mainRow.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
        {
            auto cell = mainRow.removeFromLeft(cellW).reduced(0, 8); // fill the band vertically
            k->setBounds(cell.withSizeKeepingCentre(juce::jmin(cellW - 12, 108), cell.getHeight()));
        }
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    juce::Label mTapsCaption;
    juce::TextButton mPresetBtn;  // delay-only preset menu
    std::unique_ptr<VToggle> mSyncToggle; // Free / Sync vertical switch (next to Time)
    juce::ComboBox mModeBox;      // Single / Dual / Ping-Pong stereo dropdown
    std::unique_ptr<LabeledKnob> mSyncRKnob, mHeadKnob;
    std::unique_ptr<SegmentedControl> mCharacter; // Clean / Tape Echo / Space Tape (hidden bridge)
    std::unique_ptr<DelayTaps> mTaps;
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    std::vector<juce::Rectangle<int>> mCardRects; // character card hit/paint rects
    juce::Rectangle<int> mCharCap, mNameRect, mSyncSep; // caption + name header + divider
    juce::Rectangle<int> mModeCap, mModuleTile;        // STEREO MODE caption + grouped sync tile
    juce::StringArray mDivNames;  // tempo divisions for the Time knob's synced dropdown
    int mLastDiv = 6;             // remembered division (1/4) for the Sync toggle
    bool mWasSynced = false;      // tracks free/sync to rebind the Time knob only on change
    std::function<float()> mTimeMsProvider, mTimeMsRProvider; // measured L / R delay ms
};

//==============================================================================
// Reverb impulse sketch, read left-to-right: PRE-DELAY gap -> EARLY reflections
// (discrete spikes, spread by Size) -> LATE diffuse FIELD (two accent envelopes:
// ENERGY full-length + HIGHS dying faster as Tone darkens). Decay sets how far
// right the tail reaches (max Decay == the right edge, no further); Pre-delay
// slides the onset right; Mod ripples the tail outline. A dashed RT60 marker
// sits at the tail end. The background is a dithered radial gradient (no banding).
class ReverbField : public juce::Component
{
public:
    // sizeFrac/preFrac/modFrac are pre-normalised 0..1 (the panel collapses the
    // exposed/hidden knobs to a neutral default before calling). rtMax is THIS
    // character's Decay knob maximum, so rt==rtMax lands the tail on the right edge.
    void setParams(float rtSec, float rtMax, float tone01, float preFrac,
                   float sizeFrac, float modFrac, bool frozen)
    {
        mRtMax  = juce::jmax(0.1f, rtMax);
        mRt     = juce::jlimit(0.05f, mRtMax, rtSec);
        mTone   = juce::jlimit(0.0f, 1.0f, tone01);
        mPre    = juce::jlimit(0.0f, 1.0f, preFrac);
        mSize   = juce::jlimit(0.0f, 1.0f, sizeFrac);
        mMod    = juce::jlimit(0.0f, 1.0f, modFrac);
        mFrozen = frozen;
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat();

        // Dithered radial gradient (cached) to defeat the 8-bit banding rings.
        ensureBackground((int)std::ceil(r.getWidth()), (int)std::ceil(r.getHeight()));
        if (mBg.isValid())
        {
            juce::Graphics::ScopedSaveState ss(g);
            juce::Path clip;
            clip.addRoundedRectangle(r, 13.0f);
            g.reduceClipRegion(clip);
            g.drawImageAt(mBg, 0, 0);
        }
        g.setColour(colors::cardBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 13.0f, 1.0f);

        auto in = r.reduced(14.0f, 16.0f);
        const float cy = in.getCentreY();
        const float halfH = in.getHeight() * 0.5f - 2.0f;
        const float xL = in.getX();
        const float xR = in.getRight();
        const float fullW = xR - xL;

        g.setColour(juce::Colour(0xff262c36));
        g.fillRect(xL, cy, fullW, 1.0f);

        // --- onset geometry ---------------------------------------------------
        // Pre-delay slides the onset right; Size sets how far the early
        // reflections spread before the diffuse tail starts. The tail then fills
        // the remaining width as Decay -> rtMax (so max Decay reaches xR exactly).
        const float onsetX = xL + mPre * fullW * 0.22f;            // pre-delay gap
        const float earlyW = fullW * (0.05f + 0.16f * mSize);      // early spread (Size)
        const float lateX0 = onsetX + earlyW;                      // diffuse tail start
        const float rtFrac = mFrozen ? 1.0f : juce::jlimit(0.0f, 1.0f, mRt / mRtMax);
        const float lateEndX = lateX0 + rtFrac * (xR - lateX0);

        // --- pre-delay lead-in (dotted) so the gap reads as a delay -----------
        if (mPre > 0.001f)
        {
            g.setColour(colors::accent.withAlpha(0.30f));
            for (float xx = xL; xx < onsetX - 2.0f; xx += 5.0f)
                g.fillRect(xx, cy - 0.75f, 2.5f, 1.5f);
        }

        // --- late diffuse field: two accent envelopes, Mod ripples the outline -
        const float modDepth = 0.30f * mMod;
        const float span = juce::jmax(1.0f, lateEndX - lateX0);
        // Gentle visual decay (~3.5) so the tail BODY stays visible across its whole
        // span and reaches the right edge at max Decay (a -60 dB curve would crush
        // all the visible action into the first third). kDie<1 dies sooner (highs).
        auto envAmp = [&](float u, float kDie, float cycles, float phase)
        {
            float a = std::exp(-3.5f * u / juce::jmax(0.05f, kDie));
            a *= 1.0f + modDepth * std::sin(u * cycles * juce::MathConstants<float>::twoPi + phase);
            return juce::jmax(0.0f, a);
        };
        auto spindle = [&](float scale, float kDie, float cycles, float phase)
        {
            juce::Path p;
            const int N = 220;
            for (int i = 0; i < N; ++i)
            {
                const float u = (float)i / (float)(N - 1);
                const float a = halfH * scale * envAmp(u, kDie, cycles, phase);
                const float x = lateX0 + u * span;
                if (i == 0) p.startNewSubPath(x, cy - a);
                else        p.lineTo(x, cy - a);
            }
            for (int i = N - 1; i >= 0; --i)
            {
                const float u = (float)i / (float)(N - 1);
                const float a = halfH * scale * envAmp(u, kDie, cycles, phase);
                p.lineTo(lateX0 + u * span, cy + a);
            }
            p.closeSubPath();
            return p;
        };
        auto fill = [&](const juce::Path &p, float peak)
        {
            juce::ColourGradient grad(colors::accent.withAlpha(0.0f), 0.0f, cy - halfH,
                                      colors::accent.withAlpha(0.0f), 0.0f, cy + halfH, false);
            grad.addColour(0.5, colors::accent.withAlpha(peak));
            g.setGradientFill(grad);
            g.fillPath(p);
        };
        const float kEnergy = mFrozen ? 12.0f : 1.0f;                  // frozen ~= endless
        const float kHi     = mFrozen ? 12.0f : (0.30f + 0.55f * mTone); // bright Tone -> highs ring longer
        fill(spindle(1.0f, kEnergy, 5.0f, 0.0f), 0.22f);   // ENERGY (full length, faint)
        fill(spindle(0.92f, kHi, 7.0f, 1.6f), 0.5f);       // HIGHS  (dies faster, brighter)

        // --- early reflections: discrete spikes onsetX..lateX0, gaps shrinking
        //     as they build into the tail; Size widens the whole cluster. -------
        const int K = 6;
        for (int k = 0; k < K; ++k)
        {
            const float f = (float)k / (float)(K - 1);
            const float xx = onsetX + earlyW * (1.0f - (1.0f - f) * (1.0f - f)); // gaps shrink
            const float h = halfH * (0.85f - 0.42f * f);
            g.setColour(colors::accent.withAlpha(0.6f - 0.28f * f));
            g.fillRect(xx - 0.9f, cy - h, 1.8f, 2.0f * h);
        }

        // --- RT60 marker (dashed) at the tail end -----------------------------
        // Shown even at max Decay: pin it just inside the right edge and flip the
        // "RT x.x s" label to the LEFT of the line when it nears that edge. Dashes
        // start below the caption/legend row so they don't collide with it.
        if (!mFrozen)
        {
            const float mx = juce::jmin(lateEndX, xR - 1.0f);
            for (float yy = in.getY() + 16.0f; yy < in.getBottom() - 14.0f; yy += 6.0f)
            {
                g.setColour(juce::Colour(0xff5a616b));
                g.fillRect(mx, yy, 1.0f, 3.0f);
            }
            g.setColour(colors::text2);
            g.setFont(fonts::mono(9.0f, fonts::Medium));
            const float lw = 64.0f;
            const bool flip = mx + 5.0f + lw > xR;
            g.drawText("RT " + juce::String(mRt, 1) + " s",
                       flip ? juce::Rectangle<float>(mx - 5.0f - lw, in.getBottom() - 13.0f, lw, 12.0f)
                            : juce::Rectangle<float>(mx + 5.0f, in.getBottom() - 13.0f, lw, 12.0f),
                       flip ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }

        // --- legend (bigger swatches) -----------------------------------------
        // Right-align the swatch glyphs + labels flush into the top-right corner.
        const auto legendFont = fonts::mono(10.0f, fonts::SemiBold);
        const float boxW = 13.0f, gapW = 5.0f, tailW = 6.0f;
        auto txtW = [&](const char *t) { return juce::GlyphArrangement::getStringWidth(legendFont, t); };
        auto entryW = [&](const char *t) { return boxW + gapW + txtW(t) + tailW; };
        const float legW = mFrozen ? 80.0f : (entryW("ENERGY") + entryW("HIGHS"));
        auto legend = juce::Rectangle<float>(in.getRight() - legW, in.getY(), legW, 16.0f);
        auto swatch = [&](juce::Rectangle<float> &a, float alpha, const char *t)
        {
            auto box = a.removeFromLeft(boxW).withSizeKeepingCentre(12.0f, 12.0f);
            g.setColour(colors::accent.withAlpha(alpha));
            g.fillRoundedRectangle(box, 3.0f);
            a.removeFromLeft(gapW);
            g.setColour(colors::caption);
            g.setFont(legendFont);
            g.drawText(t, a.removeFromLeft(txtW(t) + tailW), juce::Justification::centredLeft);
        };
        if (mFrozen)
        {
            g.setColour(colors::accent);
            g.setFont(fonts::mono(10.0f, fonts::Medium));
            g.drawText("RT  frozen", legend, juce::Justification::centredRight);
        }
        else
        {
            swatch(legend, 0.45f, "ENERGY");
            swatch(legend, 0.95f, "HIGHS");
        }
    }

private:
    // Build a per-pixel dithered radial gradient once per size. Adding ±1 LSB of
    // noise before the 8-bit write breaks up the concentric banding rings that a
    // plain juce::ColourGradient shows across these two near-black colours.
    void ensureBackground(int w, int h)
    {
        w = juce::jmax(1, w);
        h = juce::jmax(1, h);
        if (mBg.isValid() && w == mBgW && h == mBgH)
            return;
        mBgW = w;
        mBgH = h;
        mBg = juce::Image(juce::Image::ARGB, w, h, false);
        juce::Image::BitmapData bd(mBg, juce::Image::BitmapData::writeOnly);
        const juce::Colour c1(0xff181c26), c2(0xff0e1118);
        const float r1 = c1.getFloatRed(),   g1 = c1.getFloatGreen(),   b1 = c1.getFloatBlue();
        const float r2 = c2.getFloatRed(),   g2 = c2.getFloatGreen(),   b2 = c2.getFloatBlue();
        const float p1x = (float)w * 0.1f, p1y = (float)h * 0.5f; // inner (brighter)
        const float p2x = (float)w,        p2y = (float)h;        // outer (darker)
        const float rad = std::sqrt((p2x - p1x) * (p2x - p1x) + (p2y - p1y) * (p2y - p1y));
        const float invRad = rad > 0.0f ? 1.0f / rad : 0.0f;
        juce::Random rng(0x5eed1010); // fixed seed -> stable noise, no per-frame shimmer
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float dx = (float)x - p1x, dy = (float)y - p1y;
                const float d = juce::jlimit(0.0f, 1.0f, std::sqrt(dx * dx + dy * dy) * invRad);
                const float dz = (rng.nextFloat() - 0.5f) * (2.2f / 255.0f); // ±~1 LSB dither
                bd.setPixelColour(x, y, juce::Colour::fromFloatRGBA(
                    juce::jlimit(0.0f, 1.0f, r1 + (r2 - r1) * d + dz),
                    juce::jlimit(0.0f, 1.0f, g1 + (g2 - g1) * d + dz),
                    juce::jlimit(0.0f, 1.0f, b1 + (b2 - b1) * d + dz), 1.0f));
            }
    }

    float mRt = 2.2f, mRtMax = 6.0f, mTone = 0.5f, mPre = 0.0f, mSize = 0.45f, mMod = 0.0f;
    bool mFrozen = false;
    juce::Image mBg;
    int mBgW = 0, mBgH = 0;
};

//==============================================================================
// REVERB — a typed CHARACTER reverb, presented like a single modulation slot:
// a Character selector + animated icon, and a row of knobs where only the
// controls that character actually has are shown (the rest are hardwired to its
// sweet spot in ReverbBlock). The voicing introspection (sizeExposed/...) is the
// single source of truth shared with the audio thread + tests.
class ReverbPanel : public BlockPanel
{
public:
    explicit ReverbPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("REVERB"), mApvts(apvts)
    {
        // Only shipped characters (Ambience/Bloom locked away — RB::shipped()/kNumShipped).
        // Item IDs are 1-based and equal Type+1, matching the revType choice indices.
        for (int t = 0; t < nam_rig::ReverbBlock::kNumShipped; ++t)
            mType.addItem(nam_rig::ReverbBlock::typeName(t), t + 1);
        addChildComponent(mType); // hidden parameter bridge; the cards drive it
        mTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "revType", mType);
        mType.onChange = [this] { refresh(); };
        addChildComponent(mIcon);

        // Shimmer pitch interval (only shown for Shimmer).
        mPitch.addItemList({"Octave", "+2 Oct", "Fifth+Oct"}, 1);
        addChildComponent(mPitch);
        mPitchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "revPitch", mPitch);

        // Freeze — infinite sustain. Only on the lush/evolving characters (Hall/Shimmer/
        // Bloom); hidden elsewhere via refresh() + RB::freezeExposed.
        mFreeze.setButtonText("Freeze");
        mFreeze.getProperties().set("pill", true);
        addChildComponent(mFreeze);
        mFreezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, juce::String(nam_rig::ReverbBlock::paramId("Freeze", nam_rig::ReverbBlock::kHall)), mFreeze);

        // Build every knob once; refresh() rebinds Decay/Tone/Size/Predelay/Mod to the
        // ACTIVE character's per-character param + shows the subset the character uses.
        using RB0 = nam_rig::ReverbBlock;
        mDecay = std::make_unique<LabeledKnob>(apvts, juce::String(RB0::paramId("Decay", RB0::kHall)), "Decay");
        mSize = std::make_unique<LabeledKnob>(apvts, juce::String(RB0::paramId("Size", RB0::kHall)), "Size");
        mPredelay = std::make_unique<LabeledKnob>(apvts, juce::String(RB0::paramId("Predelay", RB0::kPlate)), "Pre-Delay");
        mInputFilter = std::make_unique<LabeledKnob>(apvts, "revInputFilter", "Input Filter"); // Plate only
        mTone = std::make_unique<LabeledKnob>(apvts, juce::String(RB0::paramId("Tone", RB0::kHall)), "Damping");
        mMod = std::make_unique<LabeledKnob>(apvts, juce::String(RB0::paramId("Mod", RB0::kHall)), "Mod");
        mShimmer = std::make_unique<LabeledKnob>(apvts, "revShimmer", "Shimmer");
        mTension = std::make_unique<LabeledKnob>(apvts, "revTension", "Tension");
        mBoing = std::make_unique<LabeledKnob>(apvts, "revBoing", "Boing");
        mSwell = std::make_unique<LabeledKnob>(apvts, "revSwell", "Swell");
        mWidth = std::make_unique<LabeledKnob>(apvts, "revWidth", "Width");
        mMix = std::make_unique<LabeledKnob>(apvts, "revMix", "Mix");
        // Decay / Tone / Width / Mix are common to every character; rest are voiced.
        addAndMakeVisible(*mDecay);
        addAndMakeVisible(*mTone);
        addAndMakeVisible(*mWidth);
        addAndMakeVisible(*mMix);
        addChildComponent(*mSize);
        addChildComponent(*mPredelay);
        addChildComponent(*mInputFilter);
        addChildComponent(*mMod);
        addChildComponent(*mShimmer);
        addChildComponent(*mTension);
        addChildComponent(*mBoing);
        addChildComponent(*mSwell);

        addAndMakeVisible(mField);
        // Every knob that shapes the sketch refreshes it live. The sliders persist
        // across character changes (only their attachments rebind), so these stick.
        mDecay->slider().onValueChange = [this] { updateField(); };
        mTone->slider().onValueChange = [this] { updateField(); };
        mSize->slider().onValueChange = [this] { updateField(); };
        mPredelay->slider().onValueChange = [this] { updateField(); };
        mMod->slider().onValueChange = [this] { updateField(); };
        mFreeze.onClick = [this] { updateField(); };

        refresh();
    }

    void updateField()
    {
        const float rtSec = (float)mDecay->slider().getValue();    // true seconds (per-character window)
        const float rtMax = (float)mDecay->slider().getMaximum();  // this character's Decay cap -> right edge
        const float tone = (float)mTone->slider().valueToProportionOfLength(mTone->slider().getValue());
        const bool frozen = mFreeze.isVisible() && mFreeze.getToggleState();
        // Only the knobs the active character actually exposes drive the sketch;
        // hidden ones collapse to a neutral default (no pre-delay, mid spread, no mod).
        float preFrac = 0.0f;
        if (mPredelay->isVisible())
        {
            const double mx = mPredelay->slider().getMaximum();
            preFrac = mx > 0.0 ? (float)(mPredelay->slider().getValue() / mx) : 0.0f;
        }
        const float sizeFrac = mSize->isVisible()
            ? (float)mSize->slider().valueToProportionOfLength(mSize->slider().getValue())
            : 0.45f;
        const float modFrac = mMod->isVisible()
            ? (float)mMod->slider().valueToProportionOfLength(mMod->slider().getValue())
            : 0.0f;
        mField.setParams(rtSec, rtMax, tone, preFrac, sizeFrac, modFrac, frozen);
    }

    void refresh()
    {
        // Read the live selection from the combo (kept in sync with the param by
        // its attachment) so a card click reliably updates the whole panel, with
        // no read-before-write race against the parameter.
        const int type = mType.getSelectedItemIndex();
        const bool on = mApvts.getRawParameterValue("reverbOn")->load() >= 0.5f;
        mIcon.setType(type);
        mIcon.setActive(on);

        if (type != mLastType)
        {
            mLastType = type;
            using RB = nam_rig::ReverbBlock;
            const auto t = (RB::Type)type;
            mSize->setVisible(RB::sizeExposed(t));
            mPredelay->setVisible(RB::predelayExposed(t));
            mInputFilter->setVisible(RB::inputFilterExposed(t));
            mMod->setVisible(RB::modExposed(t));
            mShimmer->setVisible(RB::shimmerExposed(t));
            mTension->setVisible(RB::tensionExposed(t));
            mBoing->setVisible(RB::boingExposed(t));
            mSwell->setVisible(RB::swellExposed(t));
            mPitch.setVisible(RB::pitchExposed(t));
            mFreeze.setVisible(RB::freezeExposed(t));
            // rebind shared knobs to THIS character's own params (own range + state)
            mDecay->rebind(mApvts, juce::String(RB::paramId("Decay", type)));
            mTone->rebind(mApvts, juce::String(RB::paramId("Tone", type)));
            if (RB::sizeExposed(t)) mSize->rebind(mApvts, juce::String(RB::paramId("Size", type)));
            if (RB::predelayExposed(t)) mPredelay->rebind(mApvts, juce::String(RB::paramId("Predelay", type)));
            if (RB::modExposed(t)) mMod->rebind(mApvts, juce::String(RB::paramId("Mod", type)));
            mFreezeAtt.reset(); // per-character Freeze: rebind to THIS character's own state
            if (RB::freezeExposed(t))
                mFreezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    mApvts, juce::String(RB::paramId("Freeze", type)), mFreeze);
            mTone->setCaption(RB::toneCaption(t));
            resized();
            repaint(); // card highlight + character name/description
        }
        updateField(); // always — reflects live Decay/Tone and the newly-bound character
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        for (int i = 0; i < (int)mCardRects.size(); ++i)
            if (mCardRects[(size_t)i].contains(e.getPosition()))
            {
                mType.setSelectedItemIndex(i);
                return;
            }
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        g.drawText("CHARACTER", mCharCap, juce::Justification::topLeft);

        for (int i = 0; i < (int)mCardRects.size(); ++i)
        {
            auto r = mCardRects[(size_t)i].toFloat();
            const bool sel = (i == mLastType);
            g.setColour(sel ? colors::accent.withAlpha(0.12f) : juce::Colour(0xff181b21));
            g.fillRoundedRectangle(r, 10.0f);
            g.setColour(sel ? colors::accent.withAlpha(0.55f) : colors::cardBorder);
            g.drawRoundedRectangle(r, 10.0f, 1.0f);
            // small concentric-arc glyph.
            auto gly = juce::Rectangle<float>(22.0f, 22.0f).withCentre(
                {r.getX() + 26.0f, r.getCentreY()});
            g.setColour(sel ? colors::accent : colors::caption);
            for (float rad : {4.0f, 8.0f, 11.0f})
            {
                juce::Path arc;
                arc.addCentredArc(gly.getCentreX(), gly.getCentreY(), rad, rad, 0.0f,
                                  juce::degreesToRadians(-65.0f), juce::degreesToRadians(65.0f), true);
                g.strokePath(arc, juce::PathStrokeType(1.7f));
            }
            g.setColour(sel ? colors::textBright : colors::text2);
            g.setFont(fonts::archivo(14.0f, fonts::SemiBold));
            g.drawText(nam_rig::ReverbBlock::typeName(i), r.withTrimmedLeft(46),
                       juce::Justification::centredLeft);
        }

        // Right column: character name (no description).
        const int t = juce::jmax(0, mLastType);
        g.setColour(colors::textBright);
        g.setFont(fonts::archivo(18.0f, fonts::Bold));
        g.drawText(nam_rig::ReverbBlock::typeName(t), mNameRect, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto hr = headerArea();
        mFreeze.setBounds(hr.removeFromRight(80).withSizeKeepingCentre(80, 30));

        auto body = bodyArea().reduced(24, 18);
        auto left = body.removeFromLeft(166);
        body.removeFromLeft(20);
        auto right = body;

        mCharCap = left.removeFromTop(14);
        left.removeFromTop(8);
        const int n = nam_rig::ReverbBlock::kNumShipped;
        const int gap = 8;
        const int ch = juce::jmax(34, (left.getHeight() - gap * (n - 1)) / n);
        mCardRects.clear();
        for (int i = 0; i < n; ++i)
        {
            mCardRects.push_back(left.removeFromTop(ch));
            if (i < n - 1) left.removeFromTop(gap);
        }

        auto nameRow = right.removeFromTop(24);
        if (mPitch.isVisible())
            mPitch.setBounds(nameRow.removeFromRight(124).withSizeKeepingCentre(124, 26));
        mNameRect = nameRow;
        right.removeFromTop(8);

        auto fieldArea = right.removeFromTop(juce::jmax(150, right.getHeight() - 150));
        mField.setBounds(fieldArea);
        right.removeFromTop(10);

        std::vector<juce::Component *> vis;
        for (juce::Component *k : {(juce::Component *)mDecay.get(), (juce::Component *)mSize.get(),
                                   (juce::Component *)mPredelay.get(), (juce::Component *)mInputFilter.get(),
                                   (juce::Component *)mTone.get(),
                                   (juce::Component *)mMod.get(), (juce::Component *)mTension.get(),
                                   (juce::Component *)mBoing.get(),
                                   (juce::Component *)mShimmer.get(), (juce::Component *)mSwell.get(),
                                   (juce::Component *)mWidth.get(), (juce::Component *)mMix.get()})
            if (k->isVisible())
                vis.push_back(k);
        const int nk = (int)vis.size();
        if (nk == 0)
            return;
        const int w = juce::jmin(104, right.getWidth() / nk);
        auto row = right.withSizeKeepingCentre(w * nk, juce::jmin(right.getHeight(), 116));
        for (auto *k : vis)
            k->setBounds(row.removeFromLeft(w).reduced(5, 0));
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    int mLastType = -1;
    ReverbIcon mIcon;
    juce::ComboBox mType, mPitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mTypeAtt, mPitchAtt;
    juce::ToggleButton mFreeze;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mFreezeAtt;
    ReverbField mField;
    std::vector<juce::Rectangle<int>> mCardRects;
    juce::Rectangle<int> mCharCap, mNameRect;
    std::unique_ptr<LabeledKnob> mDecay, mSize, mPredelay, mTone, mMod, mShimmer, mTension, mBoing, mSwell, mWidth, mMix, mInputFilter;
};

//==============================================================================
// Slide-switch + "Invert ø" label used per rig in the Mix panel. Decoupled from
// the parameter (no attachment): the host writes the param on click and mirrors
// the saved value back via setStateQuiet(), so Solo can show "not inverted"
// while the Dual value is retained. The whole strip (track + label) is clickable.
class InvertSwitch : public juce::Component
{
public:
    std::function<void(bool)> onToggle;

    void setAccent(juce::Colour c) { if (mAccent != c) { mAccent = c; repaint(); } }
    void setStateQuiet(bool on) { if (on != mOn) { mOn = on; repaint(); } }
    bool state() const { return mOn; }

    void enablementChanged() override { repaint(); }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!isEnabled() || !getLocalBounds().contains(e.getPosition())) return;
        mOn = !mOn;
        repaint();
        if (onToggle) onToggle(mOn);
    }

    void paint(juce::Graphics &g) override
    {
        const bool en = isEnabled();
        const float th = 23.0f, tw = 40.0f;
        juce::Rectangle<float> track(0.0f, (getHeight() - th) * 0.5f, tw, th);
        g.setColour(en && mOn ? mAccent.withAlpha(0.92f) : colors::tile);
        g.fillRoundedRectangle(track, th * 0.5f);
        g.setColour(en && mOn ? mAccent : colors::outline);
        g.drawRoundedRectangle(track.reduced(0.5f), th * 0.5f, 1.0f);
        const float d = 17.0f;
        const float kx = mOn ? track.getRight() - d - 2.0f : track.getX() + 2.0f;
        g.setColour(en ? (mOn ? colors::bg : colors::text2) : colors::captionDim);
        g.fillEllipse(kx, track.getY() + (th - d) * 0.5f, d, d);

        g.setColour(en ? colors::text2 : colors::captionDim);
        g.setFont(fonts::archivo(12.0f, fonts::Medium));
        g.drawText(juce::String::fromUTF8("Invert \xC3\xB8"),
                   juce::Rectangle<int>((int)tw + 9, 0, getWidth() - (int)tw - 9, getHeight()),
                   juce::Justification::centredLeft);
    }

private:
    bool mOn = false;
    juce::Colour mAccent{colors::accent};
};

// Per-rig OUT L·R meter: two thin vertical bars (tag-coloured fill over a dark
// track) with an "OUT L·R" caption beneath. Fed dBFS from the editor timer with
// a peak-hold fall-back so brief peaks stay readable.
class OutMeter : public juce::Component
{
public:
    static constexpr float kMinDb = -60.0f, kFallDbPerSec = 36.0f;

    void setAccent(juce::Colour c) { mAccent = c; }

    void push(float lDb, float rDb, float dt)
    {
        mL = juce::jlimit(kMinDb, 6.0f, juce::jmax(lDb, mL - kFallDbPerSec * dt));
        mR = juce::jlimit(kMinDb, 6.0f, juce::jmax(rDb, mR - kFallDbPerSec * dt));
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        const float barW = 7.0f, barGap = 4.0f, barH = 40.0f;
        const float totalW = barW * 2 + barGap;
        const float x0 = (getWidth() - totalW) * 0.5f;
        const bool en = isEnabled();
        auto bar = [&](float x, float db)
        {
            juce::Rectangle<float> b(x, 0.0f, barW, barH);
            g.setColour(colors::track);
            g.fillRoundedRectangle(b, 3.0f);
            const float norm = juce::jlimit(0.0f, 1.0f, (db - kMinDb) / (0.0f - kMinDb));
            if (norm > 0.001f)
            {
                g.setColour(en ? mAccent : colors::captionDim);
                g.fillRoundedRectangle(b.withTrimmedTop(barH * (1.0f - norm)), 3.0f);
            }
        };
        bar(x0, mL);
        bar(x0 + barW + barGap, mR);

        g.setColour(colors::caption);
        g.setFont(fonts::mono(8.5f, fonts::SemiBold, 0.1f));
        g.drawText(juce::String::fromUTF8("OUT L\xC2\xB7R"),
                   juce::Rectangle<int>(0, (int)barH + 4, getWidth(), 12),
                   juce::Justification::centred);
    }

private:
    float mL = kMinDb, mR = kMinDb;
    juce::Colour mAccent{colors::accent};
};

// MIX — dual-rig routing: mode (Solo A / Solo B / Dual), per-rig level + pan +
// polarity, the phase-align nudge, and the Auto-align button (probes both
// voices). The two rigs merge here into the shared stereo section.
class MixPanel : public BlockPanel
{
public:
    explicit MixPanel(NamRigProcessor &proc) : BlockPanel("MIX"), mProc(proc)
    {
        // Full Solo A / Solo B / Dual selector, kept in sync with the branch
        // switch (both read/write rigMode). Manual mapping: A=0, B=1, Dual=2.
        mModes = std::make_unique<SegmentedControl>(juce::StringArray{"Solo A", "Solo B", "Dual"});
        mModes->onChange = [this](int i) {
            if (auto *p = mProc.apvts.getParameter("rigMode"))
                p->setValueNotifyingHost(p->convertTo0to1((float)i));
        };
        addAndMakeVisible(*mModes);

        mLevelA = std::make_unique<LabeledKnob>(mProc.apvts, "rigLevelA", "Level");
        mPanA = std::make_unique<LabeledKnob>(mProc.apvts, "rigPanA", "Pan");
        mLevelB = std::make_unique<LabeledKnob>(mProc.apvts, "rigLevelB", "Level");
        mPanB = std::make_unique<LabeledKnob>(mProc.apvts, "rigPanB", "Pan");
        mAlign = std::make_unique<LabeledKnob>(mProc.apvts, "rigAlign", "Align");
        for (auto *k : {mLevelA.get(), mPanA.get(), mLevelB.get(), mPanB.get(), mAlign.get()})
            addAndMakeVisible(*k);

        mPolA.setAccent(colors::laneColour(0));
        mPolB.setAccent(colors::laneColour(1));
        addAndMakeVisible(mPolA);
        addAndMakeVisible(mPolB);
        // No attachment: the displayed switch is decoupled from the saved
        // parameter so Solo can read "not inverted" while the value is retained
        // for Dual. Clicks (only possible in Dual) write the parameter; refresh()
        // mirrors it back into the switch.
        mPolA.onToggle = [this](bool on) {
            if (auto *p = mProc.apvts.getParameter("rigPolA"))
                p->setValueNotifyingHost(on ? 1.0f : 0.0f);
        };
        mPolB.onToggle = [this](bool on) {
            if (auto *p = mProc.apvts.getParameter("rigPolB"))
                p->setValueNotifyingHost(on ? 1.0f : 0.0f);
        };

        mMeterA.setAccent(colors::laneColour(0));
        mMeterB.setAccent(colors::laneColour(1));
        addAndMakeVisible(mMeterA);
        addAndMakeVisible(mMeterB);

        mAutoBtn.onClick = [this] { mProc.autoAlign(); };
        addAndMakeVisible(mAutoBtn);
        mMatchBtn.onClick = [this] { mProc.matchLevels(); };
        addAndMakeVisible(mMatchBtn);
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        // Two rounded row "cards" behind each rig's controls.
        for (int rig = 0; rig < 2; ++rig)
        {
            auto card = (rig == 0 ? mCardA : mCardB).toFloat();
            if (card.isEmpty()) continue;
            g.setColour(rig == 0 ? juce::Colour(0xff1b1f26) : juce::Colour(0xff191e23));
            g.fillRoundedRectangle(card, 10.0f);
            g.setColour(juce::Colour(0xff2a2f37));
            g.drawRoundedRectangle(card.reduced(0.5f), 10.0f, 1.0f);

            // Letter + amp-name label stack at the card's left.
            auto stack = (rig == 0 ? mTagA : mTagB);
            const bool live = (rig == 0 ? mLiveA : mLiveB);
            const juce::Colour tag = rig == 0 ? colors::laneColour(0) : colors::laneColour(1);
            g.setColour(live ? tag : colors::captionDim);
            g.setFont(fonts::archivo(16.0f, fonts::ExtraBold));
            g.drawText(rig == 0 ? "A" : "B",
                       stack.removeFromTop(20), juce::Justification::centred);
            stack.removeFromTop(2);
            g.setColour(colors::caption);
            g.setFont(fonts::mono(9.5f, fonts::Medium));
            g.drawText(rig == 0 ? mNameA : mNameB, stack.removeFromTop(14),
                       juce::Justification::centred);
        }
    }

    // Called from the editor timer (dt = seconds since last tick).
    void refresh(float dt)
    {
        const int mode = (int)mProc.apvts.getRawParameterValue("rigMode")->load();
        const bool dual = (mode == 2);
        // A rig's row is live when it's in the path AND its amp is engaged, so a
        // bypassed amp greys that rig's Mix knobs too. rigMode 0=Solo A,1=Solo B,2=Dual.
        const bool ampA = mProc.apvts.getRawParameterValue("ampOnA")->load() >= 0.5f;
        const bool ampB = mProc.apvts.getRawParameterValue("ampOnB")->load() >= 0.5f;
        const bool aOn = (mode != 1) && ampA;
        const bool bOn = (mode != 0) && ampB;
        mModes->setActive(mode); // A=0, B=1, Dual=2
        mLevelA->setEnabled(aOn);
        mLevelB->setEnabled(bOn);
        // Pan only matters in Dual (Solo plays centered) and on a live row.
        mPanA->setEnabled(dual && aOn);
        mPanB->setEnabled(dual && bOn);
        // Polarity is a SAVED state but only shown/active in Dual. In Solo the
        // switch reads "not inverted" without clearing the stored value, so it
        // pops back to inverted when Dual is re-selected.
        const bool polA = mProc.apvts.getRawParameterValue("rigPolA")->load() >= 0.5f;
        const bool polB = mProc.apvts.getRawParameterValue("rigPolB")->load() >= 0.5f;
        mPolA.setEnabled(dual && aOn);
        mPolB.setEnabled(dual && bOn);
        mPolA.setStateQuiet(dual && aOn && polA);
        mPolB.setStateQuiet(dual && bOn && polB);
        // Alignment only applies when BOTH rigs are actually playing (Dual with
        // both amps engaged) — greyed in Single and when the amps are bypassed.
        const bool bothLoaded = mProc.isModelLoaded(0) && mProc.isModelLoaded(1);
        const bool aligning = dual && aOn && bOn;
        mAlign->setEnabled(aligning);
        mAutoBtn.setEnabled(aligning && bothLoaded);
        mMatchBtn.setEnabled(aligning && bothLoaded);

        // OUT L·R meters.
        mMeterA.setEnabled(aOn);
        mMeterB.setEnabled(bOn);
        mMeterA.push(mProc.rigOutLDb(0), mProc.rigOutRDb(0), dt);
        mMeterB.push(mProc.rigOutLDb(1), mProc.rigOutRDb(1), dt);

        // Amp-name captions + live state; repaint the labels when anything changed.
        auto nameFor = [this](int rig) {
            return mProc.isModelLoaded(rig)
                       ? mProc.getModelName(rig).upToFirstOccurrenceOf(".", false, false).toUpperCase()
                       : juce::String::fromUTF8("\xE2\x80\x94"); // em dash
        };
        const juce::String nA = nameFor(0), nB = nameFor(1);
        if (nA != mNameA || nB != mNameB || aOn != mLiveA || bOn != mLiveB)
        {
            mNameA = nA; mNameB = nB; mLiveA = aOn; mLiveB = bOn;
            repaint();
        }
    }

    void resized() override
    {
        // Mode pills sit in the header-right.
        auto hr = headerArea();
        const int mw = juce::jmin(mModes->idealWidth(), hr.getWidth());
        mModes->setBounds(hr.removeFromRight(mw).withSizeKeepingCentre(mw, 24));

        auto body = bodyArea().reduced(26, 18);
        const int cardH = 112, gap = 12;

        auto layoutRow = [&](juce::Rectangle<int> card, juce::Rectangle<int> &tagOut,
                             LabeledKnob &lvl, LabeledKnob &pan, InvertSwitch &pol, OutMeter &meter)
        {
            auto r = card.reduced(20, 10);
            // Label stack (drawn in paint): centre it vertically within the row.
            auto tag = r.removeFromLeft(58);
            tagOut = tag.withSizeKeepingCentre(58, 36);
            r.removeFromLeft(22);
            lvl.setBounds(r.removeFromLeft(88));
            r.removeFromLeft(22);
            pan.setBounds(r.removeFromLeft(88));
            r.removeFromLeft(22);
            pol.setBounds(r.removeFromLeft(130).withSizeKeepingCentre(130, 24));
            // Meter pinned to the card's right edge.
            meter.setBounds(r.removeFromRight(60).withSizeKeepingCentre(60, 56));
        };

        mCardA = body.removeFromTop(cardH);
        layoutRow(mCardA, mTagA, *mLevelA, *mPanA, mPolA, mMeterA);
        body.removeFromTop(gap);
        mCardB = body.removeFromTop(cardH);
        layoutRow(mCardB, mTagB, *mLevelB, *mPanB, mPolB, mMeterB);

        body.removeFromTop(gap + 4);
        auto alignRow = body.removeFromTop(96);
        mAlign->setBounds(alignRow.removeFromLeft(88));
        alignRow.removeFromLeft(20);
        mAutoBtn.setBounds(alignRow.removeFromLeft(122).withSizeKeepingCentre(122, 34));
        alignRow.removeFromLeft(10);
        mMatchBtn.setBounds(alignRow.removeFromLeft(132).withSizeKeepingCentre(132, 34));
    }

private:
    NamRigProcessor &mProc;
    std::unique_ptr<SegmentedControl> mModes;
    std::unique_ptr<LabeledKnob> mLevelA, mPanA, mLevelB, mPanB, mAlign;
    InvertSwitch mPolA, mPolB;
    OutMeter mMeterA, mMeterB;
    juce::TextButton mAutoBtn{"Auto-align"}, mMatchBtn{"Match Levels"};

    // Layout rects (set in resized, drawn in paint).
    juce::Rectangle<int> mCardA, mCardB, mTagA, mTagB;
    juce::String mNameA, mNameB;
    bool mLiveA = false, mLiveB = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPanel)
};

} // namespace nam_rig::ui
