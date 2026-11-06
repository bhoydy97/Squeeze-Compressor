#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

namespace
{
juce::String formatRate (float v, int) { return v < 1.0f ? juce::String (v, 2) + " Hz" : juce::String (v, 1) + " Hz"; }
}

juce::AudioProcessorValueTreeState::ParameterLayout OrbitAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat (orbit::ids::rate, "Rate", logRange (0.05f, 5.0f, 100.0f), 0.6f, formatRate, parseHz));
    layout.add (makeFloat (orbit::ids::depth, "Depth", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, formatPercent, parseNumber));
    layout.add (makeChoice (orbit::ids::voices, "Voices", { "1", "2", "3", "4" }, 2));
    layout.add (makeFloat (orbit::ids::delay, "Delay", juce::NormalisableRange<float> (5.0f, (float) orbit::maxDelayMs - 4.0f, 0.1f), 12.0f, formatMs, parseMs));
    layout.add (makeFloat (orbit::ids::width, "Width", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f, formatPercent, parseNumber));
    layout.add (makeFloat (orbit::ids::feedback, "Feedback", juce::NormalisableRange<float> (0.0f, 70.0f, 0.1f), 0.0f, formatPercent, parseNumber));
    layout.add (makeFloat (orbit::ids::tone, "Tone", frequencyRange (2000.0f, 20000.0f), 9000.0f, formatHz, parseHz));
    layout.add (makeFloat (orbit::ids::mix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, formatPercent, parseNumber));
    return layout;
}

OrbitAudioProcessor::OrbitAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OrbitState", createParameterLayout())
{
    pRate = apvts.getRawParameterValue (orbit::ids::rate);
    pDepth = apvts.getRawParameterValue (orbit::ids::depth);
    pVoices = apvts.getRawParameterValue (orbit::ids::voices);
    pDelay = apvts.getRawParameterValue (orbit::ids::delay);
    pWidth = apvts.getRawParameterValue (orbit::ids::width);
    pFeedback = apvts.getRawParameterValue (orbit::ids::feedback);
    pTone = apvts.getRawParameterValue (orbit::ids::tone);
    pMix = apvts.getRawParameterValue (orbit::ids::mix);
}

orbit::Params OrbitAudioProcessor::currentParams() const
{
    orbit::Params p;
    p.rateHz = pRate->load();
    p.depth = pDepth->load() * 0.01f;
    p.voices = juce::jlimit (1, orbit::maxVoices, juce::roundToInt (pVoices->load()) + 1);
    p.delayMs = pDelay->load();
    p.width = pWidth->load() * 0.01f;
    p.feedback = juce::jlimit (0.0f, 0.7f, pFeedback->load() * 0.01f);
    p.toneHz = pTone->load();
    p.mix = pMix->load() * 0.01f;
    return p;
}

void OrbitAudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    const double maxMs = orbit::maxDelayMs * (1.0 + 0.12 * (orbit::maxVoices - 1)) + orbit::maxDepthMs + 2.0;
    for (auto& l : lines) l.prepare ((int) std::ceil (maxMs * 0.001 * fs));
    smoothCoeff = 1.0f - (float) std::exp (-1.0 / (0.03 * fs));
    const auto p = currentParams();
    depth = p.depth; width = p.width; delayMs = p.delayMs; feedback = p.feedback; mix = p.mix;
    voiceGain = 1.0f / std::sqrt ((float) p.voices);
    currentTone = -1.0f;
    updateTone (p.toneHz);
    reset();
}

void OrbitAudioProcessor::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& b : toneFilter) b.reset();
    lastWet = {};
    lfoPhase = 0.0;
}

bool OrbitAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void OrbitAudioProcessor::updateTone (float hz)
{
    if (std::abs (hz - currentTone) < 1.0e-3f) return;
    currentTone = hz;
    for (auto& b : toneFilter) b.c = orbit::tone (hz, fs);
}

void OrbitAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    const int outChannels = juce::jmin (2, getTotalNumOutputChannels(), buffer.getNumChannels());
    for (int ch = outChannels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (inChannels == 0 || outChannels == 0 || n == 0) return;
    if (inChannels == 1 && outChannels == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n); // mono in: chorus it into stereo

    const auto p = currentParams();
    updateTone (p.toneHz);
    const float targetVoiceGain = 1.0f / std::sqrt ((float) p.voices);
    const double inc = p.rateHz / fs, msToSamples = 0.001 * fs;

    float* L = buffer.getWritePointer (0);
    float* R = outChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        depth += (p.depth - depth) * smoothCoeff;
        width += (p.width - width) * smoothCoeff;
        delayMs += (p.delayMs - delayMs) * smoothCoeff;
        feedback += (p.feedback - feedback) * smoothCoeff;
        mix += (p.mix - mix) * smoothCoeff;
        voiceGain += (targetVoiceGain - voiceGain) * smoothCoeff;
        lfoPhase += inc; if (lfoPhase >= 1.0) lfoPhase -= 1.0;

        orbit::Params now = p;
        now.depth = depth; now.width = width; now.delayMs = delayMs;

        const float in[2] = { L[i], R != nullptr ? R[i] : L[i] };
        peak = juce::jmax (peak, std::abs (in[0]), std::abs (in[1]));
        float wet[2] = { 0.0f, 0.0f };
        for (int side = 0; side < (R != nullptr ? 2 : 1); ++side)
        {
            float sum = 0.0f;
            for (int v = 0; v < p.voices; ++v)
            {
                const double ms = orbit::voiceDelayMs (now, v, orbit::voicePhase (now, lfoPhase, v, side == 1));
                sum += lines[(size_t) side].read (juce::jmax (1.0, ms * msToSamples - 1.0));
            }
            wet[side] = orbit::softLimit (toneFilter[(size_t) side].process (sum * voiceGain)); // untouched below full scale
            lines[(size_t) side].push (orbit::softLimit (in[side] + feedback * lastWet[(size_t) side]));
            lastWet[(size_t) side] = wet[side];
        }

        L[i] = in[0] + mix * (wet[0] - in[0]);
        if (R != nullptr) R[i] = in[1] + mix * (wet[1] - in[1]);
    }
    for (auto& b : toneFilter) b.snapDenormals();

    lfoPhaseForDisplay.store (lfoPhase);
    lbaudio::ui::storeMax (inputPeak, peak);
}

void OrbitAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void OrbitAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* OrbitAudioProcessor::createEditor() { return new OrbitAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OrbitAudioProcessor(); }
