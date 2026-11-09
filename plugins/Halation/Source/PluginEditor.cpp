#include "PluginEditor.h"

using namespace lbaudio::ui;

lbaudio::ui::Theme halationTheme()
{
    // Halation is the glow that bleeds around a bright light on film: a white-hot core with a red-orange halo.
    // So: a warm black faceplate, smoked-glass panels, and everything that's lit glows.
    Theme t;
    t.background = juce::Colour (0xff0a0708);
    t.panel      = juce::Colour (0xdc161011); // slightly see-through, so the light behind shows through the glass
    t.panelEdge  = juce::Colour (0xff302325);
    t.track      = juce::Colour (0xff2c2124);
    t.text       = juce::Colour (0xfffff3ea);
    t.dim        = juce::Colour (0xffab958f);
    t.accent     = juce::Colour (0xffffe4cf); // white-hot
    t.accent2    = halationRed();
    t.onAccent   = juce::Colour (0xff0a0708);
    t.knobTop    = juce::Colour (0xff302427);
    t.knobBottom = juce::Colour (0xff140f11);
    t.knobEdge   = juce::Colour (0xff403034);
    t.pointer    = juce::Colour (0xfffff3ea);
    t.meterBack  = juce::Colour (0xff070405);
    t.meterLow   = juce::Colour (0xffffbf52);
    t.meterMid   = juce::Colour (0xffff7d36);
    t.meterHigh  = juce::Colour (0xffff3b2f);
    t.reduction  = juce::Colour (0xffff7d36);
    t.headerText = juce::Colour (0xfffff1e6);
    t.headerType = juce::Colour (0xffff8f52);
    t.headerBrand = juce::Colour (0xffab958f);
    t.headerLine = juce::Colour (0xff2a1f21);
    t.knob = KnobStyle::Halo;
    t.panelStyle = PanelStyle::Glass;
    t.corner = 12.0f;
    t.titleSize = 26.0f;
    t.titleKerning = 0.32f;
    t.typeKerning = 0.14f;
    t.glow = 1.0f;
    t.glowTint = halationRed();
    t.titleGlow = halationRed();
    return t;
}

juce::Colour halationRed() { return juce::Colour (0xffff3d24); }

// Each band is its own colour of light, in the order of the spectrum: red light has the lowest frequency and violet
// the highest, so the lows are red, then amber, green, and the highs violet. Far enough apart to never be mistaken.
juce::Colour halationBandColour (int band)
{
    static const juce::Colour colours[halation::numBands] = {
        juce::Colour (0xffff4038), juce::Colour (0xffffb833), juce::Colour (0xff4cf08c), juce::Colour (0xffa688ff) };
    return colours[juce::jlimit (0, halation::numBands - 1, band)];
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
    Theme t = halationTheme();
    const auto c = halationBandColour (band);
    t.accent = c; t.accent2 = c.brighter (0.3f); t.reduction = c; t.onAccent = juce::Colour (0xff0a0708);
    t.glowTint = c;
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
    static const char* ids[3] = { halation::ids::split1, halation::ids::split2, halation::ids::split3 };
    return apvts.getParameter (ids[juce::jlimit (0, 2, i)]);
}

int BandSpectrum::handleAt (juce::Point<float> p) const
{
    const auto s = halation::orderedSplits (params.splits, fs);
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
    const auto s = halation::orderedSplits (params.splits, fs);
    const float edges[5] = { plot.getX(), hzToX (s.low), hzToX (s.mid), hzToX (s.high), plot.getRight() };
    auto dbToY = [&] (float db) { return juce::jmap (juce::jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, plot.getBottom(), plot.getY()); };
    auto bandRegion = [&] (int b) { return juce::Rectangle<float> (edges[b], plot.getY(), edges[b + 1] - edges[b], plot.getHeight()); };

    // The plot is a dark well, so the light in it reads
    g.setColour (t.background.withAlpha (0.6f));
    g.fillRect (plot);

    g.setColour (t.dim);
    g.setFont (styledFont (10.5f, true, false, 0.14f));
    g.drawText ("SPECTRUM", juce::Rectangle<float> (plot.getX(), 8.0f, 200.0f, 18.0f), juce::Justification::centredLeft);
    g.drawText ("DRAG THE SPLITS", juce::Rectangle<float> (plot.getRight() - 200.0f, 8.0f, 200.0f, 18.0f), juce::Justification::centredRight);

    // Band regions, each lit from above in its own colour
    for (int b = 0; b < halation::numBands; ++b)
    {
        const auto col = halationBandColour (b);
        const auto region = bandRegion (b);
        const bool off = params.bands[(size_t) b].bypass;
        g.setGradientFill (juce::ColourGradient (col.withAlpha (off ? 0.025f : 0.13f), 0.0f, region.getY(),
                                                 col.withAlpha (0.0f), 0.0f, region.getY() + region.getHeight() * 0.8f, false));
        g.fillRect (region);
    }

    // Grid
    g.setFont (font (10.0f));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = hzToX (hz);
        g.setColour (t.panelEdge.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (t.dim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, plot.getBottom() + 9.0f }), juce::Justification::centred);
    }
    g.setColour (t.panelEdge);
    g.drawHorizontalLine ((int) plot.getBottom(), plot.getX(), plot.getRight());

    // Spectra: the input as a faint trace, the output as light. Under the output each band glows in its own colour;
    // the trace itself is a white-hot filament with its halo in the band's colour.
    // A trace of a spectrum: 'shown' leaves out stretches lying on the floor (silence), 'whole' runs edge to edge
    struct Trace { juce::Path shown, whole; };
    auto trace = [&] (const std::vector<float>& bins)
    {
        Trace tr;
        bool inShown = false;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double hz = xToHz (x);
            const double bin = hz / fs * size;
            const int k = juce::jlimit (1, (int) bins.size() - 2, (int) bin);
            const float frac = (float) juce::jlimit (0.0, 1.0, bin - k);
            const float db = bins[(size_t) k] + frac * (bins[(size_t) k + 1] - bins[(size_t) k]);
            const float y = dbToY (db);
            if (x == plot.getX()) tr.whole.startNewSubPath (x, y); else tr.whole.lineTo (x, y);
            if (db > -89.0f)
            {
                if (inShown) tr.shown.lineTo (x, y); else tr.shown.startNewSubPath (x, y);
                inShown = true;
            }
            else inShown = false;
        }
        return tr;
    };
    if (size > 0 && ! spectrumOut.empty())
    {
        const auto inTrace = trace (spectrumIn), outTrace = trace (spectrumOut);
        const auto& in = inTrace.shown;
        const auto& out = outTrace.shown;
        g.setColour (t.text.withAlpha (0.16f));
        g.strokePath (in, juce::PathStrokeType (1.0f));

        auto area = outTrace.whole;
        area.lineTo (plot.getRight(), plot.getBottom());
        area.lineTo (plot.getX(), plot.getBottom());
        area.closeSubPath();
        juce::Path lit;
        juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (lit, out);

        for (int b = 0; b < halation::numBands; ++b)
        {
            const auto col = halationBandColour (b);
            const bool off = params.bands[(size_t) b].bypass;
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (bandRegion (b).withY (0.0f).withHeight ((float) getHeight()).toNearestInt());
            g.setGradientFill (juce::ColourGradient (col.withAlpha (off ? 0.06f : 0.22f), 0.0f, plot.getY() + plot.getHeight() * 0.25f,
                                                     col.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
            g.fillPath (area);
            paintGlow (g, lit, col.withAlpha (off ? 0.3f : 1.0f), 9);
            paintGlow (g, lit, col.withAlpha (off ? 0.3f : 0.9f), 3);
        }
        g.setColour (t.text.withAlpha (0.92f));
        g.strokePath (out, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Band names along the bottom of each band
    for (int b = 0; b < halation::numBands; ++b)
    {
        const auto region = bandRegion (b);
        g.setColour (halationBandColour (b).withAlpha (params.bands[(size_t) b].bypass ? 0.35f : 0.95f));
        g.setFont (styledFont (10.5f, true, false, 0.12f));
        g.drawText (juce::String (halation::bandName (b)).toUpperCase(), region.withTop (region.getBottom() - 18.0f).reduced (6.0f, 0.0f),
                    juce::Justification::centredLeft, false);
    }

    // Gain reduction, hanging from the top of each band like light spilling down
    for (int b = 0; b < halation::numBands; ++b)
    {
        const float gr = reduction[(size_t) b];
        if (gr < 0.05f || params.bands[(size_t) b].bypass) continue;
        const auto col = halationBandColour (b);
        const float h = plot.getHeight() * 0.45f * juce::jlimit (0.0f, 1.0f, gr / 24.0f);
        const auto bar = juce::Rectangle<float> (edges[b] + 3.0f, plot.getY(), edges[b + 1] - edges[b] - 6.0f, h);
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.5f), 0.0f, bar.getY(), col.withAlpha (0.08f), 0.0f, bar.getBottom(), false));
        g.fillRect (bar);
        juce::Path edge; edge.addRectangle (bar.withTop (bar.getBottom() - 2.0f));
        paintGlow (g, edge, col, 6);
        g.setColour (col.interpolatedWith (juce::Colours::white, 0.45f));
        g.fillPath (edge);
        g.setColour (col);
        g.setFont (font (11.0f, true));
        g.drawText ("-" + juce::String (gr, 1), bar.withHeight (16.0f).withY (bar.getBottom() + 3.0f), juce::Justification::centred, false);
    }

    // Split handles: points of light where two bands meet
    const float splitHz[3] = { s.low, s.mid, s.high };
    for (int i = 0; i < 3; ++i)
    {
        const float x = edges[i + 1];
        const bool hot = i == dragging || i == hover;
        const auto blend = halationBandColour (i).interpolatedWith (halationBandColour (i + 1), 0.5f);
        juce::Path line; line.startNewSubPath (x, plot.getY()); line.lineTo (x, plot.getBottom());
        juce::Path dashed; const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (hot ? 1.6f : 1.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (t.text.withAlpha (hot ? 0.85f : 0.4f));
        g.fillPath (dashed);
        const auto knob = juce::Rectangle<float> (hot ? 14.0f : 11.0f, hot ? 14.0f : 11.0f).withCentre ({ x, plot.getY() + 1.0f });
        juce::Path dot; dot.addEllipse (knob);
        paintGlow (g, dot, blend, hot ? 12 : 9);
        paintGlow (g, dot, blend.interpolatedWith (juce::Colours::white, 0.3f), 4);
        g.setGradientFill (juce::ColourGradient (halationBandColour (i), knob.getX(), 0.0f, halationBandColour (i + 1), knob.getRight(), 0.0f, false));
        g.fillEllipse (knob);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillEllipse (knob.reduced (knob.getWidth() * 0.32f));
        g.setColour (t.text);
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
      solo (state, halation::ids::band (bandIndex, "solo"), "SOLO"),
      bypass (state, halation::ids::band (bandIndex, "bypass"), "BYPASS"),
      threshold (state, halation::ids::band (bandIndex, "threshold"), "THRESHOLD"),
      ratio (state, halation::ids::band (bandIndex, "ratio"), "RATIO"),
      makeup (state, halation::ids::band (bandIndex, "makeup"), "MAKEUP", true),
      attack (state, halation::ids::band (bandIndex, "attack"), "ATTACK"),
      release (state, halation::ids::band (bandIndex, "release"), "RELEASE"),
      excite (state, halation::ids::band (bandIndex, "excite"), "EXCITE")
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

void BandStrip::setBypassed (bool shouldBeDimmed)
{
    if (shouldBeDimmed == dimmed) return;
    dimmed = shouldBeDimmed;
    for (juce::Component* c : { (juce::Component*) &threshold, (juce::Component*) &ratio, (juce::Component*) &makeup,
                                (juce::Component*) &attack, (juce::Component*) &release, (juce::Component*) &excite,
                                (juce::Component*) &grMeter, (juce::Component*) &solo })
        c->setAlpha (dimmed ? 0.42f : 1.0f);
    repaint();
}

void BandStrip::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    paintPanel (g, getLocalBounds(), t);
    const auto col = halationBandColour (band).withMultipliedAlpha (dimmed ? 0.4f : 1.0f);
    // A strip of the band's light along the top, glowing down onto the glass
    const auto lightBar = getLocalBounds().toFloat().removeFromTop (3.0f).reduced (14.0f, 0.0f);
    juce::Path bar; bar.addRoundedRectangle (lightBar, 1.5f);
    paintGlow (g, bar, col, 12);
    paintGlow (g, bar, col.interpolatedWith (juce::Colours::white, 0.3f), 3);
    g.setColour (col);
    g.fillPath (bar);
    // ...brightest in the middle, like a filament
    const auto core = col.interpolatedWith (juce::Colours::white, 0.7f);
    juce::ColourGradient hotLine (core.withAlpha (0.0f), lightBar.getX(), 0.0f, core.withAlpha (0.0f), lightBar.getRight(), 0.0f, false);
    hotLine.addColour (0.5, core);
    g.setGradientFill (hotLine);
    g.fillRect (lightBar.withHeight (1.0f).withY (lightBar.getY() + 1.0f));

    g.setColour (col.withAlpha (dimmed ? 0.55f : 1.0f));
    g.setFont (styledFont (13.0f, true, false, 0.12f));
    g.drawText (juce::String (halation::bandName (band)).toUpperCase(), titleArea, juce::Justification::centredLeft);
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
PresetBar::PresetBar (HalationAudioProcessor& p) : processor (p)
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
    const auto& list = halation::factoryPresets();
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
            auto folder = HalationAudioProcessor::userPresetFolder();
            folder.createDirectory();
            folder.revealToUser();
        }
        refresh();
    });
}

void PresetBar::step (int delta)
{
    const int factory = (int) halation::factoryPresets().size();
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
    auto* w = new juce::AlertWindow ("Save preset", "Name your preset. It's saved with your other Halation presets.", juce::MessageBoxIconType::NoIcon, this);
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
HalationAudioProcessorEditor::HalationAudioProcessorEditor (HalationAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lnf);
    for (int b = 0; b < halation::numBands; ++b)
    {
        strips[(size_t) b] = std::make_unique<BandStrip> (processorRef.apvts, b);
        addAndMakeVisible (*strips[(size_t) b]);
    }
    for (juce::Component* c : { (juce::Component*) &presets, (juce::Component*) &spectrumView, (juce::Component*) &input,
                                (juce::Component*) &output, (juce::Component*) &mix, (juce::Component*) &inMeter, (juce::Component*) &outMeter })
        addAndMakeVisible (c);

    {
        // A small tile of fine, faint grain: light and dark specks at random
        grain = juce::Image (juce::Image::ARGB, 160, 160, true);
        juce::Random rng (2049);
        juce::Image::BitmapData px (grain, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < grain.getHeight(); ++y)
            for (int x = 0; x < grain.getWidth(); ++x)
            {
                const float n = rng.nextFloat();
                if (n < 0.28f) px.setPixelColour (x, y, juce::Colours::black.withAlpha (0.10f * rng.nextFloat()));
                else if (n > 0.80f) px.setPixelColour (x, y, juce::Colour (0xffffd8c4).withAlpha (0.035f * rng.nextFloat()));
            }
    }
    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

HalationAudioProcessorEditor::~HalationAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void HalationAudioProcessorEditor::refreshFromProcessor()
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
    const auto s = halation::orderedSplits (params.splits, sr);
    const float edges[5] = { 20.0f, s.low, s.mid, s.high, 20000.0f };
    std::array<float, halation::numBands> red {};
    for (int b = 0; b < halation::numBands; ++b)
    {
        auto& strip = *strips[(size_t) b];
        strip.grMeter.push (processorRef.meterReduction[(size_t) b].exchange (0.0f));
        red[(size_t) b] = strip.grMeter.getDisplayed();
        strip.setRange (edges[b], edges[b + 1]);
        strip.setBypassed (params.bands[(size_t) b].bypass);
    }
    inMeter.push (processorRef.meterInputPeak.exchange (0.0f));
    outMeter.push (processorRef.meterOutputPeak.exchange (0.0f));
    spectrumView.setState (params, inDb, outDb, fftSize, sr, red);
    presets.refresh();
}

void HalationAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& t = lnf.theme;
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    g.fillAll (t.background);

    // The light source: a warm bloom behind the spectrum that bleeds through the glass panels, hotter at its centre
    {
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::scale (1.0f, 0.42f, w * 0.5f, 150.0f));
        juce::ColourGradient bloom (halationRed().withAlpha (0.20f), w * 0.5f, 150.0f, halationRed().withAlpha (0.0f), w * 0.5f + w * 0.62f, 150.0f, true);
        bloom.addColour (0.35, juce::Colour (0xffff6a2a).withAlpha (0.09f));
        g.setGradientFill (bloom);
        g.fillRect (bounds.withHeight (h * 3.0f).withY (-h));
    }
    {
        juce::ColourGradient core (juce::Colour (0xffffb27a).withAlpha (0.10f), w * 0.5f, 110.0f, juce::Colour (0xffffb27a).withAlpha (0.0f), w * 0.5f + w * 0.28f, 110.0f, true);
        g.setGradientFill (core);
        g.fillRect (bounds);
    }
    // ...falling away into a dark vignette at the edges
    {
        juce::ColourGradient vignette (juce::Colours::black.withAlpha (0.0f), w * 0.5f, h * 0.45f, juce::Colours::black.withAlpha (0.55f), 0.0f, h * 1.05f, true);
        vignette.addColour (0.55, juce::Colours::black.withAlpha (0.0f));
        g.setGradientFill (vignette);
        g.fillRect (bounds);
    }

    // A thin strip of light along the top edge, running through the four band colours, with its own halo
    {
        juce::ColourGradient leak (halationBandColour (0), 0.0f, 0.0f, halationBandColour (3), w, 0.0f, false);
        leak.addColour (0.36, halationBandColour (1));
        leak.addColour (0.66, halationBandColour (2));
        juce::Path strip; strip.addRectangle (0.0f, 0.0f, w, 2.0f);
        paintGlow (g, strip, halationRed().withAlpha (0.7f), 10);
        g.setGradientFill (leak);
        g.fillPath (strip);
    }

    // Film grain, so the black reads as something photographed
    if (grain.isValid())
    {
        g.setTiledImageFill (grain, 0, 0, 1.0f);
        g.fillRect (bounds);
    }

    paintHeader (g, getWidth(), "Halation", LBAUDIO_PLUGIN_TYPE, t);
    paintPanel (g, globalPanel, t);
}

void HalationAudioProcessorEditor::resized()
{
    presets.setBounds (getWidth() - 20 - 380, 13, 380, 30);
    auto area = getLocalBounds();
    area.removeFromTop (56);
    area.reduce (16, 16);
    spectrumView.setBounds (area.removeFromTop (220));
    area.removeFromTop (12);

    globalPanel = area.removeFromRight (176);
    area.removeFromRight (12);
    const int gap = 12, w = (area.getWidth() - gap * (halation::numBands - 1)) / halation::numBands;
    for (int b = 0; b < halation::numBands; ++b)
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
