#pragma once

// LBAudio shared look: colours, fonts, knob/toggle drawing and level meters.
//
// The original five plugins use the house palette (Colours, below) through the default Theme. Newer plugins each
// carry their own Theme: palette, knob style, panel style and title lettering, so every plugin can have its own
// identity while sharing the same controls, behaviour and code.

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
// A plugin's visual identity. Default-constructed, it is the house look of the original range.
enum class KnobStyle
{
    Classic,  // track arc, value arc, dark cap, pointer line
    Halo,     // wide soft-glowing arc, rounded cap, pointer dot
    Hairline, // thin arc, flat cap with an outline, long pointer: for light faceplates
    Neon,     // arc running from accent to accent2 with a glow, bright pointer
    Chunky,   // thick square-ended arc and a thick pointer bar on a solid cap
    Orbit     // fine arc with a "planet" dot riding its end
};

enum class PanelStyle { Filled, Outline, Screen, Glass };

// A soft bloom around a path: its outline blurred and filled in a colour.
inline void paintGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, int radius)
{
    if (c.getAlpha() > 0 && radius > 0)
        juce::DropShadow (c, radius, {}).drawForPath (g, p);
}

struct Theme
{
    juce::Colour background { Colours::background }, panel { Colours::panel }, panelEdge { Colours::panelEdge },
                 track { Colours::track }, text { Colours::text }, dim { Colours::dim },
                 accent { Colours::accent }, accent2 { Colours::gr }, onAccent { 0xff07211e },
                 knobTop { 0xff323843 }, knobBottom { 0xff1c1f25 }, knobEdge { 0xff3d434f }, pointer { Colours::text },
                 meterBack { Colours::background }, meterLow { Colours::green }, meterMid { Colours::yellow },
                 meterHigh { Colours::red }, reduction { Colours::gr },
                 headerText { Colours::text }, headerType { Colours::accent }, headerBrand { Colours::dim },
                 headerLine { Colours::panelEdge };
    KnobStyle knob = KnobStyle::Classic;
    PanelStyle panelStyle = PanelStyle::Filled;
    float corner = 8.0f;
    // Title lettering
    float titleSize = 24.0f, titleKerning = 0.0f, typeKerning = 0.0f;
    bool titleBold = true, titleItalic = false, titleCentred = false;
    // Bloom: 0 is off (the original look). Above 0, lit things (value arcs, pointers, switches that are on, meters)
    // glow in their own colour, fringed with glowTint, and the title glows in titleGlow.
    float glow = 0.0f;
    juce::Colour glowTint { juce::Colours::transparentBlack }, titleGlow { juce::Colours::transparentBlack };
};

inline const Theme& defaultTheme() { static const Theme t; return t; }

inline juce::Font styledFont (float height, bool bold, bool italic, float kerning = 0.0f)
{
    int flags = (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0);
    return juce::Font (juce::FontOptions (height, flags)).withExtraKerningFactor (kerning);
}

//==============================================================================
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (Theme t = {}) : theme (t)
    {
        setColour (juce::Slider::textBoxTextColourId, theme.text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, theme.accent.withAlpha (0.3f));
        setColour (juce::Label::textColourId, theme.text);
        setColour (juce::Label::textWhenEditingColourId, theme.text);
        setColour (juce::Label::backgroundWhenEditingColourId, theme.panelEdge);
        setColour (juce::Label::outlineWhenEditingColourId, theme.accent);
        setColour (juce::TextEditor::highlightColourId, theme.accent.withAlpha (0.3f));
        setColour (juce::TextEditor::highlightedTextColourId, theme.text);
        setColour (juce::CaretComponent::caretColourId, theme.accent);
        setColour (juce::TextButton::textColourOffId, theme.dim);
        setColour (juce::TextButton::textColourOnId, theme.onAccent);
        setColour (juce::TooltipWindow::backgroundColourId, theme.panel);
        setColour (juce::TooltipWindow::textColourId, theme.text);
        setColour (juce::TooltipWindow::outlineColourId, theme.panelEdge);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Bipolar knobs (e.g. makeup) draw their arc from the zero point, or from an "origin" value if set
        // (e.g. 100% width).
        float arcStart = startAngle;
        const auto& props = slider.getProperties();
        if (props.contains ("origin"))
            arcStart = startAngle + (float) slider.valueToProportionOfLength ((double) props["origin"]) * (endAngle - startAngle);
        else if (props.contains ("bipolar"))
            arcStart = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);
        const bool hasValue = std::abs (angle - arcStart) > 0.001f;
        const float a0 = juce::jmin (arcStart, angle), a1 = juce::jmax (arcStart, angle);
        auto arc = [&] (float r, float from, float to)
        {
            juce::Path p; p.addCentredArc (cx, cy, r, r, 0.0f, from, to, true); return p;
        };
        auto onCircle = [&] (float r, float a) { return juce::Point<float> (cx + r * std::sin (a), cy - r * std::cos (a)); };
        auto col = [&] (juce::Colour c) { return c.withMultipliedAlpha (alpha); };
        const juce::PathStrokeType::EndCapStyle round = juce::PathStrokeType::rounded;

        switch (theme.knob)
        {
            case KnobStyle::Classic:
            {
                const float lineW = 3.5f, arcR = radius - lineW;
                g.setColour (col (theme.track));
                g.strokePath (arc (arcR, startAngle, endAngle), { lineW, juce::PathStrokeType::curved, round });
                if (hasValue)
                {
                    g.setColour (col (theme.accent));
                    g.strokePath (arc (arcR, a0, a1), { lineW, juce::PathStrokeType::curved, round });
                }
                const float knobR = arcR - 7.0f;
                paintCap (g, cx, cy, knobR, alpha);
                juce::Path pointer;
                pointer.addRoundedRectangle (-1.5f, -knobR + 5.0f, 3.0f, knobR * 0.42f, 1.5f);
                g.setColour (col (theme.pointer));
                g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (cx, cy));
                break;
            }
            case KnobStyle::Halo:
            {
                const bool glows = theme.glow > 0.0f;
                // A glowing knob sits a little further inside its bounds, so the bloom isn't cut off
                const float lineW = glows ? 4.5f : 5.0f, arcR = radius - lineW - (glows ? 2.0f : 0.0f);
                const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, round);
                g.setColour (col (theme.track));
                g.strokePath (arc (arcR, startAngle, endAngle), stroke);
                if (hasValue && glows)
                {
                    // The value arc as a lit filament: a coloured bloom fringed with the glow tint, the arc, then a
                    // hot white core
                    const auto valueArc = arc (arcR, a0, a1);
                    juce::Path lit;
                    stroke.createStrokedPath (lit, valueArc);
                    const auto fringe = theme.glowTint.isTransparent() ? theme.accent : theme.glowTint;
                    paintGlow (g, lit, col (fringe.withMultipliedAlpha (juce::jmin (1.0f, 0.9f * theme.glow))), 8);
                    paintGlow (g, lit, col (theme.accent.withMultipliedAlpha (juce::jmin (1.0f, 1.0f * theme.glow))), 3);
                    g.setColour (col (theme.accent));
                    g.strokePath (valueArc, stroke);
                    g.setColour (col (theme.accent.interpolatedWith (juce::Colours::white, 0.65f)));
                    g.strokePath (valueArc, { 1.4f, juce::PathStrokeType::curved, round });
                }
                else if (hasValue)
                {
                    g.setColour (col (theme.accent.withAlpha (0.16f)));
                    g.strokePath (arc (arcR, a0, a1), { lineW * 3.2f, juce::PathStrokeType::curved, round });
                    g.setColour (col (theme.accent.withAlpha (0.3f)));
                    g.strokePath (arc (arcR, a0, a1), { lineW * 1.8f, juce::PathStrokeType::curved, round });
                    g.setColour (col (theme.accent));
                    g.strokePath (arc (arcR, a0, a1), stroke);
                }
                const float knobR = arcR - (glows ? 8.0f : 9.0f);
                paintCap (g, cx, cy, knobR, alpha);
                const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre (onCircle (knobR - 8.0f, angle));
                if (glows)
                {
                    // light catching the rim of the cap, and a pointer that glows like the arc
                    g.setColour (col (juce::Colours::white.withAlpha (0.07f)));
                    g.drawEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre ({ cx, cy }).reduced (1.5f), 1.0f);
                    juce::Path d; d.addEllipse (dot);
                    paintGlow (g, d, col (theme.accent.withMultipliedAlpha (juce::jmin (1.0f, 1.2f * theme.glow))), 4);
                }
                g.setColour (col (theme.pointer));
                g.fillEllipse (dot);
                break;
            }
            case KnobStyle::Hairline:
            {
                const float lineW = 2.0f, arcR = radius - 3.0f;
                g.setColour (col (theme.track));
                g.strokePath (arc (arcR, startAngle, endAngle), { lineW, juce::PathStrokeType::curved, round });
                if (hasValue)
                {
                    g.setColour (col (theme.accent));
                    g.strokePath (arc (arcR, a0, a1), { lineW + 1.0f, juce::PathStrokeType::curved, round });
                }
                const float knobR = arcR - 8.0f;
                const auto cap = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre ({ cx, cy });
                g.setColour (col (theme.knobTop));
                g.fillEllipse (cap);
                g.setColour (col (theme.knobEdge));
                g.drawEllipse (cap, 1.0f);
                g.setColour (col (theme.pointer));
                g.drawLine (juce::Line<float> (onCircle (knobR * 0.15f, angle), onCircle (knobR - 3.0f, angle)), 2.0f);
                g.setColour (col (theme.accent));
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (onCircle (arcR, angle)));
                break;
            }
            case KnobStyle::Neon:
            {
                const float lineW = 4.0f, arcR = radius - lineW - 1.0f;
                g.setColour (col (theme.track));
                g.strokePath (arc (arcR, startAngle, endAngle), { lineW, juce::PathStrokeType::curved, round });
                if (hasValue)
                {
                    juce::ColourGradient grad (col (theme.accent), cx - arcR, cy, col (theme.accent2), cx + arcR, cy, false);
                    auto p = arc (arcR, a0, a1);
                    g.setGradientFill (grad);
                    g.setOpacity (0.22f);
                    g.strokePath (p, { lineW * 3.0f, juce::PathStrokeType::curved, round });
                    g.setOpacity (1.0f);
                    g.setGradientFill (grad);
                    g.strokePath (p, { lineW, juce::PathStrokeType::curved, round });
                }
                const float knobR = arcR - 8.0f;
                paintCap (g, cx, cy, knobR, alpha);
                juce::Path pointer;
                pointer.addRoundedRectangle (-1.25f, -knobR + 4.0f, 2.5f, knobR * 0.5f, 1.25f);
                g.setColour (col (theme.pointer));
                g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (cx, cy));
                break;
            }
            case KnobStyle::Chunky:
            {
                const float lineW = 7.0f, arcR = radius - lineW * 0.5f - 1.0f;
                const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
                g.setColour (col (theme.track));
                g.strokePath (arc (arcR, startAngle, endAngle), stroke);
                if (hasValue)
                {
                    g.setColour (col (theme.accent));
                    g.strokePath (arc (arcR, a0, a1), stroke);
                }
                const float knobR = arcR - lineW * 0.5f - 5.0f;
                const auto cap = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre ({ cx, cy });
                g.setColour (col (theme.knobBottom));
                g.fillEllipse (cap.translated (0.0f, 2.5f));
                g.setColour (col (theme.knobTop));
                g.fillEllipse (cap);
                g.setColour (col (theme.knobEdge));
                g.drawEllipse (cap, 2.0f);
                juce::Path pointer;
                pointer.addRectangle (-3.0f, -knobR + 3.0f, 6.0f, knobR * 0.55f);
                g.setColour (col (theme.pointer));
                g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (cx, cy));
                break;
            }
            case KnobStyle::Orbit:
            {
                const float lineW = 2.5f, arcR = radius - 6.0f;
                const float dashes[] = { 2.0f, 4.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, arc (arcR, startAngle, endAngle), dashes, 2);
                g.setColour (col (theme.track));
                g.fillPath (dashed);
                if (hasValue)
                {
                    g.setColour (col (theme.accent));
                    g.strokePath (arc (arcR, a0, a1), { lineW, juce::PathStrokeType::curved, round });
                }
                const float knobR = arcR - 10.0f;
                paintCap (g, cx, cy, knobR, alpha);
                g.setColour (col (theme.pointer));
                g.drawLine (juce::Line<float> (onCircle (knobR * 0.35f, angle), onCircle (knobR - 4.0f, angle)), 2.0f);
                const auto planet = onCircle (arcR, angle);
                g.setColour (col (theme.accent2.withAlpha (0.25f)));
                g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (planet));
                g.setColour (col (theme.accent2));
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (planet));
                break;
            }
        }
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const float corner = juce::jmin (r.getHeight() * 0.5f, theme.corner * 2.0f);

        g.setColour (on ? theme.accent.withAlpha (0.16f) : theme.panel);
        g.fillRoundedRectangle (r, corner);
        g.setColour (on ? theme.accent : theme.panelEdge.brighter (highlighted ? 0.4f : 0.1f));
        g.drawRoundedRectangle (r, corner, 1.2f);

        const float dot = 7.0f;
        const auto dotArea = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + r.getHeight() * 0.5f + 2.0f, r.getCentreY() });
        if (on && theme.glow > 0.0f)
        {
            // a switch that's on is lit: a soft light inside the pill and a glowing lamp
            g.setColour (theme.accent.withAlpha (0.10f * juce::jmin (1.0f, theme.glow)));
            g.drawRoundedRectangle (r.reduced (2.0f), juce::jmax (0.0f, corner - 2.0f), 3.0f);
            juce::Path lamp; lamp.addEllipse (dotArea);
            paintGlow (g, lamp, theme.accent.withMultipliedAlpha (juce::jmin (1.0f, 1.3f * theme.glow)), 5);
        }
        g.setColour (on ? (theme.glow > 0.0f ? theme.accent.interpolatedWith (juce::Colours::white, 0.35f) : theme.accent) : theme.track);
        g.fillEllipse (dotArea);

        g.setColour (on ? theme.text : theme.dim);
        g.setFont (font (11.5f, true));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (r.getHeight() * 0.5f + 10.0f).withTrimmedRight (8.0f),
                    juce::Justification::centred);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        g.setColour (on ? theme.accent : (highlighted ? theme.panelEdge : theme.background));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        if (! on)
        {
            g.setColour (theme.panelEdge.brighter (highlighted ? 0.3f : 0.0f));
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

    const Theme theme;

private:
    void paintCap (juce::Graphics& g, float cx, float cy, float r, float alpha) const
    {
        const auto cap = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ cx, cy });
        g.setGradientFill (juce::ColourGradient (theme.knobTop.withMultipliedAlpha (alpha), cx, cy - r,
                                                 theme.knobBottom.withMultipliedAlpha (alpha), cx, cy + r, false));
        g.fillEllipse (cap);
        g.setColour (theme.knobEdge.withMultipliedAlpha (alpha));
        g.drawEllipse (cap, 1.0f);
    }
};

// The theme of the LBAudio look-and-feel a component is drawn with (the house look if none is set).
inline const Theme& themeOf (const juce::Component& c)
{
    if (auto* l = dynamic_cast<const LookAndFeel*> (&c.getLookAndFeel())) return l->theme;
    return defaultTheme();
}

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
        const auto& t = themeOf (*this);
        auto area = getLocalBounds().toFloat();
        g.setColour (t.dim);
        g.setFont (font (11.0f, true));
        g.drawText (title, area.removeFromTop (16.0f), juce::Justification::centred);

        const auto readoutArea = area.removeFromBottom (18.0f);
        const auto bar = area.reduced (5.0f, 4.0f);
        const float barCorner = juce::jmin (3.0f, t.corner * 0.5f);

        g.setColour (t.meterBack);
        g.fillRoundedRectangle (bar, barCorner);

        if (kind == Kind::Level)
        {
            auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f)); };
            const float h = bar.getHeight() * norm (displayed);
            if (h > 0.5f)
            {
                const auto lit = bar.withTop (bar.getBottom() - h);
                if (t.glow > 0.0f)
                {
                    juce::Path p; p.addRoundedRectangle (lit, barCorner);
                    paintGlow (g, p, t.meterMid.withAlpha (0.45f * juce::jmin (1.0f, t.glow)), 4);
                }
                juce::ColourGradient grad (t.meterLow, 0.0f, bar.getBottom(), t.meterHigh, 0.0f, bar.getY(), false);
                grad.addColour (norm (-12.0f), t.meterLow);
                grad.addColour (norm (-3.0f), t.meterMid);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (lit, barCorner);
                if (t.glow > 0.0f)
                {
                    // the top of the bar burns brightest
                    g.setColour (juce::Colours::white.withAlpha (0.55f));
                    g.fillRect (lit.withHeight (1.5f));
                }
            }
            if (peakHold > -60.0f)
            {
                g.setColour (t.text.withAlpha (0.8f));
                g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * norm (peakHold) - 1.0f, bar.getWidth(), 2.0f);
            }
            drawTicks (g, t, bar, { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f }, [&] (float db) { return bar.getBottom() - bar.getHeight() * norm (db); });
        }
        else
        {
            auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, db / 24.0f); };
            const float h = bar.getHeight() * norm (displayed);
            if (h > 0.5f)
            {
                if (t.glow > 0.0f)
                {
                    juce::Path p; p.addRoundedRectangle (bar.withHeight (h), barCorner);
                    paintGlow (g, p, t.reduction.withAlpha (0.5f * juce::jmin (1.0f, t.glow)), 4);
                }
                g.setGradientFill (juce::ColourGradient (t.reduction, 0.0f, bar.getY(), t.reduction.darker (0.5f), 0.0f, bar.getBottom(), false));
                g.fillRoundedRectangle (bar.withHeight (h), barCorner);
                if (t.glow > 0.0f)
                {
                    g.setColour (t.reduction.interpolatedWith (juce::Colours::white, 0.6f));
                    g.fillRect (bar.withHeight (h).removeFromBottom (1.5f));
                }
            }
            drawTicks (g, t, bar, { 6.0f, 12.0f, 18.0f }, [&] (float db) { return bar.getY() + bar.getHeight() * norm (db); });
        }

        g.setColour (kind == Kind::GainReduction ? t.reduction : t.text);
        g.setFont (font (11.5f));
        juce::String readout;
        if (kind == Kind::Level) readout = displayed <= -60.0f ? "-inf" : juce::String (displayed, 1);
        else                     readout = displayed < 0.05f ? "0.0" : "-" + juce::String (displayed, 1);
        g.drawText (readout, readoutArea, juce::Justification::centred);
    }

private:
    template <typename ToY>
    static void drawTicks (juce::Graphics& g, const Theme& t, juce::Rectangle<float> bar, std::initializer_list<float> dbs, ToY toY)
    {
        g.setColour (t.panelEdge.withAlpha (0.9f));
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

inline void paintPanel (juce::Graphics& g, juce::Rectangle<int> r, const Theme& t)
{
    const auto f = r.toFloat();
    switch (t.panelStyle)
    {
        case PanelStyle::Filled:
            g.setColour (t.panel);
            g.fillRoundedRectangle (f, t.corner);
            g.setColour (t.panelEdge);
            g.drawRoundedRectangle (f.reduced (0.5f), t.corner, 1.0f);
            break;
        case PanelStyle::Outline: // a glowing outline over a faint fill
            g.setColour (t.panel);
            g.fillRoundedRectangle (f, t.corner);
            g.setColour (t.panelEdge.withAlpha (0.35f));
            g.drawRoundedRectangle (f.reduced (1.5f), t.corner, 4.0f);
            g.setColour (t.panelEdge);
            g.drawRoundedRectangle (f.reduced (0.5f), t.corner, 1.2f);
            break;
        case PanelStyle::Screen: // a recessed screen set into the faceplate
            g.setColour (t.panelEdge);
            g.fillRoundedRectangle (f, t.corner);
            g.setColour (t.panel);
            g.fillRoundedRectangle (f.reduced (3.0f), juce::jmax (0.0f, t.corner - 1.5f));
            break;
        case PanelStyle::Glass: // smoked glass, a little lighter at the top, with light catching its top edge
        {
            g.setGradientFill (juce::ColourGradient (t.panel.brighter (0.12f), 0.0f, f.getY(), t.panel.darker (0.35f), 0.0f, f.getBottom(), false));
            g.fillRoundedRectangle (f, t.corner);
            g.setColour (t.panelEdge);
            g.drawRoundedRectangle (f.reduced (0.5f), t.corner, 1.0f);
            const auto rimColour = (t.glowTint.isTransparent() ? t.text : t.glowTint);
            juce::ColourGradient rim (rimColour.withAlpha (0.0f), f.getX() + t.corner, 0.0f, rimColour.withAlpha (0.0f), f.getRight() - t.corner, 0.0f, false);
            rim.addColour (0.5, rimColour.withAlpha (0.55f));
            g.setGradientFill (rim);
            g.fillRect (f.getX() + t.corner, f.getY(), f.getWidth() - t.corner * 2.0f, 1.0f);
            break;
        }
    }
}

inline void paintPanel (juce::Graphics& g, juce::Rectangle<int> r) { paintPanel (g, r, defaultTheme()); }

// The header strip every LBAudio plugin shares: NAME  TYPE | LBAudio
inline void paintHeader (juce::Graphics& g, int width, const juce::String& name, const juce::String& type, const Theme& t)
{
    const auto nameFont = styledFont (t.titleSize, t.titleBold, t.titleItalic, t.titleKerning);
    const auto typeFont = styledFont (11.0f, true, false, t.typeKerning);
    const auto brandFont = font (13.0f, true);
    const auto upperName = name.toUpperCase(), upperType = type.toUpperCase();
    const int nameW = juce::GlyphArrangement::getStringWidthInt (nameFont, upperName) + 18;
    const int typeW = juce::GlyphArrangement::getStringWidthInt (typeFont, upperType) + 16;
    const int total = nameW + typeW + 14 + 70;

    auto header = juce::Rectangle<int> (0, 0, width, 56).reduced (20, 0);
    if (t.titleCentred) header = header.withSizeKeepingCentre (total, 56);

    const auto nameArea = header.removeFromLeft (nameW);
    if (! t.titleGlow.isTransparent())
    {
        // The title as a light source: the letters blurred into a wide soft halo and a tighter bright one
        juce::GlyphArrangement ga;
        ga.addCurtailedLineOfText (nameFont, upperName, 0.0f, 0.0f, (float) nameArea.getWidth(), true);
        ga.justifyGlyphs (0, ga.getNumGlyphs(), (float) nameArea.getX(), (float) nameArea.getY(),
                          (float) nameArea.getWidth(), (float) nameArea.getHeight(), juce::Justification::centredLeft);
        juce::Path letters;
        ga.createPath (letters);
        paintGlow (g, letters, t.titleGlow.withMultipliedAlpha (0.85f), 14);
        paintGlow (g, letters, t.titleGlow, 5);
        paintGlow (g, letters, t.headerText.withAlpha (0.55f), 2);
    }
    g.setColour (t.headerText);
    g.setFont (nameFont);
    g.drawText (upperName, nameArea, juce::Justification::centredLeft);
    g.setColour (t.headerType);
    g.setFont (typeFont);
    g.drawText (upperType, header.removeFromLeft (typeW).translated (0, 3), juce::Justification::centredLeft);
    g.setColour (t.headerLine.brighter (0.3f));
    g.fillRect (header.getX(), header.getCentreY() - 8, 1, 18);
    header.removeFromLeft (14);
    g.setColour (t.headerBrand);
    g.setFont (brandFont);
    g.drawText ("LBAudio", header.removeFromLeft (100).translated (0, 2), juce::Justification::centredLeft);
    g.setColour (t.headerLine);
    g.drawHorizontalLine (56, 0.0f, (float) width);
}

inline void paintHeader (juce::Graphics& g, int width, const juce::String& name, const juce::String& type)
{
    paintHeader (g, width, name, type, defaultTheme());
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

    void lookAndFeelChanged() override
    {
        label.setColour (juce::Label::textColourId, themeOf (*this).dim);
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
        const auto& t = themeOf (*this);
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float corner = juce::jmin (r.getHeight() * 0.5f, t.corner * 2.0f);
        g.setColour (t.background);
        g.fillRoundedRectangle (r, corner);
        g.setColour (t.panelEdge);
        g.drawRoundedRectangle (r, corner, 1.0f);
        const float w = r.getWidth() / (float) juce::jmax (1, choices.size());
        for (int i = 0; i < choices.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (3.0f);
            const bool on = i == selected;
            if (on)
            {
                g.setColour (t.accent);
                g.fillRoundedRectangle (cell, juce::jmin (cell.getHeight() * 0.5f, t.corner * 2.0f));
            }
            g.setColour (on ? t.onAccent : t.dim);
            g.setFont (font (11.5f, true));
            g.drawText (choices[i].toUpperCase(), cell, juce::Justification::centred);
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (t.accent.withAlpha (0.6f));
            g.drawRoundedRectangle (r, corner, 1.5f);
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
