#include "PluginEditor.h"

using namespace lbaudio::ui;

void HistoryGraph::pull (CeilingAudioProcessor& p)
{
    const uint32_t written = p.historyWritten.load (std::memory_order_acquire);
    if (written - readUpTo > (uint32_t) CeilingAudioProcessor::historySize)
        readUpTo = written - (uint32_t) CeilingAudioProcessor::historySize; // fell behind: skip ahead
    for (; readUpTo != written; ++readUpTo)
    {
        const auto& f = p.history[readUpTo % CeilingAudioProcessor::historySize];
        driven[(size_t) head] = f.driven.load (std::memory_order_relaxed);
        output[(size_t) head] = f.output.load (std::memory_order_relaxed);
        head = (head + 1) % points;
    }
    repaint();
}

void HistoryGraph::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    const auto plot = area.reduced (16.0f).withTrimmedLeft (28.0f);
    constexpr float topDb = 12.0f, bottomDb = -36.0f;
    auto toY = [&] (float db) { return juce::jmap (juce::jlimit (bottomDb, topDb, db), topDb, bottomDb, plot.getY(), plot.getBottom()); };
    auto levelDb = [] (float v) { return v > 1.0e-5f ? 20.0f * std::log10 (v) : -100.0f; };

    g.setColour (Colours::background.withAlpha (0.6f));
    g.fillRect (plot);
    g.setFont (font (10.0f));
    for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f })
    {
        const float y = toY (db);
        g.setColour (std::abs (db) < 0.01f ? Colours::panelEdge.brighter (0.3f) : Colours::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (Colours::dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 30.0f, y - 7.0f, 25.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    // Oldest on the left, newest on the right
    auto envelope = [&] (const std::vector<float>& values)
    {
        juce::Path p;
        p.startNewSubPath (plot.getX(), plot.getBottom());
        for (int i = 0; i < points; ++i)
        {
            const float x = juce::jmap ((float) i, 0.0f, (float) (points - 1), plot.getX(), plot.getRight());
            p.lineTo (x, toY (levelDb (values[(size_t) ((head + i) % points)])));
        }
        p.lineTo (plot.getRight(), plot.getBottom());
        p.closeSubPath();
        return p;
    };

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (Colours::gr.withAlpha (0.55f));
        g.fillPath (envelope (driven));
        const auto out = envelope (output);
        g.setColour (Colours::panel.interpolatedWith (Colours::background, 0.6f)); // hide the amber under the output
        g.fillPath (out);
        g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.55f), 0.0f, toY (0.0f),
                                                 Colours::accent.withAlpha (0.12f), 0.0f, plot.getBottom(), false));
        g.fillPath (out);
        g.setColour (Colours::accent);
        g.strokePath (out, juce::PathStrokeType (1.2f));
    }

    // The ceiling
    const float cy = toY (ceilingDb);
    juce::Path line;
    line.startNewSubPath (plot.getX(), cy);
    line.lineTo (plot.getRight(), cy);
    juce::Path dashed;
    const float dashes[] = { 6.0f, 4.0f };
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
    g.setColour (Colours::text.withAlpha (0.85f));
    g.fillPath (dashed);
    g.setFont (font (11.0f, true));
    const auto label = "CEILING " + juce::String (ceilingDb, 1) + " dB";
    g.drawText (label, juce::Rectangle<float> (plot.getRight() - 140.0f, cy - 18.0f, 134.0f, 16.0f), juce::Justification::centredRight);

    g.setColour (Colours::dim);
    g.setFont (font (10.0f, true));
    g.drawText ("GAIN REDUCTION", juce::Rectangle<float> (plot.getX() + 6.0f, plot.getY() + 4.0f, 140.0f, 14.0f), juce::Justification::centredLeft);
    g.setColour (Colours::gr);
    g.fillRoundedRectangle (plot.getX() + 6.0f + 92.0f, plot.getY() + 9.0f, 12.0f, 4.0f, 1.0f);
}

void Readout::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::background.withAlpha (0.7f));
    g.fillRoundedRectangle (r, 6.0f);
    r.reduce (10.0f, 6.0f);
    g.setColour (Colours::dim);
    g.setFont (font (10.5f, true));
    g.drawText (title, r.removeFromTop (14.0f), juce::Justification::centredLeft);
    g.setColour (Colours::text);
    g.setFont (font (20.0f, true));
    const auto text = value <= -99.0f ? juce::String ("-inf") : juce::String (value, 1);
    g.drawText (text, r, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (font (11.0f));
    g.drawText (unit, r, juce::Justification::bottomRight);
}

//==============================================================================
CeilingAudioProcessorEditor::CeilingAudioProcessorEditor (CeilingAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    for (juce::Component* c : { (juce::Component*) &graph, (juce::Component*) &gainKnob, (juce::Component*) &ceilingKnob,
                                (juce::Component*) &releaseKnob, (juce::Component*) &truePeak, (juce::Component*) &momentary,
                                (juce::Component*) &shortTerm, (juce::Component*) &integrated, (juce::Component*) &peak,
                                (juce::Component*) &resetButton, (juce::Component*) &grMeter, (juce::Component*) &outMeter })
        addAndMakeVisible (c);

    gainKnob.slider.setTooltip ("Pushes the level into the limiter. More gain = louder, with more limiting");
    ceilingKnob.slider.setTooltip ("The output never goes above this. -1 dB is a safe choice for streaming");
    releaseKnob.slider.setTooltip ("How quickly the level recovers after a peak. Longer is smoother, shorter is louder");
    truePeak.button.setTooltip ("Also catches peaks between samples, which can clip in converters and MP3/AAC encoders");
    resetButton.setTooltip ("Restart the integrated loudness and true-peak measurements");
    resetButton.onClick = [this] { processorRef.resetMeasurementsRequested.store (true); };

    setLookAndFeel (&lnf);
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

CeilingAudioProcessorEditor::~CeilingAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void CeilingAudioProcessorEditor::refreshFromProcessor()
{
    outMeter.push (processorRef.meterOutputPeak.exchange (0.0f));
    grMeter.push (processorRef.meterGainReductionDb.exchange (0.0f));
    graph.setCeiling (processorRef.currentParams().ceilingDb);
    graph.pull (processorRef);
    momentary.setValue (processorRef.momentaryLufs.load());
    shortTerm.setValue (processorRef.shortTermLufs.load());
    integrated.setValue (processorRef.integratedLufs.load());
    const float tp = processorRef.truePeakMax.load();
    peak.setValue (tp > 1.0e-5f ? 20.0f * std::log10 (tp) : -100.0f);
}

void CeilingAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    paintHeader (g, getWidth(), "Ceiling", LBAUDIO_PLUGIN_TYPE);
    paintPanel (g, controlPanel);
    paintPanel (g, meterPanel);
    g.setColour (Colours::dim);
    g.setFont (font (11.5f, true));
    g.drawText ("LOUDNESS", loudnessLabel, juce::Justification::centredLeft);
}

void CeilingAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);

    graph.setBounds (area.removeFromLeft (440));
    area.removeFromLeft (12);
    meterPanel = area.removeFromRight (106);
    area.removeFromRight (12);
    controlPanel = area;

    auto meters = meterPanel.reduced (10, 10);
    grMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2));
    outMeter.setBounds (meters);

    auto c = controlPanel.reduced (14, 12);
    auto knobs = c.removeFromTop (124);
    const int w = knobs.getWidth() / 3;
    for (auto* k : { &gainKnob, &ceilingKnob, &releaseKnob })
        k->setBounds (knobs.removeFromLeft (w));

    c.removeFromTop (6);
    truePeak.setBounds (c.removeFromTop (30).withSizeKeepingCentre (150, 30));

    c.removeFromTop (12);
    auto header = c.removeFromTop (24);
    resetButton.setBounds (header.removeFromRight (64).withSizeKeepingCentre (64, 22));
    loudnessLabel = header;
    c.removeFromTop (6);

    const int cellW = (c.getWidth() - 8) / 2, cellH = (c.getHeight() - 8) / 2;
    auto row1 = c.removeFromTop (cellH);
    momentary.setBounds (row1.removeFromLeft (cellW));
    shortTerm.setBounds (row1.removeFromRight (cellW));
    c.removeFromTop (8);
    auto row2 = c.removeFromTop (cellH);
    integrated.setBounds (row2.removeFromLeft (cellW));
    peak.setBounds (row2.removeFromRight (cellW));
}
