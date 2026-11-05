#pragma once

#include <juce_dsp/juce_dsp.h>
#include <LBAudio/UI.h>
#include "PluginProcessor.h"

// Frequency-response graph with draggable band nodes and a live spectrum behind it.
class EqGraph final : public juce::Component
{
public:
    explicit EqGraph (CarveAudioProcessor& p) : proc (p) {}

    std::function<void (int)> onSelect;

    void setSelected (int b) { selected = b; repaint(); }
    void setState (const carve::Params& p, double sampleRate) { params = p; fs = sampleRate; repaint(); }
    void setSpectrum (const std::vector<float>& binsDb, int fftSize) { spectrum = binsDb; spectrumFftSize = fftSize; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr float minHz = 20.0f, maxHz = 20000.0f, rangeDb = 18.0f;

private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (14.0f).withTrimmedLeft (22.0f).withTrimmedBottom (16.0f); }
    float xForFreq (double f) const;
    double freqForX (float x) const;
    float yForDb (double db) const;
    double dbForY (float y) const;
    juce::Point<float> nodePos (int band) const;
    int hitNode (juce::Point<float> p) const;
    juce::RangedAudioParameter* param (const char* id) const { return *id ? proc.apvts.getParameter (id) : nullptr; }
    static void setParam (juce::RangedAudioParameter* p, float v) { if (p) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

    CarveAudioProcessor& proc;
    carve::Params params = carve::defaultParams();
    double fs = 48000.0;
    std::vector<float> spectrum;
    int spectrumFftSize = 0;
    int selected = carve::Bell2, dragging = -1, hover = -1;
};

class CarveAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CarveAudioProcessorEditor (CarveAudioProcessor&);
    ~CarveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 940, defaultHeight = 540;

private:
    void timerCallback() override { refreshFromProcessor(); }
    void selectBand (int b);
    void updateSpectrum();

    CarveAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf;

    EqGraph graph { processorRef };
    std::array<juce::TextButton, carve::NumBands> tabs;
    struct BandControls
    {
        std::unique_ptr<lbaudio::ui::Switch> on;
        std::unique_ptr<lbaudio::ui::LabelledKnob> freq, gain, q;
        std::unique_ptr<lbaudio::ui::Segmented> slope;
    };
    std::array<BandControls, carve::NumBands> controls;
    juce::Label slopeLabel;
    lbaudio::ui::LabelledKnob output { processorRef.apvts, carve::outputId, "OUTPUT", true };
    lbaudio::ui::LevelMeter inMeter  { lbaudio::ui::LevelMeter::Kind::Level, "IN" };
    lbaudio::ui::LevelMeter outMeter { lbaudio::ui::LevelMeter::Kind::Level, "OUT" };
    int selected = carve::Bell2;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> spectrumDb = std::vector<float> ((size_t) fftSize / 2, -120.0f);

    juce::Rectangle<int> graphPanel, meterPanel, stripPanel, helpArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarveAudioProcessorEditor)
};
