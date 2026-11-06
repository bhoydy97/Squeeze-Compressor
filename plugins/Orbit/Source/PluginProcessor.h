#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "OrbitDSP.h"

namespace orbit::ids
{
inline constexpr const char* rate     = "rate";
inline constexpr const char* depth    = "depth";
inline constexpr const char* voices   = "voices";
inline constexpr const char* delay    = "delay";
inline constexpr const char* width    = "width";
inline constexpr const char* feedback = "feedback";
inline constexpr const char* tone     = "tone";
inline constexpr const char* mix      = "mix";
}

class OrbitAudioProcessor final : public juce::AudioProcessor
{
public:
    OrbitAudioProcessor();
    ~OrbitAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    orbit::Params currentParams() const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<double> lfoPhaseForDisplay { 0.0 };
    std::atomic<float> inputPeak { 0.0f };

private:
    void updateTone (float hz);

    double fs = 44100.0;
    std::array<lbaudio::dsp::DelayLine, 2> lines;
    std::array<lbaudio::dsp::Biquad, 2> toneFilter;
    std::array<float, 2> lastWet {};
    float currentTone = -1.0f;
    double lfoPhase = 0.0;
    float depth = 0.5f, width = 0.8f, delayMs = 12.0f, feedback = 0.0f, mix = 0.5f, voiceGain = 1.0f, smoothCoeff = 0.001f;

    std::atomic<float>* pRate = nullptr; std::atomic<float>* pDepth = nullptr; std::atomic<float>* pVoices = nullptr;
    std::atomic<float>* pDelay = nullptr; std::atomic<float>* pWidth = nullptr; std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pTone = nullptr; std::atomic<float>* pMix = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrbitAudioProcessor)
};
