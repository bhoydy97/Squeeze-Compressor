#pragma once

#include <LBAudio/UI.h>
#include "PluginProcessor.h"

// Orbit's look: "deep space". Violet sky with a starfield and nebula glows, lilac and aqua, knobs with a little
// orbiting planet, round soft panels and a centred title.
lbaudio::ui::Theme orbitTheme();

// The chorus voices as planets orbiting the input (the star): each ring is a voice's delay, each planet where
// its LFO is in the turn (lilac left, aqua right), swinging in and out with Depth.
class OrbitView final : public juce::Component
{
public:
    void setState (const orbit::Params& p, double lfoPhase, float inputLevel) { params = p; phase = lfoPhase; level = inputLevel; repaint(); }
    void paint (juce::Graphics&) override;

private:
    orbit::Params params;
    double phase = 0.0;
    float level = 0.0f;
};

class OrbitAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OrbitAudioProcessorEditor (OrbitAudioProcessor&);
    ~OrbitAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 980, defaultHeight = 500;

private:
    void timerCallback() override { refreshFromProcessor(); }

    OrbitAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf { orbitTheme() };

    OrbitView view;
    lbaudio::ui::LabelledKnob rate     { processorRef.apvts, orbit::ids::rate,     "RATE" };
    lbaudio::ui::LabelledKnob depth    { processorRef.apvts, orbit::ids::depth,    "DEPTH" };
    lbaudio::ui::LabelledKnob delay    { processorRef.apvts, orbit::ids::delay,    "DELAY" };
    lbaudio::ui::LabelledKnob feedback { processorRef.apvts, orbit::ids::feedback, "FEEDBACK" };
    lbaudio::ui::LabelledKnob width    { processorRef.apvts, orbit::ids::width,    "WIDTH" };
    lbaudio::ui::LabelledKnob tone     { processorRef.apvts, orbit::ids::tone,     "TONE" };
    lbaudio::ui::LabelledKnob mix      { processorRef.apvts, orbit::ids::mix,      "MIX" };
    lbaudio::ui::Segmented voices { processorRef.apvts, orbit::ids::voices };

    juce::Rectangle<int> leftPanel, rightPanel, voicesLabel;
    std::vector<juce::Point<float>> stars;
    std::vector<float> starSizes, starAlpha;
    float level = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrbitAudioProcessorEditor)
};
