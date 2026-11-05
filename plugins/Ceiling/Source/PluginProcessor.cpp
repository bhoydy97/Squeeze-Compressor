#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

juce::AudioProcessorValueTreeState::ParameterLayout CeilingAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat (ceiling::ids::gain, "Gain", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f, formatDb, parseNumber));
    layout.add (makeFloat (ceiling::ids::ceiling, "Ceiling", juce::NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -1.0f,
                           [] (float v, int) { return juce::String (v, 1) + " dB"; }, parseNumber));
    layout.add (makeFloat (ceiling::ids::release, "Release", logRange (1.0f, 1000.0f, 10.0f), 80.0f, formatMs, parseMs));
    layout.add (makeBool (ceiling::ids::truePeak, "True Peak", true));
    return layout;
}

CeilingAudioProcessor::CeilingAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CeilingState", createParameterLayout())
{
    pGain = apvts.getRawParameterValue (ceiling::ids::gain);
    pCeiling = apvts.getRawParameterValue (ceiling::ids::ceiling);
    pRelease = apvts.getRawParameterValue (ceiling::ids::release);
    pTruePeak = apvts.getRawParameterValue (ceiling::ids::truePeak);
    setLatencySamples (ceiling::latencySamples (48000.0)); // corrected for the real rate in prepareToPlay
}

ceiling::Params CeilingAudioProcessor::currentParams() const
{
    ceiling::Params p;
    p.gainDb = pGain->load();
    p.ceilingDb = pCeiling->load();
    p.releaseMs = pRelease->load();
    p.truePeak = pTruePeak->load() > 0.5f;
    return p;
}

void CeilingAudioProcessor::prepareToPlay (double sampleRate, int)
{
    limiter.setParams (currentParams());
    limiter.prepare (sampleRate);
    limiter.setParams (currentParams());
    setLatencySamples (limiter.getLatency());
    loudness.prepare (sampleRate);
    reset();
}

void CeilingAudioProcessor::reset()
{
    limiter.reset();
    loudness.reset();
    truePeakMeter.reset();
    truePeakMax.store (0.0f);
    frameDriven = frameOutput = 0.0f;
    frameFill = 0;
}

bool CeilingAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void CeilingAudioProcessor::pushHistory (float driven, float output)
{
    const auto index = historyWritten.load (std::memory_order_relaxed);
    auto& frame = history[index % historySize];
    frame.driven.store (driven, std::memory_order_relaxed);
    frame.output.store (output, std::memory_order_relaxed);
    historyWritten.store (index + 1, std::memory_order_release);
}

void CeilingAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (channels == 0) return;

    if (resetMeasurementsRequested.exchange (false))
    {
        loudness.resetIntegrated();
        truePeakMeter.resetPeak();
        truePeakMax.store (0.0f);
    }

    limiter.setParams (currentParams());
    float* left = buffer.getWritePointer (0);
    float* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;

    float outPeak = 0.0f, minGain = 1.0f;
    int clamped = 0;
    for (int start = 0; start < n;)
    {
        // Work in pieces that end on history-frame boundaries
        const int len = juce::jmin (n - start, historyFrameSamples - frameFill);
        limiter.process (left + start, right != nullptr ? right + start : nullptr, len);
        const auto& s = limiter.lastStats();
        frameDriven = juce::jmax (frameDriven, s.drivenPeak);
        frameOutput = juce::jmax (frameOutput, s.outputPeak);
        outPeak = juce::jmax (outPeak, s.outputPeak);
        minGain = juce::jmin (minGain, s.minGain);
        clamped += s.clampedSamples;
        frameFill += len;
        if (frameFill == historyFrameSamples)
        {
            pushHistory (frameDriven, frameOutput);
            frameDriven = frameOutput = 0.0f;
            frameFill = 0;
        }
        start += len;
    }

    loudness.process (left, right, n);
    truePeakMeter.process (left, right, n);

    lbaudio::ui::storeMax (meterOutputPeak, outPeak);
    lbaudio::ui::storeMax (meterGainReductionDb, -20.0f * std::log10 (juce::jmax (1.0e-6f, minGain)));
    momentaryLufs.store ((float) juce::jmax (-100.0, loudness.momentaryLufs()));
    shortTermLufs.store ((float) juce::jmax (-100.0, loudness.shortTermLufs()));
    integratedLufs.store ((float) juce::jmax (-100.0, loudness.integratedLufs()));
    truePeakMax.store (truePeakMeter.peak());
    if (clamped > 0) clampedSamples.fetch_add (clamped);
}

void CeilingAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void CeilingAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* CeilingAudioProcessor::createEditor() { return new CeilingAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CeilingAudioProcessor(); }
