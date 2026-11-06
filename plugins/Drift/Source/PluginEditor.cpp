#include "PluginEditor.h"

using namespace lbaudio::ui;

lbaudio::ui::Theme driftTheme()
{
    Theme t;
    t.background = juce::Colour (0xff1a0a12);
    t.panel      = juce::Colour (0xff2a1120);
    t.panelEdge  = juce::Colour (0xff4f2338);
    t.track      = juce::Colour (0xff42202f);
    t.text       = juce::Colour (0xffffeedd);
    t.dim        = juce::Colour (0xffc99e8c);
    t.accent     = juce::Colour (0xffff9a3c);
    t.accent2    = juce::Colour (0xffffd9a8);
    t.onAccent   = juce::Colour (0xff2a0f1a);
    t.knobTop    = juce::Colour (0xff53263a);
    t.knobBottom = juce::Colour (0xff250e19);
    t.knobEdge   = juce::Colour (0xff6b3249);
    t.pointer    = juce::Colour (0xffffe3c2);
    t.meterBack  = juce::Colour (0xff1a0a12);
    t.reduction  = juce::Colour (0xffff6b5e);
    t.headerText = juce::Colour (0xffffeedd);
    t.headerType = juce::Colour (0xffff9a3c);
    t.headerBrand = juce::Colour (0xffc99e8c);
    t.headerLine = juce::Colour (0xff4f2338);
    t.knob = KnobStyle::Halo;
    t.corner = 14.0f;
    t.titleSize = 27.0f;
    t.titleItalic = true;
    t.titleKerning = 0.06f;
    t.typeKerning = 0.12f;
    return t;
}

//==============================================================================
void EchoTrail::paint (juce::Graphics& g)
{
    const auto& t = themeOf (*this);
    auto area = getLocalBounds().toFloat();
    paintPanel (g, getLocalBounds(), t);

    const auto& p = state.params;
    const double delay = drift::delayMs (p, state.bpm);
    const auto list = drift::echoes (p, state.bpm, 12);

    // Readouts
    auto top = area.reduced (18.0f, 12.0f).removeFromTop (20.0f);
    g.setFont (styledFont (13.0f, true, true, 0.04f));
    g.setColour (t.text);
    juce::String timeText = p.sync ? juce::String (drift::divisions()[(size_t) p.division].name) + "   " : juce::String();
    timeText << juce::String (juce::roundToInt (delay)) << " ms";
    if (p.sync) timeText << "   " << juce::String (state.bpm, 1) << " BPM";
    g.drawText (timeText, top, juce::Justification::centredLeft);
    g.setColour (t.dim);
    g.setFont (styledFont (11.0f, true, false, 0.12f));
    g.drawText (p.mode == drift::PingPong ? "PING-PONG" : "STEREO", top, juce::Justification::centredRight);

    // Timeline: from the hit (x0) to the last echo shown
    const auto plot = area.reduced (24.0f, 0.0f).withTrimmedTop (42.0f).withTrimmedBottom (26.0f);
    const float cy = plot.getCentreY();
    const double spanMs = juce::jmax (delay * (list.size() + 0.6), 200.0);
    const float x0 = plot.getX() + 18.0f;
    auto toX = [&] (double ms) { return x0 + (float) (ms / spanMs) * (plot.getRight() - x0); };
    const float halfH = plot.getHeight() * 0.5f - 4.0f;

    // Beat grid when synced, 100 ms grid otherwise
    {
        const double step = p.sync ? 60000.0 / state.bpm : 100.0;
        for (double ms = step; ms < spanMs; ms += step)
        {
            g.setColour (t.panelEdge.withAlpha (0.55f));
            g.drawVerticalLine ((int) toX (ms), plot.getY(), plot.getBottom());
        }
        g.setColour (t.dim.withAlpha (0.7f));
        g.setFont (font (10.0f));
        g.drawText (p.sync ? "BEATS" : "100 MS GRID", plot.withTop (plot.getBottom() + 4.0f).withHeight (16.0f), juce::Justification::centredRight);
    }

    // The tape: a centre line that wavers with Wobble
    {
        juce::Path tape;
        const float amp = 1.0f + 7.0f * p.wobble;
        bool first = true;
        for (float x = plot.getX(); x <= plot.getRight(); x += 3.0f)
        {
            const float y = cy + amp * std::sin (state.wobblePhase + (x - plot.getX()) * 0.035f);
            if (first) tape.startNewSubPath (x, y); else tape.lineTo (x, y);
            first = false;
        }
        g.setColour (t.accent.withAlpha (0.35f));
        g.strokePath (tape, juce::PathStrokeType (1.5f));
    }

    // The tape head sweeping out from the last hit
    const double sinceMs = state.secondsSinceHit * 1000.0;
    if (sinceMs < spanMs)
    {
        const float hx = toX (sinceMs);
        g.setGradientFill (juce::ColourGradient (t.accent2.withAlpha (0.0f), hx - 60.0f, cy, t.accent2.withAlpha (0.18f), hx, cy, false));
        g.fillRect (juce::Rectangle<float> (hx - 60.0f, plot.getY(), 60.0f, plot.getHeight()));
        g.setColour (t.accent2.withAlpha (0.8f));
        g.drawVerticalLine ((int) hx, plot.getY(), plot.getBottom());
    }

    // The original hit
    {
        const auto bar = juce::Rectangle<float> (10.0f, halfH * 1.8f).withCentre ({ x0, cy });
        g.setColour (t.text);
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (t.dim);
        g.setFont (font (10.0f, true));
        g.drawText ("IN", juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x0, plot.getBottom() + 12.0f }), juce::Justification::centred);
    }

    // The echoes
    const float duckGain = std::pow (10.0f, -state.duckDb / 20.0f);
    for (const auto& e : list)
    {
        const float x = toX (e.ms);
        if (x > plot.getRight() - 4.0f) break;
        const float level = juce::jlimit (0.0f, 1.0f, juce::jmap (e.db, -48.0f, 0.0f, 0.0f, 1.0f)) * (p.mix > 0.001f ? 1.0f : 0.25f);
        const float h = juce::jmax (3.0f, halfH * level * (0.35f + 0.65f * duckGain));
        const auto colour = t.accent.darker (0.9f).interpolatedWith (t.accent2, e.brightness);
        juce::Rectangle<float> bar;
        if (p.mode == drift::PingPong) bar = e.side == 0 ? juce::Rectangle<float> (x - 5.0f, cy - h, 10.0f, h) : juce::Rectangle<float> (x - 5.0f, cy, 10.0f, h);
        else                           bar = juce::Rectangle<float> (10.0f, 2.0f * h).withCentre ({ x, cy });

        const bool lit = std::abs (sinceMs - e.ms) < 70.0;
        if (lit)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillRoundedRectangle (bar.expanded (8.0f), 9.0f);
        }
        g.setColour (colour.withAlpha (lit ? 1.0f : 0.85f));
        g.fillRoundedRectangle (bar, 5.0f);
    }

    if (p.mode == drift::PingPong)
    {
        g.setColour (t.dim.withAlpha (0.8f));
        g.setFont (font (10.0f, true));
        g.drawText ("L", juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ plot.getX() - 8.0f, plot.getY() + 10.0f }), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ plot.getX() - 8.0f, plot.getBottom() - 10.0f }), juce::Justification::centred);
    }

    if (state.duckDb > 0.5f)
    {
        g.setColour (t.reduction);
        g.setFont (styledFont (11.0f, true, false, 0.1f));
        g.drawText ("DUCKING -" + juce::String (state.duckDb, 1) + " dB", top.withTrimmedRight (110.0f), juce::Justification::centredRight);
    }
}

//==============================================================================
DriftAudioProcessorEditor::DriftAudioProcessorEditor (DriftAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    for (juce::Component* c : { (juce::Component*) &trail, (juce::Component*) &mode, (juce::Component*) &sync,
                                (juce::Component*) &time, (juce::Component*) &division, (juce::Component*) &feedback,
                                (juce::Component*) &lowCut, (juce::Component*) &highCut, (juce::Component*) &wobble,
                                (juce::Component*) &duck, (juce::Component*) &mix })
        addAndMakeVisible (c);
    setLookAndFeel (&lnf); // after adding the controls, so they all pick up the theme

    sync.button.setTooltip ("Lock the echoes to your project's tempo");
    duck.slider.setTooltip ("Turns the echoes down while you play, and lets them back up in the gaps");
    wobble.slider.setTooltip ("Tape-style wow and flutter on the echoes");
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

DriftAudioProcessorEditor::~DriftAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DriftAudioProcessorEditor::refreshFromProcessor()
{
    const auto params = processorRef.currentParams();
    time.setVisible (! params.sync);
    division.setVisible (params.sync);
    wobblePhase += 0.12f + 0.25f * params.wobble;
    if (wobblePhase > 1000.0f) wobblePhase -= 1000.0f;
    EchoTrail::State s;
    s.params = params;
    s.bpm = processorRef.displayBpm.load();
    s.secondsSinceHit = processorRef.secondsSinceHit.load();
    s.duckDb = processorRef.duckNowDb.load();
    s.wobblePhase = wobblePhase;
    trail.setState (s);
}

void DriftAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e1020), 0.0f, 0.0f, juce::Colour (0xff160810), 0.0f, b.getBottom(), false));
    g.fillAll();
    // A low sun glowing behind the top right
    g.setGradientFill (juce::ColourGradient (t.accent.withAlpha (0.16f), b.getRight() - 120.0f, 10.0f,
                                             t.accent.withAlpha (0.0f), b.getRight() - 120.0f, 330.0f, true));
    g.fillRect (b);
    paintHeader (g, getWidth(), "Drift", LBAUDIO_PLUGIN_TYPE, t);
    paintPanel (g, controlPanel, t);
    g.setColour (t.dim);
    g.setFont (styledFont (11.0f, true, false, 0.12f));
    g.drawText ("MODE", modeLabel, juce::Justification::centredLeft);
}

void DriftAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 14);
    trail.setBounds (area.removeFromTop (190));
    area.removeFromTop (12);
    controlPanel = area;

    auto c = controlPanel.reduced (14, 12);
    auto left = c.removeFromLeft (168);
    modeLabel = left.removeFromTop (20).withTrimmedLeft (2);
    left.removeFromTop (4);
    mode.setBounds (left.removeFromTop (34));
    left.removeFromTop (14);
    sync.setBounds (left.removeFromTop (30).withWidth (96));
    c.removeFromLeft (10);

    const int w = c.getWidth() / 7;
    auto slot = c.removeFromLeft (w).reduced (3, 0);
    time.setBounds (slot);
    division.setBounds (slot);
    for (auto* k : { &feedback, &lowCut, &highCut, &wobble, &duck, &mix })
        k->setBounds (c.removeFromLeft (w).reduced (3, 0));
}
