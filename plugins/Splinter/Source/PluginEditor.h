#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class SplinterAudioProcessorEditor;

// Splinter's look: graphite and silver, with green kept for what's selected or switched on.
namespace splinter::ui
{
inline const juce::Colour bg { 0xff0b0c0d }, panel { 0xff121315 }, line { 0xff1f2124 }, line2 { 0xff2b2e32 },
                          text { 0xffeceef0 }, muted { 0xff8e9399 }, accent { 0xff3ee08f }, onAccent { 0xff06150d },
                          track { 0xff24272b }, stepOff { 0xff18191c }, padBg { 0xff16181b }, padEdge { 0xff26292d },
                          cap { 0xff1a1c1f }, capEdge { 0xff33373c }, white { 0xfff3f4f5 };
juce::Font sans (float h, bool bold = false);
juce::Font mono (float h);
juce::Colour silver (int i);

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};

// A row of options; one is lit. Clicking the lit one again can turn it off (for Note Repeat).
class Seg final : public juce::Component
{
public:
    Seg (juce::StringArray options, bool accentStyle = false, bool fill = false);
    std::function<int()> selected;              // which is lit, -1 for none
    std::function<void (int)> onPick;
    std::function<bool()> accentWhen;           // lit in green (not white) while this says so
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::StringArray options;
    bool accentStyle, fill;
    float fontSize = 12.0f;
private:
    juce::Rectangle<float> cell (int i) const;
};

// A rotary knob for the selected pad, its name above and value below
class Knob final : public juce::Component
{
public:
    explicit Knob (const juce::String& label);
    void attach (juce::RangedAudioParameter* p);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;
    std::function<juce::String (double)> valueText;   // overrides the parameter's own text
private:
    juce::String label;
    std::unique_ptr<juce::SliderParameterAttachment> att;
};

// A rounded pill button: lit green when on
class Pill final : public juce::Component
{
public:
    std::function<juce::String()> text;
    std::function<bool()> on;
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
};

// A small round button with a symbol: "<", ">", "x", "undo" or "redo". Dimmed, and deaf to clicks, while enabled() says no.
class RoundButton final : public juce::Component
{
public:
    explicit RoundButton (juce::String s) : symbol (std::move (s)) {}
    std::function<void()> onClick;
    std::function<bool()> enabled;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick && (! enabled || enabled())) onClick(); }
private:
    juce::String symbol;
};

// The big dark button with a lit green edge (FLIP, FLIP AGAIN)
class FlipButton final : public juce::Component
{
public:
    FlipButton (juce::String t, float size, bool icon) : label (std::move (t)), fontSize (size), withIcon (icon) {}
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void flash() { pressedAt = nowMs(); repaint(); }
    double nowMs() const { return clock ? clock() * 1000.0 : juce::Time::getMillisecondCounterHiRes(); }
    float press() const;
    double pressedAt = -1e9;
    std::function<double()> clock;   // for the video: the time to measure the press flash against
private:
    juce::String label; float fontSize; bool withIcon;
};

// Drag this into the DAW: a .mid of the pattern
class DragMidi final : public juce::Component
{
public:
    explicit DragMidi (SplinterAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    float glow = 0.0f;
private:
    SplinterAudioProcessor& proc;
    bool dragging = false;
};

// The sample: its chops, the one selected, the ones playing, the playhead
class WaveView final : public juce::Component
{
public:
    explicit WaveView (::SplinterAudioProcessorEditor& e);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::Rectangle<int> waveArea;
    Seg mode { { "On the hits", "Even", "Tap" } };
    Pill play;
    RoundButton undo { "undo" }, redo { "redo" };   // Tap mode: take back a chop, or put it back
private:
    int xToSample (float x) const;
    float sampleToX (double s) const;
    void cache();
    ::SplinterAudioProcessorEditor& ed;
    const SampleData* cachedFor = nullptr;
    std::vector<std::pair<float, float>> cols;
};

class PadButton final : public juce::Component
{
public:
    PadButton (::SplinterAudioProcessorEditor& e, int pad) : ed (e), index (pad) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    ::SplinterAudioProcessorEditor& ed;
    int index;
};

// Flip's lanes: one row per pad the pattern uses, its steps, the step playing. Lock buttons in the Flip window.
class Lanes final : public juce::Component
{
public:
    Lanes (::SplinterAudioProcessorEditor& e, bool withLocks) : ed (e), locks (withLocks) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onClick;
    std::map<int, double> lockFlash;   // pad -> when its lock was last pressed
private:
    juce::Rectangle<float> lockBox (int row) const;
    float rowH() const;
    ::SplinterAudioProcessorEditor& ed;
    bool locks;
};

class FlipWindow final : public juce::Component
{
public:
    explicit FlipWindow (::SplinterAudioProcessorEditor& e);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    Lanes lanes;
    FlipButton again { "FLIP AGAIN", 15.0f, false };
    DragMidi drag;
private:
    ::SplinterAudioProcessorEditor& ed;
    Seg length { { "1 bar", "2 bars", "4 bars" }, false, true };
    juce::Slider busy;
    RoundButton close { "x" }, prev { "<" }, next { ">" };
    std::unique_ptr<juce::SliderParameterAttachment> busyAtt;
    juce::Rectangle<int> panelArea, checkArea, infoArea, navLabel;
};
} // namespace splinter::ui

class SplinterAudioProcessorEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit SplinterAudioProcessorEditor (SplinterAudioProcessor&);
    ~SplinterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;   // Cmd/Ctrl+Z and Shift+Cmd/Ctrl+Z (or Ctrl+Y) in Tap mode
    void undoChop();                                    // as the undo and redo buttons do
    void redoChop();

    // Shared with the parts of the window
    SplinterAudioProcessor& proc;
    int selected() const { return proc.selectedPad.load(); }
    void select (int pad);
    float flash (int pad) const;            // 1 just hit, falling to 0
    double now() const { return proc.audioTime.load(); }
    void setFlipWindowOpen (bool open);
    bool flipWindowOpen() const { return flipWindow.isVisible(); }
    void pressFlip();                       // as the FLIP button does
    void pressFlipAgain();
    std::function<double()> buttonClock;    // what the FLIP buttons' flash is timed by (wall clock in a DAW, audio time in the video)
    splinter::ui::DragMidi& dragBox() { return drag; }
    void pressLock (int pad, bool on);      // as a lock in the Flip window does
    // Where each part of the window is, in window coordinates (for documentation and the promo)
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> layout() const;

    static constexpr int defaultWidth = 1200, defaultHeight = 760;

private:
    void timerCallback() override { refreshFromProcessor(); }
    void showLoadMenu();

    splinter::ui::Look look;
    juce::Rectangle<int> kitArea, swingArea, padPanel, editPanel, flipPanel, flipInfoArea, padHeader;
    splinter::ui::Seg repeat { { "1/8", "1/16", "1/16T", "1/32" } };
    splinter::ui::WaveView wave { *this };
    std::vector<std::unique_ptr<splinter::ui::PadButton>> pads;
    splinter::ui::Seg era { { "Clean", "12-bit grit", "Vintage 26k", "Dusty" }, true, true };
    std::array<std::unique_ptr<splinter::ui::Knob>, 8> knobs;
    splinter::ui::Pill reverse, oneShot, choke;
    splinter::ui::FlipButton flipButton { "FLIP", 24.0f, true };
    splinter::ui::RoundButton prev { "<" }, next { ">" };
    splinter::ui::Lanes lanes { *this, false };
    splinter::ui::DragMidi drag { proc };
    splinter::ui::FlipWindow flipWindow { *this };
    juce::Slider swingSlider;
    std::unique_ptr<juce::SliderParameterAttachment> swingAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    int attachedPad = -1;
    void attachPad (int pad);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplinterAudioProcessorEditor)
};
