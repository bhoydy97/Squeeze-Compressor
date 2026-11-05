#include "PluginEditor.h"
#include <LBAudio/Params.h>

using namespace lbaudio::ui;

namespace
{
// A tidy time span (seconds) that shows the whole tail
double spanFor (double seconds)
{
    for (double s : { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0, 15.0, 20.0, 25.0 })
        if (seconds <= s) return s;
    return 25.0;
}

double tickFor (double span)
{
    for (double t : { 0.1, 0.25, 0.5, 1.0, 2.0, 2.5, 5.0 })
        if (span / t <= 6.0) return t;
    return 5.0;
}

juce::String timeLabel (double t)
{
    if (t < 1.0) return juce::String (juce::roundToInt (t * 1000.0)) + " ms";
    return (std::abs (t - std::round (t)) < 1.0e-6 ? juce::String ((int) std::round (t)) : juce::String (t, 1)) + " s";
}
}

void DecayGraph::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    auto inner = area.reduced (16.0f);
    auto legend = inner.removeFromTop (36.0f);
    inner.removeFromTop (6.0f);
    const auto plot = inner.withTrimmedBottom (18.0f).withTrimmedLeft (30.0f);

    const double pre = params.preDelayMs * 0.001;
    const double span = spanFor ((pre + params.decayS) * 1.12);
    constexpr double floorDb = -66.0;
    auto toX = [&] (double t)  { return juce::jmap ((float) t, 0.0f, (float) span, plot.getX(), plot.getRight()); };
    auto toY = [&] (double db) { return juce::jmap ((float) db, 0.0f, (float) floorDb, plot.getY(), plot.getBottom()); };

    g.setColour (Colours::background.withAlpha (0.6f));
    g.fillRect (plot);

    // Grid: level every 12 dB, time at tidy steps
    g.setFont (font (10.0f));
    for (int db = 0; db >= -60; db -= 12)
    {
        const float y = toY (db);
        g.setColour (db == 0 || db == -60 ? Colours::panelEdge.brighter (0.3f) : Colours::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (Colours::dim);
        g.drawText (juce::String (db), juce::Rectangle<float> (plot.getX() - 32.0f, y - 7.0f, 27.0f, 14.0f), juce::Justification::centredRight);
    }
    const double tick = tickFor (span);
    for (double t = 0.0; t <= span + 1.0e-9; t += tick)
    {
        const float x = toX (t);
        g.setColour (Colours::panelEdge);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (Colours::dim);
        auto label = juce::Rectangle<float> (x - 30.0f, plot.getBottom() + 3.0f, 60.0f, 14.0f);
        if (t <= 1.0e-9) label = label.withX (x - 2.0f).withWidth (60.0f);
        g.drawText (timeLabel (t), label, t <= 1.0e-9 ? juce::Justification::centredLeft : juce::Justification::centred);
    }

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());

        // Pre-delay: the gap before the reverb starts
        if (pre > 0.0)
        {
            g.setColour (Colours::text.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), plot.getY(), toX (pre), plot.getBottom()));
        }

        // Lows: filled; highs: a line on top
        auto tailPath = [&] (bool highs, bool closed)
        {
            const double rt = params.decayS * (highs ? haze::hfDecayRatio : 1.0);
            const double tEnd = pre + rt * (-floorDb / 60.0);
            juce::Path p;
            p.startNewSubPath (toX (pre), closed ? plot.getBottom() : toY (0.0));
            p.lineTo (toX (pre), toY (0.0));
            p.lineTo (toX (tEnd), toY (floorDb));
            if (closed) p.closeSubPath();
            return p;
        };

        const auto lows = tailPath (false, true);
        g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.32f), plot.getX(), plot.getY(),
                                                 Colours::accent.withAlpha (0.02f), plot.getX(), plot.getBottom(), false));
        g.fillPath (lows);
        g.setColour (Colours::accent);
        g.strokePath (tailPath (false, false), juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Colours::gr);
        g.strokePath (tailPath (true, false), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Legend: what each line means, with the numbers
    auto row = [&] (juce::Rectangle<float> r, juce::Colour c, const juce::String& name, const juce::String& value)
    {
        g.setColour (c);
        g.fillRoundedRectangle (r.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 3.0f), 1.5f);
        r.removeFromLeft (8.0f);
        g.setColour (Colours::dim);
        g.setFont (font (11.0f, true));
        g.drawText (name, r, juce::Justification::centredLeft);
        g.setColour (Colours::text);
        g.setFont (font (12.0f));
        g.drawText (value, r, juce::Justification::centredRight);
    };
    const auto top = legend.removeFromTop (18.0f);
    row (top, Colours::accent, "LOWS", lbaudio::params::formatSeconds (params.decayS, 0));
    row (legend, Colours::gr, "HIGHS > " + lbaudio::params::formatHz (params.dampingHz, 0),
         lbaudio::params::formatSeconds ((float) (params.decayS * haze::hfDecayRatio), 0));
}

//==============================================================================
HazeAudioProcessorEditor::HazeAudioProcessorEditor (HazeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    addAndMakeVisible (graph);
    for (auto* k : { &preDelay, &size, &decay, &modulation, &damping, &lowCut, &width, &mix })
        addAndMakeVisible (k);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    width.slider.getProperties().set ("origin", 100.0); // arc grows either side of natural width

    preDelay.slider.setTooltip ("Gap before the reverb starts. Keeps vocals and drums clear of their own reverb");
    size.slider.setTooltip ("How big the space is: the spacing of the echoes that build the tail");
    decay.slider.setTooltip ("Time for the tail to fall by 60 dB");
    modulation.slider.setTooltip ("Slow movement in the tail. Smooths out metallic ringing");
    damping.slider.setTooltip ("Above this frequency the tail dies away four times faster, like soft furnishings");
    lowCut.slider.setTooltip ("Keeps low end out of the reverb so it doesn't muddy the mix");
    width.slider.setTooltip ("Stereo spread of the reverb. 0% is mono, 200% is extra wide");
    mix.slider.setTooltip ("Balance between the dry signal and the reverb");

    setLookAndFeel (&lnf);
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

HazeAudioProcessorEditor::~HazeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void HazeAudioProcessorEditor::refreshFromProcessor()
{
    inMeter.push (processorRef.meterInputPeak.exchange (0.0f));
    outMeter.push (processorRef.meterOutputPeak.exchange (0.0f));
    graph.setState (processorRef.currentParams());
}

void HazeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    paintHeader (g, getWidth(), "Haze", LBAUDIO_PLUGIN_TYPE);
    paintPanel (g, controlPanel);
    paintPanel (g, meterPanel);
}

void HazeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);

    graph.setBounds (area.removeFromLeft (360));
    area.removeFromLeft (12);
    meterPanel = area.removeFromRight (106);
    area.removeFromRight (12);
    controlPanel = area;

    auto meters = meterPanel.reduced (10, 10);
    inMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2));
    outMeter.setBounds (meters);

    auto c = controlPanel.reduced (12, 14);
    auto top = c.removeFromTop (c.getHeight() / 2);
    const int w = c.getWidth() / 4;
    for (auto* k : { &preDelay, &size, &decay, &modulation })
        k->setBounds (top.removeFromLeft (w));
    for (auto* k : { &damping, &lowCut, &width, &mix })
        k->setBounds (c.removeFromLeft (w));
}
