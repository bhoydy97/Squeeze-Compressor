#include "PluginEditor.h"

using namespace lbaudio::ui;

lbaudio::ui::Theme snapTheme()
{
    Theme t;
    t.background = juce::Colour (0xfff5c518);
    t.panel      = juce::Colour (0xff121212);
    t.panelEdge  = juce::Colour (0xff1d1d1d);
    t.track      = juce::Colour (0xffc99b06);
    t.text       = juce::Colour (0xff111111);
    t.dim        = juce::Colour (0xff4a3b00);
    t.accent     = juce::Colour (0xff111111);
    t.accent2    = juce::Colour (0xffe63a2e);
    t.onAccent   = juce::Colour (0xfff5c518);
    t.knobTop    = juce::Colour (0xff262626);
    t.knobBottom = juce::Colour (0xff000000);
    t.knobEdge   = juce::Colour (0xff3a3a3a);
    t.pointer    = juce::Colour (0xfff5c518);
    t.meterBack  = juce::Colour (0xff141414);
    t.meterLow   = juce::Colour (0xfff5c518);
    t.meterMid   = juce::Colour (0xffff9d00);
    t.meterHigh  = juce::Colour (0xffe63a2e);
    t.reduction  = juce::Colour (0xffe63a2e);
    t.headerText = juce::Colour (0xff111111);
    t.headerType = juce::Colour (0xffbf2a1f);
    t.headerBrand = juce::Colour (0xff4a3b00);
    t.headerLine = juce::Colour (0xff111111);
    t.knob = KnobStyle::Chunky;
    t.panelStyle = PanelStyle::Screen;
    t.corner = 4.0f;
    t.titleSize = 36.0f;
    t.titleKerning = 0.02f;
    t.typeKerning = 0.12f;
    return t;
}

namespace
{
const juce::Colour screenText { 0xfff5c518 }, screenDim { 0xff7a6a2a }, inputGrey { 0xff3d3d3d }, neutral { 0xffd8d2bd };
}

//==============================================================================
void HitScope::paint (juce::Graphics& g)
{
    const auto& t = themeOf (*this);
    paintPanel (g, getLocalBounds(), t);
    auto area = getLocalBounds().toFloat().reduced (16.0f, 12.0f);

    auto top = area.removeFromTop (16.0f);
    g.setFont (styledFont (10.5f, true, false, 0.2f));
    g.setColour (screenDim);
    g.drawText ("HITS", top, juce::Justification::centredLeft);
    g.setColour (screenText);
    g.drawText ("BOOST", top.withTrimmedRight (90.0f), juce::Justification::centredRight);
    g.setColour (t.accent2);
    g.drawText ("CUT", top, juce::Justification::centredRight);

    auto gainLane = area.removeFromBottom (30.0f);
    area.removeFromBottom (6.0f);
    const float cy = area.getCentreY(), half = area.getHeight() * 0.5f;
    const float step = area.getWidth() / (float) frames;
    auto amp = [&] (float v) { return half * juce::jlimit (0.0f, 1.0f, juce::jmap (snap::toDb (v), -48.0f, 0.0f, 0.0f, 1.0f)); };

    g.setColour (screenDim.withAlpha (0.35f));
    g.drawHorizontalLine ((int) cy, area.getX(), area.getRight());

    for (int i = 0; i < frames; ++i)
    {
        const auto& f = data[(size_t) i];
        const float x = area.getX() + step * (float) i;
        const float hi = amp (f.in), ho = amp (f.out);
        g.setColour (inputGrey);
        g.fillRect (x, cy - hi, step + 0.4f, 2.0f * hi);
        const float k = juce::jlimit (0.0f, 1.0f, std::abs (f.gainDb) / 8.0f);
        const auto c = f.gainDb >= 0.0f ? neutral.interpolatedWith (screenText, k) : neutral.interpolatedWith (t.accent2, k);
        g.setColour (c.withAlpha (0.92f));
        g.fillRect (x, cy - ho, step + 0.4f, 2.0f * ho);
    }

    // Gain lane: +-15 dB around a centre line
    const float gy = gainLane.getCentreY(), gh = gainLane.getHeight() * 0.5f;
    g.setColour (screenDim.withAlpha (0.5f));
    g.drawHorizontalLine ((int) gy, gainLane.getX(), gainLane.getRight());
    for (int i = 0; i < frames; ++i)
    {
        const float gdb = data[(size_t) i].gainDb;
        if (std::abs (gdb) < 0.05f) continue;
        const float x = gainLane.getX() + step * (float) i;
        const float h = gh * juce::jlimit (0.0f, 1.0f, std::abs (gdb) / 15.0f);
        g.setColour (gdb > 0.0f ? screenText : t.accent2);
        g.fillRect (x, gdb > 0.0f ? gy - h : gy, step + 0.4f, h);
    }
    const auto tag = gainLane.withWidth (40.0f).withSizeKeepingCentre (40.0f, 14.0f);
    g.setColour (t.panel);
    g.fillRect (tag);
    g.setColour (screenDim);
    g.setFont (styledFont (9.5f, true, false, 0.2f));
    g.drawText ("GAIN", tag, juce::Justification::centredLeft);
}

//==============================================================================
SnapAudioProcessorEditor::SnapAudioProcessorEditor (SnapAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    for (juce::Component* c : { (juce::Component*) &scope, (juce::Component*) &attack, (juce::Component*) &sustain,
                                (juce::Component*) &speed, (juce::Component*) &mix, (juce::Component*) &output,
                                (juce::Component*) &inMeter, (juce::Component*) &outMeter })
        addAndMakeVisible (c);
    setLookAndFeel (&lnf);

    attack.slider.setTooltip ("Turn up for harder, snappier hits; down to soften them");
    sustain.slider.setTooltip ("Turn up to bring out the ring and the room; down to tighten the tail");
    speed.slider.setTooltip ("How much of the front of each hit counts as the attack");
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

SnapAudioProcessorEditor::~SnapAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SnapAudioProcessorEditor::refreshFromProcessor()
{
    processorRef.feed.copyLatest (buffer.data(), HitScope::frames);
    scope.setFrames (buffer.data());
    inMeter.push (processorRef.meterInputPeak.exchange (0.0f));
    outMeter.push (processorRef.meterOutputPeak.exchange (0.0f));
}

void SnapAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    const auto b = getLocalBounds().toFloat();
    g.fillAll (t.background);
    // A little shading so the plate reads as metal
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), 0.0f, 0.0f, juce::Colours::black.withAlpha (0.08f), 0.0f, b.getBottom(), false));
    g.fillRect (b);
    paintHeader (g, getWidth(), "Snap", LBAUDIO_PLUGIN_TYPE, t);
    g.setColour (t.headerLine);
    g.fillRect (0.0f, 55.0f, b.getWidth(), 3.0f);

    // Hazard stripes along the bottom
    {
        const auto band = b.withTop (b.getBottom() - 16.0f);
        g.setColour (juce::Colour (0xff111111));
        g.fillRect (band);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (band.toNearestInt());
        g.setColour (t.background);
        for (float x = -20.0f; x < b.getRight() + 20.0f; x += 28.0f)
        {
            juce::Path stripe;
            stripe.addQuadrilateral (x, band.getBottom(), x + 14.0f, band.getBottom(), x + 30.0f, band.getY(), x + 16.0f, band.getY());
            g.fillPath (stripe);
        }
    }
    // Screws in the corners of the plate
    for (auto pt : { juce::Point<float> (12.0f, 70.0f), juce::Point<float> (b.getRight() - 12.0f, 70.0f),
                     juce::Point<float> (12.0f, b.getBottom() - 28.0f), juce::Point<float> (b.getRight() - 12.0f, b.getBottom() - 28.0f) })
    {
        const auto head = juce::Rectangle<float> (11.0f, 11.0f).withCentre (pt);
        g.setColour (juce::Colour (0xffb08a00));
        g.fillEllipse (head);
        g.setColour (juce::Colour (0xff5a4600));
        g.drawEllipse (head, 1.0f);
        g.drawLine (pt.x - 3.5f, pt.y + 2.0f, pt.x + 3.5f, pt.y - 2.0f, 1.4f);
    }
}

void SnapAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (58);
    area.removeFromBottom (16);
    area.reduce (26, 12);
    scope.setBounds (area.removeFromTop (178));
    area.removeFromTop (10);

    auto meters = area.removeFromRight (96);
    inMeter.setBounds (meters.removeFromLeft (48));
    outMeter.setBounds (meters);
    area.removeFromRight (8);
    attack.setBounds (area.removeFromLeft (196));
    sustain.setBounds (area.removeFromLeft (196));
    area.removeFromLeft (10);
    const int w = area.getWidth() / 3;
    for (auto* k : { &speed, &mix, &output })
        k->setBounds (area.removeFromLeft (w).withSizeKeepingCentre (w, 150).reduced (4, 0));
}
