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
            g.setColour((mDragging ? acc : colors::text2).withMultipliedAlpha(ga));
            g.setFont(fonts::mono(11.0f, fonts::Medium));
            g.drawText(txt, mValueRect, juce::Justification::centred);
        }
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
            mUnit = param->getLabel();
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
    int mCaptionH = 15, mValueH = 16;
    bool mShowValue = true, mDragging = false, mRotationReadout = false;
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

    int index() const { return juce::jmax(0, mCombo.getSelectedItemIndex()); }
    void setEnabledIndex(int i) { mCombo.setSelectedItemIndex(i); }

    int idealWidth() const
    {
        auto f = fonts::archivo(12.0f, fonts::SemiBold);
        int w = 0;
        for (auto &o : mOptions)
            w += (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, o)) + 26 + mGap;
        return juce::jmax(0, w - mGap);
    }

    void paint(juce::Graphics &g) override
    {
        auto f = fonts::archivo(12.0f, fonts::SemiBold);
        g.setFont(f);
        const int active = index();
        int x = 0;
        for (int i = 0; i < mOptions.size(); ++i)
        {
            const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, mOptions[i])) + 26;
            auto r = juce::Rectangle<float>((float)x, 0.0f, (float)w, (float)getHeight());
            const bool on = i == active;
            g.setColour(on ? colors::accent : colors::tile);
            g.fillRoundedRectangle(r, 7.0f);
            g.setColour(on ? colors::accent : colors::outline);
            g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);
            g.setColour(on ? colors::bg : colors::textDim);
            g.drawText(mOptions[i], r, juce::Justification::centred);
            x += w + mGap;
        }
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        auto f = fonts::archivo(12.0f, fonts::SemiBold);
        int x = 0;
        for (int i = 0; i < mOptions.size(); ++i)
        {
            const int w = (int)std::ceil(juce::GlyphArrangement::getStringWidth(f, mOptions[i])) + 26;
            if (e.x >= x && e.x < x + w) { mCombo.setSelectedItemIndex(i); return; }
            x += w + mGap;
        }
    }

private:
    juce::StringArray mOptions;
    juce::ComboBox mCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mAtt;
    int mGap = 8;
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
class BlockPanel : public juce::Component
{
public:
    static constexpr int kHeaderH = 46;

    explicit BlockPanel(const juce::String &title) : mTitle(title) {}

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

protected:
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
    static constexpr int N = 260;

    void setAccent(juce::Colour c) { mAccent = c; repaint(); }
    void setLabel(const juce::String &l) { mLabel = l; repaint(); }
    void setSpanDb(float d) { mMaxDb = d; }

    void push(float grDb, float dt)
    {
        grDb = juce::jlimit(0.0f, mMaxDb, grDb);
        mHist[(size_t)mW] = grDb;
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
        juce::Path area = line;
        area.lineTo(in.getRight(), in.getY());
        area.lineTo(in.getX(), in.getY());
        area.closeSubPath();
        juce::ColourGradient fill(mAccent.withAlpha(0.40f), in.getX(), in.getY(),
                                  mAccent.withAlpha(0.02f), in.getX(), in.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(area);
        g.setColour(mAccent);
        g.strokePath(line, juce::PathStrokeType(1.6f));

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
    float mCur = 0.0f, mMaxDb = 14.0f;
    juce::Colour mAccent = colors::accent;
    juce::String mLabel = "GR";
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

        mAnalyser.setLabel("ATTEN");
        mAnalyser.setAccent(colors::green);
        mAnalyser.setSpanDb(24.0f);
        addAndMakeVisible(mAnalyser);

        mDetail.setButtonText("DETAIL");
        mDetail.getProperties().set("pill", true);
        mDetail.setClickingTogglesState(true);
        mDetail.onClick = [this] { resized(); repaint(); };
        addAndMakeVisible(mDetail);
    }

    // Editor timer feed: gate attenuation (>=0 dB). Drives the live meter + state.
    void pushActivity(float redDb, float dt) { mCurDb = redDb; mAnalyser.push(redDb, dt); }

    void setLowLatency(bool ll)
    {
        if (mLowLat == ll) return;
        mLowLat = ll;
        if (mKnobs.size() >= 6) mKnobs[5]->setEnabled(!ll);
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        // OPEN / CLOSED state pill (header-right). Fixed min-width so the dot
        // never moves; label centred in the remaining slot.
        const bool open = mCurDb < 1.5f;
        auto pill = mStatePill.toFloat();
        g.setColour(open ? colors::green.withAlpha(0.14f) : colors::tile);
        g.fillRoundedRectangle(pill, 7.0f);
        g.setColour(open ? colors::green.withAlpha(0.5f) : colors::outline);
        g.drawRoundedRectangle(pill, 7.0f, 1.0f);
        auto pc = pill.reduced(11.0f, 0.0f);
        auto dot = juce::Rectangle<float>(7.0f, 7.0f).withCentre({pc.getX() + 3.5f, pc.getCentreY()});
        g.setColour(open ? colors::green : colors::caption);
        g.fillEllipse(dot);
        g.setColour(open ? colors::green : colors::textDim);
        g.setFont(fonts::archivo(10.0f, fonts::Bold, 0.12f));
        g.drawText(open ? "OPEN" : "CLOSED", pc.withTrimmedLeft(10), juce::Justification::centred);

        if (mDetail.getToggleState())
        {
            RigLookAndFeel::drawWell(g, mDetailWell.toFloat());
            g.setColour(colors::text2);
            g.setFont(fonts::mono(9.0f, fonts::SemiBold, 0.14f));
            g.drawText("LAST TRIGGER", mDetailWell.reduced(12, 10).removeFromTop(12),
                       juce::Justification::topLeft);
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(10.0f, fonts::Medium));
            g.drawText("waiting for first trigger…", mDetailWell, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto hr = headerArea();
        const int pillW = 90;
        mStatePill = hr.removeFromRight(pillW).withSizeKeepingCentre(pillW, 24);
        hr.removeFromRight(10);
        mDetail.setBounds(hr.removeFromRight(58).withSizeKeepingCentre(58, 24));

        auto area = bodyArea().reduced(24, 14);
        auto knobRow = area.removeFromBottom(118);
        area.removeFromBottom(12);

        auto display = area;
        if (mDetail.getToggleState())
        {
            mDetailWell = display.removeFromRight(juce::jmax(180, display.getWidth() / 3));
            display.removeFromRight(14);
        }
        mAnalyser.setBounds(display);

        auto row = knobRow.withSizeKeepingCentre(
            juce::jmin(knobRow.getWidth(), 92 * (int)mKnobs.size()), knobRow.getHeight());
        const int w = row.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
            k->setBounds(row.removeFromLeft(w).reduced(4, 0));
    }

private:
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    GrAnalyser mAnalyser;
    juce::TextButton mDetail;
    juce::Rectangle<int> mStatePill, mDetailWell;
    float mCurDb = 0.0f;
    bool mLowLat = false;
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

        // Voicing selector (Clean / OTA / Opto / FET) -> compMode param.
        mModes = std::make_unique<SegmentedControl>(apvts, "compMode",
                                                    juce::StringArray{"Clean", "OTA", "Opto", "FET"});
        mModes->onChange = [this](int) { updateCurveShape(); };
        addAndMakeVisible(*mModes);
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
        const int mw = juce::jmin(mModes->idealWidth(), hr.getWidth());
        mModes->setBounds(hr.removeFromRight(mw).withSizeKeepingCentre(mw, 24));

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
        const int idx = mModes ? mModes->index() : 0;
        const auto v = nam_rig::CompBlock::voicingFor((nam_rig::CompBlock::Mode)idx);
        mCurve.setShape(v.ratio, v.kneeDb);
    }

    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    GrAnalyser mGr;
    PeakMeter mIn, mOut;
    juce::Rectangle<int> mInLabelCol, mOutLabelCol;
    CompCurve mCurve;
    std::unique_ptr<SegmentedControl> mModes;
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

        if (getToggleState() && mLit)
        {
            auto glow = juce::Rectangle<float>(80.0f, 80.0f).withCentre(c);
            juce::ColourGradient rg(mAccent.withAlpha(0.55f), c.x, c.y,
                                    mAccent.withAlpha(0.0f), c.x, c.y - 40.0f, true);
            rg.addColour(0.42, mAccent.withAlpha(0.16f));
            g.setGradientFill(rg);
            g.fillEllipse(glow);
        }

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
        g.setGradientFill(hg);
        g.fillPath(hex);

        // Chrome cap.
        const float cs = down ? 32.0f : 34.0f;
        auto cap = juce::Rectangle<float>(cs, cs).withCentre(c);
        juce::ColourGradient cg(juce::Colour(0xffd8dce2), c.x, cap.getY() + cs * 0.32f,
                                juce::Colour(0xff565d67), c.x, cap.getBottom(), true);
        g.setGradientFill(cg);
        g.fillEllipse(cap);
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.drawEllipse(cap.reduced(0.5f), 1.0f);
    }

private:
    juce::Colour mAccent = colors::accent;
    bool mLit = false;
};

// Monochrome silkscreen art glyph per drive family, drawn from the design's
// 64x48-viewBox paths, scaled/centred into box.
inline void paintDriveGlyph(juce::Graphics &g, int type, juce::Rectangle<float> box, juce::Colour col)
{
    juce::Path p;
    bool fill = false;
    switch (type)
    {
    case 1: // Boost -> twin sparkle (filled)
        p.startNewSubPath(28, 7);  p.quadraticTo(28, 23, 44, 23); p.quadraticTo(28, 23, 28, 39);
        p.quadraticTo(28, 23, 12, 23); p.quadraticTo(28, 23, 28, 7); p.closeSubPath();
        p.startNewSubPath(47, 9);  p.quadraticTo(47, 16, 54, 16); p.quadraticTo(47, 16, 47, 23);
        p.quadraticTo(47, 16, 40, 16); p.quadraticTo(47, 16, 47, 9); p.closeSubPath();
        fill = true; break;
    case 2: // Overdrive -> smooth hill (stroked)
        p.startNewSubPath(8, 33); p.cubicTo(19, 33, 23, 15, 32, 15); p.cubicTo(41, 15, 45, 33, 56, 33);
        break;
    case 3: // Distortion -> rodent (stroked)
        p.startNewSubPath(53, 27); p.cubicTo(46, 21, 36, 20, 27, 22); p.cubicTo(18, 24, 12, 26, 12, 30);
        p.cubicTo(12, 34, 19, 36, 28, 34); p.cubicTo(38, 33, 47, 32, 53, 27); p.closeSubPath();
        p.startNewSubPath(36, 21); p.cubicTo(33, 13, 39, 8, 42, 10); p.cubicTo(45, 12, 46, 16, 44, 21);
        p.startNewSubPath(14, 31); p.cubicTo(8, 33, 3, 31, 3, 25); p.cubicTo(3, 21, 5, 18, 8, 17);
        break;
    case 4: // Fuzz -> round wave (stroked)
        p.startNewSubPath(4, 24); p.cubicTo(5, 14, 9, 13, 13, 13); p.lineTo(20, 13);
        p.cubicTo(24, 13, 25, 35, 29, 35); p.lineTo(36, 35); p.cubicTo(40, 35, 41, 13, 45, 13);
        p.lineTo(52, 13); p.cubicTo(56, 13, 58, 19, 60, 24);
        p.applyTransform(juce::AffineTransform(0.82f, 0.0f, 5.76f, 0.0f, 1.0f, 0.0f));
        break;
    default: return;
    }
    const float s = juce::jmin(box.getWidth() / 64.0f, box.getHeight() / 48.0f);
    p.applyTransform(juce::AffineTransform::scale(s)
                         .translated(box.getCentreX() - 32.0f * s, box.getCentreY() - 24.0f * s));
    g.setColour(col);
    if (fill)
        g.fillPath(p);
    else
        g.strokePath(p, juce::PathStrokeType(2.0f * s + 0.4f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

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
        const int type = mType.getSelectedItemIndex();
        const juce::String pid = "drv" + juce::String(mSlot + 1);
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
        const auto pair = colors::driveAccent(accentIndex());
        const juce::Colour tint = mActive ? pair.tint : juce::Colour(0xff343a43);

        // Enclosure: neutral base + tint wash.
        juce::ColourGradient base(juce::Colour(0xff262b33), 0.0f, b.getY(),
                                  juce::Colour(0xff15181d), 0.0f, b.getBottom(), false);
        g.setGradientFill(base);
        g.fillRoundedRectangle(b, 16.0f);
        juce::ColourGradient wash(tint.withAlpha(0.20f), 0.0f, b.getY(),
                                  tint.withAlpha(0.05f), 0.0f, b.getBottom(), false);
        g.setGradientFill(wash);
        g.fillRoundedRectangle(b, 16.0f);
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
        if (mActive) { g.setColour(pair.accent.withAlpha(0.45f)); g.fillEllipse(jewel.expanded(3.0f)); }
        g.setColour(mActive ? pair.accent : juce::Colour(0xff2a2f37));
        g.fillEllipse(jewel);

        // Silkscreen art glyph.
        if (mLastType != 0)
            paintDriveGlyph(g, mLastType, mGlyphRect.toFloat(),
                            mActive ? pair.accent.withAlpha(0.85f) : juce::Colour(0xff5a616b));

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
        mOn.setBounds(fs.withSizeKeepingCentre(80, 80));
        a.removeFromBottom(4);
        mGlyphRect = a.removeFromBottom(juce::jmin(64, juce::jmax(40, a.getHeight() / 2)));
        a.removeFromBottom(4);

        if (mRangeSeg->isVisible())
        {
            auto rr = a.removeFromTop(24);
            const int rw = juce::jmin(mRangeSeg->idealWidth(), getWidth() - 40);
            mRangeSeg->setBounds(rr.withSizeKeepingCentre(rw, 22));
            a.removeFromTop(8);
        }

        auto pillRow = a.removeFromTop(40);
        const int pw = juce::jlimit(90, getWidth() - 24,
            (int)std::ceil(juce::GlyphArrangement::getStringWidth(
                fonts::archivo(23.0f, fonts::Bold), mModelStr.isEmpty() ? "Off" : mModelStr)) + 34);
        mPillRect = pillRow.withSizeKeepingCentre(pw, 38);
        a.removeFromTop(5);
        mSubRect = a.removeFromTop(16);

        LabeledKnob *ks[3] = {mDrive.get(), mTone.get(), mLevel.get()};
        int nVis = 0;
        for (auto *k : ks) if (k->isVisible()) ++nVis;
        if (nVis > 0)
        {
            const int kw = juce::jmin(78, juce::jmax(1, knobRow.getWidth() / nVis));
            auto grp = knobRow.withSizeKeepingCentre(kw * nVis, knobRow.getHeight());
            for (auto *k : ks)
                if (k->isVisible())
                    k->setBounds(grp.removeFromLeft(kw).reduced(3, 0));
        }
    }

private:
    int accentIndex() const // map drive type -> driveAccent palette index
    {
        switch (mLastType) { case 1: return 0; case 2: return 1; case 3: return 2; case 4: return 3; default: return 5; }
    }
    int curModel() const { return juce::jmax(0, mModel.getSelectedItemIndex()); }

    void showMenu()
    {
        using DB = nam_rig::DriveBlock;
        static const char *names[] = {"Off", "Boost", "Overdrive", "Distortion", "Fuzz"};
        juce::PopupMenu m;
        m.addItem(1, "Off", true, mType.getSelectedItemIndex() == 0);
        for (int t = 1; t <= 4; ++t)
        {
            const auto cat = (DB::Kind)t;
            const int n = DB::modelCount(cat);
            if (n > 1)
            {
                juce::PopupMenu sub;
                for (int i = 0; i < n; ++i)
                    sub.addItem(t * 100 + i + 10, DB::modelName(cat, i), true,
                                mType.getSelectedItemIndex() == t && curModel() == i);
                m.addSubMenu(names[t], sub);
            }
            else
                m.addItem(t * 100 + 10, DB::modelName(cat, 0), true, mType.getSelectedItemIndex() == t);
        }
        m.showMenuAsync(juce::PopupMenu::Options()
                            .withTargetScreenArea(localAreaToGlobal(mPillRect))
                            .withMinimumWidth(180),
                        [this](int r)
                        {
                            if (r <= 0) return;
                            if (r == 1) { mType.setSelectedItemIndex(0); return; }
                            const int t = (r - 10) / 100, i = (r - 10) % 100;
                            mType.setSelectedItemIndex(t);
                            if (nam_rig::DriveBlock::modelCount((nam_rig::DriveBlock::Kind)t) > 1)
                                mModel.setSelectedItemIndex(i);
                        });
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
            mDrive->rebind(mApvts, p + "oDrive"); mDrive->setCaption("Drive");
            mTone->rebind(mApvts, p + "oTone");   mTone->setCaption("Tone");
            mLevel->rebind(mApvts, p + "oLevel"); mLevel->setCaption("Level");
            mDrive->setVisible(true); mTone->setVisible(true); mLevel->setVisible(true);
            break;
        case 3:
            mDrive->rebind(mApvts, p + "dDrive"); mDrive->setCaption("Dist");
            mTone->rebind(mApvts, p + "dTone");   mTone->setCaption("Filter");
            mLevel->rebind(mApvts, p + "dLevel"); mLevel->setCaption("Volume");
            mDrive->setVisible(true); mTone->setVisible(true); mLevel->setVisible(true);
            break;
        case 4:
            mDrive->rebind(mApvts, p + "fDrive"); mDrive->setCaption("Fuzz");
            mLevel->rebind(mApvts, p + "fLevel"); mLevel->setCaption("Volume");
            mDrive->setVisible(true); mTone->setVisible(false); mLevel->setVisible(true);
            break;
        default:
            mDrive->setVisible(false); mTone->setVisible(false); mLevel->setVisible(false);
            break;
        }
        for (auto *k : {mDrive.get(), mTone.get(), mLevel.get()})
            k->setAccent(colors::driveAccent(accentIndex()).accent);
        mModelStr = type == 0 ? juce::String("Off")
                              : juce::String(nam_rig::DriveBlock::modelName(cat, model));
        mKindStr = names[juce::jlimit(0, 4, type)];
        mSubStr = type == 0 ? juce::String("select a pedal")
                            : juce::String(nam_rig::DriveBlock::modelSub(cat, model));
        mRangeSeg->setVisible(nam_rig::DriveBlock::modelHasRange(cat, model));
        mOn.setAccent(colors::driveAccent(accentIndex()).accent);
        mOn.setLit(mActive);
    }

    juce::AudioProcessorValueTreeState &mApvts;
    int mSlot;
    juce::ComboBox mType, mModel;
    std::unique_ptr<SegmentedControl> mRangeSeg;
    Footswitch mOn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mTypeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mOnAtt;
    std::unique_ptr<LabeledKnob> mDrive, mTone, mLevel;
    juce::String mModelStr{"Off"}, mKindStr{"DRIVE"}, mSubStr{"select a pedal"};
    juce::Rectangle<int> mHeaderRect, mGlyphRect, mPillRect, mSubRect;
    int mLastType = -1, mLastModel = -1;
    bool mLastOn = true, mActive = false;
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
        g.setGradientFill(lg);
        g.fillRoundedRectangle(lp, 9.0f);
        g.setColour(juce::Colour(0xff3a414c));
        g.drawRoundedRectangle(lp, 9.0f, 1.0f);
        auto dot = juce::Rectangle<float>(9.0f, 9.0f).withCentre({lp.getX() + 18.0f, lp.getCentreY()});
        if (mLoaded) { g.setColour(colors::green.withAlpha(0.4f)); g.fillEllipse(dot.expanded(2.5f)); }
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
        mGraph.removeFromBottom(24); // hint line
    }

    // map a band's normalised slider position to a y inside the graph.
    float bandY(int b) const
    {
        const float prop = (float)mSliders[(size_t)b]->valueToProportionOfLength(
            mSliders[(size_t)b]->getValue());
        return (float)mGraph.getBottom() - prop * (float)mGraph.getHeight();
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

        // dB grid: +12 / 0 / −12 (design percentages).
        struct Grid { float pct; juce::Colour c; const char *lbl; };
        const Grid grid[] = {{0.0875f, juce::Colour(0xff21262d), "+12"},
                             {0.50f,   juce::Colour(0xff363d47), "0 dB"},
                             {0.9125f, juce::Colour(0xff21262d), "-12"}};
        for (auto &gr : grid)
        {
            const float y = in.getY() + gr.pct * in.getHeight();
            g.setColour(gr.c);
            g.fillRect(in.getX(), y, in.getWidth(), 1.0f);
            g.setColour(juce::Colour(0xff5a616b));
            g.setFont(fonts::mono(8.5f, fonts::SemiBold));
            g.drawText(gr.lbl, juce::Rectangle<float>(in.getX() + 6.0f, y - 7.0f, 32.0f, 14.0f),
                       juce::Justification::centredLeft);
        }

        // Response curve from the SAME RBJ designs the DSP runs.
        const double fs = 48000.0;
        std::array<Biquad, EqBlock::kNumBands> filters;
        for (int b = 0; b < EqBlock::kNumBands; ++b)
            filters[(size_t)b] = Biquad::peaking(fs, EqBlock::kBandHz[(size_t)b], EqBlock::kQ,
                                                 mSliders[(size_t)b]->getValue());
        juce::Path curve;
        const double fLo = 30.0, fHi = 16000.0;
        const int n = juce::jmax(2, (int)in.getWidth());
        for (int x = 0; x < n; ++x)
        {
            const double f = fLo * std::pow(fHi / fLo, (double)x / (double)(n - 1));
            double db = 0.0;
            for (auto &bi : filters)
                if (!bi.isIdentity())
                    db += 20.0 * std::log10(std::max(bi.magnitudeAt(fs, f), 1.0e-6));
            const float y = juce::jlimit(in.getY(), in.getBottom(),
                                         in.getCentreY() - (float)(db / 12.0) * in.getHeight() * 0.4125f);
            const float px = in.getX() + (float)x;
            if (x == 0) curve.startNewSubPath(px, y);
            else curve.lineTo(px, y);
        }
        juce::Path area = curve;
        area.lineTo(in.getRight(), in.getBottom());
        area.lineTo(in.getX(), in.getBottom());
        area.closeSubPath();
        juce::ColourGradient fill(colors::accent.withAlpha(0.30f), in.getX(), in.getY(),
                                  colors::accent.withAlpha(0.0f), in.getX(), in.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(area);
        g.setColour(colors::accent);
        g.strokePath(curve, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));

        // Node handles + value + frequency labels.
        for (int b = 0; b < (int)mSliders.size(); ++b)
        {
            const float x = bandX(b), y = juce::jlimit(in.getY(), in.getBottom(), bandY(b));
            const float db = (float)mSliders[(size_t)b]->getValue();
            g.setColour(colors::text);
            g.setFont(fonts::mono(9.0f, fonts::SemiBold));
            g.drawText((db >= 0 ? "+" : "") + juce::String(db, 1),
                       juce::Rectangle<float>(x - 24.0f, y - 26.0f, 48.0f, 12.0f),
                       juce::Justification::centred);
            auto dot = juce::Rectangle<float>(13.0f, 13.0f).withCentre({x, y});
            g.setColour(colors::accent.withAlpha(0.4f));
            g.fillEllipse(dot.expanded(3.0f));
            g.setColour(colors::accent);
            g.fillEllipse(dot);
            g.setColour(juce::Colour(0xff11141b));
            g.drawEllipse(dot, 2.0f);
            g.setColour(colors::caption);
            g.setFont(fonts::archivo(9.5f, fonts::SemiBold, 0.03f));
            g.drawText(mCaptions[b], juce::Rectangle<float>(x - 26.0f, in.getBottom() - 16.0f, 52.0f, 12.0f),
                       juce::Justification::centred);
        }

        g.setColour(juce::Colour(0xff5a616b));
        g.setFont(fonts::mono(11.0f));
        g.drawText(juce::String::fromUTF8("Drag any band to shape the curve \xC2\xB7 8-band graphic EQ, pre-cab"),
                   juce::Rectangle<int>(mGraph.getX(), mGraph.getBottom() + 6, mGraph.getWidth(), 18),
                   juce::Justification::centredLeft);
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
class CabPanel : public BlockPanel
{
public:
    CabPanel(NamRigProcessor &proc, int rig)
        : BlockPanel(rig == 0 ? "CABINET A - IR" : "CABINET B - IR"), mProc(proc), mRig(rig)
    {
        setHeaderRight(rig == 0 ? "RIG A" : "RIG B");
        mHpfId = rig == 0 ? "cabHpf" : "rigBcabHpf";
        mLpfId = rig == 0 ? "cabLpf" : "rigBcabLpf";

        mLoadBtn.setButtonText(juce::String::fromUTF8("Load custom IR\xE2\x80\xA6"));
        addAndMakeVisible(mLoadBtn);
        mLoadBtn.onClick = [this]
        {
            mChooser = std::make_unique<juce::FileChooser>("Select a cab IR", juce::File{},
                                                           "*.wav;*.aif;*.aiff");
            mChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectFiles,
                                  [this](const juce::FileChooser &fc)
                                  {
                                      if (fc.getResult().existsAsFile())
                                          mProc.loadIr(fc.getResult(), mRig);
                                  });
        };

        mHpf = std::make_unique<LabeledKnob>(mProc.apvts, mHpfId, "Low Cut");
        mLpf = std::make_unique<LabeledKnob>(mProc.apvts, mLpfId, "High Cut");
        mHpf->slider().onValueChange = [this] { repaint(); };
        mLpf->slider().onValueChange = [this] { repaint(); };
        addAndMakeVisible(*mHpf);
        addAndMakeVisible(*mLpf);
    }

    void refresh()
    {
        const bool loaded = mProc.isIrLoaded(mRig);
        auto name = loaded ? mProc.getIrName(mRig) : juce::String::fromUTF8("No IR \xC2\xB7 amp runs direct");
        if (loaded != mLoaded || name != mIrName) { mLoaded = loaded; mIrName = name; repaint(); }
    }

    void resized() override
    {
        auto area = bodyArea().reduced(24, 13);
        mCard = area.removeFromLeft(330);
        area.removeFromLeft(20);
        mRight = area;

        // IR library card: load button pinned to the bottom.
        auto card = mCard.reduced(12);
        mLoadBtn.setBounds(card.removeFromBottom(34));
        mListRect = mCard.reduced(8).withTrimmedTop(64).withTrimmedBottom(46);

        // Right column: response well on top, knobs + hint below.
        auto right = mRight;
        right.removeFromTop(22); // caption row
        mRespRect = right.removeFromTop(juce::jmax(150, right.getHeight() - 122));
        right.removeFromTop(10);
        auto knobRow = right.removeFromTop(96);
        mHpf->setBounds(knobRow.removeFromLeft(100).reduced(2, 0));
        knobRow.removeFromLeft(14);
        mLpf->setBounds(knobRow.removeFromLeft(100).reduced(2, 0));
        mHintRect = knobRow;
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);
        paintCard(g);
        paintResponse(g);
    }

private:
    void paintCard(juce::Graphics &g)
    {
        auto c = mCard.toFloat();
        g.setColour(juce::Colour(0xff181b21));
        g.fillRoundedRectangle(c, 11.0f);
        g.setColour(colors::cardBorder);
        g.drawRoundedRectangle(c, 11.0f, 1.0f);

        auto head = mCard.reduced(14, 0).withTop(mCard.getY() + 13).withHeight(16);
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        g.drawText("IR LIBRARY", head, juce::Justification::centredLeft);
        g.setColour(colors::captionDim);
        g.setFont(fonts::mono(10.0f));
        g.drawText(mLoaded ? "1 cab" : "0 cabs", head, juce::Justification::centredRight);

        // Search box (decorative).
        auto search = mCard.reduced(12, 0).withTop(mCard.getY() + 36).withHeight(30).toFloat();
        g.setColour(juce::Colour(0xff13161f));
        g.fillRoundedRectangle(search, 8.0f);
        g.setColour(colors::cardBorder);
        g.drawRoundedRectangle(search, 8.0f, 1.0f);
        g.setColour(colors::captionDim);
        g.drawEllipse(search.getX() + 12.0f, search.getCentreY() - 5.0f, 10.0f, 10.0f, 1.5f);
        g.setFont(fonts::archivo(12.0f));
        g.drawText(juce::String::fromUTF8("Search cabinets\xE2\x80\xA6"), search.withTrimmedLeft(30), juce::Justification::centredLeft);

        // List: the loaded IR (selected row) or an empty hint.
        auto row = juce::Rectangle<int>(mListRect.getX(), mListRect.getY(), mListRect.getWidth(), 46).toFloat();
        if (mLoaded)
        {
            g.setColour(colors::accent.withAlpha(0.10f));
            g.fillRoundedRectangle(row, 9.0f);
            g.setColour(colors::accent.withAlpha(0.5f));
            g.drawRoundedRectangle(row, 9.0f, 1.0f);
            auto dot = juce::Rectangle<float>(9.0f, 9.0f).withCentre({row.getX() + 17.0f, row.getCentreY()});
            g.setColour(colors::green.withAlpha(0.4f)); g.fillEllipse(dot.expanded(2.5f));
            g.setColour(colors::green); g.fillEllipse(dot);
            auto txt = row.withTrimmedLeft(32).withTrimmedRight(46);
            g.setColour(colors::text);
            g.setFont(fonts::archivo(13.0f, fonts::SemiBold));
            g.drawText(mIrName, txt.removeFromTop(22).toNearestInt(), juce::Justification::centredLeft, true);
            g.setColour(colors::caption);
            g.setFont(fonts::mono(10.5f));
            g.drawText(juce::String::fromUTF8("custom \xC2\xB7 loaded"), txt.toNearestInt(), juce::Justification::centredLeft);
            auto tag = juce::Rectangle<float>(30.0f, 18.0f).withCentre({row.getRight() - 24.0f, row.getCentreY()});
            g.setColour(juce::Colour(0xff22262d)); g.fillRoundedRectangle(tag, 5.0f);
            g.setColour(colors::cardBorder); g.drawRoundedRectangle(tag, 5.0f, 1.0f);
            g.setColour(juce::Colour(0xff7a808a)); g.setFont(fonts::archivo(8.5f, fonts::SemiBold, 0.08f));
            g.drawText("IR", tag, juce::Justification::centred);
        }
        else
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(11.0f));
            g.drawText(juce::String::fromUTF8("No cabs yet \xE2\x80\x94 load a custom IR below"), mListRect,
                       juce::Justification::centredTop);
        }
    }

    void paintResponse(juce::Graphics &g)
    {
        // Caption row.
        auto cap = juce::Rectangle<int>(mRight.getX(), mRight.getY(), mRight.getWidth(), 16);
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        g.drawText("FREQUENCY RESPONSE", cap, juce::Justification::centredLeft);
        g.setColour(colors::text2);
        g.setFont(fonts::mono(11.0f));
        g.drawText(mLoaded ? mIrName : juce::String("direct"), cap, juce::Justification::centredRight);

        RigLookAndFeel::drawWell(g, mRespRect.toFloat());
        auto in = mRespRect.toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff262c34));
        g.fillRect(in.getX(), in.getCentreY(), in.getWidth(), 1.0f);

        const double fs = 48000.0;
        const float hpf = (float)mProc.apvts.getRawParameterValue(mHpfId)->load();
        const float lpf = (float)mProc.apvts.getRawParameterValue(mLpfId)->load();
        const bool hpOn = hpf > 20.5f, lpOn = lpf < 19990.0f;
        Biquad hp = hpOn ? Biquad::highpass(fs, hpf) : Biquad::identity();
        Biquad lp = lpOn ? Biquad::lowpass(fs, lpf) : Biquad::identity();

        juce::Path line;
        const double fLo = 20.0, fHi = 20000.0;
        const int n = juce::jmax(2, (int)in.getWidth());
        for (int x = 0; x < n; ++x)
        {
            const double f = fLo * std::pow(fHi / fLo, (double)x / (double)(n - 1));
            double db = 0.0;
            if (hpOn) db += 20.0 * std::log10(std::max(hp.magnitudeAt(fs, f), 1.0e-6));
            if (lpOn) db += 20.0 * std::log10(std::max(lp.magnitudeAt(fs, f), 1.0e-6));
            const float y = juce::jlimit(in.getY(), in.getBottom(),
                                         in.getCentreY() - (float)(db / 24.0) * in.getHeight());
            const float px = in.getX() + (float)x;
            if (x == 0) line.startNewSubPath(px, y);
            else line.lineTo(px, y);
        }
        juce::Path area = line;
        area.lineTo(in.getRight(), in.getBottom());
        area.lineTo(in.getX(), in.getBottom());
        area.closeSubPath();
        juce::ColourGradient fill(colors::accent.withAlpha(0.20f), in.getX(), in.getY(),
                                  colors::accent.withAlpha(0.0f), in.getX(), in.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(area);
        g.setColour(colors::accent);
        g.strokePath(line, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved));

        g.setColour(juce::Colour(0xff4a515c));
        g.setFont(fonts::mono(8.5f, fonts::SemiBold));
        g.drawText("20 Hz", in.toNearestInt().reduced(8, 6), juce::Justification::bottomLeft);
        g.drawText("20 kHz", in.toNearestInt().reduced(8, 6), juce::Justification::bottomRight);

        g.setColour(juce::Colour(0xff5a616b));
        g.setFont(fonts::mono(11.0f));
        g.drawText(juce::String::fromUTF8("Post-cab cuts \xC2\xB7 12 dB/oct \xC2\xB7 knob extremes = off"),
                   mHintRect, juce::Justification::centredRight);
    }

    NamRigProcessor &mProc;
    int mRig = 0;
    juce::String mHpfId, mLpfId, mIrName{juce::String::fromUTF8("No IR \xC2\xB7 amp runs direct")};
    juce::TextButton mLoadBtn;
    std::unique_ptr<LabeledKnob> mHpf, mLpf;
    std::unique_ptr<juce::FileChooser> mChooser;
    juce::Rectangle<int> mCard, mRight, mListRect, mRespRect, mHintRect;
    bool mLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabPanel)
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

        mNum.setText(label, juce::dontSendNotification);
        mNum.setJustificationType(juce::Justification::centred);
        mNum.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mNum);
        addAndMakeVisible(mIcon);

        const juce::Colour laneCol = colors::laneColour(soloSlot);
        mIcon.setAccent(laneCol); // tint the glyph + box border per lane
        mIcon.onClick = [this] {   // click the icon to toggle this slot on/off
            if (auto *prm = mApvts.getParameter(mPrefix + "On"))
            {
                const bool now = prm->getValue() >= 0.5f;
                prm->beginChangeGesture();
                prm->setValueNotifyingHost(now ? 0.0f : 1.0f);
                prm->endChangeGesture();
            }
            refresh();
        };

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
        mSeries.setButtonText(""); // checkbox only; caption sits beneath (saves width)
        addChildComponent(mSeries); // bi-phase only (series/parallel routing)
        mSeriesAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "Series", mSeries);
        mSeriesLabel.setText("Series", juce::dontSendNotification);
        mSeriesLabel.setJustificationType(juce::Justification::centred);
        mSeriesLabel.setColour(juce::Label::textColourId, colors::textDim);
        addChildComponent(mSeriesLabel); // bi-phase only (font sized by row height, like knob captions)

        mExtreme.setButtonText(""); // checkbox only; "Extreme" caption sits beneath
        addChildComponent(mExtreme); // phaser/uni-vibe/bi-phase only (unlocks the wild ranges)
        mExtremeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "Extreme", mExtreme);
        mExtremeLabel.setText("Extreme", juce::dontSendNotification);
        mExtremeLabel.setJustificationType(juce::Justification::centred);
        mExtremeLabel.setColour(juce::Label::textColourId, colors::textDim);
        addChildComponent(mExtremeLabel);

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

    // Show/relayout the controls for the current Type/Sync/On (reads the params;
    // never touches the combo, so it's safe to call from a fresh user selection).
    void applyType()
    {
        const juce::String p = mPrefix;
        const int type = (int)mApvts.getRawParameterValue(p + "Type")->load();
        const int sync = (int)mApvts.getRawParameterValue(p + "Sync")->load();
        const bool on = mApvts.getRawParameterValue(p + "On")->load() >= 0.5f;
        mIcon.setType(type);
        mIcon.setActive(on);
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
            mSeriesLabel.setVisible(type == 8);
            const bool extremeable = (type == 0 || type == 1 || type == 2 || type == 4 || type == 6 || type == 8); // chorus/flanger/phaser/vibrato/uni-vibe/bi-phase
            mExtreme.setVisible(extremeable);               // unlock the wild ranges
            mExtremeLabel.setVisible(extremeable);
        }
        mRate->setEnabled(sync == 0 || type == 9); // rate greyed when synced; ring mod ignores sync so its Freq stays live
        repaint();
        if (typeChanged)
            resized();
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(colors::panel);
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 8.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(9, 7);
        auto numCol = area.removeFromLeft(16);
        mNum.setBounds(numCol); // lane number (LED removed)
        area.removeFromLeft(6);

        auto iconCol = area.removeFromLeft(62);
        mIcon.setBounds(iconCol.withSizeKeepingCentre(62, juce::jmin(iconCol.getHeight(), 46)));
        area.removeFromLeft(10);

        auto meta = area.removeFromLeft(92).withSizeKeepingCentre(92, juce::jmin(area.getHeight(), 50));
        mType.setBounds(meta.removeFromTop(24));
        meta.removeFromTop(4);
        auto bottomMeta = meta.removeFromTop(22);
        mSync.setBounds(bottomMeta.withWidth(92));          // non-rotary
        mRotFast.setBounds(bottomMeta.removeFromLeft(52));  // rotary: slow/fast
        area.removeFromLeft(10);

        if (mSoloSlot >= 0) // front slots only
        {
            mSolo.setBounds(area.removeFromRight(30).withSizeKeepingCentre(30, 22));
            area.removeFromRight(8);
        }
        if (mSeries.isVisible()) // "Series" caption above a small checkbox, centred as one unit
        {
            auto col = area.removeFromRight(44);
            area.removeFromRight(6);
            const int groupH = 14 + 3 + 13; // caption (knob-size) + gap + small box
            auto stack = col.withSizeKeepingCentre(44, groupH);
            mSeriesLabel.setBounds(stack.removeFromTop(14));
            stack.removeFromTop(3);
            mSeries.setBounds(stack.withSizeKeepingCentre(13, 13));
        }
        if (mExtreme.isVisible()) // "Extreme" caption above a small checkbox, same idiom as Series
        {
            auto col = area.removeFromRight(52);
            area.removeFromRight(6);
            const int groupH = 14 + 3 + 13;
            auto stack = col.withSizeKeepingCentre(52, groupH);
            mExtremeLabel.setBounds(stack.removeFromTop(14));
            stack.removeFromTop(3);
            mExtreme.setBounds(stack.withSizeKeepingCentre(13, 13));
        }
        if (mWave.isVisible())
        {
            mWave.setBounds(area.removeFromRight(84).withSizeKeepingCentre(84, 24));
            area.removeFromRight(8);
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
    juce::Label mNum;
    ModFxIcon mIcon;
    std::unique_ptr<LaneScope> mScope;
    juce::ComboBox mType, mWave, mSync;
    std::unique_ptr<juce::ParameterAttachment> mTypeParamAtt; // robust binding for the filtered Type list
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mWaveAtt, mSyncAtt;
    juce::ToggleButton mSolo; // momentary dial-in (not APVTS-attached)
    std::unique_ptr<LabeledKnob> mRate, mDepth, mFeedback, mMix, mWidth, mDrive, mManual, mP2Ratio, mHornDrum;
    juce::ToggleButton mRotFast, mSeries, mExtreme;
    juce::Label mSeriesLabel, mExtremeLabel; // captions beneath the small checkboxes
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
        g.setColour(colors::textDim); // header (mirrors the rack's "SIGNAL FLOW")
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        g.drawText("MOD MIX", getLocalBounds().removeFromTop(13), juce::Justification::centred);
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

        g.setFont(RigLookAndFeel::withHeight(10.0f));
        for (int i = 0; i < 3; ++i)
        {
            const auto p = scr(r, i);
            const float rad = 3.0f + 4.0f * (mActive[i] ? w[i] : 0.0f);
            g.setColour(mActive[i] ? colors::laneColour(i) : colors::outline);
            g.fillEllipse(p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
            const float ly = (i == 0) ? p.y - 13.0f - rad : p.y + 3.0f + rad; // clear the (weighted) node
            if (mActive[i]) // blend weight as a percentage, in the lane colour
            {
                g.setColour(colors::laneColour(i));
                // Anchor the corner labels inward so they never clip the box edge:
                // bottom-left reads from the node rightward, bottom-right leftward.
                const auto just = (i == 1) ? juce::Justification::centredLeft
                                  : (i == 2) ? juce::Justification::centredRight
                                             : juce::Justification::centred;
                const int lx = (i == 1) ? (int)(p.x - 2.0f)
                               : (i == 2) ? (int)(p.x - 38.0f)
                                          : (int)(p.x - 20.0f);
                g.drawText(juce::String(juce::roundToInt(w[i] * 100.0f)) + "%",
                           lx, (int)ly, 40, 11, just);
            }
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
        g.setColour(colors::textDim);
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        g.drawText("SIGNAL FLOW", getLocalBounds().removeFromTop(13),
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

        mRouting.addItemList({"Series", "Parallel"}, 1);
        addAndMakeVisible(mRouting);
        mRoutingAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "modRouting", mRouting);
        mRouting.onChange = [this] { refreshRouting(); };

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
        const bool par = (mRouting.getSelectedId() == 2);
        mPad->setVisible(par);
        mModMix->setVisible(par);
        mRack->setVisible(!par);
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
    }

    void resized() override
    {
        // Routing toggle (Series/Parallel) sits in the header's top-right corner,
        // vertically centred in the same band as the title.
        auto hdr = getLocalBounds().removeFromTop(contentArea().getY()).reduced(16, 0);
        mRouting.setBounds(hdr.removeFromRight(86).withSizeKeepingCentre(86, 24));

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
    juce::ComboBox mRouting;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mRoutingAtt;
    std::unique_ptr<BlendPad> mPad;
    std::unique_ptr<ChainRack> mRack;
    std::unique_ptr<HKnob> mModMix;
    juce::AudioProcessorValueTreeState &mApvts;
    bool mParallel = false;
};

//==============================================================================
class DelayPanel : public BlockPanel
{
public:
    explicit DelayPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("DELAY"), mApvts(apvts)
    {
        mSyncLabel.setText("Sync", juce::dontSendNotification);
        mSyncLabel.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mSyncLabel);
        mSync.addItemList({"Free", "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4",
                           "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T"},
                          1);
        addAndMakeVisible(mSync);
        mSyncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "delaySync", mSync);

        mPingPong.setButtonText("Ping-Pong");
        mPingPong.getProperties().set("pill", true);
        addAndMakeVisible(mPingPong);
        mPpAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, "delayPingPong", mPingPong);

        const std::pair<const char *, const char *> defs[] = {
            {"delayTime", "Time"},        {"delayFeedback", "Feedback"},
            {"delayTone", "Tone"},        {"delayMod", "Wow/Flutter"},
            {"delayWidth", "Width"},      {"delayMix", "Mix"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }
    }

    void refresh() // time knob is owned by the sync division when sync != Free
    {
        const int sync = (int)mApvts.getRawParameterValue("delaySync")->load();
        mKnobs[0]->setEnabled(sync == 0);
    }

    void resized() override
    {
        // Sync + Ping-Pong in the header-right.
        auto hr = headerArea();
        mPingPong.setBounds(hr.removeFromRight(96).withSizeKeepingCentre(96, 26));
        hr.removeFromRight(10);
        mSync.setBounds(hr.removeFromRight(110).withSizeKeepingCentre(110, 28));
        hr.removeFromRight(8);
        mSyncLabel.setBounds(hr.removeFromRight(42).withSizeKeepingCentre(42, 20));
        mSyncLabel.setJustificationType(juce::Justification::centredRight);

        auto area = bodyArea().reduced(24, 14)
                        .withSizeKeepingCentre(bodyArea().getWidth() - 48,
                                               juce::jmin(bodyArea().getHeight() - 28, 170));
        const int w = area.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
            k->setBounds(area.removeFromLeft(w).reduced(6, 0));
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    juce::ComboBox mSync;
    juce::Label mSyncLabel;
    juce::ToggleButton mPingPong;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mSyncAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mPpAtt;
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
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
// MIX — dual-rig routing: mode (Solo A / Solo B / Dual), per-rig level + pan +
// polarity, the phase-align nudge, and the Auto-align button (probes both
// voices). The two rigs merge here into the shared stereo section.
class MixPanel : public BlockPanel
{
public:
    explicit MixPanel(NamRigProcessor &proc) : BlockPanel("MIX"), mProc(proc)
    {
        mModes = std::make_unique<SegmentedControl>(mProc.apvts, "rigMode",
                                                    juce::StringArray{"Solo A", "Solo B", "Dual"});
        addAndMakeVisible(*mModes);

        auto rigTag = [this](juce::Label &l, const char *txt, juce::Colour c)
        {
            l.setText(txt, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centredLeft);
            l.setColour(juce::Label::textColourId, c);
            l.setFont(fonts::archivo(12.0f, fonts::Bold, 0.08f));
            addAndMakeVisible(l);
        };
        rigTag(mALabel, "RIG A", colors::titleAccent);
        rigTag(mBLabel, "RIG B", colors::laneColour(1));

        mLevelA = std::make_unique<LabeledKnob>(mProc.apvts, "rigLevelA", "Level");
        mPanA = std::make_unique<LabeledKnob>(mProc.apvts, "rigPanA", "Pan");
        mLevelB = std::make_unique<LabeledKnob>(mProc.apvts, "rigLevelB", "Level");
        mPanB = std::make_unique<LabeledKnob>(mProc.apvts, "rigPanB", "Pan");
        mAlign = std::make_unique<LabeledKnob>(mProc.apvts, "rigAlign", "Align");
        for (auto *k : {mLevelA.get(), mPanA.get(), mLevelB.get(), mPanB.get(), mAlign.get()})
            addAndMakeVisible(*k);

        mPolA.setButtonText(juce::String::fromUTF8("\xC3\xB8 Invert"));
        mPolB.setButtonText(juce::String::fromUTF8("\xC3\xB8 Invert"));
        mPolA.getProperties().set("pill", true);
        mPolB.getProperties().set("pill", true);
        addAndMakeVisible(mPolA);
        addAndMakeVisible(mPolB);
        mPolAAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            mProc.apvts, "rigPolA", mPolA);
        mPolBAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            mProc.apvts, "rigPolB", mPolB);

        mAutoBtn.onClick = [this] { mProc.autoAlign(); };
        addAndMakeVisible(mAutoBtn);
        mMatchBtn.onClick = [this] { mProc.matchLevels(); };
        addAndMakeVisible(mMatchBtn);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        mHint.setText("Auto-align matches timing/polarity; Match Levels matches loudness.",
                      juce::dontSendNotification);
        addAndMakeVisible(mHint);
    }

    // Called from the editor timer.
    void refresh()
    {
        const int mode = (int)mProc.apvts.getRawParameterValue("rigMode")->load();
        const bool dual = (mode == 2);
        // Pan / polarity only matter in Dual (Solo plays centered); grey otherwise.
        mPanA->setEnabled(dual);
        mPanB->setEnabled(dual);
        mPolA.setEnabled(dual);
        mPolB.setEnabled(dual);
        // Auto-align + Match Levels need a model in BOTH rigs.
        const bool bothLoaded = mProc.isModelLoaded(0) && mProc.isModelLoaded(1);
        mAutoBtn.setEnabled(bothLoaded);
        mMatchBtn.setEnabled(bothLoaded);
    }

    void resized() override
    {
        // Mode pills sit in the header-right.
        auto hr = headerArea();
        const int mw = juce::jmin(mModes->idealWidth(), hr.getWidth());
        mModes->setBounds(hr.removeFromRight(mw).withSizeKeepingCentre(mw, 24));

        auto area = bodyArea().reduced(24, 16);

        auto rigRow = [&](juce::Label &tag, LabeledKnob &lvl, LabeledKnob &pan,
                          juce::ToggleButton &pol)
        {
            auto r = area.removeFromTop(104);
            tag.setBounds(r.removeFromLeft(66).withSizeKeepingCentre(56, 20));
            lvl.setBounds(r.removeFromLeft(100).reduced(6, 0));
            pan.setBounds(r.removeFromLeft(100).reduced(6, 0));
            pol.setBounds(r.removeFromLeft(104).withSizeKeepingCentre(96, 26));
            area.removeFromTop(6);
        };
        rigRow(mALabel, *mLevelA, *mPanA, mPolA);
        rigRow(mBLabel, *mLevelB, *mPanB, mPolB);
        area.removeFromTop(6);

        auto alignRow = area.removeFromTop(104);
        alignRow.removeFromLeft(66);
        mAlign->setBounds(alignRow.removeFromLeft(100).reduced(6, 0));
        alignRow.removeFromLeft(12);
        mAutoBtn.setBounds(alignRow.removeFromLeft(130).withSizeKeepingCentre(130, 30));
        alignRow.removeFromLeft(8);
        mMatchBtn.setBounds(alignRow.removeFromLeft(130).withSizeKeepingCentre(130, 30));
        alignRow.removeFromLeft(12);
        mHint.setBounds(alignRow.withSizeKeepingCentre(alignRow.getWidth(), 44));
    }

private:
    NamRigProcessor &mProc;
    std::unique_ptr<SegmentedControl> mModes;
    juce::Label mALabel, mBLabel, mHint;
    std::unique_ptr<LabeledKnob> mLevelA, mPanA, mLevelB, mPanB, mAlign;
    juce::ToggleButton mPolA, mPolB;
    juce::TextButton mAutoBtn{"Auto-align"}, mMatchBtn{"Match Levels"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mPolAAtt, mPolBAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPanel)
};

} // namespace nam_rig::ui
