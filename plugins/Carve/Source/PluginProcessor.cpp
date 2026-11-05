#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

juce::AudioProcessorValueTreeState::ParameterLayout CarveAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto defaults = carve::defaultParams();
    const juce::StringArray slopes { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    const auto gainRange = juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f);
    const auto qRange = skewed (0.1f, 18.0f, 0.01f, 1.0f);

    struct FreqLimits { float lo, hi; };
    const std::array<FreqLimits, carve::NumBands> limits {{ { 10, 1000 }, { 20, 1000 }, { 20, 20000 }, { 20, 20000 }, { 20, 20000 }, { 1000, 20000 }, { 1000, 22000 } }};

    for (int b = 0; b < carve::NumBands; ++b)
    {
        const auto& id = carve::bandIds()[(size_t) b];
        const auto& d = defaults.bands[(size_t) b];
        const juce::String title (id.title);
        if (*id.on)    layout.add (makeBool (id.on, title + " On", d.on));
        layout.add (makeFloat (id.freq, title + " Freq", frequencyRange (limits[(size_t) b].lo, limits[(size_t) b].hi), d.freq, formatHz, parseHz));
        if (*id.gain)  layout.add (makeFloat (id.gain, title + " Gain", gainRange, d.gainDb, formatSignedDb, parseNumber));
        if (*id.q)     layout.add (makeFloat (id.q, title + " Q", qRange, d.q, formatQ, parseNumber));
        if (*id.slope) layout.add (makeChoice (id.slope, title + " Slope", slopes, d.slope));
    }
    layout.add (makeFloat (carve::outputId, "Output", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    return layout;
}

CarveAudioProcessor::CarveAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CarveState", createParameterLayout())
{
    auto raw = [this] (const char* id) { return *id ? apvts.getRawParameterValue (id) : nullptr; };
    for (int b = 0; b < carve::NumBands; ++b)
    {
        const auto& id = carve::bandIds()[(size_t) b];
        ptrs[(size_t) b] = { raw (id.on), raw (id.freq), raw (id.gain), raw (id.q), raw (id.slope) };
    }
    pOutput = apvts.getRawParameterValue (carve::outputId);
}

carve::Params CarveAudioProcessor::currentParams() const
{
    auto p = carve::defaultParams();
    for (int b = 0; b < carve::NumBands; ++b)
    {
        const auto& src = ptrs[(size_t) b];
        auto& dst = p.bands[(size_t) b];
        if (src.on)    dst.on = src.on->load() > 0.5f;
        if (src.freq)  dst.freq = src.freq->load();
        if (src.gain)  dst.gainDb = src.gain->load();
        if (src.q)     dst.q = src.q->load();
        if (src.slope) dst.slope = juce::roundToInt (src.slope->load());
    }
    p.outputDb = pOutput->load();
    return p;
}

void CarveAudioProcessor::prepareToPlay (double sampleRate, int)
{
    eq.setParams (currentParams());
    eq.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
}

bool CarveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void CarveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    float inPeak = 0.0f;
    for (int ch = 0; ch < channels; ++ch) inPeak = juce::jmax (inPeak, buffer.getMagnitude (ch, 0, n));

    eq.setParams (currentParams());
    eq.process (buffer.getArrayOfWritePointers(), channels, n);

    float outPeak = 0.0f;
    for (int ch = 0; ch < channels; ++ch) outPeak = juce::jmax (outPeak, buffer.getMagnitude (ch, 0, n));
    lbaudio::ui::storeMax (meterInputPeak, inPeak);
    lbaudio::ui::storeMax (meterOutputPeak, outPeak);
    scope.push (buffer.getReadPointer (0), channels > 1 ? buffer.getReadPointer (1) : nullptr, n);
}

void CarveAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void CarveAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* CarveAudioProcessor::createEditor() { return new CarveAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CarveAudioProcessor(); }
