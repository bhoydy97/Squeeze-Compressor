#pragma once

#include <LBAudio/UI.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

// Prism's look: light through a prism. A deep graphite faceplate, and each band in its own colour of the
// spectrum, from warm red for the lows to sky blue for the highs.
lbaudio::ui::Theme prismTheme();
juce::Colour prismBandColour (int band);

// The spectrum, split into the four bands: the input faintly behind the output, each band's gain reduction
// hanging from the top, and the three split points as handles you drag.
class BandSpectrum final : public juce::Component
{
public:
    explicit BandSpectrum (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

    void setState (const prism::Params& p, const std::vector<float>& inDb, const std::vector<float>& outDb,
                   int fftSize, double sampleRate, const std::array<float, prism::numBands>& reductionDb)
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
    prism::Params params;
    std::vector<float> spectrumIn, spectrumOut;
    int size = 0, dragging = -1, hover = -1;
    double fs = 48000.0;
    std::array<float, prism::numBands> reduction {};
};

// One band's controls: name and range, Solo and Bypass, six knobs and its gain-reduction meter.
class BandStrip final : public juce::Component
{
public:
    BandStrip (juce::AudioProcessorValueTreeState& state, int bandIndex);
    ~BandStrip() override;

    void setRange (float loHz, float hiHz) { lo = loHz; hi = hiHz; repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;

    lbaudio::ui::LevelMeter grMeter { lbaudio::ui::LevelMeter::Kind::GainReduction, "GR" };

private:
    int band;
    float lo = 20.0f, hi = 20000.0f;
    lbaudio::ui::LookAndFeel lnf;
    lbaudio::ui::Switch solo, bypass;
    lbaudio::ui::LabelledKnob threshold, ratio, makeup, attack, release, excite;
    juce::Rectangle<int> titleArea;
};

// Preset name and menu, previous and next, and Save.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (PrismAudioProcessor& p);
    void refresh();
    void resized() override;

private:
    void showMenu();
    void step (int delta);
    void save();

    PrismAudioProcessor& processor;
    juce::TextButton prev { "<" }, next { ">" }, name, saveButton { "SAVE" };
    juce::String shownName;
};

class PrismAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrismAudioProcessorEditor (PrismAudioProcessor&);
    ~PrismAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 1200, defaultHeight = 640;

private:
    void timerCallback() override { refreshFromProcessor(); }

    PrismAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf { prismTheme() };

    PresetBar presets { processorRef };
    BandSpectrum spectrumView { processorRef.apvts };
    std::array<std::unique_ptr<BandStrip>, prism::numBands> strips;
    lbaudio::ui::LabelledKnob input  { processorRef.apvts, prism::ids::input,  "INPUT", true };
    lbaudio::ui::LabelledKnob output { processorRef.apvts, prism::ids::output, "OUTPUT", true };
    lbaudio::ui::LabelledKnob mix    { processorRef.apvts, prism::ids::mix,    "MIX" };
    lbaudio::ui::LevelMeter inMeter  { lbaudio::ui::LevelMeter::Kind::Level, "IN" };
    lbaudio::ui::LevelMeter outMeter { lbaudio::ui::LevelMeter::Kind::Level, "OUT" };
    juce::Rectangle<int> globalPanel;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> inDb = std::vector<float> ((size_t) fftSize / 2, -120.0f);
    std::vector<float> outDb = std::vector<float> ((size_t) fftSize / 2, -120.0f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismAudioProcessorEditor)
};
