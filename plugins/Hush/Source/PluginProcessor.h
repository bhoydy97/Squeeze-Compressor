#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <LBAudio/Scope.h>
#include "HushDSP.h"

namespace hush::ids
{
inline constexpr const char* frequency = "frequency";
inline constexpr const char* threshold = "threshold";
inline constexpr const char* range     = "range";
inline constexpr const char* mode      = "mode";
inline constexpr const char* listen    = "listen";
}

class HushAudioProcessor final : public juce::AudioProcessor
{
public:
    HushAudioProcessor();
    ~HushAudioProcessor() override = default;

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
    hush::Params currentParams() const;
    double getSampleRateForDisplay() const { return fs; }

    juce::AudioProcessorValueTreeState apvts;
    lbaudio::ScopeBuffer scope;                       // the input, for the spectrum
    std::atomic<float> meterReduction { 0.0f };       // dB, most since last read
    std::atomic<float> meterDetector { -120.0f };     // dBFS, most since last read

private:
    void updateFilters (float frequency);

    double fs = 44100.0;
    std::array<lbaudio::dsp::Biquad, 2> detHP, detLP, splitA, splitB;
    float currentFrequency = -1.0f;
    float env = 0.0f, attackCoeff = 0.5f, releaseCoeff = 0.001f;

    std::atomic<float>* pFrequency = nullptr; std::atomic<float>* pThreshold = nullptr; std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pMode = nullptr; std::atomic<float>* pListen = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HushAudioProcessor)
};
