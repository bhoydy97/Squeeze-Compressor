#pragma once

#include <LBAudio/UI.h>
#include "PluginProcessor.h"

// Snap's look: "hazard hardware". A safety-yellow faceplate with black screens, chunky black knobs with yellow
// pointer bars, screws, hazard stripes and a heavy title.
lbaudio::ui::Theme snapTheme();

// The last 1.2 s of hits: input in grey, output on top, yellow where Snap boosts and red where it cuts, with the
// gain it applied along the bottom.
class HitScope final : public juce::Component
{
public:
    static constexpr int frames = 480;
    void setFrames (const snap::Frame* f) { std::copy (f, f + frames, data.begin()); repaint(); }
    void paint (juce::Graphics&) override;

private:
    std::array<snap::Frame, frames> data {};
};

class SnapAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SnapAudioProcessorEditor (SnapAudioProcessor&);
    ~SnapAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

    static constexpr int defaultWidth = 980, defaultHeight = 480;

private:
    void timerCallback() override { refreshFromProcessor(); }

    SnapAudioProcessor& processorRef;
    lbaudio::ui::LookAndFeel lnf { snapTheme() };

    HitScope scope;
    lbaudio::ui::LabelledKnob attack  { processorRef.apvts, snap::ids::attack,  "ATTACK", true };
    lbaudio::ui::LabelledKnob sustain { processorRef.apvts, snap::ids::sustain, "SUSTAIN", true };
    lbaudio::ui::LabelledKnob speed   { processorRef.apvts, snap::ids::speed,   "SPEED" };
    lbaudio::ui::LabelledKnob mix     { processorRef.apvts, snap::ids::mix,     "MIX" };
    lbaudio::ui::LabelledKnob output  { processorRef.apvts, snap::ids::output,  "OUTPUT", true };
    lbaudio::ui::LevelMeter inMeter  { lbaudio::ui::LevelMeter::Kind::Level, "IN" };
    lbaudio::ui::LevelMeter outMeter { lbaudio::ui::LevelMeter::Kind::Level, "OUT" };

    std::array<snap::Frame, HitScope::frames> buffer {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapAudioProcessorEditor)
};
