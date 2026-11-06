#include "PluginEditor.h"

using namespace lbaudio::ui;

lbaudio::ui::Theme hushTheme()
{
    Theme t;
    t.background = juce::Colour (0xffeef3f0);
    t.panel      = juce::Colour (0xffffffff);
    t.panelEdge  = juce::Colour (0xffd2ddd7);
    t.track      = juce::Colour (0xffdde6e1);
    t.text       = juce::Colour (0xff1b2a25);
    t.dim        = juce::Colour (0xff62756d);
    t.accent     = juce::Colour (0xff137a63);
    t.accent2    = juce::Colour (0xffd9466a);
    t.onAccent   = juce::Colour (0xffffffff);
    t.knobTop    = juce::Colour (0xffffffff);
    t.knobBottom = juce::Colour (0xfff1f5f3);
    t.knobEdge   = juce::Colour (0xffc2cec8);
    t.pointer    = juce::Colour (0xff1b2a25);
    t.meterBack  = juce::Colour (0xffe7eeea);
    t.meterLow   = juce::Colour (0xff137a63);
    t.meterMid   = juce::Colour (0xffe2a93b);
    t.meterHigh  = juce::Colour (0xffd9466a);
    t.reduction  = juce::Colour (0xffd9466a);
    t.headerText = juce::Colour (0xff1b2a25);
    t.headerType = juce::Colour (0xff137a63);
    t.headerBrand = juce::Colour (0xff62756d);
    t.headerLine = juce::Colour (0xffd2ddd7);
    t.knob = KnobStyle::Hairline;
    t.corner = 6.0f;
    t.titleSize = 25.0f;
    t.titleBold = false;
    t.titleKerning = 0.38f;
    t.typeKerning = 0.12f;
    return t;
}

namespace
{
constexpr float loHz = 500.0f, hiHz = 20000.0f;
float hzToX (juce::Rectangle<float> r, double hz) { return r.getX() + (float) (std::log (hz / loHz) / std::log (hiHz / loHz)) * r.getWidth(); }
}

//==============================================================================
void SibilanceSpectrum::paint (juce::Graphics& g)
{
    const auto& t = themeOf (*this);
    paintPanel (g, getLocalBounds(), t);
    auto area = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
    auto top = area.removeFromTop (18.0f);
    g.setColour (t.dim);
    g.setFont (styledFont (10.5f, true, false, 0.14f));
    g.drawText ("INPUT SPECTRUM", top, juce::Justification::centredLeft);

    const auto plot = area.withTrimmedTop (6.0f).withTrimmedBottom (16.0f);
    auto dbToY = [&] (float db) { return juce::jmap (juce::jlimit (-96.0f, -12.0f, db), -96.0f, -12.0f, plot.getBottom(), plot.getY()); };

    // Frequency grid
    g.setFont (font (10.0f));
    for (double hz : { 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = hzToX (plot, hz);
        g.setColour (t.panelEdge.withAlpha (0.8f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (t.dim);
        const auto label = hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, plot.getBottom() + 9.0f }), juce::Justification::centred);
    }

    // The band Hush listens to
    {
        juce::Path band;
        band.startNewSubPath (plot.getX(), plot.getBottom());
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double hz = loHz * std::pow (hiHz / loHz, (x - plot.getX()) / plot.getWidth());
            const float r = (float) hush::detectorResponseDb (params, hz, fs);
            band.lineTo (x, juce::jmap (juce::jlimit (-24.0f, 0.0f, r), -24.0f, 0.0f, plot.getBottom(), plot.getY() + plot.getHeight() * 0.15f));
        }
        band.lineTo (plot.getRight(), plot.getBottom());
        band.closeSubPath();
        const float hot = juce::jlimit (0.0f, 1.0f, reduction / 8.0f);
        const auto colour = t.accent.interpolatedWith (t.accent2, hot);
        g.setColour (colour.withAlpha (0.10f + 0.12f * hot));
        g.fillPath (band);
        g.setColour (colour.withAlpha (0.55f));
        g.strokePath (band, juce::PathStrokeType (1.2f));
    }

    // Spectrum
    if (! spectrum.empty() && size > 0)
    {
        juce::Path s;
        s.startNewSubPath (plot.getX(), plot.getBottom());
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double hz = loHz * std::pow (hiHz / loHz, (x - plot.getX()) / plot.getWidth());
            const double bin = hz / fs * size;
            const int k = juce::jlimit (1, (int) spectrum.size() - 2, (int) bin);
            const float frac = (float) (bin - k);
            const float db = spectrum[(size_t) k] + frac * (spectrum[(size_t) k + 1] - spectrum[(size_t) k]);
            s.lineTo (x, dbToY (db));
        }
        s.lineTo (plot.getRight(), plot.getBottom());
        s.closeSubPath();
        g.setColour (t.text.withAlpha (0.08f));
        g.fillPath (s);
        g.setColour (t.text.withAlpha (0.45f));
        g.strokePath (s, juce::PathStrokeType (1.0f));
    }

    // The frequency marker
    const float fx = hzToX (plot, params.frequencyHz);
    g.setColour (t.accent);
    g.fillRect (fx - 0.75f, plot.getY(), 1.5f, plot.getHeight());
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ fx, plot.getY() + 4.0f }));
    g.setFont (styledFont (11.0f, true, false, 0.05f));
    const juce::String f = params.frequencyHz >= 10000.0f ? juce::String (params.frequencyHz / 1000.0f, 1) + " kHz" : juce::String (params.frequencyHz / 1000.0f, 2) + " kHz";
    g.drawText (f, juce::Rectangle<float> (80.0f, 16.0f).withPosition (fx + 8.0f, plot.getY() - 2.0f), juce::Justification::centredLeft);
    if (params.listen)
    {
        g.setColour (t.accent2);
        g.setFont (styledFont (10.5f, true, false, 0.14f));
        g.drawText ("LISTENING TO THE BAND", top, juce::Justification::centredRight);
    }
}

//==============================================================================
void ReductionHistory::push (float detectorDb, float reductionDb, float thresholdDb, float rangeDb)
{
    detector[(size_t) write] = detectorDb;
    reduction[(size_t) write] = reductionDb;
    write = (write + 1) % length;
    threshold = thresholdDb; range = rangeDb;
    repaint();
}

void ReductionHistory::paint (juce::Graphics& g)
{
    const auto& t = themeOf (*this);
    paintPanel (g, getLocalBounds(), t);
    auto area = getLocalBounds().toFloat().reduced (16.0f, 10.0f);
    const float laneGap = 8.0f;
    auto levelLane = area.removeFromTop ((area.getHeight() - laneGap) * 0.5f);
    area.removeFromTop (laneGap);
    auto redLane = area;

    g.setFont (styledFont (10.0f, true, false, 0.14f));
    g.setColour (t.dim);
    g.drawText ("SIBILANCE", levelLane.withHeight (14.0f), juce::Justification::topLeft);
    g.drawText ("REDUCTION", redLane.withHeight (14.0f), juce::Justification::topLeft);
    levelLane = levelLane.withTrimmedLeft (78.0f);
    redLane = redLane.withTrimmedLeft (78.0f);

    const float step = levelLane.getWidth() / (float) (length - 1);
    auto levelY = [&] (float db) { return juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, levelLane.getBottom(), levelLane.getY()); };

    // Threshold
    juce::Path thr; thr.startNewSubPath (levelLane.getX(), levelY (threshold)); thr.lineTo (levelLane.getRight(), levelY (threshold));
    juce::Path dashed; const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, thr, dashes, 2);
    g.setColour (t.text.withAlpha (0.5f));
    g.fillPath (dashed);

    juce::Path level, red;
    level.startNewSubPath (levelLane.getX(), levelLane.getBottom());
    red.startNewSubPath (redLane.getX(), redLane.getY());
    for (int i = 0; i < length; ++i)
    {
        const int k = (write + i) % length;
        const float x = levelLane.getX() + step * (float) i;
        level.lineTo (x, levelY (detector[(size_t) k]));
        red.lineTo (x, redLane.getY() + redLane.getHeight() * juce::jlimit (0.0f, 1.0f, reduction[(size_t) k] / juce::jmax (6.0f, range)));
    }
    level.lineTo (levelLane.getRight(), levelLane.getBottom()); level.closeSubPath();
    red.lineTo (redLane.getRight(), redLane.getY()); red.closeSubPath();

    g.setColour (t.accent.withAlpha (0.16f)); g.fillPath (level);
    g.setColour (t.accent); g.strokePath (level, juce::PathStrokeType (1.3f));
    g.setColour (t.panelEdge); g.drawHorizontalLine ((int) redLane.getY(), redLane.getX(), redLane.getRight());
    g.setColour (t.accent2.withAlpha (0.28f)); g.fillPath (red);
    g.setColour (t.accent2); g.strokePath (red, juce::PathStrokeType (1.3f));
}

//==============================================================================
HushAudioProcessorEditor::HushAudioProcessorEditor (HushAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    for (juce::Component* c : { (juce::Component*) &spectrumView, (juce::Component*) &history, (juce::Component*) &mode,
                                (juce::Component*) &listen, (juce::Component*) &frequency, (juce::Component*) &threshold,
                                (juce::Component*) &range, (juce::Component*) &grMeter })
        addAndMakeVisible (c);
    setLookAndFeel (&lnf);

    listen.button.setTooltip ("Hear only the band Hush is listening to, to find the S sounds");
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

HushAudioProcessorEditor::~HushAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void HushAudioProcessorEditor::refreshFromProcessor()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    processorRef.scope.copyLatest (fftData.data(), fftSize);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    const float norm = 2.0f / ((float) fftSize * 0.5f);
    for (size_t k = 0; k < spectrumDb.size(); ++k)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[k] * norm, -120.0f);
        spectrumDb[k] = juce::jmax (-120.0f, spectrumDb[k] + (db - spectrumDb[k]) * (db > spectrumDb[k] ? 0.45f : 0.12f));
    }

    const auto params = processorRef.currentParams();
    const float red = processorRef.meterReduction.exchange (0.0f);
    const float det = processorRef.meterDetector.exchange (-120.0f);
    grMeter.push (red);
    history.push (det, red, params.thresholdDb, params.rangeDb);
    spectrumView.setState (params, spectrumDb, fftSize, processorRef.getSampleRateForDisplay(), grMeter.getDisplayed());
}

void HushAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    g.fillAll (t.background);
    // Engineering-paper dots
    g.setColour (juce::Colour (0xffd4ded9));
    for (int y = 64; y < getHeight(); y += 14)
        for (int x = 7; x < getWidth(); x += 14)
            g.fillRect ((float) x, (float) y, 1.6f, 1.6f);
    paintHeader (g, getWidth(), "Hush", LBAUDIO_PLUGIN_TYPE, t);
    paintPanel (g, controlPanel, t);
    g.setColour (t.dim);
    g.setFont (styledFont (10.5f, true, false, 0.14f));
    g.drawText ("MODE", modeLabel, juce::Justification::centredLeft);
}

void HushAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);
    auto left = area.removeFromLeft (548);
    spectrumView.setBounds (left.removeFromTop (210));
    left.removeFromTop (12);
    history.setBounds (left);
    area.removeFromLeft (14);
    controlPanel = area;

    auto c = controlPanel.reduced (14, 14);
    grMeter.setBounds (c.removeFromRight (58));
    c.removeFromRight (8);
    modeLabel = c.removeFromTop (18);
    c.removeFromTop (4);
    auto modeRow = c.removeFromTop (32);
    listen.setBounds (modeRow.removeFromRight (92));
    modeRow.removeFromRight (8);
    mode.setBounds (modeRow);
    c.removeFromTop (12);
    frequency.setBounds (c.removeFromTop (150).withSizeKeepingCentre (150, 150));
    c.removeFromTop (6);
    const int w = c.getWidth() / 2;
    threshold.setBounds (c.removeFromLeft (w).reduced (2, 0));
    range.setBounds (c.reduced (2, 0));
}
