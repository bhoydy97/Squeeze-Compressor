#pragma once

#include <LBAudio/UI.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

// Halation's look: the glow that bleeds around a bright light on film. A warm black faceplate behind smoked glass, a
// red-orange halo around everything that's lit, and each band in its own colour of light, in spectrum order: red for
// the lows, amber, green, and violet for the highs.
lbaudio::ui::Theme halationTheme();
juce::Colour halationRed();
juce::Colour halationBandColour (int band);
void styleMenus (lbaudio::ui::LookAndFeel&); // pop-up menus in Halation's colours

// The spectrum, split into the four bands: the input faintly behind the output, each band's gain reduction
// hanging from the top, and the three split points as handles you drag.
class BandSpectrum final : public juce::Component
{
public:
    explicit BandSpectrum (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

    void setState (const halation::Params& p, const std::vector<float>& inDb, const std::vector<float>& outDb,
                   int fftSize, double sampleRate, const std::array<float, halation::numBands>& reductionDb)
    {
        params = p; spectrumIn = inDb; spectrumOut = outDb; size = fftSize; fs = sampleRate; reduction = reductionDb;
        repaint();
    }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float hzToX (double hz) const;
    double xToHz (float x) const;
    int handleAt (juce::Point<float> p) const;
    juce::RangedAudioParameter* splitParam (int i) const;

    juce::AudioProcessorValueTreeState& apvts;
    halation::Params params;
    std::vector<float> spectrumIn, spectrumOut;
    int size = 0, dragging = -1, hover = -1;
    double fs = 48000.0;
    std::array<float, halation::numBands> reduction {};
};

// The tone of a band's exciter: a pill showing its name, lit in the band's colour, that opens a menu of the tones
// with what each one does. Up and down step through them; Return or Space opens the menu.
class ToneMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToneMenu (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, juce::Colour colour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    int getSelected() const { return selected; }

private:
    void showMenu();
    void choose (int index);
    juce::Colour colour;
    int selected = 0;
    bool hover = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// One band's controls: name and range, Solo and Bypass, the compressor's five knobs and its gain-reduction meter,
// and the exciter: its tone, amount and mix.
class BandStrip final : public juce::Component
{
public:
    BandStrip (juce::AudioProcessorValueTreeState& state, int bandIndex);
    ~BandStrip() override;

    void setRange (float loHz, float hiHz)
    {
        if (juce::exactlyEqual (loHz, lo) && juce::exactlyEqual (hiHz, hi)) return;
        lo = loHz; hi = hiHz; repaint (titleArea);
    }
    // A bypassed band's lights go down: its knobs, meter and light bar dim, so the bands at work stand out
    void setBypassed (bool shouldBeDimmed);
    void paint (juce::Graphics&) override;
    void resized() override;

    lbaudio::ui::LevelMeter grMeter { lbaudio::ui::LevelMeter::Kind::GainReduction, "GR" };

private:
    int band;
    float lo = 20.0f, hi = 20000.0f;
    bool dimmed = false;
    lbaudio::ui::LookAndFeel lnf;
    lbaudio::ui::Switch solo, bypass;
    lbaudio::ui::LabelledKnob threshold, ratio, makeup, attack, release, excite, exciteMix;
    juce::Label toneLabel;
    ToneMenu tone;
    juce::Rectangle<int> titleArea, exciteHeader;
};

// Preset name and menu, previous and next, and Save.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (HalationAudioProcessor& p);
    void refresh();
    void resized() override;

private:
    void showMenu();
    void step (int delta);
    void save();

    HalationAudioProcessor& processor;
    juce::TextButton prev { "<" }, next { ">" }, name, saveButton { "SAVE" };
    juce::String shownName;
};

class HalationAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HalationAudioProcessorEditor (HalationAudioProcessor&);
    ~HalationAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 1200, defaultHeight = 720;

private:
    void timerCallback() override { refreshFromProcessor(); }

    HalationAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf { halationTheme() };

    PresetBar presets { processorRef };
    BandSpectrum spectrumView { processorRef.apvts };
    std::array<std::unique_ptr<BandStrip>, halation::numBands> strips;
    lbaudio::ui::LabelledKnob input  { processorRef.apvts, halation::ids::input,  "INPUT", true };
    lbaudio::ui::LabelledKnob output { processorRef.apvts, halation::ids::output, "OUTPUT", true };
    lbaudio::ui::LabelledKnob mix    { processorRef.apvts, halation::ids::mix,    "MIX" };
    lbaudio::ui::Switch ceiling      { processorRef.apvts, halation::ids::ceiling, "CEILING" };
    lbaudio::ui::LevelMeter inMeter  { lbaudio::ui::LevelMeter::Kind::Level, "IN" };
    lbaudio::ui::LevelMeter outMeter { lbaudio::ui::LevelMeter::Kind::Level, "OUT" };
    juce::Rectangle<int> globalPanel;
    juce::Image grain; // a fine film grain over the faceplate, made once

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> inDb = std::vector<float> ((size_t) fftSize / 2, -120.0f);
    std::vector<float> outDb = std::vector<float> ((size_t) fftSize / 2, -120.0f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HalationAudioProcessorEditor)
};
