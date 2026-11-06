#pragma once

#include <LBAudio/UI.h>
#include "PluginProcessor.h"

// Spread's look: "neon night". Pitch black, glowing magenta and cyan outlines, neon gradient knobs,
// wide-spaced title.
lbaudio::ui::Theme spreadTheme();

// Vectorscope: each output sample as a point (side across, mid up), so mono is a vertical line and wide stereo
// a broad cloud. Underneath, the phase correlation from -1 (out of phase) to +1 (mono).
class Vectorscope final : public juce::Component
{
public:
    void setState (const spread::Params& p, const float* left, const float* right, int n, float correlation);
    void paint (juce::Graphics&) override;

    static constexpr int points = 2048;

private:
    spread::Params params;
    std::array<float, points> l {}, r {};
    float corr = 1.0f, scale = 4.0f;
};

class SpreadAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpreadAudioProcessorEditor (SpreadAudioProcessor&);
    ~SpreadAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 940, defaultHeight = 480;

private:
    void timerCallback() override { refreshFromProcessor(); }

    SpreadAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf { spreadTheme() };

    Vectorscope scopeView;
    lbaudio::ui::LabelledKnob width    { processorRef.apvts, spread::ids::width,    "WIDTH" };
    lbaudio::ui::LabelledKnob bassMono { processorRef.apvts, spread::ids::bassMono, "BASS MONO" };
    lbaudio::ui::LabelledKnob balance  { processorRef.apvts, spread::ids::balance,  "BALANCE", true };
    lbaudio::ui::LabelledKnob output   { processorRef.apvts, spread::ids::output,   "OUTPUT", true };
    lbaudio::ui::Switch mono { processorRef.apvts, spread::ids::mono, "MONO" };

    juce::Rectangle<int> controlPanel;
    std::array<float, Vectorscope::points> bufL {}, bufR {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpreadAudioProcessorEditor)
};
