#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "HazeDSP.h"

namespace haze::ids
{
inline constexpr const char* mix        = "mix";
inline constexpr const char* preDelay   = "preDelay";
inline constexpr const char* size       = "size";
inline constexpr const char* decay      = "decay";
inline constexpr const char* damping    = "damping";
inline constexpr const char* lowCut     = "lowCut";
inline constexpr const char* width      = "width";
inline constexpr const char* modulation = "modulation";
}

class HazeAudioProcessor final : public juce::AudioProcessor
{
public:
    HazeAudioProcessor();
    ~HazeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    haze::Params currentParams() const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> meterInputPeak { 0.0f }, meterOutputPeak { 0.0f };

private:
    haze::Reverb reverb;

    std::atomic<float>* pMix = nullptr; std::atomic<float>* pPreDelay = nullptr; std::atomic<float>* pSize = nullptr;
    std::atomic<float>* pDecay = nullptr; std::atomic<float>* pDamping = nullptr; std::atomic<float>* pLowCut = nullptr;
    std::atomic<float>* pWidth = nullptr; std::atomic<float>* pModulation = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HazeAudioProcessor)
};
