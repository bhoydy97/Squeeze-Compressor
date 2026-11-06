#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

juce::AudioProcessorValueTreeState::ParameterLayout SnapAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat (snap::ids::attack, "Attack", juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (makeFloat (snap::ids::sustain, "Sustain", juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (makeFloat (snap::ids::speed, "Speed", skewed (3.0f, 60.0f, 0.1f, 15.0f), 15.0f, formatMs, parseMs));
    layout.add (makeFloat (snap::ids::output, "Output", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (makeFloat (snap::ids::mix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, formatPercent, parseNumber));
    return layout;
}

SnapAudioProcessor::SnapAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SnapState", createParameterLayout())
{
    pAttack = apvts.getRawParameterValue (snap::ids::attack);
    pSustain = apvts.getRawParameterValue (snap::ids::sustain);
    pSpeed = apvts.getRawParameterValue (snap::ids::speed);
    pOutput = apvts.getRawParameterValue (snap::ids::output);
    pMix = apvts.getRawParameterValue (snap::ids::mix);
}

snap::Params SnapAudioProcessor::currentParams() const
{
    snap::Params p;
    p.attackDb = pAttack->load();
    p.sustainDb = pSustain->load();
    p.speedMs = pSpeed->load();
    p.outputDb = pOutput->load();
    p.mix = pMix->load() * 0.01f;
    return p;
}

void SnapAudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    attFast.attack = snap::coeff (snap::fastAttackMs, fs);
    attFast.release = snap::coeff (snap::attackPairReleaseMs, fs);
    susShort.attack = susLong.attack = snap::coeff (snap::fastAttackMs, fs);
    susShort.release = snap::coeff (snap::sustainShortReleaseMs, fs);
    susLong.release = snap::coeff (snap::sustainLongReleaseMs, fs);
    currentSpeed = -1.0f;
    gainSmooth = snap::coeff (0.2, fs);   // quick enough to let go of a tail cut the moment the next hit lands
    smoothCoeff = snap::coeff (20.0, fs);
    frameLength = juce::jmax (1, (int) std::round (0.0025 * fs));
    const auto p = currentParams();
    mix = p.mix;
    outGain = std::pow (10.0f, p.outputDb / 20.0f);
    reset();
}

void SnapAudioProcessor::reset()
{
    for (auto* f : { &attFast, &susShort, &susLong }) f->value = 0.0f;
    attSlow.value = 0.0f;
    gain = 1.0f;
    frame = {}; frameCount = 0;
}

bool SnapAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void SnapAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (channels == 0 || n == 0) return;

    const auto p = currentParams();
    if (std::abs (p.speedMs - currentSpeed) > 1.0e-4f) { currentSpeed = p.speedMs; attSlow.attack = snap::coeff (currentSpeed, fs); }
    const float targetOut = std::pow (10.0f, p.outputDb / 20.0f);
    const bool neutral = std::abs (p.attackDb) < 1.0e-6f && std::abs (p.sustainDb) < 1.0e-6f;

    float* L = buffer.getWritePointer (0);
    float* R = channels > 1 ? buffer.getWritePointer (1) : nullptr;
    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float a = R != nullptr ? juce::jmax (std::abs (L[i]), std::abs (R[i])) : std::abs (L[i]);
        const float fastA = attFast.process (a), slowA = attSlow.process (fastA);
        const float shortS = susShort.process (a), longS = susLong.process (a);

        float targetGain = 1.0f;
        float gainDb = 0.0f;
        if (! neutral)
        {
            const float wA = snap::weight (snap::toDb (fastA) - snap::toDb (slowA) - snap::attackDeadZoneDb);
            const float wS = snap::weight (snap::toDb (longS) - snap::toDb (shortS) - snap::sustainDeadZoneDb) * (1.0f - wA);
            gainDb = p.attackDb * wA + p.sustainDb * wS;
            targetGain = std::pow (10.0f, gainDb / 20.0f);
            gain += (targetGain - gain) * gainSmooth;
        }
        else
        {
            gain = 1.0f;
        }

        mix += (p.mix - mix) * smoothCoeff;
        outGain += (targetOut - outGain) * smoothCoeff;
        const float wetMinusDry = gain - 1.0f;     // (g x - x) for both sides
        L[i] = (L[i] + mix * wetMinusDry * L[i]) * outGain;
        if (R != nullptr) R[i] = (R[i] + mix * wetMinusDry * R[i]) * outGain;

        const float o = R != nullptr ? juce::jmax (std::abs (L[i]), std::abs (R[i])) : std::abs (L[i]);
        inPeak = juce::jmax (inPeak, a); outPeak = juce::jmax (outPeak, o);
        frame.in = juce::jmax (frame.in, a); frame.out = juce::jmax (frame.out, o);
        if (std::abs (gainDb) > std::abs (frame.gainDb)) frame.gainDb = gainDb;
        if (++frameCount >= frameLength) { feed.push (frame); frame = {}; frameCount = 0; }
    }

    lbaudio::ui::storeMax (meterInputPeak, inPeak);
    lbaudio::ui::storeMax (meterOutputPeak, outPeak);
}

void SnapAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void SnapAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* SnapAudioProcessor::createEditor() { return new SnapAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SnapAudioProcessor(); }
