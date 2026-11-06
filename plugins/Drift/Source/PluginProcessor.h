#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DriftDSP.h"

namespace drift::ids
{
inline constexpr const char* time     = "time";
inline constexpr const char* sync     = "sync";
inline constexpr const char* division = "division";
inline constexpr const char* feedback = "feedback";
inline constexpr const char* mode     = "mode";
inline constexpr const char* lowCut   = "lowCut";
inline constexpr const char* highCut  = "highCut";
inline constexpr const char* wobble   = "wobble";
inline constexpr const char* duck     = "duck";
inline constexpr const char* mix      = "mix";
}

class DriftAudioProcessor final : public juce::AudioProcessor
{
public:
    DriftAudioProcessor();
    ~DriftAudioProcessor() override = default;

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
    drift::Params currentParams() const;

    juce::AudioProcessorValueTreeState apvts;

    // For the display
    std::atomic<float> displayBpm { 120.0f }, duckNowDb { 0.0f }, inputPeak { 0.0f };
    std::atomic<double> secondsSinceHit { 1000.0 };

private:
    void updateFilters (const drift::Params& p);

    double fs = 44100.0;
    std::array<drift::DelayLine, 2> lines;
    std::array<lbaudio::dsp::Biquad, 2> loopLow, loopHigh;
    float currentLowCut = -1.0f, currentHighCut = -1.0f;

    double delaySamples = 0.0, delayCoeff = 0.001;
    double wowPhase = 0.0, flutterPhase = 0.0;
    float env = 0.0f, envAttack = 0.0f, envRelease = 0.0f;
    float hitFast = 0.0f, hitSlow = 0.0f, hitFastRel = 0.0f, hitSlowAtt = 0.0f;
    double sinceHit = 1000.0;
    float duckGain = 1.0f, mix = 0.3f, smoothCoeff = 0.001f;
    float wobble = 0.15f, feedback = 0.4f;

    std::atomic<float>* pTime = nullptr; std::atomic<float>* pSync = nullptr; std::atomic<float>* pDivision = nullptr;
    std::atomic<float>* pFeedback = nullptr; std::atomic<float>* pMode = nullptr; std::atomic<float>* pLowCut = nullptr;
    std::atomic<float>* pHighCut = nullptr; std::atomic<float>* pWobble = nullptr; std::atomic<float>* pDuck = nullptr;
    std::atomic<float>* pMix = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftAudioProcessor)
};
