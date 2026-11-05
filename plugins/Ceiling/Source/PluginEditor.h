#pragma once

#include <LBAudio/UI.h>
#include "PluginProcessor.h"

// The last few seconds, scrolling: level going in (after Gain), level coming out, and the ceiling.
// The amber band between them is the gain reduction.
class HistoryGraph final : public juce::Component
{
public:
    static constexpr int points = 1100; // about 6 s at 48 kHz

    void pull (CeilingAudioProcessor& p);
    void setCeiling (float db) { ceilingDb = db; }
    void paint (juce::Graphics&) override;

private:
    std::vector<float> driven = std::vector<float> (points, 0.0f), output = std::vector<float> (points, 0.0f);
    int head = 0;          // next write position
    uint32_t readUpTo = 0;
    float ceilingDb = -1.0f;
};

// A labelled number (loudness readouts)
class Readout final : public juce::Component
{
public:
    Readout (juce::String titleText, juce::String unitText) : title (std::move (titleText)), unit (std::move (unitText)) {}
    void setValue (float v) { if (std::abs (v - value) > 0.001f) { value = v; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    juce::String title, unit;
    float value = -100.0f;
};

class CeilingAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CeilingAudioProcessorEditor (CeilingAudioProcessor&);
    ~CeilingAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 940, defaultHeight = 420;

private:
    void timerCallback() override { refreshFromProcessor(); }

    CeilingAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf;

    HistoryGraph graph;
    lbaudio::ui::LabelledKnob gainKnob    { processorRef.apvts, ceiling::ids::gain,    "GAIN" };
    lbaudio::ui::LabelledKnob ceilingKnob { processorRef.apvts, ceiling::ids::ceiling, "CEILING" };
    lbaudio::ui::LabelledKnob releaseKnob { processorRef.apvts, ceiling::ids::release, "RELEASE" };
    lbaudio::ui::Switch truePeak { processorRef.apvts, ceiling::ids::truePeak, "TRUE PEAK" };
    Readout momentary { "MOMENTARY", "LUFS" }, shortTerm { "SHORT-TERM", "LUFS" };
    Readout integrated { "INTEGRATED", "LUFS" }, peak { "TRUE PEAK", "dBTP" };
    juce::TextButton resetButton { "RESET" };
    lbaudio::ui::LevelMeter grMeter  { lbaudio::ui::LevelMeter::Kind::GainReduction, "GR" };
    lbaudio::ui::LevelMeter outMeter { lbaudio::ui::LevelMeter::Kind::Level, "OUT" };

    juce::Rectangle<int> controlPanel, meterPanel, loudnessLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeilingAudioProcessorEditor)
};
