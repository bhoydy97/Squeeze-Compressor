#include "PluginEditor.h"

using namespace lbaudio::ui;

//==============================================================================
float EqGraph::xForFreq (double f) const
{
    const auto r = plot();
    return r.getX() + (float) (std::log (f / minHz) / std::log (maxHz / minHz)) * r.getWidth();
}

double EqGraph::freqForX (float x) const
{
    const auto r = plot();
    return minHz * std::pow (maxHz / minHz, (double) ((x - r.getX()) / r.getWidth()));
}

float EqGraph::yForDb (double db) const
{
    const auto r = plot();
    return r.getCentreY() - (float) (db / rangeDb) * r.getHeight() * 0.5f;
}

double EqGraph::dbForY (float y) const
{
    const auto r = plot();
    return (double) ((r.getCentreY() - y) / (r.getHeight() * 0.5f)) * rangeDb;
}

juce::Point<float> EqGraph::nodePos (int b) const
{
    const auto& bp = params.bands[(size_t) b];
    const bool pass = carve::isPass (b);
    const auto r = plot();
    return { juce::jlimit (r.getX() + 9.0f, r.getRight() - 9.0f, xForFreq (juce::jlimit ((double) minHz, (double) maxHz, (double) bp.freq))),
             yForDb (pass ? 0.0 : juce::jlimit (-(double) rangeDb, (double) rangeDb, (double) bp.gainDb)) };
}

int EqGraph::hitNode (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 13.0f;
    for (int b = 0; b < carve::NumBands; ++b)
    {
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

void EqGraph::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (Colours::panelEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    const auto r = plot();
    g.setColour (Colours::background.withAlpha (0.6f));
    g.fillRect (r);

    // Grid
    g.setFont (font (10.0f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForFreq (f);
        g.setColour (Colours::panelEdge);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (Colours::dim.withAlpha (0.8f));
        const juce::String label = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
        g.drawText (label, juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ x, r.getBottom() + 10.0f }), juce::Justification::centred);
    }
    for (double db = -rangeDb; db <= rangeDb + 0.01; db += 6.0)
    {
        const float y = yForDb (db);
        g.setColour (std::abs (db) < 0.01 ? Colours::panelEdge.brighter (0.35f) : Colours::panelEdge);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (Colours::dim.withAlpha (0.8f));
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (30.0f, 12.0f).withCentre ({ r.getX() - 18.0f, y }),
                    juce::Justification::centredRight);
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (r.toNearestInt());

    // Spectrum (post-EQ), -90..0 dBFS over the full height
    if (! spectrum.empty() && spectrumFftSize > 0)
    {
        juce::Path spec;
        const int steps = (int) r.getWidth();
        bool started = false;
        for (int i = 0; i <= steps; i += 2)
        {
            const double f0 = freqForX (r.getX() + (float) i), f1 = freqForX (r.getX() + (float) i + 2.0f);
            const double fb0 = f0 / fs * spectrumFftSize, fb1 = f1 / fs * spectrumFftSize;
            const int last = (int) spectrum.size() - 1;
            float db;
            if (fb1 - fb0 < 1.0)
            {
                // Fewer than one bin per step: interpolate between neighbouring bins
                const int k0 = juce::jlimit (1, last - 1, (int) fb0);
                const float t = (float) juce::jlimit (0.0, 1.0, fb0 - k0);
                db = spectrum[(size_t) k0] + t * (spectrum[(size_t) k0 + 1] - spectrum[(size_t) k0]);
            }
            else
            {
                db = -120.0f;
                for (int k = juce::jlimit (1, last, (int) fb0); k <= juce::jlimit (1, last, (int) fb1); ++k)
                    db = juce::jmax (db, spectrum[(size_t) k]);
            }
            const float y = juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, r.getBottom(), r.getY());
            const float x = r.getX() + (float) i;
            if (! started) { spec.startNewSubPath (x, r.getBottom()); started = true; }
            spec.lineTo (x, y);
        }
        spec.lineTo (r.getRight(), r.getBottom());
        spec.closeSubPath();
        g.setColour (Colours::accent.withAlpha (0.07f));
        g.fillPath (spec);
        g.setColour (Colours::accent.withAlpha (0.18f));
        g.strokePath (spec, juce::PathStrokeType (1.0f));
    }

    // Selected band's own curve, shaded
    {
        juce::Path band;
        const int steps = (int) r.getWidth();
        for (int i = 0; i <= steps; i += 2)
        {
            const float x = r.getX() + (float) i;
            const double db = carve::bandResponseDb (selected, params.bands[(size_t) selected], freqForX (x), fs);
            if (i == 0) band.startNewSubPath (x, yForDb (db)); else band.lineTo (x, yForDb (db));
        }
        juce::Path fill (band);
        fill.lineTo (r.getRight(), yForDb (0.0));
        fill.lineTo (r.getX(), yForDb (0.0));
        fill.closeSubPath();
        g.setColour (Colours::gr.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (Colours::gr.withAlpha (0.55f));
        g.strokePath (band, juce::PathStrokeType (1.2f));
    }

    // Total response
    juce::Path total;
    const int steps = (int) r.getWidth();
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.getX() + (float) i;
        const float y = yForDb (carve::responseDb (params, freqForX (x), fs));
        if (i == 0) total.startNewSubPath (x, y); else total.lineTo (x, y);
    }
    g.setColour (Colours::accent);
    g.strokePath (total, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Nodes
    const auto& ids = carve::bandIds();
    for (int b = 0; b < carve::NumBands; ++b)
    {
        const auto p = nodePos (b);
        const bool off = carve::isPass (b) && ! params.bands[(size_t) b].on;
        const bool sel = b == selected;
        const float radius = sel ? 10.0f : 8.5f;
        if (sel || b == hover)
        {
            g.setColour ((sel ? Colours::gr : Colours::accent).withAlpha (0.22f));
            g.fillEllipse (juce::Rectangle<float> (radius * 3.2f, radius * 3.2f).withCentre (p));
        }
        g.setColour (off ? Colours::panelEdge.brighter (0.2f) : (sel ? Colours::gr : Colours::accent));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
        g.setColour (juce::Colour (0xff07211e).withAlpha (off ? 0.6f : 1.0f));
        const juce::String label = juce::String (ids[(size_t) b].shortName).substring (0, b == carve::LowShelf || b == carve::HighShelf ? 1 : 2);
        g.setFont (font (label.length() > 1 ? 8.5f : 10.5f, true));
        g.drawText (label, juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p), juce::Justification::centred);
    }
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitNode (e.position);
    if (dragging < 0) return;
    selected = dragging;
    if (onSelect) onSelect (selected);
    const auto& id = carve::bandIds()[(size_t) dragging];
    if (auto* p = param (id.freq)) p->beginChangeGesture();
    if (auto* p = param (id.gain)) p->beginChangeGesture();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto& id = carve::bandIds()[(size_t) dragging];
    const auto r = plot();
    setParam (param (id.freq), (float) freqForX (juce::jlimit (r.getX(), r.getRight(), e.position.x)));
    setParam (param (id.gain), (float) juce::jlimit (-(double) rangeDb, (double) rangeDb, dbForY (e.position.y)));
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0) return;
    const auto& id = carve::bandIds()[(size_t) dragging];
    if (auto* p = param (id.freq)) p->endChangeGesture();
    if (auto* p = param (id.gain)) p->endChangeGesture();
    dragging = -1;
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitNode (e.position);
    if (b < 0) return;
    const auto& id = carve::bandIds()[(size_t) b];
    if (auto* on = param (id.on))
    {
        on->beginChangeGesture();
        on->setValueNotifyingHost (on->getValue() > 0.5f ? 0.0f : 1.0f);
        on->endChangeGesture();
    }
    else if (auto* gain = param (id.gain))
    {
        gain->beginChangeGesture();
        setParam (gain, 0.0f);
        gain->endChangeGesture();
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = hitNode (e.position);
    if (b < 0) b = selected;
    const auto& id = carve::bandIds()[(size_t) b];
    const float delta = std::abs (w.deltaY) > 0.0f ? w.deltaY : w.deltaX;
    if (auto* q = param (id.q))
    {
        const float current = q->convertFrom0to1 (q->getValue());
        q->beginChangeGesture();
        setParam (q, juce::jlimit (0.1f, 18.0f, current * std::exp (delta * 1.5f)));
        q->endChangeGesture();
    }
    else if (auto* slope = param (id.slope))
    {
        const int now = juce::roundToInt (slope->convertFrom0to1 (slope->getValue()));
        const int next = juce::jlimit (0, 2, now + (delta > 0 ? 1 : -1));
        slope->beginChangeGesture();
        setParam (slope, (float) next);
        slope->endChangeGesture();
    }
}

//==============================================================================
CarveAudioProcessorEditor::CarveAudioProcessorEditor (CarveAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    addAndMakeVisible (graph);
    graph.onSelect = [this] (int b) { selectBand (b); };

    const auto& ids = carve::bandIds();
    for (int b = 0; b < carve::NumBands; ++b)
    {
        auto& t = tabs[(size_t) b];
        t.setButtonText (ids[(size_t) b].shortName);
        t.setClickingTogglesState (false);
        t.onClick = [this, b] { selectBand (b); };
        addAndMakeVisible (t);

        auto& c = controls[(size_t) b];
        auto& state = processorRef.apvts;
        if (*ids[(size_t) b].on)    { c.on = std::make_unique<Switch> (state, ids[(size_t) b].on, "ON"); addChildComponent (*c.on); }
        c.freq = std::make_unique<LabelledKnob> (state, ids[(size_t) b].freq, "FREQ");
        addChildComponent (*c.freq);
        if (*ids[(size_t) b].gain)  { c.gain = std::make_unique<LabelledKnob> (state, ids[(size_t) b].gain, "GAIN", true); addChildComponent (*c.gain); }
        if (*ids[(size_t) b].q)     { c.q = std::make_unique<LabelledKnob> (state, ids[(size_t) b].q, "Q"); addChildComponent (*c.q); }
        if (*ids[(size_t) b].slope) { c.slope = std::make_unique<Segmented> (state, ids[(size_t) b].slope); addChildComponent (*c.slope); }
    }

    slopeLabel.setText ("SLOPE", juce::dontSendNotification);
    slopeLabel.setFont (font (11.5f, true));
    slopeLabel.setColour (juce::Label::textColourId, Colours::dim);
    slopeLabel.setJustificationType (juce::Justification::centred);
    addChildComponent (slopeLabel);

    addAndMakeVisible (output);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    setLookAndFeel (&lnf);
    setSize (defaultWidth, defaultHeight);
    selectBand (carve::Bell2);
    refreshFromProcessor();
    startTimerHz (30);
}

CarveAudioProcessorEditor::~CarveAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void CarveAudioProcessorEditor::selectBand (int b)
{
    selected = b;
    graph.setSelected (b);
    for (int i = 0; i < carve::NumBands; ++i)
    {
        tabs[(size_t) i].setToggleState (i == b, juce::dontSendNotification);
        auto& c = controls[(size_t) i];
        for (juce::Component* comp : { (juce::Component*) c.on.get(), (juce::Component*) c.freq.get(), (juce::Component*) c.gain.get(),
                                       (juce::Component*) c.q.get(), (juce::Component*) c.slope.get() })
            if (comp != nullptr) comp->setVisible (i == b);
    }
    slopeLabel.setVisible (controls[(size_t) b].slope != nullptr);
    resized();
}

void CarveAudioProcessorEditor::updateSpectrum()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    processorRef.scope.copyLatest (fftData.data(), fftSize);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    const float norm = 2.0f / ((float) fftSize * 0.5f); // Hann coherent gain is 0.5
    for (size_t k = 0; k < spectrumDb.size(); ++k)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[k] * norm, -120.0f);
        const float blended = spectrumDb[k] + (db - spectrumDb[k]) * (db > spectrumDb[k] ? 0.45f : 0.12f);
        spectrumDb[k] = juce::jmax (-120.0f, blended);
    }
    graph.setSpectrum (spectrumDb, fftSize);
}

void CarveAudioProcessorEditor::refreshFromProcessor()
{
    inMeter.push (processorRef.meterInputPeak.exchange (0.0f));
    outMeter.push (processorRef.meterOutputPeak.exchange (0.0f));
    updateSpectrum();
    graph.setState (processorRef.currentParams(), processorRef.currentSampleRate());
}

void CarveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    paintHeader (g, getWidth(), "Carve", LBAUDIO_PLUGIN_TYPE);
    paintPanel (g, meterPanel);
    paintPanel (g, stripPanel);

    g.setColour (Colours::text);
    g.setFont (font (15.0f, true));
    g.drawText (carve::bandIds()[(size_t) selected].title, helpArea.removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (font (12.0f));
    g.drawFittedText ("Drag a dot on the graph to move a band. Scroll over it to change Q or slope. Double-click it to reset or switch it off.",
                      helpArea, juce::Justification::topLeft, 3);
}

void CarveAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);

    auto top = area.removeFromTop (300);
    meterPanel = top.removeFromRight (106);
    top.removeFromRight (12);
    graph.setBounds (top);
    auto meters = meterPanel.reduced (10, 10);
    inMeter.setBounds (meters.removeFromLeft (meters.getWidth() / 2));
    outMeter.setBounds (meters);

    area.removeFromTop (12);
    stripPanel = area;
    auto strip = stripPanel.reduced (12, 10);

    auto left = strip.removeFromLeft (360);
    auto tabRow = left.removeFromTop (30);
    const int tabW = tabRow.getWidth() / carve::NumBands;
    for (auto& t : tabs) t.setBounds (tabRow.removeFromLeft (tabW).reduced (2, 0));
    left.removeFromTop (10);
    helpArea = left;

    auto outCell = strip.removeFromRight (104);
    output.setBounds (outCell);
    strip.removeFromRight (8);

    // Selected band's controls, left to right
    auto& c = controls[(size_t) selected];
    strip.removeFromLeft (20);
    if (c.on)
    {
        c.on->setBounds (strip.removeFromLeft (84).withSizeKeepingCentre (76, 28));
        strip.removeFromLeft (8);
    }
    c.freq->setBounds (strip.removeFromLeft (104));
    if (c.gain) c.gain->setBounds (strip.removeFromLeft (104));
    if (c.q)    c.q->setBounds (strip.removeFromLeft (104));
    if (c.slope)
    {
        auto cell = strip.removeFromLeft (220).reduced (8, 0);
        auto group = cell.withSizeKeepingCentre (cell.getWidth(), 18 + 6 + 30);
        slopeLabel.setBounds (group.removeFromTop (18));
        group.removeFromTop (6);
        c.slope->setBounds (group);
    }
}
