#pragma once

#include "PluginProcessor.h"
#include "UI.h"

class SqueezeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit SqueezeAudioProcessorEditor (SqueezeAudioProcessor&);
    ~SqueezeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Pulls meter values and parameters into the UI. Called by the timer
    // (and directly by the snapshot test tool).
    void refreshFromProcessor();

    static constexpr int defaultWidth = 940, defaultHeight = 420;

private:
    void timerCallback() override { refreshFromProcessor(); }

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const char* paramID, const juce::String& title, bool bipolar = false);
    void setupToggle (juce::ToggleButton&, const char* paramID,
                      std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>&);

    SqueezeAudioProcessor& processorRef;
    squeeze::ui::LookAndFeel lnf; // declared first so it outlives the components using it

    Knob threshold, ratio, knee, scHpf, attack, release, makeup, mix;
    juce::ToggleButton autoMakeupButton { "AUTO GAIN" }, extScButton { "EXT SIDECHAIN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoMakeupAttachment, extScAttachment;

    squeeze::ui::TransferCurve curve;
    squeeze::ui::LevelMeter inMeter  { squeeze::ui::LevelMeter::Kind::Level, "IN" };
    squeeze::ui::LevelMeter grMeter  { squeeze::ui::LevelMeter::Kind::GainReduction, "GR" };
    squeeze::ui::LevelMeter outMeter { squeeze::ui::LevelMeter::Kind::Level, "OUT" };

    juce::Rectangle<int> knobPanel, meterPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SqueezeAudioProcessorEditor)
};
