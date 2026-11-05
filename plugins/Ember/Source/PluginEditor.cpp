#include "PluginEditor.h"

using namespace lbaudio::ui;

void SaturationCurve::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    const auto plot = area.reduced (16.0f).withTrimmedBottom (14.0f);
    auto toX = [&] (float v) { return juce::jmap (v, -1.0f, 1.0f, plot.getX(), plot.getRight()); };
    auto toY = [&] (float v) { return juce::jmap (v, -1.0f, 1.0f, plot.getBottom(), plot.getY()); };

    g.setColour (Colours::background.withAlpha (0.6f));
    g.fillRect (plot);
    for (float v : { -0.5f, 0.0f, 0.5f })
    {
        g.setColour (std::abs (v) < 0.01f ? Colours::panelEdge.brighter (0.3f) : Colours::panelEdge);
        g.drawVerticalLine ((int) toX (v), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) toY (v), plot.getX(), plot.getRight());
    }

    juce::Path unity;
    unity.startNewSubPath (toX (-1.0f), toY (-1.0f));
    unity.lineTo (toX (1.0f), toY (1.0f));
    juce::Path dashed;
    const float dashes[] = { 4.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dashes, 2);
    g.setColour (Colours::dim.withAlpha (0.35f));
    g.fillPath (dashed);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());

    // Shade the range the live signal is covering
    if (level > 0.001f)
    {
        const float l = juce::jmin (1.0f, level);
        g.setColour (Colours::gr.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (toX (-l), plot.getY(), toX (l), plot.getBottom()));
    }

    juce::Path curve;
    const int steps = (int) plot.getWidth();
    for (int i = 0; i <= steps; ++i)
    {
        const float x = juce::jmap ((float) i, 0.0f, (float) steps, -1.0f, 1.0f);
        const float y = ember::displayCurve (params, x);
        if (i == 0) curve.startNewSubPath (toX (x), toY (y)); else curve.lineTo (toX (x), toY (y));
    }
    g.setColour (Colours::accent);
    g.strokePath (curve, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (level > 0.001f)
    {
        const float l = juce::jmin (1.0f, level);
        const juce::Point<float> p { toX (l), toY (ember::displayCurve (params, l)) };
        g.setColour (Colours::gr.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (p));
        g.setColour (Colours::gr);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
    }

    g.setColour (Colours::dim);
    g.setFont (font (10.0f));
    g.drawText ("INPUT", plot.withTop (plot.getBottom() - 14.0f).withTrimmedRight (4.0f), juce::Justification::centredRight);
    g.drawText ("OUTPUT", plot.withHeight (14.0f).withTrimmedLeft (4.0f), juce::Justification::centredLeft);
}

//==============================================================================
EmberAudioProcessorEditor::EmberAudioProcessorEditor (EmberAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    for (juce::Component* c : { (juce::Component*) &curve, (juce::Component*) &character, (juce::Component*) &autoGain,
                                (juce::Component*) &drive, (juce::Component*) &tone, (juce::Component*) &mix,
                                (juce::Component*) &output, (juce::Component*) &inMeter, (juce::Component*) &outMeter })
        addAndMakeVisible (c);

    autoGain.button.setTooltip ("Keeps the level about the same as you change Drive");
    setLookAndFeel (&lnf);
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

EmberAudioProcessorEditor::~EmberAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EmberAudioProcessorEditor::refreshFromProcessor()
{
    inMeter.push (processorRef.meterInputPeak.exchange (0.0f));
    outMeter.push (processorRef.meterOutputPeak.exchange (0.0f));
    const float inDb = inMeter.getDisplayed();
    curve.setState (processorRef.currentParams(), inDb > -60.0f ? std::pow (10.0f, inDb / 20.0f) : 0.0f);
}

void EmberAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    paintHeader (g, getWidth(), "Ember", LBAUDIO_PLUGIN_TYPE);
    paintPanel (g, controlPanel);
    paintPanel (g, meterPanel);
    g.setColour (Colours::dim);
    g.setFont (font (11.5f, true));
    g.drawText ("CHARACTER", characterLabel, juce::Justification::centredLeft);
}

void EmberAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);

    curve.setBounds (area.removeFromLeft (area.getHeight()));
    area.removeFromLeft (12);
    meterPanel = area.removeFromRight (106);
    area.removeFromRight (12);
    controlPanel = area;

    auto meters = meterPanel.reduced (10, 10);
    inMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2));
    outMeter.setBounds (meters);

    auto c = controlPanel.reduced (16, 14);
    auto top = c.removeFromTop (64);
    characterLabel = top.removeFromTop (20);
    top.removeFromTop (6);
    autoGain.setBounds (top.removeFromRight (128).withSizeKeepingCentre (128, 30));
    top.removeFromRight (16);
    character.setBounds (top.withSizeKeepingCentre (top.getWidth(), 34));

    c.removeFromTop (16);
    const int w = c.getWidth() / 4;
    for (auto* k : { &drive, &tone, &mix, &output })
        k->setBounds (c.removeFromLeft (w).reduced (4, 0));
}
