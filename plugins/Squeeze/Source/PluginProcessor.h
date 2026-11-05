#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CompressorDSP.h"

namespace ParamIDs
{
    inline constexpr const char* threshold  = "threshold";
    inline constexpr const char* ratio      = "ratio";
    inline constexpr const char* knee       = "knee";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* release    = "release";
    inline constexpr const char* makeup     = "makeup";
    inline constexpr const char* autoMakeup = "autoMakeup";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* scHpf      = "scHpf";
    inline constexpr const char* extSc      = "extSidechain";
}

class SqueezeAudioProcessor final : public juce::AudioProcessor
{
public:
    SqueezeAudioProcessor();
    ~SqueezeAudioProcessor() override = default;

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
    squeeze::CompressorParams currentParams() const;

    juce::AudioProcessorValueTreeState apvts;

    // Meter values written by the audio thread, read (and reset) by the editor.
    // Each holds the peak since the editor last looked.
    std::atomic<float> meterGainReductionDb { 0.0f };
    std::atomic<float> meterInputPeak  { 0.0f };
    std::atomic<float> meterOutputPeak { 0.0f };

private:
    static void storeMax (std::atomic<float>& target, float value) noexcept
    {
        auto prev = target.load (std::memory_order_relaxed);
        while (value > prev && ! target.compare_exchange_weak (prev, value, std::memory_order_relaxed)) {}
    }

    squeeze::Compressor compressor;

    std::atomic<float>* pThreshold  = nullptr;
    std::atomic<float>* pRatio      = nullptr;
    std::atomic<float>* pKnee       = nullptr;
    std::atomic<float>* pAttack     = nullptr;
    std::atomic<float>* pRelease    = nullptr;
    std::atomic<float>* pMakeup     = nullptr;
    std::atomic<float>* pAutoMakeup = nullptr;
    std::atomic<float>* pMix        = nullptr;
    std::atomic<float>* pScHpf      = nullptr;
    std::atomic<float>* pExtSc      = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SqueezeAudioProcessor)
};
