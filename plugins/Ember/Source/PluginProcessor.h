#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "EmberDSP.h"

namespace ember::ids
{
inline constexpr const char* drive    = "drive";
inline constexpr const char* mode     = "mode";
inline constexpr const char* tone     = "tone";
inline constexpr const char* mix      = "mix";
inline constexpr const char* output   = "output";
inline constexpr const char* autoGain = "autoGain";
}

class EmberAudioProcessor final : public juce::AudioProcessor
{
public:
    EmberAudioProcessor();
    ~EmberAudioProcessor() override = default;

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
    ember::Params currentParams() const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> meterInputPeak { 0.0f }, meterOutputPeak { 0.0f };

    static constexpr int oversamplingStages = 4; // 16x

private:
    void processChunk (float* const* io, int numChannels, int numSamples, const ember::Params& p);
    void updateTargets (const ember::Params& p);

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int maxChunk = 512, latency = 0;
    double fs = 44100.0;

    // Dry path delayed by the oversampling latency, so Mix lines up exactly
    std::array<std::vector<float>, 2> dryDelay;
    int delayPos = 0;
    juce::AudioBuffer<float> dryChunk;

    std::array<double, 2> dcX1 {}, dcY1 {};
    double dcR = 0.9995;
    std::array<lbaudio::dsp::Biquad, 2> tiltLow, tiltHigh;
    float currentTone = 0.0f;

    // Smoothed values
    float drive = 1.0f, driveTarget = 1.0f, trim = 1.0f, trimTarget = 1.0f;
    float mix = 1.0f, mixTarget = 1.0f, outGain = 1.0f, outTarget = 1.0f;
    float smoothCoeff = 0.001f;
    int mode = ember::Tape, prevMode = ember::Tape;
    float modeFade = 1.0f, modeFadeStep = 0.001f;
    float lastDriveDb = -1000.0f; int lastMode = -1; bool lastAuto = false; float cachedAuto = 1.0f;

    std::atomic<float>* pDrive = nullptr; std::atomic<float>* pMode = nullptr; std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pMix = nullptr; std::atomic<float>* pOutput = nullptr; std::atomic<float>* pAuto = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberAudioProcessor)
};
