#pragma once

#include <LBAudio/UI.h>
#include "PluginProcessor.h"

// Drift's look: "sunset tape". Deep maroon, glowing orange and cream, soft halo knobs, italic title.
lbaudio::ui::Theme driftTheme();

// The echo trail: every repeat on a timeline (left above the line, right below), fading as the feedback and
// the loop filters wear it down. A tape-head line sweeps out from the last hit and lights each echo as it plays.
class EchoTrail final : public juce::Component
{
public:
    struct State
    {
        drift::Params params;
        double bpm = 120.0, secondsSinceHit = 1000.0;
        float duckDb = 0.0f, wobblePhase = 0.0f;
    };
    void setState (const State& s) { state = s; repaint(); }
    void paint (juce::Graphics&) override;

private:
    State state;
};

class DriftAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DriftAudioProcessorEditor (DriftAudioProcessor&);
    ~DriftAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 1000, defaultHeight = 460;

private:
    void timerCallback() override { refreshFromProcessor(); }

    DriftAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf { driftTheme() };

    EchoTrail trail;
    lbaudio::ui::Segmented mode { processorRef.apvts, drift::ids::mode };
    lbaudio::ui::Switch sync { processorRef.apvts, drift::ids::sync, "SYNC" };
    lbaudio::ui::LabelledKnob time     { processorRef.apvts, drift::ids::time,     "TIME" };
    lbaudio::ui::LabelledKnob division { processorRef.apvts, drift::ids::division, "DIVISION" };
    lbaudio::ui::LabelledKnob feedback { processorRef.apvts, drift::ids::feedback, "FEEDBACK" };
    lbaudio::ui::LabelledKnob lowCut   { processorRef.apvts, drift::ids::lowCut,   "LOW CUT" };
    lbaudio::ui::LabelledKnob highCut  { processorRef.apvts, drift::ids::highCut,  "HIGH CUT" };
    lbaudio::ui::LabelledKnob wobble   { processorRef.apvts, drift::ids::wobble,   "WOBBLE" };
    lbaudio::ui::LabelledKnob duck     { processorRef.apvts, drift::ids::duck,     "DUCK" };
    lbaudio::ui::LabelledKnob mix      { processorRef.apvts, drift::ids::mix,      "MIX" };

    juce::Rectangle<int> controlPanel, modeLabel;
    float wobblePhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftAudioProcessorEditor)
};
