#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

juce::AudioProcessorValueTreeState::ParameterLayout HushAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat (hush::ids::frequency, "Frequency", frequencyRange (2000.0f, 12000.0f), 6000.0f, formatHz, parseHz));
    layout.add (makeFloat (hush::ids::threshold, "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f, formatDb, parseNumber));
    layout.add (makeFloat (hush::ids::range, "Range", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 10.0f, formatDb, parseNumber));
    layout.add (makeChoice (hush::ids::mode, "Mode", { "Split", "Wide" }, hush::Split));
    layout.add (makeBool (hush::ids::listen, "Listen", false));
    return layout;
}

HushAudioProcessor::HushAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HushState", createParameterLayout())
{
    pFrequency = apvts.getRawParameterValue (hush::ids::frequency);
    pThreshold = apvts.getRawParameterValue (hush::ids::threshold);
    pRange = apvts.getRawParameterValue (hush::ids::range);
    pMode = apvts.getRawParameterValue (hush::ids::mode);
    pListen = apvts.getRawParameterValue (hush::ids::listen);
}

hush::Params HushAudioProcessor::currentParams() const
{
    hush::Params p;
    p.frequencyHz = pFrequency->load();
    p.thresholdDb = pThreshold->load();
    p.rangeDb = pRange->load();
    p.mode = juce::jlimit (0, hush::NumModes - 1, juce::roundToInt (pMode->load()));
    p.listen = pListen->load() > 0.5f;
    return p;
}

void HushAudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    attackCoeff = 1.0f - (float) std::exp (-1.0 / (0.0005 * fs));   // 0.5 ms: catches the front of an "s"
    releaseCoeff = 1.0f - (float) std::exp (-1.0 / (0.06 * fs));    // 60 ms: lets go between words
    currentFrequency = -1.0f;
    updateFilters (pFrequency->load());
    reset();
}

void HushAudioProcessor::reset()
{
    for (auto* set : { &detHP, &detLP, &splitA, &splitB })
        for (auto& b : *set) b.reset();
    env = 0.0f;
}

bool HushAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void HushAudioProcessor::updateFilters (float frequency)
{
    if (std::abs (frequency - currentFrequency) < 1.0e-3f) return;
    currentFrequency = frequency;
    for (int ch = 0; ch < 2; ++ch)
    {
        detHP[(size_t) ch].c = hush::detectHighPass (frequency, fs);
        detLP[(size_t) ch].c = hush::detectLowPass (frequency, fs);
        splitA[(size_t) ch].c = hush::splitLowPass (frequency, fs, 0);
        splitB[(size_t) ch].c = hush::splitLowPass (frequency, fs, 1);
    }
}

void HushAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (channels == 0 || n == 0) return;

    const auto p = currentParams();
    updateFilters (p.frequencyHz);
    scope.push (buffer.getReadPointer (0), channels > 1 ? buffer.getReadPointer (1) : nullptr, n);

    float* x[2] = { buffer.getWritePointer (0), channels > 1 ? buffer.getWritePointer (1) : nullptr };
    float mostReduction = 0.0f, mostDetector = -120.0f;

    for (int i = 0; i < n; ++i)
    {
        float band[2] = { 0.0f, 0.0f }, level = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            band[ch] = detLP[(size_t) ch].process (detHP[(size_t) ch].process (x[ch][i]));
            level = juce::jmax (level, std::abs (band[ch]));
        }
        env += (level - env) * (level > env ? attackCoeff : releaseCoeff);
        const float detDb = env > 1.0e-6f ? 20.0f * std::log10 (env) : -120.0f;
        const float red = hush::reductionDb (p, detDb);
        const float g = std::pow (10.0f, -red / 20.0f);
        mostReduction = juce::jmax (mostReduction, red);
        mostDetector = juce::jmax (mostDetector, detDb);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float in = x[ch][i];
            const float low = splitB[(size_t) ch].process (splitA[(size_t) ch].process (in));
            if (p.listen)                  x[ch][i] = band[ch];
            else if (p.mode == hush::Wide) x[ch][i] = g * in;
            else                           x[ch][i] = g * in + (1.0f - g) * low;
        }
    }
    for (int ch = 0; ch < 2; ++ch) { detHP[(size_t) ch].snapDenormals(); detLP[(size_t) ch].snapDenormals(); splitA[(size_t) ch].snapDenormals(); splitB[(size_t) ch].snapDenormals(); }

    lbaudio::ui::storeMax (meterReduction, mostReduction);
    lbaudio::ui::storeMax (meterDetector, mostDetector);
}

void HushAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void HushAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* HushAudioProcessor::createEditor() { return new HushAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HushAudioProcessor(); }
