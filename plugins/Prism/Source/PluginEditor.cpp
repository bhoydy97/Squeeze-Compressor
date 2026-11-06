#include "PluginEditor.h"

using namespace lbaudio::ui;

lbaudio::ui::Theme prismTheme()
{
    Theme t;
    t.background = juce::Colour (0xff0c0d12);
    t.panel      = juce::Colour (0xff15171f);
    t.panelEdge  = juce::Colour (0xff2a2e3b);
    t.track      = juce::Colour (0xff262a36);
    t.text       = juce::Colour (0xffeef0f6);
    t.dim        = juce::Colour (0xff8a90a3);
    t.accent     = juce::Colour (0xffeef0f6);
    t.accent2    = juce::Colour (0xffffb547);
    t.onAccent   = juce::Colour (0xff0c0d12);
    t.knobTop    = juce::Colour (0xff2c303c);
    t.knobBottom = juce::Colour (0xff171920);
    t.knobEdge   = juce::Colour (0xff3a3f4d);
    t.pointer    = juce::Colour (0xffeef0f6);
    t.meterBack  = juce::Colour (0xff0a0b0f);
    t.meterLow   = juce::Colour (0xff3ee6a8);
    t.meterMid   = juce::Colour (0xffffb547);
    t.meterHigh  = juce::Colour (0xffff5a4e);
    t.reduction  = juce::Colour (0xffffb547);
    t.headerText = juce::Colour (0xffeef0f6);
    t.headerType = juce::Colour (0xffffb547);
    t.headerBrand = juce::Colour (0xff8a90a3);
    t.headerLine = juce::Colour (0xff2a2e3b);
    t.knob = KnobStyle::Halo;
    t.corner = 10.0f;
    t.titleSize = 25.0f;
    t.titleKerning = 0.30f;
    t.typeKerning = 0.12f;
    return t;
}

juce::Colour prismBandColour (int band)
{
    static const juce::Colour colours[prism::numBands] = {
        juce::Colour (0xffff5a4e), juce::Colour (0xffffb547), juce::Colour (0xff3ee6a8), juce::Colour (0xff4cc9ff) };
    return colours[juce::jlimit (0, prism::numBands - 1, band)];
}

namespace
{
constexpr double loHz = 20.0, hiHz = 20000.0;

// 120 Hz, 1.2 kHz, 6 kHz, 20 kHz
juce::String hzText (float hz)
{
    if (hz < 1000.0f) return juce::String (juce::roundToInt (hz)) + " Hz";
    auto k = juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2);
    if (k.containsChar ('.')) k = k.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return k + " kHz";
}

// The range of a band, sharing the unit when both ends have it: 120 Hz - 1.2 kHz, 1.2 - 6 kHz
juce::String rangeText (float lo, float hi)
{
    const auto a = hzText (lo), b = hzText (hi);
    if (a.endsWith (" kHz") && b.endsWith (" kHz")) return a.dropLastCharacters (4) + " - " + b;
    if (a.endsWith (" Hz") && b.endsWith (" Hz") && ! a.endsWith ("kHz") && ! b.endsWith ("kHz")) return a.dropLastCharacters (3) + " - " + b;
    return a + " - " + b;
}

Theme bandTheme (int band)
{
    Theme t = prismTheme();
    const auto c = prismBandColour (band);
    t.accent = c; t.accent2 = c.brighter (0.3f); t.reduction = c; t.onAccent = juce::Colour (0xff0c0d12);
    return t;
}
} // namespace

//==============================================================================
juce::Rectangle<float> BandSpectrum::plotArea() const
{
    return getLocalBounds().toFloat().reduced (16.0f, 12.0f).withTrimmedTop (20.0f).withTrimmedBottom (18.0f);
}

float BandSpectrum::hzToX (double hz) const
{
    const auto r = plotArea();
    return r.getX() + (float) (std::log (hz / loHz) / std::log (hiHz / loHz)) * r.getWidth();
}

double BandSpectrum::xToHz (float x) const
{
    const auto r = plotArea();
    return loHz * std::pow (hiHz / loHz, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth())));
}

juce::RangedAudioParameter* BandSpectrum::splitParam (int i) const
{
    static const char* ids[3] = { prism::ids::split1, prism::ids::split2, prism::ids::split3 };
    return apvts.getParameter (ids[juce::jlimit (0, 2, i)]);
}

int BandSpectrum::handleAt (juce::Point<float> p) const
{
    const auto s = prism::orderedSplits (params.splits, fs);
    const float xs[3] = { hzToX (s.low), hzToX (s.mid), hzToX (s.high) };
    int best = -1; float bestD = 10.0f;
    for (int i = 0; i < 3; ++i)
        if (std::abs (p.x - xs[i]) < bestD) { bestD = std::abs (p.x - xs[i]); best = i; }
    return plotArea().expanded (0.0f, 14.0f).contains (p) ? best : -1;
}

void BandSpectrum::paint (juce::Graphics& g)
{
    const auto& t = themeOf (*this);
    paintPanel (g, getLocalBounds(), t);
    const auto plot = plotArea();
    const auto s = prism::orderedSplits (params.splits, fs);
    const float edges[5] = { plot.getX(), hzToX (s.low), hzToX (s.mid), hzToX (s.high), plot.getRight() };
    auto dbToY = [&] (float db) { return juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, plot.getBottom(), plot.getY()); };

    g.setColour (t.dim);
    g.setFont (styledFont (10.5f, true, false, 0.14f));
    g.drawText ("SPECTRUM", juce::Rectangle<float> (plot.getX(), 8.0f, 200.0f, 18.0f), juce::Justification::centredLeft);
    g.drawText ("DRAG THE SPLITS", juce::Rectangle<float> (plot.getRight() - 200.0f, 8.0f, 200.0f, 18.0f), juce::Justification::centredRight);

    // Band regions
    for (int b = 0; b < prism::numBands; ++b)
    {
        const auto col = prismBandColour (b);
        const auto region = juce::Rectangle<float> (edges[b], plot.getY(), edges[b + 1] - edges[b], plot.getHeight());
        const bool off = params.bands[(size_t) b].bypass;
        g.setGradientFill (juce::ColourGradient (col.withAlpha (off ? 0.02f : 0.10f), 0.0f, region.getY(),
                                                 col.withAlpha (0.0f), 0.0f, region.getBottom(), false));
        g.fillRect (region);
        g.setColour (col.withAlpha (off ? 0.35f : 0.9f));
        g.setFont (styledFont (10.5f, true, false, 0.12f));
        g.drawText (juce::String (prism::bandName (b)).toUpperCase(), region.withTop (region.getBottom() - 18.0f).reduced (6.0f, 0.0f),
                    juce::Justification::centredLeft, false);
    }

    // Grid
    g.setFont (font (10.0f));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = hzToX (hz);
        g.setColour (t.panelEdge.withAlpha (0.55f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (t.dim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, plot.getBottom() + 9.0f }), juce::Justification::centred);
    }

    // Spectra: input faintly, output brighter
    auto curve = [&] (const std::vector<float>& bins)
    {
        juce::Path p;
        p.startNewSubPath (plot.getX(), plot.getBottom());
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double hz = xToHz (x);
            const double bin = hz / fs * size;
            const int k = juce::jlimit (1, (int) bins.size() - 2, (int) bin);
            const float frac = (float) juce::jlimit (0.0, 1.0, bin - k);
            p.lineTo (x, dbToY (bins[(size_t) k] + frac * (bins[(size_t) k + 1] - bins[(size_t) k])));
        }
        p.lineTo (plot.getRight(), plot.getBottom());
        p.closeSubPath();
        return p;
    };
    if (size > 0 && ! spectrumOut.empty())
    {
        const auto in = curve (spectrumIn), out = curve (spectrumOut);
        g.setColour (t.text.withAlpha (0.18f));
        g.strokePath (in, juce::PathStrokeType (1.0f));
        g.setColour (t.text.withAlpha (0.07f));
        g.fillPath (out);
        g.setColour (t.text.withAlpha (0.75f));
        g.strokePath (out, juce::PathStrokeType (1.3f));
    }

    // Gain reduction, hanging from the top of each band
    for (int b = 0; b < prism::numBands; ++b)
    {
        const float gr = reduction[(size_t) b];
        if (gr < 0.05f || params.bands[(size_t) b].bypass) continue;
        const auto col = prismBandColour (b);
        const float h = plot.getHeight() * 0.45f * juce::jlimit (0.0f, 1.0f, gr / 24.0f);
        const auto bar = juce::Rectangle<float> (edges[b] + 3.0f, plot.getY(), edges[b + 1] - edges[b] - 6.0f, h);
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.55f), 0.0f, bar.getY(), col.withAlpha (0.15f), 0.0f, bar.getBottom(), false));
        g.fillRect (bar);
        g.setColour (col);
        g.fillRect (bar.withTop (bar.getBottom() - 2.0f));
        g.setFont (font (11.0f, true));
        g.drawText ("-" + juce::String (gr, 1), bar.withHeight (16.0f).withY (bar.getBottom() + 2.0f), juce::Justification::centred, false);
    }

    // Split handles
    const float splitHz[3] = { s.low, s.mid, s.high };
    for (int i = 0; i < 3; ++i)
    {
        const float x = edges[i + 1];
        const bool hot = i == dragging || i == hover;
        juce::Path line; line.startNewSubPath (x, plot.getY()); line.lineTo (x, plot.getBottom());
        juce::Path dashed; const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (hot ? 1.6f : 1.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (t.text.withAlpha (hot ? 0.9f : 0.45f));
        g.fillPath (dashed);
        const auto knob = juce::Rectangle<float> (hot ? 14.0f : 11.0f, hot ? 14.0f : 11.0f).withCentre ({ x, plot.getY() + 1.0f });
        g.setGradientFill (juce::ColourGradient (prismBandColour (i), knob.getX(), 0.0f, prismBandColour (i + 1), knob.getRight(), 0.0f, false));
        g.fillEllipse (knob);
        g.setColour (t.text);
        g.drawEllipse (knob, 1.2f);
        g.setFont (font (11.0f, true));
        g.drawText (hzText (splitHz[i]),
                    juce::Rectangle<float> (70.0f, 14.0f).withCentre ({ x, plot.getY() - 9.0f }), juce::Justification::centred, false);
    }
}

void BandSpectrum::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void BandSpectrum::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position);
    if (dragging >= 0)
        if (auto* p = splitParam (dragging)) p->beginChangeGesture();
}

void BandSpectrum::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    if (auto* p = splitParam (dragging))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) xToHz (e.position.x)));
}

void BandSpectrum::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        if (auto* p = splitParam (dragging)) p->endChangeGesture();
    dragging = -1;
    repaint();
}

void BandSpectrum::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (auto* p = h >= 0 ? splitParam (h) : nullptr)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

//==============================================================================
BandStrip::BandStrip (juce::AudioProcessorValueTreeState& state, int bandIndex)
    : band (bandIndex), lnf (bandTheme (bandIndex)),
      solo (state, prism::ids::band (bandIndex, "solo"), "SOLO"),
      bypass (state, prism::ids::band (bandIndex, "bypass"), "BYPASS"),
      threshold (state, prism::ids::band (bandIndex, "threshold"), "THRESHOLD"),
      ratio (state, prism::ids::band (bandIndex, "ratio"), "RATIO"),
      makeup (state, prism::ids::band (bandIndex, "makeup"), "MAKEUP", true),
      attack (state, prism::ids::band (bandIndex, "attack"), "ATTACK"),
      release (state, prism::ids::band (bandIndex, "release"), "RELEASE"),
      excite (state, prism::ids::band (bandIndex, "excite"), "EXCITE")
{
    setLookAndFeel (&lnf);
    for (juce::Component* c : { (juce::Component*) &solo, (juce::Component*) &bypass, (juce::Component*) &threshold,
                                (juce::Component*) &ratio, (juce::Component*) &makeup, (juce::Component*) &attack,
                                (juce::Component*) &release, (juce::Component*) &excite, (juce::Component*) &grMeter })
        addAndMakeVisible (c);
    solo.button.setTooltip ("Hear only this band (and any other soloed band)");
    bypass.button.setTooltip ("Leave this band untouched: no compression, no excite");
    excite.slider.setTooltip ("Adds harmonics made from this band only: brighter highs, or warmer, fuller lows");
}

BandStrip::~BandStrip() { setLookAndFeel (nullptr); }

void BandStrip::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    paintPanel (g, getLocalBounds(), t);
    const auto col = prismBandColour (band);
    g.setColour (col);
    g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromTop (3.0f).reduced (14.0f, 0.0f), 1.5f);
    g.setFont (styledFont (13.0f, true, false, 0.12f));
    g.drawText (juce::String (prism::bandName (band)).toUpperCase(), titleArea, juce::Justification::centredLeft);
    g.setColour (t.dim);
    g.setFont (font (11.5f));
    g.drawText (rangeText (lo, hi), titleArea, juce::Justification::centredRight);
}

void BandStrip::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (4);
    titleArea = r.removeFromTop (22);
    r.removeFromTop (6);
    auto switches = r.removeFromTop (26);
    const int sw = (switches.getWidth() - 8) / 2;
    solo.setBounds (switches.removeFromLeft (sw));
    switches.removeFromLeft (8);
    bypass.setBounds (switches);
    r.removeFromTop (8);
    grMeter.setBounds (r.removeFromRight (34));
    r.removeFromRight (4);
    const int rowH = r.getHeight() / 2, colW = r.getWidth() / 3;
    for (auto* k : { &threshold, &ratio, &makeup, &attack, &release, &excite })
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (90, colW - 8), 20);
    auto row1 = r.removeFromTop (rowH), row2 = r;
    threshold.setBounds (row1.removeFromLeft (colW)); ratio.setBounds (row1.removeFromLeft (colW)); makeup.setBounds (row1);
    attack.setBounds (row2.removeFromLeft (colW)); release.setBounds (row2.removeFromLeft (colW)); excite.setBounds (row2);
}

//==============================================================================
PresetBar::PresetBar (PrismAudioProcessor& p) : processor (p)
{
    for (auto* b : { &prev, &next, &name, &saveButton }) addAndMakeVisible (b);
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    name.setTooltip ("Choose a preset");
    saveButton.setTooltip ("Save these settings as your own preset");
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    name.onClick = [this] { showMenu(); };
    saveButton.onClick = [this] { save(); };
    refresh();
}

void PresetBar::refresh()
{
    const auto n = processor.getPresetName();
    if (n != shownName) { shownName = n; name.setButtonText (n); }
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    saveButton.setBounds (r.removeFromRight (64));
    r.removeFromRight (8);
    prev.setBounds (r.removeFromLeft (30));
    next.setBounds (r.removeFromRight (30));
    r.reduce (6, 0);
    name.setBounds (r);
}

void PresetBar::showMenu()
{
    juce::PopupMenu menu;
    const auto& list = prism::factoryPresets();
    juce::String category;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (category != list[(size_t) i].category) { category = list[(size_t) i].category; menu.addSectionHeader (category); }
        menu.addItem (i + 1, list[(size_t) i].name, true, processor.getPresetIndex() == i);
    }
    const auto user = processor.userPresets();
    menu.addSectionHeader ("Your presets");
    if (user.isEmpty())
        menu.addItem (-1, "Save one with SAVE", false, false);
    for (int i = 0; i < user.size(); ++i)
        menu.addItem (1000 + i, user[i].getFileNameWithoutExtension(), true,
                      processor.getPresetIndex() < 0 && processor.getPresetName() == user[i].getFileNameWithoutExtension());
    menu.addSeparator();
    menu.addItem (999, "Open your presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name), [this, user] (int r)
    {
        if (r >= 1 && r <= 998) processor.applyFactoryPreset (r - 1);
        else if (r >= 1000 && r - 1000 < user.size()) processor.loadUserPreset (user[r - 1000]);
        else if (r == 999)
        {
            auto folder = PrismAudioProcessor::userPresetFolder();
            folder.createDirectory();
            folder.revealToUser();
        }
        refresh();
    });
}

void PresetBar::step (int delta)
{
    const int factory = (int) prism::factoryPresets().size();
    const auto user = processor.userPresets();
    const int total = factory + user.size();
    int current = processor.getPresetIndex();
    if (current < 0)
    {
        current = factory;
        for (int i = 0; i < user.size(); ++i)
            if (user[i].getFileNameWithoutExtension() == processor.getPresetName()) current = factory + i;
    }
    const int nextIndex = ((current + delta) % total + total) % total;
    if (nextIndex < factory) processor.applyFactoryPreset (nextIndex);
    else processor.loadUserPreset (user[nextIndex - factory]);
    refresh();
}

void PresetBar::save()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name your preset. It's saved with your other Prism presets.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", processor.getPresetName());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetBar> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result == 1 && safe != nullptr)
        {
            safe->processor.saveUserPreset (w->getTextEditorContents ("name"));
            safe->refresh();
        }
    }), true);
}

//==============================================================================
PrismAudioProcessorEditor::PrismAudioProcessorEditor (PrismAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lnf);
    for (int b = 0; b < prism::numBands; ++b)
    {
        strips[(size_t) b] = std::make_unique<BandStrip> (processorRef.apvts, b);
        addAndMakeVisible (*strips[(size_t) b]);
    }
    for (juce::Component* c : { (juce::Component*) &presets, (juce::Component*) &spectrumView, (juce::Component*) &input,
                                (juce::Component*) &output, (juce::Component*) &mix, (juce::Component*) &inMeter, (juce::Component*) &outMeter })
        addAndMakeVisible (c);

    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

PrismAudioProcessorEditor::~PrismAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PrismAudioProcessorEditor::refreshFromProcessor()
{
    auto analyse = [this] (const lbaudio::ScopeBuffer& scope, std::vector<float>& dest)
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        scope.copyLatest (fftData.data(), fftSize);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        const float norm = 2.0f / ((float) fftSize * 0.5f);
        for (size_t k = 0; k < dest.size(); ++k)
        {
            const float db = juce::Decibels::gainToDecibels (fftData[k] * norm, -120.0f);
            dest[k] = juce::jmax (-120.0f, dest[k] + (db - dest[k]) * (db > dest[k] ? 0.45f : 0.12f));
        }
    };
    analyse (processorRef.scopeIn, inDb);
    analyse (processorRef.scopeOut, outDb);

    const auto params = processorRef.currentParams();
    const double sr = processorRef.getSampleRateForDisplay();
    const auto s = prism::orderedSplits (params.splits, sr);
    const float edges[5] = { 20.0f, s.low, s.mid, s.high, 20000.0f };
    std::array<float, prism::numBands> red {};
    for (int b = 0; b < prism::numBands; ++b)
    {
        auto& strip = *strips[(size_t) b];
        strip.grMeter.push (processorRef.meterReduction[(size_t) b].exchange (0.0f));
        red[(size_t) b] = strip.grMeter.getDisplayed();
        strip.setRange (edges[b], edges[b + 1]);
    }
    inMeter.push (processorRef.meterInputPeak.exchange (0.0f));
    outMeter.push (processorRef.meterOutputPeak.exchange (0.0f));
    spectrumView.setState (params, inDb, outDb, fftSize, sr, red);
    presets.refresh();
}

void PrismAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    g.fillAll (t.background);
    // A faint spectrum along the top edge
    for (int b = 0; b < prism::numBands; ++b)
    {
        const float w = (float) getWidth() / prism::numBands;
        g.setGradientFill (juce::ColourGradient (prismBandColour (b), w * (float) b, 0.0f,
                                                 prismBandColour (juce::jmin (b + 1, prism::numBands - 1)), w * (float) (b + 1), 0.0f, false));
        g.fillRect (w * (float) b, 0.0f, w, 3.0f);
    }
    paintHeader (g, getWidth(), "Prism", LBAUDIO_PLUGIN_TYPE, t);
    paintPanel (g, globalPanel, t);
}

void PrismAudioProcessorEditor::resized()
{
    presets.setBounds (getWidth() - 20 - 380, 13, 380, 30);
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);
    spectrumView.setBounds (area.removeFromTop (220));
    area.removeFromTop (12);

    globalPanel = area.removeFromRight (176);
    area.removeFromRight (12);
    const int gap = 12, w = (area.getWidth() - gap * (prism::numBands - 1)) / prism::numBands;
    for (int b = 0; b < prism::numBands; ++b)
    {
        strips[(size_t) b]->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (gap);
    }

    auto c = globalPanel.reduced (12, 12);
    auto meters = c.removeFromRight (64);
    inMeter.setBounds (meters.removeFromLeft (32));
    outMeter.setBounds (meters);
    c.removeFromRight (4);
    const int h = c.getHeight() / 3;
    for (auto* k : { &input, &output, &mix })
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (90, c.getWidth() - 8), 20);
    input.setBounds (c.removeFromTop (h));
    output.setBounds (c.removeFromTop (h));
    mix.setBounds (c);
}
