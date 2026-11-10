#include "PluginEditor.h"

using namespace splinter::ui;
namespace ids = splinter::ids;

juce::Font splinter::ui::sans (float h, bool bold) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font splinter::ui::mono (float h) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)); }
juce::Colour splinter::ui::silver (int i)
{
    static const juce::uint32 c[] = { 0xffd9dcdf, 0xff9fa4aa, 0xffc3c7cb, 0xff80868c, 0xffe7e9eb, 0xffb0b5ba, 0xffcdd0d4, 0xff90959b };
    return juce::Colour (c[i & 7]);
}

namespace
{
juce::RangedAudioParameter* param (SplinterAudioProcessor& p, const juce::String& id) { return p.apvts.getParameter (id); }
void setParam (SplinterAudioProcessor& p, const juce::String& id, float value)
{
    if (auto* q = param (p, id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (value)); q->endChangeGesture(); }
}
float paramValue (SplinterAudioProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

void drawSpaced (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Font f, float kerning, juce::Justification j)
{
    g.setFont (f.withExtraKerningFactor (kerning));
    g.drawText (s, r, j, false);
}

juce::Path shuffleIcon (juce::Rectangle<float> r)
{
    // the Flip icon: two crossing arrows (24-unit grid)
    juce::Path p;
    p.startNewSubPath (16, 3); p.lineTo (21, 3); p.lineTo (21, 8);
    p.startNewSubPath (4, 20); p.lineTo (21, 3);
    p.startNewSubPath (21, 16); p.lineTo (21, 21); p.lineTo (16, 21);
    p.startNewSubPath (15, 15); p.lineTo (21, 21);
    p.startNewSubPath (4, 4); p.lineTo (9, 9);
    p.applyTransform (juce::AffineTransform::scale (r.getWidth() / 24.0f, r.getHeight() / 24.0f).translated (r.getX(), r.getY()));
    return p;
}

juce::String msText (double ms) { return ms >= 1000.0 ? juce::String (ms / 1000.0, 2) + " s" : juce::String (juce::roundToInt (ms)) + " ms"; }
} // namespace

//==============================================================================
Look::Look()
{
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, line2);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const float s = (float) juce::jmin (w, h) / 48.0f;
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f, r = 18.0f * s;
    const float ang = a0 + pos * (a1 - a0);
    juce::Path trackArc, valueArc;
    trackArc.addCentredArc (cx, cy, r, r, 0.0f, a0, a1, true);
    valueArc.addCentredArc (cx, cy, r, r, 0.0f, a0, juce::jmax (a0 + 0.01f, ang), true);
    const juce::PathStrokeType stroke (4.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (track); g.strokePath (trackArc, stroke);
    g.setColour (accent); g.strokePath (valueArc, stroke);
    g.setColour (cap); g.fillEllipse (cx - 12 * s, cy - 12 * s, 24 * s, 24 * s);
    g.setColour (capEdge); g.drawEllipse (cx - 12 * s, cy - 12 * s, 24 * s, 24 * s, 1.0f);
    g.setColour (text);
    const float px = cx + 8 * s * std::sin (ang), py = cy - 8 * s * std::cos (ang);
    g.fillEllipse (px - 2.2f * s, py - 2.2f * s, 4.4f * s, 4.4f * s);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& sl)
{
    if (sl.getName() == "swing") return;   // drawn by the window as a value box
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (track); g.fillRoundedRectangle ((float) x, cy - 2, (float) w, 4, 2);
    g.setColour (accent); g.fillRoundedRectangle ((float) x, cy - 2, pos - (float) x, 4, 2);
    g.setColour (text); g.fillEllipse (pos - 8, cy - 8, 16, 16);
}

//==============================================================================
Seg::Seg (juce::StringArray o, bool a, bool f) : options (std::move (o)), accentStyle (a), fill (f) {}

juce::Rectangle<float> Seg::cell (int i) const
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    if (fill)
    {
        const float w = r.getWidth() / (float) options.size();
        return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
    }
    std::vector<float> ws; float total = 0;
    for (auto& o : options) { ws.push_back (juce::GlyphArrangement::getStringWidth (sans (fontSize, true), o) + 18.0f); total += ws.back(); }
    const float scale = r.getWidth() / total;
    float x = r.getX();
    for (int k = 0; k < i; ++k) x += ws[(size_t) k] * scale;
    return { x, r.getY(), ws[(size_t) i] * scale, r.getHeight() };
}

void Seg::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (bg); g.fillRoundedRectangle (r, 10.0f);
    g.setColour (line2); g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    const int sel = selected ? selected() : -1;
    const bool green = accentStyle || (accentWhen && accentWhen());
    for (int i = 0; i < options.size(); ++i)
    {
        const auto c = cell (i);
        if (i == sel)
        {
            if (green)
            {
                g.setColour (accent.withAlpha (0.12f)); g.fillRoundedRectangle (c, 7.0f);
                g.setColour (accent.withAlpha (0.6f)); g.drawRoundedRectangle (c.reduced (0.5f), 7.0f, 1.0f);
                g.setColour (accent);
            }
            else { g.setColour (text); g.fillRoundedRectangle (c, 7.0f); g.setColour (bg); }
            g.setFont (sans (fontSize, true));
        }
        else { g.setColour (muted); g.setFont (sans (fontSize, true)); }
        g.drawText (options[i], c, juce::Justification::centred, false);
    }
}

void Seg::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < options.size(); ++i)
        if (cell (i).contains (e.position)) { if (onPick) onPick (i); repaint(); return; }
}

//==============================================================================
Knob::Knob (const juce::String& l) : label (l)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::attach (juce::RangedAudioParameter* p)
{
    att.reset();
    if (p == nullptr) return;
    att = std::make_unique<juce::SliderParameterAttachment> (*p, slider);
    slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    repaint();
}

// label, knob and value, kept together in the middle of the space
static juce::Rectangle<int> knobContent (juce::Rectangle<int> r) { return r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 102)); }

void Knob::resized()
{
    auto r = knobContent (getLocalBounds());
    r.removeFromTop (18); r.removeFromBottom (22);
    slider.setBounds (r.withSizeKeepingCentre (60, 60));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = knobContent (getLocalBounds()).toFloat();
    g.setColour (muted);
    drawSpaced (g, label, r.removeFromTop (14), sans (10.0f, true), 0.05f, juce::Justification::centred);
    g.setColour (text); g.setFont (mono (11.5f));
    const auto v = valueText ? valueText (slider.getValue()) : slider.getTextFromValue (slider.getValue());
    g.drawText (v, r.removeFromBottom (16), juce::Justification::centred, false);
}

//==============================================================================
void Pill::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool lit = on && on();
    const float rad = r.getHeight() * 0.5f;
    if (lit) { g.setColour (accent.withAlpha (0.1f)); g.fillRoundedRectangle (r, rad); g.setColour (accent.withAlpha (0.6f)); }
    else g.setColour (line2);
    g.drawRoundedRectangle (r, rad, 1.0f);
    g.setColour (lit ? accent : splinter::ui::text);
    g.setFont (sans (12.0f, true));
    g.drawText (text ? text() : juce::String(), r, juce::Justification::centred, false);
}

void RoundButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (line2); g.drawEllipse (r, 1.0f);
    const auto c = r.getCentre(); const float u = r.getWidth() * 0.12f;
    juce::Path p;
    if (symbol == "<") { p.startNewSubPath (c.x + u * 0.6f, c.y - u * 1.2f); p.lineTo (c.x - u * 0.6f, c.y); p.lineTo (c.x + u * 0.6f, c.y + u * 1.2f); }
    else if (symbol == ">") { p.startNewSubPath (c.x - u * 0.6f, c.y - u * 1.2f); p.lineTo (c.x + u * 0.6f, c.y); p.lineTo (c.x - u * 0.6f, c.y + u * 1.2f); }
    else { p.startNewSubPath (c.x - u, c.y - u); p.lineTo (c.x + u, c.y + u); p.startNewSubPath (c.x + u, c.y - u); p.lineTo (c.x - u, c.y + u); }
    g.setColour (text); g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

float FlipButton::press() const
{
    const double t = nowMs() - pressedAt;
    return t < 0 || t > 1200 ? 0.0f : (float) std::exp (-t / 260.0);
}

void FlipButton::paint (juce::Graphics& g)
{
    const float pr = press();
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    juce::Path shape; shape.addRoundedRectangle (r, 12.0f);
    juce::DropShadow (accent.withAlpha (0.14f + 0.4f * pr), (int) (18 + 22 * pr), {}).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1e21).interpolatedWith (juce::Colour (0xff1f3a2c), pr), 0, r.getY(),
                                             juce::Colour (0xff121416).interpolatedWith (juce::Colour (0xff183023), pr), 0, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 10, r.getRight() - 10);
    g.setColour (accent.withAlpha (0.6f + 0.4f * pr)); g.strokePath (shape, juce::PathStrokeType (1.0f));
    const auto f = sans (fontSize, true).withExtraKerningFactor (fontSize > 20 ? 0.18f : 0.08f);
    const float tw = juce::GlyphArrangement::getStringWidth (f, label), iw = withIcon ? fontSize * 0.92f : 0.0f, gap = withIcon ? 12.0f : 0.0f;
    const float x0 = r.getCentreX() - (tw + iw + gap) * 0.5f;
    if (withIcon)
    {
        g.setColour (accent);
        g.strokePath (shuffleIcon ({ x0, r.getCentreY() - iw * 0.5f, iw, iw }), juce::PathStrokeType (2.4f * iw / 24.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (white); g.setFont (f);
    g.drawText (label, juce::Rectangle<float> (x0 + iw + gap, r.getY(), tw + 4, r.getHeight()), juce::Justification::centredLeft, false);
}

//==============================================================================
void DragMidi::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (glow > 0.0f) { g.setColour (accent.withAlpha (0.12f * glow)); g.fillRoundedRectangle (r, 10.0f); }
    juce::Path outline; outline.addRoundedRectangle (r, 10.0f);
    juce::Path dashed; const float dashes[] = { 4.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (accent.withAlpha (0.55f + 0.45f * glow)); g.fillPath (dashed);
    const auto f = sans (13.0f, true);
    const juce::String s ("Drag MIDI into your DAW");
    const float tw = juce::GlyphArrangement::getStringWidth (f, s), iw = 16.0f, x0 = r.getCentreX() - (tw + iw + 10) * 0.5f, cy = r.getCentreY();
    juce::Path grid;
    for (float v : { 9.0f, 15.0f }) { grid.startNewSubPath (x0 + v * iw / 24, cy - iw / 2 + 4 * iw / 24); grid.lineTo (x0 + v * iw / 24, cy + iw / 2 - 4 * iw / 24); }
    for (float v : { 9.0f, 15.0f }) { grid.startNewSubPath (x0 + 4 * iw / 24, cy - iw / 2 + v * iw / 24); grid.lineTo (x0 + 20 * iw / 24, cy - iw / 2 + v * iw / 24); }
    g.setColour (accent); g.strokePath (grid, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (text); g.setFont (f);
    g.drawText (s, juce::Rectangle<float> (x0 + iw + 10, r.getY(), tw + 4, r.getHeight()), juce::Justification::centredLeft, false);
}

void DragMidi::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6 || proc.pattern() == nullptr) return;
    const auto f = proc.writePatternMidi();
    if (! f.existsAsFile()) return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
}

//==============================================================================
WaveView::WaveView (SplinterAudioProcessorEditor& e) : ed (e)
{
    mode.selected = [this] { return (int) ed.proc.chopMode(); };
    mode.onPick = [this] (int i) { setParam (ed.proc, ids::chopMode, (float) i); };
    play.text = [this] { return ed.proc.isPreviewing() ? juce::String ("Stop") : juce::String ("Play and tap"); };
    play.on = [this] { return ed.proc.isPreviewing(); };
    play.onClick = [this] { ed.proc.previewSample (! ed.proc.isPreviewing()); };
    addAndMakeVisible (mode);
    addChildComponent (play);
}

void WaveView::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (44).reduced (14, 0);
    head.removeFromRight (62);                       // the chop count
    mode.setBounds (head.removeFromRight (180).withSizeKeepingCentre (180, 32));
    head.removeFromRight (52);                       // "CHOP"
    play.setBounds (head.removeFromRight (112).withSizeKeepingCentre (112, 28));
    waveArea = r.reduced (1, 0).withTrimmedBottom (1);
}

void WaveView::cache()
{
    auto d = ed.proc.sampleData();
    if (d.get() == cachedFor) return;
    cachedFor = d.get();
    cols.clear();
    if (d == nullptr) return;
    const int w = juce::jmax (1, waveArea.getWidth() / 2);
    const int a = d->soundStart, b = juce::jmax (a + 1, d->soundEnd);
    std::vector<float> pk ((size_t) w), rm ((size_t) w);
    float maxPk = 1e-9f, maxRm = 1e-9f;
    for (int c = 0; c < w; ++c)
    {
        const int s0 = a + (int) ((double) (b - a) * c / w), s1 = juce::jmax (s0 + 1, a + (int) ((double) (b - a) * (c + 1) / w));
        float p = 0; double sq = 0;
        for (int i = s0; i < s1; ++i) { const float v = std::abs (d->sample.mono (i)); p = juce::jmax (p, v); sq += (double) v * v; }
        pk[(size_t) c] = p; rm[(size_t) c] = (float) std::sqrt (sq / (s1 - s0));
        maxPk = juce::jmax (maxPk, p); maxRm = juce::jmax (maxRm, rm[(size_t) c]);
    }
    for (int c = 0; c < w; ++c)
    {
        const float e = juce::jlimit (0.04f, 1.0f, 0.45f * pk[(size_t) c] / maxPk + 0.55f * std::pow (rm[(size_t) c] / maxRm, 1.3f));
        cols.push_back ({ e * (0.92f + 0.08f * std::sin ((float) c * 0.9f)), e * 0.92f });
    }
}

float WaveView::sampleToX (double s) const
{
    auto d = ed.proc.sampleData();
    if (d == nullptr) return (float) waveArea.getX();
    return (float) waveArea.getX() + (float) ((s - d->soundStart) / juce::jmax (1, d->soundEnd - d->soundStart)) * (float) waveArea.getWidth();
}

int WaveView::xToSample (float x) const
{
    auto d = ed.proc.sampleData();
    if (d == nullptr) return 0;
    return d->soundStart + (int) ((x - (float) waveArea.getX()) / (float) waveArea.getWidth() * (float) (d->soundEnd - d->soundStart));
}

void WaveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (panel); g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.018f));
    for (float y = (float) waveArea.getY(); y < (float) waveArea.getBottom(); y += 3.0f) g.fillRect ((float) waveArea.getX(), y, (float) waveArea.getWidth(), 1.0f);
    g.setColour (line); g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    g.drawHorizontalLine (44, 1.0f, r.getRight() - 1);

    auto d = ed.proc.sampleData();
    auto cs = ed.proc.chops();
    play.setVisible (ed.proc.chopMode() == splinter::ChopMode::Tap && d != nullptr);
    // the header row: the file, its tempo and key, how it's chopped
    auto head = juce::Rectangle<float> (14, 0, r.getWidth() - 28, 44);
    if (d == nullptr)
    {
        g.setColour (muted); g.setFont (sans (13.0f, true));
        g.drawText ("No sample loaded", head, juce::Justification::centredLeft, false);
        g.setFont (sans (14.0f));
        g.drawText ("Drop a sample here, or click Kit to load one", waveArea.toFloat(), juce::Justification::centred, false);
        juce::Path box; box.addRoundedRectangle (waveArea.toFloat().reduced (24, 20), 10.0f);
        juce::Path dashed; const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, box, dashes, 2);
        g.setColour (line2); g.fillPath (dashed);
        g.setColour (muted); g.setFont (sans (11.0f, true));
        drawSpaced (g, "CHOP", { (float) mode.getX() - 52, 0, 46, 44 }, sans (11.0f, true), 0.1f, juce::Justification::centredRight);
        return;
    }
    {
        auto h = head;
        g.setColour (text); g.setFont (sans (13.0f, true));
        const float nw = juce::jmin (260.0f, juce::GlyphArrangement::getStringWidth (sans (13.0f, true), d->name) + 4);
        g.drawText (d->name, h.removeFromLeft (nw), juce::Justification::centredLeft, true);
        h.removeFromLeft (12);
        auto chip = [&] (const juce::String& s)
        {
            const float w = juce::GlyphArrangement::getStringWidth (mono (11.0f), s) + 16;
            auto c = h.removeFromLeft (w).withSizeKeepingCentre (w, 22);
            g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRoundedRectangle (c, 8.0f);
            g.setColour (juce::Colour (0xffd9dcdf)); g.setFont (mono (11.0f)); g.drawText (s, c, juce::Justification::centred, false);
            h.removeFromLeft (12);
        };
        chip (juce::String (juce::roundToInt (d->tempo)) + " BPM");
        chip (d->key);
        const bool synced = ed.proc.hostBpm.load() > 0.0;
        g.setColour (synced ? accent : muted);
        g.fillEllipse (h.getX(), h.getCentreY() - 3, 6, 6);
        g.setColour (muted); g.setFont (sans (11.0f));
        g.drawText (synced ? "synced to your project" : "plays at the sample's tempo", h.withTrimmedLeft (12).withWidth (180), juce::Justification::centredLeft, false);
    }
    drawSpaced (g, "CHOP", { (float) mode.getX() - 52, 0, 46, 44 }, sans (11.0f, true), 0.1f, juce::Justification::centredRight);
    g.setColour (text); g.setFont (mono (12.0f));
    g.drawText (juce::String (cs->size()) + (cs->size() == 1 ? " chop" : " chops"), juce::Rectangle<float> ((float) mode.getRight() + 6, 0, 62, 44), juce::Justification::centredRight, false);

    // the waveform, chop by chop
    cache();
    const int sel = ed.selected();
    const auto wa = waveArea.toFloat();
    const float mid = wa.getY() + 88.0f * wa.getHeight() / 160.0f, amp = 66.0f * wa.getHeight() / 160.0f;
    for (int i = 0; i < cs->size(); ++i)
    {
        const auto [a, b] = cs->span (i);
        const float x0 = sampleToX (a), x1 = sampleToX (b);
        const bool isSel = i == sel;
        const float lit = ed.flash (i);
        const auto col = isSel ? accent : (lit > 0 ? silver (i).interpolatedWith (juce::Colours::white, 0.5f * lit) : silver (i));
        g.setColour ((isSel ? accent : juce::Colours::white).withAlpha (isSel ? 0.12f + 0.1f * lit : 0.025f + 0.09f * lit));
        g.fillRect (x0, wa.getY(), x1 - x0, wa.getHeight());
        juce::Path wave;
        const int c0 = juce::jlimit (0, (int) cols.size() - 1, (int) ((x0 - wa.getX()) / 2)), c1 = juce::jlimit (0, (int) cols.size() - 1, (int) std::ceil ((x1 - wa.getX()) / 2));
        wave.startNewSubPath (x0, mid);
        for (int c = c0; c <= c1; ++c) wave.lineTo (juce::jlimit (x0, x1, wa.getX() + (float) c * 2.0f), mid - cols[(size_t) c].first * amp);
        for (int c = c1; c >= c0; --c) wave.lineTo (juce::jlimit (x0, x1, wa.getX() + (float) c * 2.0f), mid + cols[(size_t) c].second * amp);
        wave.closeSubPath();
        g.setColour (col.withAlpha (isSel ? 0.9f : 0.62f + 0.3f * lit)); g.fillPath (wave);
        // the cut and its number
        g.setColour (isSel ? accent : juce::Colour (0xff3a3e43));
        g.fillRect (x0 - (isSel ? 1.0f : 0.5f), wa.getY(), isSel ? 2.0f : 1.0f, wa.getHeight());
        const auto tab = juce::Rectangle<float> (x0, wa.getY(), 20, 16);
        g.setColour (isSel ? accent : (lit > 0 ? line2.interpolatedWith (text, lit) : line2)); g.fillRect (tab);
        g.setColour (isSel || lit > 0.5f ? onAccent : juce::Colour (0xffc9cdd2)); g.setFont (sans (10.0f, true));
        g.drawText (juce::String (i + 1), tab, juce::Justification::centred, false);
    }
    // the playhead
    const double pp = ed.proc.playPos.load();
    if (pp >= 0.0)
    {
        const float x = sampleToX (pp);
        if (x >= wa.getX() && x <= wa.getRight()) { g.setColour (juce::Colours::white); g.fillRect (x - 0.75f, wa.getY(), 1.5f, wa.getHeight()); }
    }
    if (ed.proc.chopMode() == splinter::ChopMode::Tap)
    {
        g.setColour (muted.withAlpha (0.9f)); g.setFont (sans (11.0f));
        g.drawText ("Tap the pads as it plays, or click to place a chop · right-click removes one", wa.reduced (10, 6), juce::Justification::bottomRight, false);
    }
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    auto d = ed.proc.sampleData();
    if (d == nullptr || ! waveArea.contains (e.getPosition())) return;
    const int s = xToSample (e.position.x);
    if (ed.proc.chopMode() == splinter::ChopMode::Tap)
    {
        if (e.mods.isPopupMenu()) ed.proc.removeTapNear (s); else ed.proc.addTap (s);
        repaint(); return;
    }
    auto cs = ed.proc.chops();
    for (int i = cs->size() - 1; i >= 0; --i)
        if (s >= cs->span (i).first) { ed.select (i); ed.proc.padDown (i, 0.85f); ed.proc.padUp (i); break; }
}

//==============================================================================
void PadButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool sel = index == ed.selected();
    const float lit = ed.flash (index);
    auto cs = ed.proc.chops();
    const bool has = index < cs->size();
    if (lit > 0) { juce::Path p; p.addRoundedRectangle (r, 10.0f); juce::DropShadow (accent.withAlpha (0.45f * lit), (int) (10 + 22 * lit), {}).drawForPath (g, p); }
    g.setColour (lit > 0 ? padBg.interpolatedWith (juce::Colour (0xff1d3b2b), lit) : padBg); g.fillRoundedRectangle (r, 10.0f);
    g.setColour (sel ? accent : (lit > 0 ? padEdge.interpolatedWith (accent, 0.7f * lit) : padEdge));
    g.drawRoundedRectangle (r.reduced (sel ? 0.75f : 0.5f), 10.0f, sel ? 1.5f : 1.0f);
    // the light along the bottom
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip; clip.addRoundedRectangle (r, 10.0f); g.reduceClipRegion (clip);
        const bool on = sel || lit > 0.05f;
        g.setColour (on ? accent : juce::Colour (0xff33373c));
        g.fillRect (r.getX(), r.getBottom() - 3, r.getWidth(), 3.0f);
    }
    g.setColour (muted); g.setFont (sans (11.0f, true));
    g.drawText (juce::String (index + 1), r.reduced (8, 6), juce::Justification::topLeft, false);
    g.setFont (mono (10.0f));
    g.drawText (splinter::noteName (index), r.reduced (8, 6), juce::Justification::topRight, false);
    g.setColour (has ? text : muted.withAlpha (0.6f)); g.setFont (sans (12.0f, true));
    g.drawText (has ? "Chop " + juce::String (index + 1) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), r.reduced (8, 0).withTrimmedBottom (12), juce::Justification::bottomLeft, false);
}

void PadButton::mouseDown (const juce::MouseEvent& e)
{
    ed.select (index);
    if (ed.proc.chopMode() == splinter::ChopMode::Tap && ed.proc.isPreviewing())
    {
        ed.proc.addTap ((int) ed.proc.previewPosition());
        return;
    }
    // harder near the top of the pad
    const float vel = juce::jlimit (0.4f, 1.0f, 1.0f - 0.6f * e.position.y / (float) juce::jmax (1, getHeight()));
    ed.proc.padDown (index, vel);
}

void PadButton::mouseUp (const juce::MouseEvent&) { ed.proc.padUp (index); }

//==============================================================================
float Lanes::rowH() const { return locks ? 29.0f : juce::jlimit (16.0f, 34.0f, (float) getHeight() / 6.0f); }
juce::Rectangle<float> Lanes::lockBox (int row) const { return { 0.0f, (float) row * rowH(), 24.0f, 24.0f }; }

void Lanes::paint (juce::Graphics& g)
{
    auto p = ed.proc.pattern();
    const int steps = p != nullptr ? p->steps : 16;
    auto rows = p != nullptr ? p->rows() : std::vector<int> {};
    const auto lockedRows = ed.proc.lockedRows();
    const int maxRows = locks ? 8 : 6;
    if ((int) rows.size() > maxRows) rows.resize ((size_t) maxRows);
    const int shown = juce::jmax ((int) rows.size(), locks ? 0 : 6);
    const int cur = ed.proc.patternStep.load();
    const float h = rowH(), cellH = locks ? 22.0f : juce::jmax (12.0f, h * 0.62f);
    const float x0 = locks ? 56.0f : 24.0f, w = (float) getWidth() - x0, gap = 2.0f, cw = (w - gap * (float) (steps - 1)) / (float) steps;
    for (int ri = 0; ri < shown; ++ri)
    {
        const bool real = ri < (int) rows.size();
        const int pad = real ? rows[(size_t) ri] : -1;
        const bool lk = real && lockedRows.count (pad) > 0;
        const float y = (float) ri * h;
        if (locks && real)
        {
            auto b = lockBox (ri);
            const auto it = lockFlash.find (pad);
            const float hot = it != lockFlash.end() ? (float) juce::jlimit (0.0, 1.0, 1.0 - (ed.now() - it->second) / 0.6) : 0.0f;
            if (hot > 0) { juce::Path pp; pp.addRoundedRectangle (b, 6.0f); juce::DropShadow (accent.withAlpha (0.6f * hot), 12, {}).drawForPath (g, pp); }
            g.setColour (lk ? accent.withAlpha (0.16f) : juce::Colours::transparentBlack); g.fillRoundedRectangle (b, 6.0f);
            g.setColour (hot > 0 ? accent : line2); g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
            // a padlock, open or shut
            const auto c = b.getCentre();
            g.setColour (lk ? accent : juce::Colour (0xff5d6268));
            g.drawRoundedRectangle (c.x - 4.5f, c.y - 0.5f, 9.0f, 7.0f, 1.5f, 1.6f);
            juce::Path shackle;
            shackle.addCentredArc (c.x, c.y - 2.5f, 2.8f, 3.0f, 0.0f, -juce::MathConstants<float>::halfPi, lk ? juce::MathConstants<float>::halfPi : 0.4f, true);
            g.strokePath (shackle, juce::PathStrokeType (1.6f));
        }
        g.setColour (lk ? accent : (real && ! locks && pad == ed.selected() ? accent : muted));
        g.setFont (sans (locks ? 10.5f : 10.0f, true));
        g.drawText (real ? juce::String (pad + 1) : juce::String(), juce::Rectangle<float> (locks ? 32.0f : 0.0f, y, 22, cellH), juce::Justification::centredLeft, false);
        for (int s = 0; s < steps; ++s)
        {
            const auto cell = juce::Rectangle<float> (x0 + (float) s * (cw + gap), y, cw, cellH);
            const bool on = real && p->has (s, pad);
            const bool isCur = s == cur;
            juce::Colour c = on ? (isCur ? juce::Colours::white : (lk || (! locks && pad == ed.selected()) ? accent : juce::Colour (0xffc9cdd2)))
                                : (s % 4 == 0 ? track : stepOff);
            if (on && isCur) { juce::Path pp; pp.addRoundedRectangle (cell, 3.0f); juce::DropShadow (accent.withAlpha (0.9f), 8, {}).drawForPath (g, pp); }
            g.setColour (c); g.fillRoundedRectangle (cell, 3.0f);
            if (isCur) { g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawRoundedRectangle (cell.expanded (0.5f), 3.0f, 1.0f); }
        }
    }
    if (p == nullptr && ! locks)
    {
        g.setColour (muted); g.setFont (sans (11.0f));
        const juce::String msg ("Press FLIP for a pattern from your chops");
        const float mw = juce::GlyphArrangement::getStringWidth (sans (11.0f), msg) + 28;
        const auto box = getLocalBounds().toFloat().withSizeKeepingCentre (mw, 28);
        g.setColour (panel.withAlpha (0.92f)); g.fillRoundedRectangle (box, 8.0f);
        g.setColour (muted); g.drawText (msg, box, juce::Justification::centred, false);
    }
}

void Lanes::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    if (locks)
    {
        auto p = ed.proc.pattern();
        if (p == nullptr) return;
        auto rows = p->rows();
        for (int ri = 0; ri < (int) rows.size() && ri < 8; ++ri)
            if (lockBox (ri).contains (e.position))
            {
                const int pad = rows[(size_t) ri];
                ed.proc.setRowLocked (pad, ed.proc.lockedRows().count (pad) == 0);
                lockFlash[pad] = ed.now();
                repaint(); return;
            }
        return;
    }
    if (onClick) onClick();
}

//==============================================================================
FlipWindow::FlipWindow (SplinterAudioProcessorEditor& e) : lanes (e, true), drag (e.proc), ed (e)
{
    length.selected = [this] { return juce::roundToInt (paramValue (ed.proc, ids::flipLength)); };
    length.onPick = [this] (int i) { setParam (ed.proc, ids::flipLength, (float) i); };
    busy.setSliderStyle (juce::Slider::LinearHorizontal);
    busy.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    busy.onValueChange = [this] { repaint(); };
    busyAtt = std::make_unique<juce::SliderParameterAttachment> (*param (ed.proc, ids::flipBusy), busy);
    again.onClick = [this] { ed.pressFlipAgain(); };
    close.onClick = [this] { ed.setFlipWindowOpen (false); };
    prev.onClick = [this] { ed.proc.stepFlip (-1); repaint(); };
    next.onClick = [this] { ed.proc.stepFlip (1); repaint(); };
    for (juce::Component* c : { (juce::Component*) &lanes, (juce::Component*) &again, (juce::Component*) &drag, (juce::Component*) &length,
                                (juce::Component*) &busy, (juce::Component*) &close, (juce::Component*) &prev, (juce::Component*) &next })
        addAndMakeVisible (c);
}

void FlipWindow::resized()
{
    panelArea = juce::Rectangle<int> (320, 0, 560, getHeight());
    auto r = panelArea.reduced (20);
    auto title = r.removeFromTop (48);
    close.setBounds (title.removeFromRight (32).withSizeKeepingCentre (32, 32));
    r.removeFromTop (14);
    infoArea = r.removeFromTop (38);
    r.removeFromTop (14);
    auto cols = r.removeFromTop (58);
    auto left = cols.removeFromLeft ((cols.getWidth() - 12) / 2); cols.removeFromLeft (12);
    length.setBounds (left.withTrimmedTop (22).withHeight (34));
    busy.setBounds (cols.withTrimmedTop (26).withHeight (24));
    r.removeFromTop (14);
    checkArea = r.removeFromTop (24);
    r.removeFromTop (14);
    auto dragArea = r.removeFromBottom (48); r.removeFromBottom (14);
    auto nav = r.removeFromBottom (44); r.removeFromBottom (14);
    drag.setBounds (dragArea);
    again.setBounds (nav.removeFromRight (150));
    prev.setBounds (nav.removeFromLeft (32).withSizeKeepingCentre (32, 32));
    navLabel = nav.removeFromLeft (90);
    next.setBounds (nav.removeFromLeft (32).withSizeKeepingCentre (32, 32));
    lanes.setBounds (r.reduced (12).withTrimmedTop (24));
}

void FlipWindow::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff050607).withAlpha (0.62f));
    auto pa = panelArea.toFloat();
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 60, {}).drawForRectangle (g, panelArea);
    g.setColour (bg); g.fillRect (pa);
    g.setColour (line2); g.drawVerticalLine (panelArea.getX(), pa.getY(), pa.getBottom()); g.drawVerticalLine (panelArea.getRight() - 1, pa.getY(), pa.getBottom());
    auto r = pa.reduced (20);
    auto title = r.removeFromTop (48);
    g.setColour (accent);
    g.strokePath (shuffleIcon ({ title.getX(), title.getY() + 4, 20, 20 }), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (white);
    drawSpaced (g, "FLIP", { title.getX() + 30, title.getY(), 200, 28 }, sans (24.0f, true), 0.18f, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffa3a8ae)); g.setFont (sans (12.5f));
    g.drawText ("Turns your chops into a new pattern, in time with your track.", juce::Rectangle<float> (title.getX(), title.getY() + 30, 440, 18), juce::Justification::centredLeft, false);
    // where the pattern comes from
    auto d = ed.proc.sampleData(); auto cs = ed.proc.chops();
    const auto ia = infoArea.toFloat();
    g.setColour (panel); g.fillRoundedRectangle (ia, 10.0f); g.setColour (line); g.drawRoundedRectangle (ia.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (muted); g.setFont (mono (11.5f));
    g.drawText ("From " + juce::String (cs->size()) + " chops of " + (d != nullptr ? d->name : juce::String ("no sample")) + " · " + juce::String (juce::roundToInt (ed.proc.tempoForTiming()))
                    + " BPM · swing " + juce::String (juce::roundToInt (paramValue (ed.proc, ids::swing))) + " %",
                ia.reduced (12, 0), juce::Justification::centredLeft, true);
    g.setColour (muted);
    drawSpaced (g, "LENGTH", length.getBounds().toFloat().withY ((float) length.getY() - 22).withHeight (16), sans (11.0f, true), 0.1f, juce::Justification::centredLeft);
    drawSpaced (g, "BUSY", busy.getBounds().toFloat().withY ((float) busy.getY() - 26).withHeight (16), sans (11.0f, true), 0.1f, juce::Justification::centredLeft);
    g.setColour (text); g.setFont (mono (11.0f));
    g.drawText (juce::String (juce::roundToInt (busy.getValue())) + " %", busy.getBounds().toFloat().withY ((float) busy.getY() - 26).withHeight (16), juce::Justification::centredRight, false);
    // start every bar on chop 1
    const bool on1 = paramValue (ed.proc, ids::flipStart) > 0.5f;
    auto box = checkArea.toFloat().withWidth (18).withSizeKeepingCentre (18, 18);
    g.setColour (on1 ? accent : juce::Colours::transparentBlack); g.fillRoundedRectangle (box, 4.0f);
    g.setColour (on1 ? accent : line2); g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
    if (on1)
    {
        juce::Path tick; tick.startNewSubPath (box.getX() + 4.5f, box.getCentreY()); tick.lineTo (box.getX() + 7.8f, box.getBottom() - 5); tick.lineTo (box.getRight() - 4, box.getY() + 5);
        g.setColour (onAccent); g.strokePath (tick, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (juce::Colour (0xffc9cdd2)); g.setFont (sans (12.5f));
    g.drawText ("Start every bar on chop 1", checkArea.toFloat().withTrimmedLeft (28), juce::Justification::centredLeft, false);
    // the lanes' panel
    auto lp = lanes.getBounds().toFloat().expanded (12).withTrimmedTop (-24);
    g.setColour (panel); g.fillRoundedRectangle (lp, 12.0f); g.setColour (line); g.drawRoundedRectangle (lp.reduced (0.5f), 12.0f, 1.0f);
    g.setColour (muted); g.setFont (sans (11.0f));
    g.drawText ("Lock a row to keep it when you flip again", lp.reduced (12, 0).withHeight (34), juce::Justification::centredLeft, false);
    auto p = ed.proc.pattern();
    juce::StringArray bars; for (int b = 0; b < (p != nullptr ? p->steps / 16 : 2); ++b) bars.add ("bar " + juce::String (b + 1));
    g.setFont (mono (11.0f));
    g.drawText (bars.joinIntoString (" · "), lp.reduced (12, 0).withHeight (34), juce::Justification::centredRight, false);
    g.setColour (muted); g.setFont (sans (12.0f));
    g.drawText (ed.proc.flipCount() > 0 ? "Flip " + juce::String (ed.proc.flipIndex() + 1) + " of " + juce::String (ed.proc.flipCount()) : juce::String ("No flips yet"),
                navLabel.toFloat(), juce::Justification::centred, false);
}

void FlipWindow::mouseDown (const juce::MouseEvent& e)
{
    if (! panelArea.contains (e.getPosition())) { ed.setFlipWindowOpen (false); return; }
    if (checkArea.contains (e.getPosition()))
    {
        setParam (ed.proc, ids::flipStart, paramValue (ed.proc, ids::flipStart) > 0.5f ? 0.0f : 1.0f);
        repaint();
    }
}

//==============================================================================
SplinterAudioProcessorEditor::SplinterAudioProcessorEditor (SplinterAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    repeat.selected = [this] { return juce::roundToInt (paramValue (proc, ids::noteRepeat)) - 1; };
    repeat.onPick = [this] (int i) { const int cur = juce::roundToInt (paramValue (proc, ids::noteRepeat)); setParam (proc, ids::noteRepeat, cur == i + 1 ? 0.0f : (float) (i + 1)); };
    repeat.accentWhen = [this] { return proc.repeating.load(); };
    addAndMakeVisible (repeat);
    addAndMakeVisible (wave);
    for (int i = 0; i < splinter::numPads; ++i) { pads.push_back (std::make_unique<PadButton> (*this, i)); addAndMakeVisible (*pads.back()); }
    era.selected = [this] { return juce::roundToInt (paramValue (proc, ids::pad (selected(), "era"))); };
    era.onPick = [this] (int i) { setParam (proc, ids::pad (selected(), "era"), (float) i); };
    addAndMakeVisible (era);
    const char* names[] = { "PITCH", "START", "END", "ATTACK", "RELEASE", "FILTER", "RESONANCE", "DRIVE" };
    for (int k = 0; k < 8; ++k) { knobs[(size_t) k] = std::make_unique<Knob> (names[k]); addAndMakeVisible (*knobs[(size_t) k]); }
    reverse.text = [] { return juce::String ("Reverse"); };
    reverse.on = [this] { return paramValue (proc, ids::pad (selected(), "reverse")) > 0.5f; };
    reverse.onClick = [this] { setParam (proc, ids::pad (selected(), "reverse"), reverse.on() ? 0.0f : 1.0f); repaint(); };
    oneShot.text = [] { return juce::String ("One-shot"); };
    oneShot.on = [this] { return paramValue (proc, ids::pad (selected(), "oneshot")) > 0.5f; };
    oneShot.onClick = [this] { setParam (proc, ids::pad (selected(), "oneshot"), oneShot.on() ? 0.0f : 1.0f); repaint(); };
    choke.text = [this] { const int c = juce::roundToInt (paramValue (proc, ids::pad (selected(), "choke"))); return "Choke: " + (c == 0 ? juce::String ("off") : juce::String (c)); };
    choke.on = [this] { return juce::roundToInt (paramValue (proc, ids::pad (selected(), "choke"))) > 0; };
    choke.onClick = [this] { const int c = juce::roundToInt (paramValue (proc, ids::pad (selected(), "choke"))); setParam (proc, ids::pad (selected(), "choke"), (float) ((c + 1) % 5)); repaint(); };
    for (auto* c : { &reverse, &oneShot, &choke }) addAndMakeVisible (*c);
    flipButton.onClick = [this] { pressFlip(); };
    prev.onClick = [this] { proc.stepFlip (-1); repaint(); };
    next.onClick = [this] { proc.stepFlip (1); repaint(); };
    lanes.onClick = [this] { setFlipWindowOpen (true); };
    for (juce::Component* c : { (juce::Component*) &flipButton, (juce::Component*) &prev, (juce::Component*) &next, (juce::Component*) &lanes, (juce::Component*) &drag })
        addAndMakeVisible (c);
    swingSlider.setName ("swing");
    swingSlider.setSliderStyle (juce::Slider::LinearVertical);
    swingSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    swingSlider.onValueChange = [this] { repaint (swingArea.expanded (2)); };
    swingAtt = std::make_unique<juce::SliderParameterAttachment> (*param (proc, ids::swing), swingSlider);
    addAndMakeVisible (swingSlider);
    addChildComponent (flipWindow);
    flipButton.clock = [this] { return buttonClock ? buttonClock() : juce::Time::getMillisecondCounterHiRes() / 1000.0; };
    flipWindow.again.clock = flipButton.clock;
    setSize (defaultWidth, defaultHeight);
    attachPad (selected());
    startTimerHz (30);
}

SplinterAudioProcessorEditor::~SplinterAudioProcessorEditor() { setLookAndFeel (nullptr); }

void SplinterAudioProcessorEditor::attachPad (int pad)
{
    attachedPad = pad;
    const char* ctl[] = { "pitch", "start", "end", "attack", "release", "filter", "resonance", "drive" };
    for (int k = 0; k < 8; ++k) knobs[(size_t) k]->attach (param (proc, ids::pad (pad, ctl[k])));
    // Start and End read as times into the chop
    auto chopMs = [this, pad]
    {
        auto d = proc.sampleData(); auto cs = proc.chops();
        if (d == nullptr || pad >= cs->size()) return 0.0;
        const auto sp = cs->span (pad);
        return (sp.second - sp.first) * 1000.0 / d->sample.rate;
    };
    knobs[1]->valueText = [chopMs] (double v) { return msText (chopMs() * v / 100.0); };
    knobs[2]->valueText = [chopMs] (double v) { return msText (chopMs() * v / 100.0); };
    repaint();
}

void SplinterAudioProcessorEditor::select (int pad)
{
    proc.selectedPad.store (juce::jlimit (0, splinter::numPads - 1, pad));
    attachPad (selected());
}

float SplinterAudioProcessorEditor::flash (int pad) const
{
    const double dt = now() - proc.lastHit[(size_t) pad].load();
    return dt < 0 || dt > 0.6 ? 0.0f : (float) std::exp (-dt / 0.16);
}

void SplinterAudioProcessorEditor::setFlipWindowOpen (bool open)
{
    flipWindow.setVisible (open);
    if (open) flipWindow.toFront (false);
}

void SplinterAudioProcessorEditor::pressFlip() { proc.flip(); flipButton.flash(); repaint(); }
void SplinterAudioProcessorEditor::pressFlipAgain() { proc.flip(); flipWindow.again.flash(); flipWindow.repaint(); repaint(); }

void SplinterAudioProcessorEditor::pressLock (int pad, bool on)
{
    proc.setRowLocked (pad, on);
    flipWindow.lanes.lockFlash[pad] = now();
    flipWindow.repaint();
}

std::vector<std::pair<juce::String, juce::Rectangle<int>>> SplinterAudioProcessorEditor::layout() const
{
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> out;
    auto add = [&] (const juce::String& n, const juce::Component& c) { out.push_back ({ n, getLocalArea (&c, c.getLocalBounds()) }); };
    out.push_back ({ "kit", kitArea }); out.push_back ({ "swing", swingArea });
    out.push_back ({ "padPanel", padPanel }); out.push_back ({ "editPanel", editPanel }); out.push_back ({ "flipPanel", flipPanel });
    add ("repeat", repeat); add ("wave", wave); add ("mode", wave.mode); add ("play", wave.play);
    out.push_back ({ "waveArea", wave.waveArea + wave.getPosition() });
    for (int i = 0; i < (int) pads.size(); ++i) add ("pad" + juce::String (i + 1), *pads[(size_t) i]);
    add ("era", era);
    const char* names[] = { "pitch", "start", "end", "attack", "release", "filter", "resonance", "drive" };
    for (int k = 0; k < 8; ++k) add (names[k], *knobs[(size_t) k]);
    add ("reverse", reverse); add ("oneShot", oneShot); add ("choke", choke);
    add ("flip", flipButton); add ("prev", prev); add ("next", next); add ("lanes", lanes); add ("drag", drag);
    out.push_back ({ "flipWindowPanel", juce::Rectangle<int> (320, 0, 560, getHeight()) });
    add ("flipAgain", flipWindow.again); add ("flipWindowLanes", flipWindow.lanes); add ("flipWindowDrag", flipWindow.drag);
    return out;
}

void SplinterAudioProcessorEditor::showLoadMenu()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", proc.samplePath().existsAsFile() ? proc.samplePath().getParentDirectory() : juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile()) { proc.loadSample (fc.getResult()); attachPad (selected()); repaint(); }
    });
}

void SplinterAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (proc.loadSample (juce::File (f))) { attachPad (selected()); repaint(); break; }
}

void SplinterAudioProcessorEditor::refreshFromProcessor()
{
    if (attachedPad != selected()) attachPad (selected());
    wave.repaint();
    for (auto& p : pads) p->repaint();
    lanes.repaint();
    repeat.repaint();
    flipButton.repaint();
    if (flipWindow.isVisible()) { flipWindow.lanes.repaint(); flipWindow.again.repaint(); }
    drag.repaint();
    repaint (flipInfoArea);
    repaint (padHeader);
}

void SplinterAudioProcessorEditor::resized()
{
    // header, right to left
    repeat.setBounds (1013, 12, 167, 32);
    swingArea = { 855, 16, 49, 26 };
    swingSlider.setBounds (swingArea);
    kitArea = { 600, 13, 190, 30 };
    wave.setBounds (16, 68, 1168, 206);
    padPanel = { 16, 286, 416, 458 };
    editPanel = { 446, 286, 394, 458 };
    flipPanel = { 854, 286, 330, 458 };
    {
        auto r = padPanel.reduced (12);
        const int gap = 9, w = (r.getWidth() - 3 * gap) / 4, h = (r.getHeight() - 3 * gap) / 4;
        const int order[] = { 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3 };
        for (int k = 0; k < 16; ++k)
            pads[(size_t) order[k]]->setBounds (r.getX() + (k % 4) * (w + gap), r.getY() + (k / 4) * (h + gap), w, h);
    }
    {
        auto r = editPanel.reduced (14);
        padHeader = r.removeFromTop (20).expanded (2);
        r.removeFromTop (10);
        r.removeFromTop (20);           // "ERA"
        era.setBounds (r.removeFromTop (38));
        r.removeFromTop (12);
        auto bottom = r.removeFromBottom (32);
        const int pw = (bottom.getWidth() - 16) / 3;
        reverse.setBounds (bottom.removeFromLeft (pw)); bottom.removeFromLeft (8);
        oneShot.setBounds (bottom.removeFromLeft (pw)); bottom.removeFromLeft (8);
        choke.setBounds (bottom);
        r.removeFromBottom (10);
        auto grid = r;
        const int kw = grid.getWidth() / 4, kh = grid.getHeight() / 2;
        for (int k = 0; k < 8; ++k) knobs[(size_t) k]->setBounds (grid.getX() + (k % 4) * kw, grid.getY() + (k / 4) * kh, kw, kh);
    }
    {
        auto r = flipPanel.reduced (14);
        flipButton.setBounds (r.removeFromTop (64));
        r.removeFromTop (12);
        flipInfoArea = r.removeFromTop (28);
        auto navArea = flipInfoArea;
        next.setBounds (navArea.removeFromRight (28)); navArea.removeFromRight (6);
        prev.setBounds (navArea.removeFromRight (28));
        r.removeFromTop (12);
        drag.setBounds (r.removeFromBottom (46)); r.removeFromBottom (12);
        lanes.setBounds (r.reduced (11));
    }
    flipWindow.setBounds (getLocalBounds());
}

void SplinterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    // a fine green line along the top, fading out
    {
        juce::ColourGradient gr (accent, 0, 0, accent.withAlpha (0.0f), (float) getWidth() * 0.6f, 0, false);
        gr.addColour (0.47, accent.withAlpha (0.35f));
        g.setGradientFill (gr); g.fillRect (0, 0, getWidth(), 2);
    }
    // header
    g.setColour (white);
    drawSpaced (g, "SPLINTER", { 20, 2, 140, 53 }, sans (22.0f, true), 0.1f, juce::Justification::centredLeft);
    const float sw = juce::GlyphArrangement::getStringWidth (sans (22.0f, true).withExtraKerningFactor (0.1f), "SPLINTER");
    g.setColour (accent);
    drawSpaced (g, "SAMPLER", { 20 + sw + 14, 5, 90, 53 }, sans (11.0f, true), 0.12f, juce::Justification::centredLeft);
    const float x2 = 20 + sw + 14 + juce::GlyphArrangement::getStringWidth (sans (11.0f, true).withExtraKerningFactor (0.12f), "SAMPLER") + 14;
    g.setColour (line2); g.fillRect (x2, 20.0f, 1.0f, 18.0f);
    g.setColour (muted); g.setFont (sans (13.0f, true)); g.drawText ("LBAudio", juce::Rectangle<float> (x2 + 14, 2, 80, 53), juce::Justification::centredLeft, false);
    g.setColour (line); g.drawHorizontalLine (55, 0, (float) getWidth());
    // the kit (the sample) button
    {
        auto d = proc.sampleData();
        const auto k = kitArea.toFloat();
        g.setColour (panel); g.fillRoundedRectangle (k, 15.0f);
        g.setColour (line2); g.drawRoundedRectangle (k.reduced (0.5f), 15.0f, 1.0f);
        g.setColour (text); g.setFont (sans (12.0f, true));
        g.drawText (d != nullptr ? "Kit: " + d->name.upToLastOccurrenceOf (".", false, false) : juce::String ("Load a sample"), k.reduced (14, 0), juce::Justification::centred, true);
    }
    g.setColour (muted);
    drawSpaced (g, "SWING", { 798, 16, 50, 26 }, sans (11.0f, true), 0.1f, juce::Justification::centredRight);
    drawSpaced (g, "NOTE REPEAT", { 912, 16, 94, 26 }, sans (11.0f, true), 0.1f, juce::Justification::centredRight);
    {
        const auto s = swingArea.toFloat();
        g.setColour (panel); g.fillRoundedRectangle (s, 8.0f); g.setColour (line2); g.drawRoundedRectangle (s.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (text); g.setFont (mono (12.0f));
        g.drawText (juce::String (juce::roundToInt (paramValue (proc, ids::swing))) + " %", s, juce::Justification::centred, false);
    }
    // panels
    for (auto r : { padPanel, editPanel, flipPanel })
    {
        g.setColour (panel); g.fillRoundedRectangle (r.toFloat(), 12.0f);
        g.setColour (line); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 12.0f, 1.0f);
    }
    // the selected pad's controls
    {
        const int pad = selected();
        auto h = padHeader.toFloat().reduced (2, 0);
        const auto f = sans (13.0f, true).withExtraKerningFactor (0.12f);
        const juce::String a = "PAD " + juce::String (pad + 1) + " ", b = juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 CHOP ")) + juce::String (pad + 1);
        g.setFont (f); g.setColour (text);
        const float aw = juce::GlyphArrangement::getStringWidth (f, a);
        g.drawText (a, h.withWidth (aw + 2), juce::Justification::centredLeft, false);
        g.setColour (accent); g.drawText (b, h.withTrimmedLeft (aw), juce::Justification::centredLeft, false);
        const int c = juce::roundToInt (paramValue (proc, ids::pad (pad, "choke")));
        const bool os = paramValue (proc, ids::pad (pad, "oneshot")) > 0.5f;
        g.setColour (muted); g.setFont (sans (11.0f));
        g.drawText ((c > 0 ? "choke group " + juce::String (c) : juce::String ("no choke")) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + (os ? "one-shot" : "gate"),
                    h, juce::Justification::centredRight, false);
        g.setColour (muted);
        drawSpaced (g, "ERA", era.getBounds().toFloat().withY ((float) era.getY() - 20).withHeight (16), sans (11.0f, true), 0.1f, juce::Justification::centredLeft);
    }
    // Flip: which one this is
    {
        g.setColour (muted); g.setFont (sans (12.0f));
        const int n = proc.flipCount();
        auto p = proc.pattern();
        g.drawText (n > 0 ? "Flip " + juce::String (proc.flipIndex() + 1) + " of " + juce::String (n) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (p != nullptr ? p->steps / 16 : 2) + " bars"
                          : juce::String ("No flips yet"),
                    flipInfoArea.toFloat().withTrimmedRight (70), juce::Justification::centredLeft, false);
    }
}

void SplinterAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (kitArea.contains (e.getPosition())) showLoadMenu();
}
