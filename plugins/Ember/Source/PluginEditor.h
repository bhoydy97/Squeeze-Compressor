#pragma once

#include <LBAudio/UI.h>
#include "PluginProcessor.h"

// The saturation curve (input amplitude across, output up), with the live input level riding on it.
class SaturationCurve final : public juce::Component
{
public:
    void setState (const ember::Params& p, float inputPeak) { params = p; level = inputPeak; repaint(); }
    void paint (juce::Graphics&) override;

private:
    ember::Params params;
    float level = 0.0f;
};

class EmberAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EmberAudioProcessorEditor (EmberAudioProcessor&);
    ~EmberAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 940, defaultHeight = 380;

private:
    void timerCallback() override { refreshFromProcessor(); }

    EmberAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf;

    SaturationCurve curve;
    lbaudio::ui::Segmented character { processorRef.apvts, ember::ids::mode };
    lbaudio::ui::Switch autoGain { processorRef.apvts, ember::ids::autoGain, "AUTO GAIN" };
    lbaudio::ui::LabelledKnob drive  { processorRef.apvts, ember::ids::drive,  "DRIVE" };
    lbaudio::ui::LabelledKnob tone   { processorRef.apvts, ember::ids::tone,   "TONE", true };
    lbaudio::ui::LabelledKnob mix    { processorRef.apvts, ember::ids::mix,    "MIX" };
    lbaudio::ui::LabelledKnob output { processorRef.apvts, ember::ids::output, "OUTPUT", true };
    lbaudio::ui::LevelMeter inMeter  { lbaudio::ui::LevelMeter::Kind::Level, "IN" };
    lbaudio::ui::LevelMeter outMeter { lbaudio::ui::LevelMeter::Kind::Level, "OUT" };

    juce::Rectangle<int> controlPanel, meterPanel, characterLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberAudioProcessorEditor)
};
