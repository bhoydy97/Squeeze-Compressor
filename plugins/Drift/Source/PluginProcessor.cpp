#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

juce::AudioProcessorValueTreeState::ParameterLayout DriftAudioProcessor::createParameterLayout()
{
    juce::StringArray divs;
    for (const auto& d : drift::divisions()) divs.add (d.name);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat (drift::ids::time, "Time", skewed (1.0f, 2000.0f, 0.1f, 250.0f), 375.0f, formatMs, parseMs));
    layout.add (makeBool (drift::ids::sync, "Sync", true));
    layout.add (makeChoice (drift::ids::division, "Division", divs, drift::defaultDivision));
    layout.add (makeFloat (drift::ids::feedback, "Feedback", juce::NormalisableRange<float> (0.0f, 95.0f, 0.1f), 40.0f, formatPercent, parseNumber));
    layout.add (makeChoice (drift::ids::mode, "Mode", { "Stereo", "Ping-pong" }, drift::PingPong));
    layout.add (makeFloat (drift::ids::lowCut, "Low Cut", frequencyRange (20.0f, 2000.0f), 150.0f, formatHz, parseHz));
    layout.add (makeFloat (drift::ids::highCut, "High Cut", frequencyRange (1000.0f, 20000.0f), 6000.0f, formatHz, parseHz));
    layout.add (makeFloat (drift::ids::wobble, "Wobble", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 15.0f, formatPercent, parseNumber));
    layout.add (makeFloat (drift::ids::duck, "Duck", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f, formatDb, parseNumber));
    layout.add (makeFloat (drift::ids::mix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, formatPercent, parseNumber));
    return layout;
}

DriftAudioProcessor::DriftAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DriftState", createParameterLayout())
{
    pTime = apvts.getRawParameterValue (drift::ids::time);
    pSync = apvts.getRawParameterValue (drift::ids::sync);
    pDivision = apvts.getRawParameterValue (drift::ids::division);
    pFeedback = apvts.getRawParameterValue (drift::ids::feedback);
    pMode = apvts.getRawParameterValue (drift::ids::mode);
    pLowCut = apvts.getRawParameterValue (drift::ids::lowCut);
    pHighCut = apvts.getRawParameterValue (drift::ids::highCut);
    pWobble = apvts.getRawParameterValue (drift::ids::wobble);
    pDuck = apvts.getRawParameterValue (drift::ids::duck);
    pMix = apvts.getRawParameterValue (drift::ids::mix);
}

drift::Params DriftAudioProcessor::currentParams() const
{
    drift::Params p;
    p.timeMs = pTime->load();
    p.sync = pSync->load() > 0.5f;
    p.division = juce::jlimit (0, (int) drift::divisions().size() - 1, juce::roundToInt (pDivision->load()));
    p.feedback = juce::jlimit (0.0f, 0.95f, pFeedback->load() * 0.01f);
    p.mode = juce::jlimit (0, drift::NumModes - 1, juce::roundToInt (pMode->load()));
    p.lowCutHz = pLowCut->load();
    p.highCutHz = pHighCut->load();
    p.wobble = pWobble->load() * 0.01f;
    p.duckDb = pDuck->load();
    p.mix = pMix->load() * 0.01f;
    return p;
}

double DriftAudioProcessor::getTailLengthSeconds() const
{
    const auto p = currentParams();
    const double t = drift::delayMs (p, displayBpm.load()) * 0.001;
    const double repeats = p.feedback > 0.001f ? 60.0 / (-20.0 * std::log10 ((double) p.feedback)) : 0.0;
    return juce::jmin (30.0, t * (1.0 + repeats));
}

void DriftAudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& l : lines) l.prepare ((int) std::ceil (fs * (drift::maxDelaySeconds + 0.05)));
    delayCoeff = 1.0 - std::exp (-1.0 / (0.06 * fs));            // delay time glides over ~60 ms
    smoothCoeff = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
    envAttack = 1.0f - (float) std::exp (-1.0 / (0.005 * fs));
    envRelease = 1.0f - (float) std::exp (-1.0 / (0.1 * fs));
    hitFastRel = 1.0f - (float) std::exp (-1.0 / (0.05 * fs));
    hitSlowAtt = 1.0f - (float) std::exp (-1.0 / (0.04 * fs));

    const auto p = currentParams();
    delaySamples = drift::delayMs (p, displayBpm.load()) * 0.001 * fs;
    mix = p.mix; wobble = p.wobble; feedback = p.feedback;
    currentLowCut = currentHighCut = -1.0f;
    updateFilters (p);
    reset();
}

void DriftAudioProcessor::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& b : loopLow) b.reset();
    for (auto& b : loopHigh) b.reset();
    env = hitFast = hitSlow = 0.0f;
    duckGain = 1.0f;
    wowPhase = flutterPhase = 0.0;
    sinceHit = 1000.0;
}

bool DriftAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void DriftAudioProcessor::updateFilters (const drift::Params& p)
{
    if (std::abs (p.lowCutHz - currentLowCut) > 1.0e-3f)
    {
        currentLowCut = p.lowCutHz;
        for (auto& b : loopLow) b.c = drift::lowCut (currentLowCut, fs);
    }
    if (std::abs (p.highCutHz - currentHighCut) > 1.0e-3f)
    {
        currentHighCut = p.highCutHz;
        for (auto& b : loopHigh) b.c = drift::highCut (currentHighCut, fs);
    }
}

void DriftAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (channels == 0 || n == 0) return;

    double bpm = 120.0;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
    displayBpm.store ((float) bpm);

    const auto p = currentParams();
    updateFilters (p);
    const double target = juce::jlimit (1.0, drift::maxDelaySeconds * fs, drift::delayMs (p, bpm) * 0.001 * fs);
    const bool pingPong = p.mode == drift::PingPong && channels == 2;

    float* L = buffer.getWritePointer (0);
    float* R = channels > 1 ? buffer.getWritePointer (1) : nullptr;
    const double wowInc = 0.55 / fs, flutterInc = 5.3 / fs;
    const double msToSamples = 0.001 * fs;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        delaySamples += (target - delaySamples) * delayCoeff;
        wobble += (p.wobble - wobble) * smoothCoeff;
        feedback += (p.feedback - feedback) * smoothCoeff;
        mix += (p.mix - mix) * smoothCoeff;

        // Wow and flutter, a little out of step between the sides
        wowPhase += wowInc; if (wowPhase >= 1.0) wowPhase -= 1.0;
        flutterPhase += flutterInc; if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        const double twoPi = 2.0 * lbaudio::dsp::pi;
        const double wowMs = 2.0 * wobble, flutterMs = 0.15 * wobble;
        const double modL = (wowMs * std::sin (twoPi * wowPhase) + flutterMs * std::sin (twoPi * flutterPhase)) * msToSamples;
        const double modR = (wowMs * std::sin (twoPi * (wowPhase + 0.25)) + flutterMs * std::sin (twoPi * (flutterPhase + 0.37))) * msToSamples;
        const double dL = juce::jmax (2.0, delaySamples + modL) - 1.0;
        const double dR = juce::jmax (2.0, delaySamples + modR) - 1.0;

        const float inL = L[i], inR = R != nullptr ? R[i] : inL;
        const float yL = lines[0].read (dL);
        const float yR = channels > 1 ? lines[1].read (dR) : yL;

        // Envelope for ducking, and a simple hit detector for the display
        const float a = juce::jmax (std::abs (inL), std::abs (inR));
        peak = juce::jmax (peak, a);
        env += (a - env) * (a > env ? envAttack : envRelease);
        hitFast = a > hitFast ? a : hitFast + (a - hitFast) * hitFastRel;
        hitSlow += (a - hitSlow) * hitSlowAtt;
        sinceHit += 1.0 / fs;
        if (hitFast > 0.02f && hitFast > 1.8f * hitSlow && sinceHit > 0.08) sinceHit = 0.0;

        const float envDb = env > 1.0e-6f ? 20.0f * std::log10 (env) : -120.0f;
        const float duckTarget = std::pow (10.0f, -drift::duckAmountDb (p.duckDb, envDb) / 20.0f);
        duckGain += (duckTarget - duckGain) * smoothCoeff;

        if (pingPong)
        {
            const float mono = 0.5f * (inL + inR);
            lines[0].push (drift::softLimit (loopHigh[0].process (loopLow[0].process (mono + feedback * yR))));
            lines[1].push (drift::softLimit (loopHigh[1].process (loopLow[1].process (feedback * yL))));
        }
        else
        {
            lines[0].push (drift::softLimit (loopHigh[0].process (loopLow[0].process (inL + feedback * yL))));
            if (channels > 1)
                lines[1].push (drift::softLimit (loopHigh[1].process (loopLow[1].process (inR + feedback * yR))));
        }

        L[i] = inL + mix * (duckGain * yL - inL);
        if (R != nullptr) R[i] = inR + mix * (duckGain * yR - inR);
    }
    for (int ch = 0; ch < 2; ++ch) { loopLow[(size_t) ch].snapDenormals(); loopHigh[(size_t) ch].snapDenormals(); }

    duckNowDb.store (duckGain > 0.0f ? -20.0f * std::log10 (duckGain) : 60.0f);
    secondsSinceHit.store (sinceHit);
    lbaudio::ui::storeMax (inputPeak, peak);
}

void DriftAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void DriftAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* DriftAudioProcessor::createEditor() { return new DriftAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DriftAudioProcessor(); }
