#pragma once

// LBAudio shared look: colours, fonts, knob/toggle drawing and level meters.
// Every LBAudio plugin uses these so the range looks and behaves the same.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace lbaudio::ui
{

namespace Colours
{
    inline const juce::Colour background { 0xff121418 };
    inline const juce::Colour panel      { 0xff1a1d23 };
    inline const juce::Colour panelEdge  { 0xff2a2e37 };
    inline const juce::Colour track      { 0xff2c313b };
    inline const juce::Colour text       { 0xffe6e9ef };
    inline const juce::Colour dim        { 0xff8a93a3 };
    inline const juce::Colour accent     { 0xff5ad1c3 };
    inline const juce::Colour gr         { 0xffff8c42 };
    inline const juce::Colour green      { 0xff4ade80 };
    inline const juce::Colour yellow     { 0xfffacc15 };
    inline const juce::Colour red        { 0xffef4444 };
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, Colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, Colours::accent.withAlpha (0.3f));
        setColour (juce::Label::textColourId, Colours::text);
        setColour (juce::Label::textWhenEditingColourId, Colours::text);
        setColour (juce::Label::backgroundWhenEditingColourId, Colours::panelEdge);
        setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);
        setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.3f));
        setColour (juce::TextEditor::highlightedTextColourId, Colours::text);
        setColour (juce::CaretComponent::caretColourId, Colours::accent);
        setColour (juce::TextButton::textColourOffId, Colours::dim);
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xff07211e));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
        const float lineW = 3.5f;
        const float arcR = radius - lineW;
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colours::track.withMultipliedAlpha (alpha));
        g.strokePath (track, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Bipolar knobs (e.g. makeup) draw their arc from the zero point, or from an "origin" value if set
        // (e.g. 100% width).
        float arcStart = startAngle;
        const auto& props = slider.getProperties();
        if (props.contains ("origin"))
            arcStart = startAngle + (float) slider.valueToProportionOfLength ((double) props["origin"]) * (endAngle - startAngle);
        else if (props.contains ("bipolar"))
            arcStart = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);

        if (std::abs (angle - arcStart) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (cx, cy, arcR, arcR, 0.0f, juce::jmin (arcStart, angle), juce::jmax (arcStart, angle), true);
            g.setColour (Colours::accent.withMultipliedAlpha (alpha));
            g.strokePath (value, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        const float knobR = arcR - 7.0f;
        const auto knob = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre ({ cx, cy });
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff323843).withMultipliedAlpha (alpha), cx, cy - knobR,
                                                 juce::Colour (0xff1c1f25).withMultipliedAlpha (alpha), cx, cy + knobR, false));
        g.fillEllipse (knob);
        g.setColour (juce::Colour (0xff3d434f).withMultipliedAlpha (alpha));
        g.drawEllipse (knob, 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -knobR + 5.0f, 3.0f, knobR * 0.42f, 1.5f);
        g.setColour (Colours::text.withMultipliedAlpha (alpha));
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (cx, cy));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const float corner = r.getHeight() * 0.5f;

        g.setColour (on ? Colours::accent.withAlpha (0.16f) : Colours::panel);
        g.fillRoundedRectangle (r, corner);
        g.setColour (on ? Colours::accent : Colours::panelEdge.brighter (highlighted ? 0.4f : 0.1f));
        g.drawRoundedRectangle (r, corner, 1.2f);

        const float dot = 7.0f;
        const auto dotArea = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + corner + 2.0f, r.getCentreY() });
        g.setColour (on ? Colours::accent : juce::Colour (0xff3a3f4a));
        g.fillEllipse (dotArea);

        g.setColour (on ? Colours::text : Colours::dim);
        g.setFont (font (11.5f, true));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (corner + 10.0f).withTrimmedRight (8.0f),
                    juce::Justification::centred);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        g.setColour (on ? Colours::accent : (highlighted ? Colours::panelEdge : Colours::background));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        if (! on)
        {
            g.setColour (Colours::panelEdge.brighter (highlighted ? 0.3f : 0.0f));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (11.5f, true); }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (13.0f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
};

//==============================================================================
class LevelMeter final : public juce::Component
{
public:
    enum class Kind { Level, GainReduction };

    LevelMeter (Kind k, juce::String name)
        : kind (k), title (std::move (name)), displayed (k == Kind::Level ? -100.0f : 0.0f) {}

    // Level meters take a linear peak; the GR meter takes dB of reduction.
    void push (float value)
    {
        if (kind == Kind::Level)
        {
            const float db = value > 1.0e-5f ? 20.0f * std::log10 (value) : -100.0f;
            displayed = db >= displayed ? db : juce::jmax (db, displayed - 1.5f); // ~45 dB/s fall
            if (displayed >= peakHold) { peakHold = displayed; holdTicks = 45; }
            else if (--holdTicks <= 0) peakHold = juce::jmax (displayed, peakHold - 1.5f);
        }
        else
        {
            displayed = value >= displayed ? value : displayed + (value - displayed) * 0.4f;
        }
        repaint();
    }

    float getDisplayed() const noexcept { return displayed; }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Colours::dim);
        g.setFont (font (11.0f, true));
        g.drawText (title, area.removeFromTop (16.0f), juce::Justification::centred);

        const auto readoutArea = area.removeFromBottom (18.0f);
        const auto bar = area.reduced (5.0f, 4.0f);

        g.setColour (Colours::background);
        g.fillRoundedRectangle (bar, 3.0f);

        if (kind == Kind::Level)
        {
            auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f)); };
            const float h = bar.getHeight() * norm (displayed);
            if (h > 0.5f)
            {
                juce::ColourGradient grad (Colours::green, 0.0f, bar.getBottom(), Colours::red, 0.0f, bar.getY(), false);
                grad.addColour (norm (-12.0f), Colours::green);
                grad.addColour (norm (-3.0f), Colours::yellow);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (bar.withTop (bar.getBottom() - h), 3.0f);
            }
            if (peakHold > -60.0f)
            {
                g.setColour (Colours::text.withAlpha (0.8f));
                g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * norm (peakHold) - 1.0f, bar.getWidth(), 2.0f);
            }
            drawTicks (g, bar, { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f }, [&] (float db) { return bar.getBottom() - bar.getHeight() * norm (db); });
        }
        else
        {
            auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, db / 24.0f); };
            const float h = bar.getHeight() * norm (displayed);
            if (h > 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (Colours::gr, 0.0f, bar.getY(), Colours::gr.darker (0.5f), 0.0f, bar.getBottom(), false));
                g.fillRoundedRectangle (bar.withHeight (h), 3.0f);
            }
            drawTicks (g, bar, { 6.0f, 12.0f, 18.0f }, [&] (float db) { return bar.getY() + bar.getHeight() * norm (db); });
        }

        g.setColour (kind == Kind::GainReduction ? Colours::gr : Colours::text);
        g.setFont (font (11.5f));
        juce::String readout;
        if (kind == Kind::Level) readout = displayed <= -60.0f ? "-inf" : juce::String (displayed, 1);
        else                     readout = displayed < 0.05f ? "0.0" : "-" + juce::String (displayed, 1);
        g.drawText (readout, readoutArea, juce::Justification::centred);
    }

private:
    template <typename ToY>
    static void drawTicks (juce::Graphics& g, juce::Rectangle<float> bar, std::initializer_list<float> dbs, ToY toY)
    {
        g.setColour (Colours::panelEdge.withAlpha (0.9f));
        for (auto db : dbs)
            g.fillRect (bar.getX(), toY (db), bar.getWidth(), 1.0f);
    }

    Kind kind;
    juce::String title;
    float displayed;
    float peakHold = -100.0f;
    int holdTicks = 0;
};


//==============================================================================
// Shared painting helpers

inline void paintPanel (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

// The header strip every LBAudio plugin shares: NAME  TYPE | LBAudio
inline void paintHeader (juce::Graphics& g, int width, const juce::String& name, const juce::String& type)
{
    auto header = juce::Rectangle<int> (0, 0, width, 56).reduced (20, 0);
    g.setColour (Colours::text);
    g.setFont (font (24.0f, true));
    const int nameW = juce::GlyphArrangement::getStringWidthInt (font (24.0f, true), name.toUpperCase()) + 18;
    g.drawText (name.toUpperCase(), header.removeFromLeft (nameW), juce::Justification::centredLeft);
    g.setColour (Colours::accent);
    g.setFont (font (11.0f, true));
    const int typeW = juce::GlyphArrangement::getStringWidthInt (font (11.0f, true), type.toUpperCase()) + 16;
    g.drawText (type.toUpperCase(), header.removeFromLeft (typeW).translated (0, 3), juce::Justification::centredLeft);
    g.setColour (Colours::panelEdge.brighter (0.3f));
    g.fillRect (header.getX(), header.getCentreY() - 8, 1, 18);
    header.removeFromLeft (14);
    g.setColour (Colours::dim);
    g.setFont (font (13.0f, true));
    g.drawText ("LBAudio", header.removeFromLeft (100).translated (0, 2), juce::Justification::centredLeft);
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (56, 0.0f, (float) width);
}

//==============================================================================
// A rotary knob with its title above and value box below, bound to a parameter.
class LabelledKnob final : public juce::Component
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                  const juce::String& title, bool bipolar = false)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        if (bipolar) slider.getProperties().set ("bipolar", true);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
        if (auto* param = state.getParameter (paramID))
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

        label.setText (title, juce::dontSendNotification);
        label.setFont (font (11.5f, true));
        label.setColour (juce::Label::textColourId, Colours::dim);
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int knob = juce::jmin (r.getWidth() - 8, r.getHeight() - 18 - 22);
        auto group = r.withSizeKeepingCentre (r.getWidth(), 18 + knob + 22);
        label.setBounds (group.removeFromTop (18));
        slider.setBounds (group);
    }

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// A pill toggle bound to a bool parameter.
class Switch final : public juce::Component
{
public:
    Switch (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& text)
        : button (text)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramID, button);
        addAndMakeVisible (button);
    }
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::ToggleButton button;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
// Segmented buttons for a choice parameter (e.g. Tape | Tube | Clip).
class Segmented final : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramID)
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID));
        jassert (param != nullptr);
        choices = param->choices;
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { selected = juce::roundToInt (v); repaint(); });
        attachment->sendInitialUpdate();
        setWantsKeyboardFocus (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Colours::background);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        const float w = r.getWidth() / (float) juce::jmax (1, choices.size());
        for (int i = 0; i < choices.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (3.0f);
            const bool on = i == selected;
            if (on)
            {
                g.setColour (Colours::accent);
                g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
            }
            g.setColour (on ? juce::Colour (0xff07211e) : Colours::dim);
            g.setFont (font (11.5f, true));
            g.drawText (choices[i].toUpperCase(), cell, juce::Justification::centred);
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (Colours::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, choices.size() - 1, (int) ((float) e.x / (float) getWidth() * (float) choices.size()));
        attachment->setValueAsCompleteGesture ((float) i);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::rightKey)
        {
            const int i = juce::jlimit (0, choices.size() - 1, selected + (k == juce::KeyPress::rightKey ? 1 : -1));
            attachment->setValueAsCompleteGesture ((float) i);
            return true;
        }
        return false;
    }

private:
    juce::StringArray choices;
    int selected = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
// Thread-safe "peak since last read" value for meters.
inline void storeMax (std::atomic<float>& target, float value) noexcept
{
    auto prev = target.load (std::memory_order_relaxed);
    while (value > prev && ! target.compare_exchange_weak (prev, value, std::memory_order_relaxed)) {}
}

} // namespace lbaudio::ui
