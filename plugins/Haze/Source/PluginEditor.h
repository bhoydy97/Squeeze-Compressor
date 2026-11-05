#pragma once

#include <LBAudio/UI.h>
#include "PluginProcessor.h"

// How the tail dies away: level (dB) against time, for the lows and for the highs above the damping frequency.
class DecayGraph final : public juce::Component
{
public:
    void setState (const haze::Params& p) { params = p; repaint(); }
    void paint (juce::Graphics&) override;

private:
    haze::Params params;
};

class HazeAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HazeAudioProcessorEditor (HazeAudioProcessor&);
    ~HazeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 940, defaultHeight = 420;

private:
    void timerCallback() override { refreshFromProcessor(); }

    HazeAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf;

    DecayGraph graph;
    lbaudio::ui::LabelledKnob preDelay   { processorRef.apvts, haze::ids::preDelay,   "PRE-DELAY" };
    lbaudio::ui::LabelledKnob size       { processorRef.apvts, haze::ids::size,       "SIZE" };
    lbaudio::ui::LabelledKnob decay      { processorRef.apvts, haze::ids::decay,      "DECAY" };
    lbaudio::ui::LabelledKnob modulation { processorRef.apvts, haze::ids::modulation, "MODULATION" };
    lbaudio::ui::LabelledKnob damping    { processorRef.apvts, haze::ids::damping,    "DAMPING" };
    lbaudio::ui::LabelledKnob lowCut     { processorRef.apvts, haze::ids::lowCut,     "LOW CUT" };
    lbaudio::ui::LabelledKnob width      { processorRef.apvts, haze::ids::width,      "WIDTH" };
    lbaudio::ui::LabelledKnob mix        { processorRef.apvts, haze::ids::mix,        "MIX" };
    lbaudio::ui::LevelMeter inMeter  { lbaudio::ui::LevelMeter::Kind::Level, "IN" };
    lbaudio::ui::LevelMeter outMeter { lbaudio::ui::LevelMeter::Kind::Level, "OUT" };

    juce::Rectangle<int> controlPanel, meterPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HazeAudioProcessorEditor)
};
