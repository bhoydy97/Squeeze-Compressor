#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <LBAudio/Loudness.h>
#include "CeilingDSP.h"

namespace ceiling::ids
{
inline constexpr const char* gain     = "gain";
inline constexpr const char* ceiling  = "ceiling";
inline constexpr const char* release  = "release";
inline constexpr const char* truePeak = "truePeak";
}

// One point of the history graph: peaks over a short stretch, before and after limiting
struct HistoryFrame
{
    std::atomic<float> driven { 0.0f }, output { 0.0f };
};

class CeilingAudioProcessor final : public juce::AudioProcessor
{
public:
    CeilingAudioProcessor();
    ~CeilingAudioProcessor() override = default;

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
    ceiling::Params currentParams() const;

    juce::AudioProcessorValueTreeState apvts;

    // Meters (written by the audio thread, read by the editor)
    std::atomic<float> meterOutputPeak { 0.0f }, meterGainReductionDb { 0.0f };
    std::atomic<float> momentaryLufs { -100.0f }, shortTermLufs { -100.0f }, integratedLufs { -100.0f };
    std::atomic<float> truePeakMax { 0.0f };          // linear, since the last reset
    std::atomic<int> clampedSamples { 0 };            // diagnostics: the safety clamp should never be needed
    std::atomic<bool> resetMeasurementsRequested { false };

    static constexpr int historySize = 2048, historyFrameSamples = 256;
    std::array<HistoryFrame, historySize> history;
    std::atomic<uint32_t> historyWritten { 0 };

private:
    void pushHistory (float driven, float output);

    ceiling::Limiter limiter;
    lbaudio::LoudnessMeter loudness;
    ceiling::TruePeakMeter truePeakMeter;
    float frameDriven = 0.0f, frameOutput = 0.0f;
    int frameFill = 0;

    std::atomic<float>* pGain = nullptr; std::atomic<float>* pCeiling = nullptr;
    std::atomic<float>* pRelease = nullptr; std::atomic<float>* pTruePeak = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeilingAudioProcessor)
};
