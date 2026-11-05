#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using juce::String;

    String formatDb (float v, int)    { return String (v, 1) + " dB"; }
    String formatRatio (float v, int) { return String (v, v < 10.0f ? 1 : 0) + ":1"; }
    String formatPercent (float v, int) { return String (juce::roundToInt (v)) + " %"; }
    String formatHz (float v, int)    { return v <= 20.5f ? String ("Off") : String (juce::roundToInt (v)) + " Hz"; }
    String formatOnOff (bool b, int)  { return b ? "On" : "Off"; }

    String formatMs (float v, int)
    {
        if (v >= 1000.0f) return String (v / 1000.0f, 2) + " s";
        if (v >= 100.0f)  return String (juce::roundToInt (v)) + " ms";
        if (v >= 1.0f)    return String (v, 1) + " ms";
        return String (v, 2) + " ms";
    }

    float parseNumber (const String& t) { return t.retainCharacters ("-+.0123456789").getFloatValue(); }

    float parseMs (const String& t)
    {
        const auto lower = t.trim().toLowerCase();
        const auto value = parseNumber (lower);
        return (lower.endsWith ("s") && ! lower.endsWith ("ms")) ? value * 1000.0f : value;
    }

    float parseHz (const String& t)
    {
        const auto lower = t.trim().toLowerCase();
        if (lower.startsWith ("off")) return 20.0f;
        const auto value = parseNumber (lower);
        return lower.endsWith ("khz") ? value * 1000.0f : value;
    }

    juce::NormalisableRange<float> skewedRange (float start, float end, float interval, float centre)
    {
        juce::NormalisableRange<float> r (start, end, interval);
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const char* name,
                                                          juce::NormalisableRange<float> range, float def,
                                                          String (*toText) (float, int),
                                                          float (*fromText) (const String&))
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction (toText)
                .withValueFromStringFunction (fromText));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SqueezeAudioProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeFloat (ParamIDs::threshold, "Threshold", Range (-60.0f, 0.0f, 0.1f), -18.0f, formatDb, parseNumber));
    layout.add (makeFloat (ParamIDs::ratio, "Ratio", skewedRange (1.0f, 20.0f, 0.01f, 4.0f), 4.0f, formatRatio, parseNumber));
    layout.add (makeFloat (ParamIDs::knee, "Knee", Range (0.0f, 24.0f, 0.1f), 6.0f, formatDb, parseNumber));
    layout.add (makeFloat (ParamIDs::attack, "Attack", skewedRange (0.1f, 200.0f, 0.01f, 10.0f), 10.0f, formatMs, parseMs));
    layout.add (makeFloat (ParamIDs::release, "Release", skewedRange (5.0f, 2000.0f, 0.1f, 150.0f), 120.0f, formatMs, parseMs));
    layout.add (makeFloat (ParamIDs::makeup, "Makeup", Range (-12.0f, 24.0f, 0.1f), 0.0f, formatDb, parseNumber));
    layout.add (makeFloat (ParamIDs::mix, "Mix", Range (0.0f, 100.0f, 0.1f), 100.0f, formatPercent, parseNumber));
    layout.add (makeFloat (ParamIDs::scHpf, "SC HPF", skewedRange (20.0f, 500.0f, 1.0f, 100.0f), 20.0f, formatHz, parseHz));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::autoMakeup, 1 }, "Auto Makeup", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff)));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::extSc, 1 }, "External Sidechain", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff)));

    return layout;
}

SqueezeAudioProcessor::SqueezeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SqueezeState", createParameterLayout())
{
    pThreshold  = apvts.getRawParameterValue (ParamIDs::threshold);
    pRatio      = apvts.getRawParameterValue (ParamIDs::ratio);
    pKnee       = apvts.getRawParameterValue (ParamIDs::knee);
    pAttack     = apvts.getRawParameterValue (ParamIDs::attack);
    pRelease    = apvts.getRawParameterValue (ParamIDs::release);
    pMakeup     = apvts.getRawParameterValue (ParamIDs::makeup);
    pAutoMakeup = apvts.getRawParameterValue (ParamIDs::autoMakeup);
    pMix        = apvts.getRawParameterValue (ParamIDs::mix);
    pScHpf      = apvts.getRawParameterValue (ParamIDs::scHpf);
    pExtSc      = apvts.getRawParameterValue (ParamIDs::extSc);
}

squeeze::CompressorParams SqueezeAudioProcessor::currentParams() const
{
    squeeze::CompressorParams p;
    p.thresholdDb = pThreshold->load();
    p.ratio       = pRatio->load();
    p.kneeDb      = pKnee->load();
    p.attackMs    = pAttack->load();
    p.releaseMs   = pRelease->load();
    p.makeupDb    = pMakeup->load();
    p.autoMakeup  = pAutoMakeup->load() > 0.5f;
    p.mix         = pMix->load() * 0.01f;
    p.scHpfHz     = pScHpf->load();
    return p;
}

void SqueezeAudioProcessor::prepareToPlay (double sampleRate, int)
{
    compressor.setParams (currentParams());
    compressor.prepare (sampleRate, juce::jmax (2, getTotalNumInputChannels()));
}

void SqueezeAudioProcessor::reset()
{
    compressor.reset();
}

bool SqueezeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != mono && mainOut != stereo)
        return false;

    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != mono && sc != stereo)
            return false;
    }

    return true;
}

void SqueezeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    auto mainBuffer = getBusBuffer (buffer, true, 0);

    for (int ch = mainBuffer.getNumChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    compressor.setParams (currentParams());

    // External sidechain is used only when switched on AND the host has the bus enabled.
    const auto* scBus = getBusCount (true) > 1 ? getBus (true, 1) : nullptr;
    const bool useExternal = pExtSc->load() > 0.5f && scBus != nullptr
                             && scBus->isEnabled() && scBus->getNumberOfChannels() > 0;

    // Refers to the host's channel data; no allocation on the audio thread.
    auto scBuffer = useExternal ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const float* const* scPtr = useExternal ? scBuffer.getArrayOfReadPointers() : nullptr;
    const int scChannels = useExternal ? scBuffer.getNumChannels() : 0;

    const auto stats = compressor.process (mainBuffer.getArrayOfWritePointers(), mainBuffer.getNumChannels(),
                                           scPtr, scChannels, numSamples);

    storeMax (meterGainReductionDb, stats.maxGainReductionDb);
    storeMax (meterInputPeak,  stats.inputPeak);
    storeMax (meterOutputPeak, stats.outputPeak);
}

void SqueezeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SqueezeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SqueezeAudioProcessor::createEditor()
{
    return new SqueezeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SqueezeAudioProcessor();
}
