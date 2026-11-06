#include "PluginEditor.h"

using namespace lbaudio::ui;

lbaudio::ui::Theme spreadTheme()
{
    Theme t;
    t.background = juce::Colour (0xff07060d);
    t.panel      = juce::Colour (0xd00b0a16);
    t.panelEdge  = juce::Colour (0xffc02aa6);
    t.track      = juce::Colour (0xff1f1b30);
    t.text       = juce::Colour (0xfff6f2ff);
    t.dim        = juce::Colour (0xff958fbb);
    t.accent     = juce::Colour (0xff2ee6ff);
    t.accent2    = juce::Colour (0xffff3fd2);
    t.onAccent   = juce::Colour (0xff04121a);
    t.knobTop    = juce::Colour (0xff201d33);
    t.knobBottom = juce::Colour (0xff09080f);
    t.knobEdge   = juce::Colour (0xff3d3760);
    t.pointer    = juce::Colour (0xffffffff);
    t.meterBack  = juce::Colour (0xff0b0a16);
    t.meterLow   = juce::Colour (0xff2ee6ff);
    t.meterMid   = juce::Colour (0xffa77bff);
    t.meterHigh  = juce::Colour (0xffff3fd2);
    t.headerText = juce::Colour (0xffffffff);
    t.headerType = juce::Colour (0xff2ee6ff);
    t.headerBrand = juce::Colour (0xff958fbb);
    t.headerLine = juce::Colour (0xff3a1d44);
    t.knob = KnobStyle::Neon;
    t.panelStyle = PanelStyle::Outline;
    t.corner = 16.0f;
    t.titleSize = 25.0f;
    t.titleKerning = 0.55f;
    t.typeKerning = 0.12f;
    return t;
}

//==============================================================================
void Vectorscope::setState (const spread::Params& p, const float* left, const float* right, int n, float correlation)
{
    params = p;
    std::copy (left, left + n, l.begin());
    std::copy (right, right + n, r.begin());
    corr = correlation;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
    const float target = 0.92f / juce::jmax (0.02f, peak);   // keep the cloud a readable size
    scale += (target - scale) * (target < scale ? 0.5f : 0.08f);
    repaint();
}

void Vectorscope::paint (juce::Graphics& g)
{
    const auto& t = themeOf (*this);
    paintPanel (g, getLocalBounds(), t);
    auto area = getLocalBounds().toFloat().reduced (18.0f, 14.0f);

    auto top = area.removeFromTop (18.0f);
    g.setFont (styledFont (11.0f, true, false, 0.3f));
    g.setColour (t.text);
    g.drawText ("WIDTH " + juce::String (juce::roundToInt (params.width * 100.0f)) + "%", top, juce::Justification::centredLeft);
    // Mono check overrides everything else, so it takes the bass-mono label's place
    if (params.mono)
    {
        g.setColour (t.accent2);
        g.drawText ("MONO CHECK", top, juce::Justification::centredRight);
    }
    else if (spread::bassMonoOn (params))
    {
        g.setColour (t.accent);
        g.drawText ("BASS MONO < " + juce::String (juce::roundToInt (params.bassMonoHz)) + " HZ", top, juce::Justification::centredRight);
    }

    auto corrArea = area.removeFromBottom (40.0f);
    const float side = juce::jmin (area.getWidth(), area.getHeight() - 6.0f);
    const auto plot = juce::Rectangle<float> (side, side).withCentre (area.getCentre());
    const auto c = plot.getCentre();
    const float half = side * 0.5f;

    // Guides: the mid axis, the side axis and the two speakers
    g.setColour (t.track);
    g.drawLine (c.x, plot.getY(), c.x, plot.getBottom(), 1.0f);
    g.drawLine (plot.getX(), c.y, plot.getRight(), c.y, 1.0f);
    juce::Path diag;
    diag.startNewSubPath (c.x - half * 0.7071f, c.y - half * 0.7071f); diag.lineTo (c.x + half * 0.7071f, c.y + half * 0.7071f);
    diag.startNewSubPath (c.x + half * 0.7071f, c.y - half * 0.7071f); diag.lineTo (c.x - half * 0.7071f, c.y + half * 0.7071f);
    juce::Path dashed; const float dashes[] = { 3.0f, 5.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, diag, dashes, 2);
    g.fillPath (dashed);
    g.drawEllipse (plot.reduced (half * 0.02f), 1.0f);
    g.setColour (t.dim);
    g.setFont (font (10.5f, true));
    g.drawText ("L", juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ c.x - half * 0.74f, c.y - half * 0.74f }), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ c.x + half * 0.74f, c.y - half * 0.74f }), juce::Justification::centred);
    g.drawText ("M", juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ c.x + 10.0f, plot.getY() + 8.0f }), juce::Justification::centred);

    // The cloud
    const float k = half * scale * 0.7071f;
    for (int pass = 0; pass < 2; ++pass)
    {
        const float dot = pass == 0 ? 5.0f : 1.8f;
        for (size_t i = 0; i < (size_t) points; ++i)
        {
            const float sx = (l[i] - r[i]) * k, my = (l[i] + r[i]) * k;
            if (std::abs (sx) < 1.0e-4f && std::abs (my) < 1.0e-4f) continue;
            const float x = c.x - sx, y = c.y - my;    // left speaker on the left
            const float mixT = juce::jlimit (0.0f, 1.0f, 0.5f + (x - c.x) / (half * 1.2f));
            g.setColour (t.accent.interpolatedWith (t.accent2, mixT).withAlpha (pass == 0 ? 0.05f : 0.55f));
            g.fillRect (x - dot * 0.5f, y - dot * 0.5f, dot, dot);
        }
    }

    // Correlation
    auto bar = corrArea.withTrimmedTop (16.0f).reduced (28.0f, 4.0f);
    g.setColour (t.dim);
    g.setFont (styledFont (10.0f, true, false, 0.3f));
    g.drawText ("CORRELATION", corrArea.withHeight (14.0f), juce::Justification::centred);
    g.drawText ("-1", juce::Rectangle<float> (24.0f, bar.getHeight()).withRightX (bar.getX() - 2.0f).withY (bar.getY()), juce::Justification::centredRight);
    g.drawText ("+1", juce::Rectangle<float> (24.0f, bar.getHeight()).withX (bar.getRight() + 4.0f).withY (bar.getY()), juce::Justification::centredLeft);
    g.setColour (t.track);
    g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
    const float mid = bar.getCentreX();
    const float pos = juce::jmap (corr, -1.0f, 1.0f, bar.getX(), bar.getRight());
    juce::Rectangle<float> fill = pos >= mid ? bar.withLeft (mid).withRight (pos) : bar.withLeft (pos).withRight (mid);
    g.setColour ((corr >= 0.0f ? t.accent : t.accent2).withAlpha (0.85f));
    g.fillRoundedRectangle (fill, bar.getHeight() * 0.5f);
    g.setColour (t.text);
    g.fillRect (mid - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ pos, bar.getCentreY() }));
}

//==============================================================================
SpreadAudioProcessorEditor::SpreadAudioProcessorEditor (SpreadAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    for (juce::Component* c : { (juce::Component*) &scopeView, (juce::Component*) &width, (juce::Component*) &bassMono,
                                (juce::Component*) &balance, (juce::Component*) &output, (juce::Component*) &mono })
        addAndMakeVisible (c);
    setLookAndFeel (&lnf);

    width.slider.getProperties().set ("origin", 100.0);
    bassMono.slider.setTooltip ("Everything below this frequency is made mono, so the low end stays tight and centred");
    mono.button.setTooltip ("Fold to mono to check how your mix holds up on one speaker");
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

SpreadAudioProcessorEditor::~SpreadAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpreadAudioProcessorEditor::refreshFromProcessor()
{
    processorRef.scope.copyLatest (bufL.data(), bufR.data(), Vectorscope::points);
    scopeView.setState (processorRef.currentParams(), bufL.data(), bufR.data(), Vectorscope::points, processorRef.correlation.load());
}

void SpreadAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    const auto b = getLocalBounds().toFloat();
    g.fillAll (t.background);
    // Neon glows in two corners
    g.setGradientFill (juce::ColourGradient (t.accent2.withAlpha (0.20f), 0.0f, b.getBottom(), t.accent2.withAlpha (0.0f), 380.0f, b.getBottom() - 120.0f, true));
    g.fillRect (b);
    g.setGradientFill (juce::ColourGradient (t.accent.withAlpha (0.14f), b.getRight(), 56.0f, t.accent.withAlpha (0.0f), b.getRight() - 360.0f, 160.0f, true));
    g.fillRect (b);
    // A synthwave floor fading into the dark
    {
        const float horizon = b.getHeight() * 0.62f, vx = b.getCentreX() + 120.0f;
        for (int i = -12; i <= 12; ++i)
        {
            g.setColour (t.accent2.withAlpha (0.06f));
            g.drawLine (vx, horizon, vx + (float) i * 120.0f, b.getBottom(), 1.0f);
        }
        for (int i = 1; i <= 7; ++i)
        {
            const float y = horizon + (b.getBottom() - horizon) * std::pow ((float) i / 7.0f, 1.8f);
            g.setColour (t.accent2.withAlpha (0.05f));
            g.drawHorizontalLine ((int) y, 0.0f, b.getRight());
        }
    }
    paintHeader (g, getWidth(), "Spread", LBAUDIO_PLUGIN_TYPE, t);
    paintPanel (g, controlPanel, t);
}

void SpreadAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);
    scopeView.setBounds (area.removeFromLeft (430));
    area.removeFromLeft (16);
    controlPanel = area;

    auto c = controlPanel.reduced (16, 14);
    auto top = c.removeFromTop (34);
    mono.setBounds (top.removeFromRight (100).withSizeKeepingCentre (100, 30));
    width.setBounds (c.removeFromTop (196).withSizeKeepingCentre (220, 196).translated (0, -20));
    c.removeFromTop (4);
    const int w = c.getWidth() / 3;
    for (auto* k : { &bassMono, &balance, &output })
        k->setBounds (c.removeFromLeft (w).reduced (6, 0));
}
