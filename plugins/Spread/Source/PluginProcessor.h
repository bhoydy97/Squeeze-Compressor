#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SpreadDSP.h"

namespace spread::ids
{
inline constexpr const char* width    = "width";
inline constexpr const char* bassMono = "bassMono";
inline constexpr const char* balance  = "balance";
inline constexpr const char* output   = "output";
inline constexpr const char* mono     = "mono";
}

class SpreadAudioProcessor final : public juce::AudioProcessor
{
public:
    SpreadAudioProcessor();
    ~SpreadAudioProcessor() override = default;

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
    spread::Params currentParams() const;

    juce::AudioProcessorValueTreeState apvts;
    spread::StereoScope scope;                     // the output, for the vectorscope
    std::atomic<float> correlation { 1.0f };       // output phase correlation, -1..+1 (about 300 ms average)

private:
    void updateFilters (float hz);

    double fs = 44100.0;
    std::array<lbaudio::dsp::Biquad, 2> sideHP;
    float currentBassMono = -1.0f;
    float width = 1.0f, gainL = 1.0f, gainR = 1.0f, outGain = 1.0f, monoAmount = 0.0f, smoothCoeff = 0.001f;
    double sLR = 0.0, sLL = 0.0, sRR = 0.0, corrCoeff = 0.0001;

    std::atomic<float>* pWidth = nullptr; std::atomic<float>* pBassMono = nullptr; std::atomic<float>* pBalance = nullptr;
    std::atomic<float>* pOutput = nullptr; std::atomic<float>* pMono = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpreadAudioProcessor)
};
