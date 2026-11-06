#include "PluginEditor.h"

using namespace lbaudio::ui;

lbaudio::ui::Theme orbitTheme()
{
    Theme t;
    t.background = juce::Colour (0xff140c2a);
    t.panel      = juce::Colour (0xb81d1440);
    t.panelEdge  = juce::Colour (0xff3d2d72);
    t.track      = juce::Colour (0xff4a3a86);
    t.text       = juce::Colour (0xfff1ebff);
    t.dim        = juce::Colour (0xffa596d6);
    t.accent     = juce::Colour (0xffb48cff);
    t.accent2    = juce::Colour (0xff45f0d8);
    t.onAccent   = juce::Colour (0xff160d2e);
    t.knobTop    = juce::Colour (0xff33275f);
    t.knobBottom = juce::Colour (0xff150e2e);
    t.knobEdge   = juce::Colour (0xff54429a);
    t.pointer    = juce::Colour (0xfff1ebff);
    t.meterBack  = juce::Colour (0xff140c2a);
    t.headerText = juce::Colour (0xfff1ebff);
    t.headerType = juce::Colour (0xff45f0d8);
    t.headerBrand = juce::Colour (0xffa596d6);
    t.headerLine = juce::Colour (0xff2f2260);
    t.knob = KnobStyle::Orbit;
    t.corner = 22.0f;
    t.titleSize = 28.0f;
    t.titleKerning = 0.2f;
    t.typeKerning = 0.12f;
    t.titleCentred = true;
    return t;
}

//==============================================================================
void OrbitView::paint (juce::Graphics& g)
{
    const auto& t = themeOf (*this);
    paintPanel (g, getLocalBounds(), t);
    const auto area = getLocalBounds().toFloat().reduced (14.0f);
    const auto c = area.getCentre();
    const float rMax = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 18.0f, rMin = rMax * 0.34f;
    // Rings spaced by voice; the whole system grows with Delay
    const float scale = 0.72f + 0.28f * juce::jlimit (0.0f, 1.0f, (params.delayMs - 5.0f) / 21.0f);
    auto ringRadius = [&] (int v) { return scale * (rMin + (rMax - rMin) * (0.35f + 0.65f * (float) v / (float) (orbit::maxVoices - 1))); };
    const float swing = (rMax - rMin) * 0.16f * params.depth;

    // The star: the input
    {
        const float glow = 26.0f + 40.0f * juce::jlimit (0.0f, 1.0f, level * 1.5f);
        g.setGradientFill (juce::ColourGradient (t.accent2.withAlpha (0.55f), c.x, c.y, t.accent2.withAlpha (0.0f), c.x + glow, c.y, true));
        g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (c));
        g.setColour (t.text);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (c));
    }

    for (int v = 0; v < params.voices; ++v)
    {
        const float r = juce::jmin (rMax, ringRadius (v));
        // The orbit: dashed ring with the swing zone shaded
        g.setColour (t.accent.withAlpha (0.07f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c), juce::jmax (1.0f, 2.0f * swing));
        juce::Path ring; ring.addEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
        juce::Path dashed; const float dashes[] = { 2.0f, 5.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, ring, dashes, 2);
        g.setColour (t.track);
        g.fillPath (dashed);

        for (int side = 0; side < 2; ++side)
        {
            const auto colour = side == 0 ? t.accent : t.accent2;
            const double ph = orbit::voicePhase (params, phase, v, side == 1);
            auto pos = [&] (double p)
            {
                const double a = 2.0 * juce::MathConstants<double>::pi * p;
                const float rr = r + swing * (float) std::sin (a) + (side == 0 ? -4.0f : 4.0f);
                return juce::Point<float> (c.x + rr * (float) std::sin (a), c.y - rr * (float) std::cos (a));
            };
            // Trail
            for (int k = 1; k <= 14; ++k)
            {
                const auto p = pos (ph - 0.008 * k);
                g.setColour (colour.withAlpha (0.35f * (1.0f - (float) k / 15.0f)));
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));
            }
            const auto p = pos (ph);
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (p));
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (p));
        }
    }

    // Readouts
    g.setColour (t.text);
    g.setFont (styledFont (14.0f, true, false, 0.06f));
    const juce::String rateText = params.rateHz < 1.0f ? juce::String (params.rateHz, 2) : juce::String (params.rateHz, 1);
    g.drawText (rateText + " Hz", juce::Rectangle<float> (120.0f, 18.0f).withCentre ({ c.x, c.y + 30.0f }), juce::Justification::centred);
    g.setColour (t.dim);
    g.setFont (styledFont (10.0f, true, false, 0.2f));
    g.drawText (juce::String (params.voices) + (params.voices == 1 ? " VOICE" : " VOICES"), juce::Rectangle<float> (120.0f, 14.0f).withCentre ({ c.x, c.y + 47.0f }), juce::Justification::centred);
    g.setColour (t.accent);
    g.drawText ("L", juce::Rectangle<float> (20.0f, 14.0f).withPosition (area.getX() + 6.0f, area.getBottom() - 18.0f), juce::Justification::centredLeft);
    g.setColour (t.accent2);
    g.drawText ("R", juce::Rectangle<float> (20.0f, 14.0f).withPosition (area.getRight() - 20.0f, area.getBottom() - 18.0f), juce::Justification::centredRight);
}

//==============================================================================
OrbitAudioProcessorEditor::OrbitAudioProcessorEditor (OrbitAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    for (juce::Component* c : { (juce::Component*) &view, (juce::Component*) &rate, (juce::Component*) &depth,
                                (juce::Component*) &delay, (juce::Component*) &feedback, (juce::Component*) &width,
                                (juce::Component*) &tone, (juce::Component*) &mix, (juce::Component*) &voices })
        addAndMakeVisible (c);
    setLookAndFeel (&lnf);

    width.slider.setTooltip ("How far the chorus spreads across the stereo field");
    tone.slider.setTooltip ("Softens the top of the chorus");
    feedback.slider.setTooltip ("Feeds the chorus back into itself for a richer, more flanged sound");

    juce::Random rng (2718);
    for (int i = 0; i < 160; ++i)
    {
        stars.push_back ({ rng.nextFloat() * (float) defaultWidth, 58.0f + rng.nextFloat() * (float) (defaultHeight - 58) });
        starSizes.push_back (rng.nextFloat() < 0.15f ? 2.2f : 1.2f);
        starAlpha.push_back (0.25f + 0.65f * rng.nextFloat());
    }
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

OrbitAudioProcessorEditor::~OrbitAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void OrbitAudioProcessorEditor::refreshFromProcessor()
{
    const float pk = processorRef.inputPeak.exchange (0.0f);
    level = pk > level ? pk : level * 0.9f;
    view.setState (processorRef.currentParams(), processorRef.lfoPhaseForDisplay.load(), level);
}

void OrbitAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22144a), b.getCentreX(), b.getCentreY(), juce::Colour (0xff0b0619), 0.0f, 0.0f, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (t.accent2.withAlpha (0.10f), 80.0f, 90.0f, t.accent2.withAlpha (0.0f), 380.0f, 90.0f, true));
    g.fillRect (b);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff5fc8).withAlpha (0.10f), b.getRight() - 90.0f, b.getBottom() - 40.0f,
                                             juce::Colour (0xffff5fc8).withAlpha (0.0f), b.getRight() - 420.0f, b.getBottom() - 40.0f, true));
    g.fillRect (b);
    for (size_t i = 0; i < stars.size(); ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (starAlpha[i]));
        g.fillEllipse (juce::Rectangle<float> (starSizes[i], starSizes[i]).withCentre (stars[i]));
    }
    paintHeader (g, getWidth(), "Orbit", LBAUDIO_PLUGIN_TYPE, t);
    paintPanel (g, leftPanel, t);
    paintPanel (g, rightPanel, t);
    g.setColour (t.dim);
    g.setFont (font (11.5f, true));
    g.drawText ("VOICES", voicesLabel, juce::Justification::centred);
}

void OrbitAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);
    const int side = (area.getWidth() - area.getHeight() - 32) / 2;
    leftPanel = area.removeFromLeft (side);
    rightPanel = area.removeFromRight (side);
    view.setBounds (area.reduced (16, 0));

    auto grid = [] (juce::Rectangle<int> panel)
    {
        auto r = panel.reduced (10, 14);
        const int w = r.getWidth() / 2, h = r.getHeight() / 2;
        return std::array<juce::Rectangle<int>, 4> { r.withSize (w, h), r.withSize (w, h).translated (w, 0),
                                                     r.withSize (w, h).translated (0, h), r.withSize (w, h).translated (w, h) };
    };
    const auto l = grid (leftPanel), r = grid (rightPanel);
    rate.setBounds (l[0]); depth.setBounds (l[1]); delay.setBounds (l[2]); feedback.setBounds (l[3]);
    width.setBounds (r[0]); tone.setBounds (r[1]); mix.setBounds (r[2]);
    auto vcell = r[3].reduced (6, 0);
    voicesLabel = vcell.withHeight (18).withY (vcell.getCentreY() - 30);
    voices.setBounds (vcell.withHeight (34).withY (vcell.getCentreY() - 6));
}
