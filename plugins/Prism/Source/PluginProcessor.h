#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <LBAudio/Scope.h>
#include "PrismDSP.h"
#include "PrismPresets.h"

namespace prism::ids
{
inline constexpr const char* split1 = "split1";
inline constexpr const char* split2 = "split2";
inline constexpr const char* split3 = "split3";
inline constexpr const char* input  = "input";
inline constexpr const char* output = "output";
inline constexpr const char* mix    = "mix";

// Per-band parameter IDs: "b1_threshold" ... "b4_bypass"
inline juce::String band (int b, const char* what) { return "b" + juce::String (b + 1) + "_" + what; }
inline const char* const bandControls[] = { "threshold", "ratio", "attack", "release", "makeup", "excite", "solo", "bypass" };
}

class PrismAudioProcessor final : public juce::AudioProcessor
{
public:
    PrismAudioProcessor();
    ~PrismAudioProcessor() override = default;

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

    // Factory presets are the host's programs too
    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, currentProgram.load()); } // hosts get 0 for a preset of yours
    int getPresetIndex() const { return currentProgram; } // the factory preset loaded, or -1 for one of yours
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    prism::Params currentParams() const;
    double getSampleRateForDisplay() const { return fs; }

    // ---- Presets ---------------------------------------------------------------------------------------
    void applyFactoryPreset (int index);
    static juce::File userPresetFolder();
    juce::Array<juce::File> userPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String getPresetName() const { const juce::SpinLock::ScopedLockType l (presetLock); return presetName; }

    juce::AudioProcessorValueTreeState apvts;
    lbaudio::ScopeBuffer scopeIn, scopeOut;                     // for the spectrum
    std::array<std::atomic<float>, prism::numBands> meterReduction {}; // dB, most since last read
    std::atomic<float> meterInputPeak { 0.0f }, meterOutputPeak { 0.0f };

    static constexpr int oversamplingStages = 2; // 4x, so the exciters' harmonics don't alias

private:
    void setPresetName (const juce::String& name);
    void restoreState (const juce::ValueTree& state);
    void processChunk (float* const* io, int numChannels, int numSamples, const prism::Params& p);

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double fs = 44100.0, osFs = 176400.0;
    int maxChunk = 512;

    // A host may change program from its audio thread, so the preset's name and number live here, not in the
    // value tree; they're written into the saved state when it's taken.
    std::atomic<int> currentProgram { 0 };
    juce::String presetName { "Init" };
    juce::SpinLock presetLock;

    std::array<prism::Crossover, 2> crossover;
    prism::Splits currentSplits { -1.0f, -1.0f, -1.0f };
    std::array<squeeze::Compressor, prism::numBands> compressors;
    std::array<juce::AudioBuffer<float>, prism::numBands> bandBuffers;
    juce::AudioBuffer<float> dryBuffer;
    std::array<std::array<double, 2>, prism::numBands> dcX1 {}, dcY1 {}; // DC blockers on the exciters' harmonics
    double dcR = 0.9999;

    // Smoothed values
    float inGain = 1.0f, outGain = 1.0f, mix = 1.0f, smoothCoeff = 0.001f;
    std::array<float, prism::numBands> excite {};

    std::atomic<float>* pSplit[3] {};
    std::atomic<float>* pInput = nullptr; std::atomic<float>* pOutput = nullptr; std::atomic<float>* pMix = nullptr;
    std::array<std::array<std::atomic<float>*, 8>, prism::numBands> pBand {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismAudioProcessor)
};
