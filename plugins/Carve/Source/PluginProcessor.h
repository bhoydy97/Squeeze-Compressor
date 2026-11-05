#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <LBAudio/Scope.h>
#include "CarveDSP.h"

namespace carve
{
// Parameter IDs per band. Empty strings mean the band has no such control.
struct BandIds { const char* on; const char* freq; const char* gain; const char* q; const char* slope; const char* title; const char* shortName; };

inline const std::array<BandIds, NumBands>& bandIds()
{
    static const std::array<BandIds, NumBands> ids {{
        { "hpOn", "hpFreq", "",       "",     "hpSlope", "High-pass",  "HP" },
        { "",     "lsFreq", "lsGain", "",     "",        "Low shelf",  "LOW" },
        { "",     "b1Freq", "b1Gain", "b1Q",  "",        "Bell 1",     "1" },
        { "",     "b2Freq", "b2Gain", "b2Q",  "",        "Bell 2",     "2" },
        { "",     "b3Freq", "b3Gain", "b3Q",  "",        "Bell 3",     "3" },
        { "",     "hsFreq", "hsGain", "",     "",        "High shelf", "HIGH" },
        { "lpOn", "lpFreq", "",       "",     "lpSlope", "Low-pass",   "LP" },
    }};
    return ids;
}

inline constexpr const char* outputId = "output";
} // namespace carve

class CarveAudioProcessor final : public juce::AudioProcessor
{
public:
    CarveAudioProcessor();
    ~CarveAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { eq.reset(); }

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
    carve::Params currentParams() const;
    double currentSampleRate() const noexcept { return eq.sampleRate(); }

    juce::AudioProcessorValueTreeState apvts;
    lbaudio::ScopeBuffer scope;
    std::atomic<float> meterInputPeak { 0.0f }, meterOutputPeak { 0.0f };

private:
    struct BandPtrs { std::atomic<float>* on = nullptr; std::atomic<float>* freq = nullptr; std::atomic<float>* gain = nullptr; std::atomic<float>* q = nullptr; std::atomic<float>* slope = nullptr; };
    std::array<BandPtrs, carve::NumBands> ptrs;
    std::atomic<float>* pOutput = nullptr;
    carve::Equaliser eq;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarveAudioProcessor)
};
