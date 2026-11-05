#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

juce::AudioProcessorValueTreeState::ParameterLayout EmberAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeFloat (ember::ids::drive, "Drive", juce::NormalisableRange<float> (0.0f, 36.0f, 0.1f), 12.0f, formatDb, parseNumber));
    layout.add (makeChoice (ember::ids::mode, "Character", { "Tape", "Tube", "Clip" }, ember::Tape));
    layout.add (makeFloat (ember::ids::tone, "Tone", juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (makeFloat (ember::ids::mix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, formatPercent, parseNumber));
    layout.add (makeFloat (ember::ids::output, "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (makeBool (ember::ids::autoGain, "Auto Gain", true));
    return layout;
}

EmberAudioProcessor::EmberAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EmberState", createParameterLayout())
{
    pDrive = apvts.getRawParameterValue (ember::ids::drive);
    pMode = apvts.getRawParameterValue (ember::ids::mode);
    pTone = apvts.getRawParameterValue (ember::ids::tone);
    pMix = apvts.getRawParameterValue (ember::ids::mix);
    pOutput = apvts.getRawParameterValue (ember::ids::output);
    pAuto = apvts.getRawParameterValue (ember::ids::autoGain);

    // Report latency from the start: hosts may ask before prepareToPlay.
    // The oversampling filters' delay doesn't depend on the sample rate.
    juce::dsp::Oversampling<float> probe (2, (size_t) oversamplingStages,
                                          juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    probe.initProcessing (512);
    setLatencySamples (juce::roundToInt (probe.getLatencyInSamples()));
}

ember::Params EmberAudioProcessor::currentParams() const
{
    ember::Params p;
    p.driveDb = pDrive->load();
    p.mode = juce::jlimit (0, ember::NumModes - 1, juce::roundToInt (pMode->load()));
    p.toneDb = pTone->load();
    p.mix = pMix->load() * 0.01f;
    p.outputDb = pOutput->load();
    p.autoGain = pAuto->load() > 0.5f;
    return p;
}

void EmberAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxChunk = juce::jmax (16, samplesPerBlock);
    const int channels = juce::jmax (1, juce::jmin (2, getTotalNumOutputChannels()));

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) channels, (size_t) oversamplingStages,
                                                                    juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) maxChunk);
    latency = juce::roundToInt (oversampler->getLatencyInSamples());
    setLatencySamples (latency);

    for (auto& d : dryDelay) d.assign ((size_t) juce::jmax (1, latency), 0.0f);
    dryChunk.setSize (2, maxChunk);

    dcR = std::exp (-2.0 * lbaudio::dsp::pi * 10.0 / fs);
    smoothCoeff = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
    modeFadeStep = 1.0f / (float) (0.02 * fs * (1 << oversamplingStages));

    const auto p = currentParams();
    lastDriveDb = -1000.0f; lastMode = -1;
    updateTargets (p);
    drive = driveTarget; trim = trimTarget; mix = mixTarget; outGain = outTarget;
    mode = prevMode = p.mode; modeFade = 1.0f;
    currentTone = p.toneDb;
    for (int ch = 0; ch < 2; ++ch)
    {
        tiltLow[(size_t) ch].c = ember::tiltLow (currentTone, fs);
        tiltHigh[(size_t) ch].c = ember::tiltHigh (currentTone, fs);
    }
    reset();
}

void EmberAudioProcessor::reset()
{
    if (oversampler) oversampler->reset();
    for (auto& d : dryDelay) std::fill (d.begin(), d.end(), 0.0f);
    delayPos = 0;
    dcX1 = {}; dcY1 = {};
    for (auto& b : tiltLow) b.reset();
    for (auto& b : tiltHigh) b.reset();
}

bool EmberAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void EmberAudioProcessor::updateTargets (const ember::Params& p)
{
    driveTarget = std::pow (10.0f, p.driveDb / 20.0f);
    if (std::abs (p.driveDb - lastDriveDb) > 1.0e-4f || p.mode != lastMode || p.autoGain != lastAuto)
    {
        cachedAuto = p.autoGain ? ember::autoGainFor (p.mode, p.driveDb) : 1.0f;
        lastDriveDb = p.driveDb; lastMode = p.mode; lastAuto = p.autoGain;
    }
    trimTarget = cachedAuto;
    mixTarget = p.mix;
    outTarget = std::pow (10.0f, p.outputDb / 20.0f);

    if (p.mode != mode)
    {
        prevMode = mode;
        mode = p.mode;
        modeFade = 0.0f;
    }

    if (std::abs (p.toneDb - currentTone) > 1.0e-4f)
    {
        currentTone = p.toneDb;
        for (int ch = 0; ch < 2; ++ch)
        {
            tiltLow[(size_t) ch].c = ember::tiltLow (currentTone, fs);
            tiltHigh[(size_t) ch].c = ember::tiltHigh (currentTone, fs);
        }
    }
}

void EmberAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (oversampler == nullptr || channels == 0) return;

    float inPeak = 0.0f;
    for (int ch = 0; ch < channels; ++ch) inPeak = juce::jmax (inPeak, buffer.getMagnitude (ch, 0, n));

    updateTargets (currentParams());

    for (int start = 0; start < n; start += maxChunk)
    {
        const int len = juce::jmin (maxChunk, n - start);
        float* ptrs[2] = { buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr };
        processChunk (ptrs, channels, len, currentParams());
    }

    float outPeak = 0.0f;
    for (int ch = 0; ch < channels; ++ch) outPeak = juce::jmax (outPeak, buffer.getMagnitude (ch, 0, n));
    lbaudio::ui::storeMax (meterInputPeak, inPeak);
    lbaudio::ui::storeMax (meterOutputPeak, outPeak);
}

void EmberAudioProcessor::processChunk (float* const* io, int channels, int n, const ember::Params&)
{
    // 1) Delay a copy of the dry signal by the oversampling latency
    for (int ch = 0; ch < channels; ++ch)
    {
        auto& line = dryDelay[(size_t) ch];
        float* dry = dryChunk.getWritePointer (ch);
        int pos = delayPos;
        for (int i = 0; i < n; ++i)
        {
            if (latency > 0) { dry[i] = line[(size_t) pos]; line[(size_t) pos] = io[ch][i]; pos = (pos + 1) % latency; }
            else dry[i] = io[ch][i];
        }
    }
    if (latency > 0) delayPos = (delayPos + n) % latency;

    // 2) Saturate at 16x the sample rate
    juce::dsp::AudioBlock<float> block (io, (size_t) channels, (size_t) n);
    auto up = oversampler->processSamplesUp (block);
    const int upN = (int) up.getNumSamples();
    const float driveStep = (driveTarget - drive) / (float) upN;
    const float trimStep = (trimTarget - trim) / (float) upN;
    float fadeEnd = modeFade;
    for (int ch = 0; ch < channels; ++ch)
    {
        float* x = up.getChannelPointer ((size_t) ch);
        float d = drive, t = trim, fade = modeFade;
        for (int i = 0; i < upN; ++i)
        {
            d += driveStep; t += trimStep;
            const float in = x[i] * d;
            float y = ember::shape (mode, in);
            if (fade < 1.0f)
            {
                y = ember::shape (prevMode, in) + fade * (y - ember::shape (prevMode, in));
                fade = std::min (1.0f, fade + modeFadeStep);
            }
            x[i] = y * t;
        }
        fadeEnd = fade;
    }
    drive = driveTarget; trim = trimTarget; modeFade = fadeEnd;
    oversampler->processSamplesDown (block);

    // 3) DC blocker, tone, mix with the aligned dry signal, output level
    for (int ch = 0; ch < channels; ++ch)
    {
        float* x = io[ch];
        const float* dry = dryChunk.getReadPointer (ch);
        double x1 = dcX1[(size_t) ch], y1 = dcY1[(size_t) ch];
        float m = mix, g = outGain;
        auto& lo = tiltLow[(size_t) ch];
        auto& hi = tiltHigh[(size_t) ch];
        for (int i = 0; i < n; ++i)
        {
            const double v = x[i];
            const double y = v - x1 + dcR * y1;
            x1 = v; y1 = y;
            const float wet = hi.process (lo.process ((float) y));
            m += (mixTarget - m) * smoothCoeff;
            g += (outTarget - g) * smoothCoeff;
            x[i] = (dry[i] + m * (wet - dry[i])) * g;
        }
        dcX1[(size_t) ch] = x1; dcY1[(size_t) ch] = std::abs (y1) < 1.0e-20 ? 0.0 : y1;
        lo.snapDenormals(); hi.snapDenormals();
        if (ch == channels - 1) { mix = m; outGain = g; }
    }
}

void EmberAudioProcessor::getStateInformation (juce::MemoryBlock& destData) { saveState (apvts, destData); }
void EmberAudioProcessor::setStateInformation (const void* data, int size)  { loadState (apvts, data, size); }

juce::AudioProcessorEditor* EmberAudioProcessor::createEditor() { return new EmberAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EmberAudioProcessor(); }
