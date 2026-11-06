#pragma once

#include <LBAudio/UI.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

// Hush's look: "studio paper". A light mint-white faceplate on a dotted grid, white panels, forest green and
// rose, hairline knobs, spaced-out light title.
lbaudio::ui::Theme hushTheme();

// The input spectrum (500 Hz - 20 kHz) with the band Hush listens to, glowing rose while it's working.
class SibilanceSpectrum final : public juce::Component
{
public:
    void setState (const hush::Params& p, const std::vector<float>& binsDb, int fftSize, double sampleRate, float reductionDb)
    {
        params = p; spectrum = binsDb; size = fftSize; fs = sampleRate; reduction = reductionDb; repaint();
    }
    void paint (juce::Graphics&) override;

private:
    hush::Params params;
    std::vector<float> spectrum;
    int size = 0;
    double fs = 48000.0;
    float reduction = 0.0f;
};

// The last few seconds: the sibilance level against the threshold above, and the reduction below.
class ReductionHistory final : public juce::Component
{
public:
    static constexpr int length = 240; // 8 s at 30 frames a second
    ReductionHistory() { detector.fill (-120.0f); }
    void push (float detectorDb, float reductionDb, float thresholdDb, float rangeDb);
    void paint (juce::Graphics&) override;

private:
    std::array<float, length> detector {}, reduction {};
    int write = 0;
    float threshold = -30.0f, range = 10.0f;
};

class HushAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HushAudioProcessorEditor (HushAudioProcessor&);
    ~HushAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 900, defaultHeight = 450;

private:
    void timerCallback() override { refreshFromProcessor(); }

    HushAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf { hushTheme() };

    SibilanceSpectrum spectrumView;
    ReductionHistory history;
    lbaudio::ui::Segmented mode { processorRef.apvts, hush::ids::mode };
    lbaudio::ui::Switch listen { processorRef.apvts, hush::ids::listen, "LISTEN" };
    lbaudio::ui::LabelledKnob frequency { processorRef.apvts, hush::ids::frequency, "FREQUENCY" };
    lbaudio::ui::LabelledKnob threshold { processorRef.apvts, hush::ids::threshold, "THRESHOLD" };
    lbaudio::ui::LabelledKnob range     { processorRef.apvts, hush::ids::range,     "RANGE" };
    lbaudio::ui::LevelMeter grMeter { lbaudio::ui::LevelMeter::Kind::GainReduction, "HUSH" };

    juce::Rectangle<int> controlPanel, modeLabel;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> spectrumDb = std::vector<float> ((size_t) fftSize / 2, -120.0f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HushAudioProcessorEditor)
};
