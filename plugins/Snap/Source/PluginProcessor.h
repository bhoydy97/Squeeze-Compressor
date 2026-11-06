#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SnapDSP.h"

namespace snap::ids
{
inline constexpr const char* attack  = "attack";
inline constexpr const char* sustain = "sustain";
inline constexpr const char* speed   = "speed";
inline constexpr const char* output  = "output";
inline constexpr const char* mix     = "mix";
}

class SnapAudioProcessor final : public juce::AudioProcessor
{
public:
    SnapAudioProcessor();
    ~SnapAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    snap::Params currentParams() const;

    juce::AudioProcessorValueTreeState apvts;
    snap::FrameFeed feed;
    std::atomic<float> meterInputPeak { 0.0f }, meterOutputPeak { 0.0f };

private:
    double fs = 44100.0;
    snap::Follower attFast, susShort, susLong;
    snap::Riser attSlow;
    float gain = 1.0f, gainSmooth = 0.1f, mix = 1.0f, outGain = 1.0f, smoothCoeff = 0.001f;
    float currentSpeed = -1.0f;
    int frameLength = 120, frameCount = 0;
    snap::Frame frame;

    std::atomic<float>* pAttack = nullptr; std::atomic<float>* pSustain = nullptr; std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pOutput = nullptr; std::atomic<float>* pMix = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapAudioProcessor)
};
