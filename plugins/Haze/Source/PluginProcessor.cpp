#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

juce::AudioProcessorValueTreeState::ParameterLayout HazeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat (haze::ids::mix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, formatPercent, parseNumber));
    layout.add (makeFloat (haze::ids::preDelay, "Pre-delay", skewed (0.0f, 200.0f, 0.1f, 40.0f), 10.0f, formatMs, parseMs));
    layout.add (makeFloat (haze::ids::size, "Size", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, formatPercent, parseNumber));
    layout.add (makeFloat (haze::ids::decay, "Decay", logRange (0.2f, 20.0f, 100.0f), 2.0f, formatSeconds, parseSeconds));
    layout.add (makeFloat (haze::ids::damping, "Damping", frequencyRange (1000.0f, 20000.0f), 6000.0f, formatHz, parseHz));
    layout.add (makeFloat (haze::ids::lowCut, "Low Cut", frequencyRange (20.0f, 1000.0f), 100.0f, formatHz, parseHz));
    layout.add (makeFloat (haze::ids::width, "Width", juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f, formatPercent, parseNumber));
    layout.add (makeFloat (haze::ids::modulation, "Modulation", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, formatPercent, parseNumber));
    return layout;
}

HazeAudioProcessor::HazeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HazeState", createParameterLayout())
{
    pMix = apvts.getRawParameterValue (haze::ids::mix);
    pPreDelay = apvts.getRawParameterValue (haze::ids::preDelay);
    pSize = apvts.getRawParameterValue (haze::ids::size);
    pDecay = apvts.getRawParameterValue (haze::ids::decay);
    pDamping = apvts.getRawParameterValue (haze::ids::damping);
    pLowCut = apvts.getRawParameterValue (haze::ids::lowCut);
    pWidth = apvts.getRawParameterValue (haze::ids::width);
    pModulation = apvts.getRawParameterValue (haze::ids::modulation);
}

haze::Params HazeAudioProcessor::currentParams() const
{
    haze::Params p;
    p.mix = pMix->load() * 0.01f;
    p.preDelayMs = pPreDelay->load();
    p.size = pSize->load() * 0.01f;
    p.decayS = pDecay->load();
    p.dampingHz = pDamping->load();
    p.lowCutHz = pLowCut->load();
    p.width = pWidth->load() * 0.01f;
    p.modulation = pModulation->load() * 0.01f;
    return p;
}

double HazeAudioProcessor::getTailLengthSeconds() const
{
    // Time for the tail to fall well below audibility (90 dB), after the pre-delay
    return pPreDelay->load() * 0.001 + pDecay->load() * 1.5;
}

void HazeAudioProcessor::prepareToPlay (double sampleRate, int)
{
    reverb.prepare (sampleRate);
    reverb.setParams (currentParams());
}

void HazeAudioProcessor::reset()
{
    reverb.reset();
}

bool HazeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return in.size() <= out.size(); // mono, stereo, or mono in -> stereo out
}

void HazeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int ins = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    const int outs = juce::jmin (2, getTotalNumOutputChannels(), buffer.getNumChannels());
    for (int ch = outs; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (outs == 0) return;
    if (ins == 0) { buffer.clear(); return; }

    // Mono in, stereo out: both sides start from the one input
    if (ins == 1 && outs == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    float inPeak = 0.0f;
    for (int ch = 0; ch < outs; ++ch) inPeak = juce::jmax (inPeak, buffer.getMagnitude (ch, 0, n));

    reverb.setParams (currentParams());
    reverb.process (buffer.getWritePointer (0), outs > 1 ? buffer.getWritePointer (1) : nullptr, n);

    float outPeak = 0.0f;
    for (int ch = 0; ch < outs; ++ch) outPeak = juce::jmax (outPeak, buffer.getMagnitude (ch, 0, n));
    lbaudio::ui::storeMax (meterInputPeak, inPeak);
    lbaudio::ui::storeMax (meterOutputPeak, outPeak);
}

void HazeAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void HazeAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* HazeAudioProcessor::createEditor() { return new HazeAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HazeAudioProcessor(); }
