#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

namespace
{
juce::String formatBassMono (float v, int) { return v <= spread::bassMonoOff + 0.5f ? juce::String ("Off") : formatHz (v, 0); }
float parseBassMono (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? spread::bassMonoOff : parseHz (t); }
juce::String formatBalance (float v, int)
{
    const int r = juce::roundToInt (v);
    return r == 0 ? juce::String ("Centre") : (r < 0 ? "L " + juce::String (-r) : "R " + juce::String (r));
}
float parseBalance (const juce::String& t)
{
    const auto s = t.trim().toLowerCase();
    if (s.startsWith ("c")) return 0.0f;
    const float v = std::abs (parseNumber (s));
    return s.startsWith ("l") ? -v : v * (s.startsWith ("-") ? -1.0f : 1.0f);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout SpreadAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat (spread::ids::width, "Width", juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f, formatPercent, parseNumber));
    layout.add (makeFloat (spread::ids::bassMono, "Bass Mono", frequencyRange (spread::bassMonoOff, 500.0f), spread::bassMonoOff, formatBassMono, parseBassMono));
    layout.add (makeFloat (spread::ids::balance, "Balance", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, formatBalance, parseBalance));
    layout.add (makeFloat (spread::ids::output, "Output", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (makeBool (spread::ids::mono, "Mono", false));
    return layout;
}

SpreadAudioProcessor::SpreadAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpreadState", createParameterLayout())
{
    pWidth = apvts.getRawParameterValue (spread::ids::width);
    pBassMono = apvts.getRawParameterValue (spread::ids::bassMono);
    pBalance = apvts.getRawParameterValue (spread::ids::balance);
    pOutput = apvts.getRawParameterValue (spread::ids::output);
    pMono = apvts.getRawParameterValue (spread::ids::mono);
}

spread::Params SpreadAudioProcessor::currentParams() const
{
    spread::Params p;
    p.width = pWidth->load() * 0.01f;
    p.bassMonoHz = pBassMono->load();
    p.balance = juce::jlimit (-1.0f, 1.0f, pBalance->load() * 0.01f);
    p.outputDb = pOutput->load();
    p.mono = pMono->load() > 0.5f;
    return p;
}

void SpreadAudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    smoothCoeff = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
    corrCoeff = 1.0 - std::exp (-1.0 / (0.3 * fs));
    const auto p = currentParams();
    width = p.width;
    gainL = spread::balanceGain (p.balance, false);
    gainR = spread::balanceGain (p.balance, true);
    outGain = std::pow (10.0f, p.outputDb / 20.0f);
    monoAmount = p.mono ? 1.0f : 0.0f;
    currentBassMono = -1.0f;
    updateFilters (p.bassMonoHz);
    reset();
}

void SpreadAudioProcessor::reset()
{
    for (auto& b : sideHP) b.reset();
    sLR = sLL = sRR = 0.0;
}

bool SpreadAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Width needs two sides
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void SpreadAudioProcessor::updateFilters (float hz)
{
    if (std::abs (hz - currentBassMono) < 1.0e-3f) return;
    currentBassMono = hz;
    for (int s = 0; s < 2; ++s) sideHP[(size_t) s].c = spread::bassMonoSection (juce::jmax (spread::bassMonoOff, hz), fs, s);
}

void SpreadAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (channels < 2 || n == 0) return;

    const auto p = currentParams();
    updateFilters (p.bassMonoHz);
    const bool bassMono = spread::bassMonoOn (p);
    const float targetL = spread::balanceGain (p.balance, false), targetR = spread::balanceGain (p.balance, true);
    const float targetOut = std::pow (10.0f, p.outputDb / 20.0f), targetMono = p.mono ? 1.0f : 0.0f;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        width += (p.width - width) * smoothCoeff;
        gainL += (targetL - gainL) * smoothCoeff;
        gainR += (targetR - gainR) * smoothCoeff;
        outGain += (targetOut - outGain) * smoothCoeff;
        monoAmount += (targetMono - monoAmount) * smoothCoeff;

        const float m = 0.5f * (L[i] + R[i]);
        float s = 0.5f * (L[i] - R[i]) * width;
        if (bassMono) s = sideHP[1].process (sideHP[0].process (s));
        s *= 1.0f - monoAmount;
        L[i] = (m + s) * gainL * outGain;
        R[i] = (m - s) * gainR * outGain;

        sLR += ((double) L[i] * R[i] - sLR) * corrCoeff;
        sLL += ((double) L[i] * L[i] - sLL) * corrCoeff;
        sRR += ((double) R[i] * R[i] - sRR) * corrCoeff;
    }
    for (auto& b : sideHP) b.snapDenormals();
    if (sLL < 1.0e-20 && sRR < 1.0e-20) { sLL = sRR = sLR = 0.0; }

    const double denom = std::sqrt (sLL * sRR);
    correlation.store (denom > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, sLR / denom) : 1.0f);
    scope.push (L, R, n);
}

void SpreadAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void SpreadAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* SpreadAudioProcessor::createEditor() { return new SpreadAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpreadAudioProcessor(); }
