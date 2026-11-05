#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CompressorDSP.h"

namespace squeeze::ui
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

        // Bipolar knobs (e.g. makeup) draw their arc from the zero point.
        float arcStart = startAngle;
        if (slider.getProperties().contains ("bipolar"))
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
// Static input -> output curve, with a dot riding on it at the current input level.
class TransferCurve final : public juce::Component
{
public:
    void setState (const CompressorParams& p, float inputDb)
    {
        params = p;
        inputLevelDb = inputDb;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        const auto plot = area.reduced (14.0f).withTrimmedLeft (18.0f).withTrimmedBottom (16.0f);
        auto toX = [&] (float db) { return juce::jmap (db, minDb, maxDb, plot.getX(), plot.getRight()); };
        auto toY = [&] (float db) { return juce::jmap (db, minDb, maxDb, plot.getBottom(), plot.getY()); };

        // Grid + axis labels
        g.setFont (font (10.0f));
        for (float db = minDb; db <= maxDb + 0.01f; db += 12.0f)
        {
            g.setColour (Colours::panelEdge);
            g.drawHorizontalLine ((int) toY (db), plot.getX(), plot.getRight());
            g.drawVerticalLine ((int) toX (db), plot.getY(), plot.getBottom());

            g.setColour (Colours::dim.withAlpha (0.8f));
            const auto label = juce::String ((int) db);
            g.drawText (label, juce::Rectangle<float> (28.0f, 12.0f).withCentre ({ toX (db), plot.getBottom() + 10.0f }),
                        juce::Justification::centred);
            g.drawText (label, juce::Rectangle<float> (26.0f, 12.0f).withCentre ({ plot.getX() - 15.0f, toY (db) }),
                        juce::Justification::centredRight);
        }

        // Unity line
        juce::Path unity;
        unity.startNewSubPath (toX (minDb), toY (minDb));
        unity.lineTo (toX (maxDb), toY (maxDb));
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dashes, 2);
        g.setColour (Colours::dim.withAlpha (0.35f));
        g.fillPath (dashed);

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());

        // Threshold marker + knee band
        const float tx = toX (params.thresholdDb);
        if (params.kneeDb > 0.0f)
        {
            g.setColour (Colours::accent.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (toX (params.thresholdDb - params.kneeDb * 0.5f), plot.getY(),
                                                                    toX (params.thresholdDb + params.kneeDb * 0.5f), plot.getBottom()));
        }
        g.setColour (Colours::accent.withAlpha (0.25f));
        g.drawVerticalLine ((int) tx, plot.getY(), plot.getBottom());

        // Curve
        juce::Path curve;
        const int steps = (int) plot.getWidth();
        for (int i = 0; i <= steps; ++i)
        {
            const float inDb = juce::jmap ((float) i, 0.0f, (float) steps, minDb, maxDb);
            const float y = toY (Compressor::staticOutputDb (inDb, params));
            if (i == 0) curve.startNewSubPath (toX (inDb), y);
            else        curve.lineTo (toX (inDb), y);
        }

        juce::Path fill (curve);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.16f), 0.0f, plot.getY(),
                                                 Colours::accent.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);

        g.setColour (Colours::accent);
        g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Live input dot
        if (inputLevelDb > minDb)
        {
            const juce::Point<float> p { toX (inputLevelDb), toY (Compressor::staticOutputDb (inputLevelDb, params)) };
            g.setColour (Colours::gr.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (p));
            g.setColour (Colours::gr);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
        }
    }

private:
    static constexpr float minDb = -60.0f, maxDb = 0.0f;
    CompressorParams params;
    float inputLevelDb = -100.0f;
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

} // namespace squeeze::ui
