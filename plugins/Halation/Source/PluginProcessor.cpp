#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>
#include <LBAudio/UI.h>

using namespace lbaudio::params;

namespace
{
std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                       juce::NormalisableRange<float> range, float def,
                                                       juce::String (*toText) (float, int), float (*fromText) (const juce::String&))
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText).withValueFromStringFunction (fromText));
}

juce::String formatRatio (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + ":1"; }

std::unique_ptr<juce::dsp::Oversampling<float>> makeOversampler (int channels)
{
    return std::make_unique<juce::dsp::Oversampling<float>> ((size_t) channels, (size_t) HalationAudioProcessor::oversamplingStages,
                                                            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout HalationAudioProcessor::createParameterLayout()
{
    using namespace halation;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (floatParam (ids::split1, "Low Split", frequencyRange (40.0f, 600.0f), 120.0f, formatHz, parseHz));
    layout.add (floatParam (ids::split2, "Mid Split", frequencyRange (250.0f, 5000.0f), 1000.0f, formatHz, parseHz));
    layout.add (floatParam (ids::split3, "High Split", frequencyRange (1500.0f, 16000.0f), 6000.0f, formatHz, parseHz));

    for (int b = 0; b < numBands; ++b)
    {
        const juce::String n = bandName (b);
        layout.add (floatParam (ids::band (b, "threshold"), n + " Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), 0.0f, formatDb, parseNumber));
        layout.add (floatParam (ids::band (b, "ratio"), n + " Ratio", skewed (1.0f, 20.0f, 0.01f, 4.0f), 2.0f, formatRatio, parseNumber));
        layout.add (floatParam (ids::band (b, "attack"), n + " Attack", skewed (0.1f, 200.0f, 0.01f, 10.0f), 10.0f, formatMs, parseMs));
        layout.add (floatParam (ids::band (b, "release"), n + " Release", skewed (5.0f, 2000.0f, 0.1f, 150.0f), 120.0f, formatMs, parseMs));
        layout.add (floatParam (ids::band (b, "makeup"), n + " Makeup", juce::NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
        layout.add (floatParam (ids::band (b, "excite"), n + " Excite", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, formatPercent, parseNumber));
        {
            juce::StringArray tones;
            for (int t = 0; t < numTones; ++t) tones.add (toneName (t));
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::band (b, "extone"), 1 }, n + " Excite Tone", tones, 0));
        }
        layout.add (floatParam (ids::band (b, "exmix"), n + " Excite Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, formatPercent, parseNumber));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::band (b, "solo"), 1 }, n + " Solo", false,
                                                                juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff)));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::band (b, "bypass"), 1 }, n + " Bypass", false,
                                                                juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff)));
    }

    layout.add (floatParam (ids::input, "Input", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (floatParam (ids::output, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (floatParam (ids::mix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, formatPercent, parseNumber));
    // Added in 1.2.3, last so every other parameter keeps its place. On for a new copy; off for a session saved
    // before it existed (see restoreState), so old mixes sound as they did.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::ceiling, 1 }, "Ceiling", true,
                                                            juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff)));
    return layout;
}

HalationAudioProcessor::HalationAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HalationState", createParameterLayout())
{
    pSplit[0] = apvts.getRawParameterValue (halation::ids::split1);
    pSplit[1] = apvts.getRawParameterValue (halation::ids::split2);
    pSplit[2] = apvts.getRawParameterValue (halation::ids::split3);
    pInput = apvts.getRawParameterValue (halation::ids::input);
    pOutput = apvts.getRawParameterValue (halation::ids::output);
    pMix = apvts.getRawParameterValue (halation::ids::mix);
    pCeiling = apvts.getRawParameterValue (halation::ids::ceiling);
    for (int b = 0; b < halation::numBands; ++b)
        for (int k = 0; k < halation::ids::numBandControls; ++k)
            pBand[(size_t) b][(size_t) k] = apvts.getRawParameterValue (halation::ids::band (b, halation::ids::bandControls[k]));
    for (auto& m : meterReduction) m.store (0.0f);
    for (int t = 0; t < halation::numTones; ++t) halation::toneShape (t); // build the tone tables now, not on the audio thread

    // Report latency from the start: hosts may ask before prepareToPlay. It's the oversampling filters' delay plus
    // the ceiling's look-ahead (there whether the ceiling is on or off, so switching it never moves the timing);
    // the look-ahead is 2 ms, so this is it at 48 kHz, and prepareToPlay sets it again for the real sample rate.
    auto probe = makeOversampler (2);
    probe->initProcessing (512);
    setLatencySamples (juce::roundToInt (probe->getLatencyInSamples()) + halation::ceilingLatency (48000.0));
}

halation::Params HalationAudioProcessor::currentParams() const
{
    halation::Params p;
    p.splits = { pSplit[0]->load(), pSplit[1]->load(), pSplit[2]->load() };
    for (int b = 0; b < halation::numBands; ++b)
    {
        const auto& v = pBand[(size_t) b];
        auto& band = p.bands[(size_t) b];
        band.thresholdDb = v[0]->load(); band.ratio = v[1]->load(); band.attackMs = v[2]->load();
        band.releaseMs = v[3]->load(); band.makeupDb = v[4]->load(); band.excite = v[5]->load() * 0.01f;
        band.solo = v[6]->load() > 0.5f; band.bypass = v[7]->load() > 0.5f;
        band.tone = juce::jlimit (0, halation::numTones - 1, juce::roundToInt (v[8]->load())); band.exciteMix = v[9]->load() * 0.01f;
    }
    p.inputDb = pInput->load(); p.outputDb = pOutput->load(); p.mix = pMix->load() * 0.01f;
    p.ceiling = pCeiling->load() > 0.5f;
    return p;
}

void HalationAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    osFs = fs * (1 << oversamplingStages);
    maxChunk = juce::jmax (16, samplesPerBlock);
    // Always ready for two channels, whatever the layout is now: a host may switch a mono track to stereo without
    // preparing again, and an oversampler set up for one channel would then write past its buffers
    oversampler = makeOversampler (2);
    oversampler->initProcessing ((size_t) maxChunk);
    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()) + halation::ceilingLatency (fs));
    ceilingLimiter.prepare (fs);
    ceilingLimiter.setParams ({ 0.0f, halation::ceilingDb, halation::ceilingReleaseMs, true });
    ceilingDelay.setSize (2, halation::ceilingLatency (fs));
    ceilingDry.setSize (2, maxChunk);

    const int osMax = maxChunk * (1 << oversamplingStages);
    for (auto& b : bandBuffers) b.setSize (2, osMax);
    dryBuffer.setSize (2, osMax);
    harmonicsBuffer.setSize (2, osMax);
    splitsAt.assign ((size_t) (osMax / glideStep + 2), halation::Splits {});
    for (auto& c : compressors) c.prepare (osFs, 2);

    // The harmonics' low-pass: steep (16th-order Butterworth) at 0.4 of the host's rate, so nothing the exciters make
    // reaches the band where the down-sampling filter lets it fold back
    const auto qs = lbaudio::dsp::butterworthQs (antiAliasOrder);
    for (auto& chain : antiAlias)
        for (size_t s = 0; s < chain.size(); ++s)
            chain[s].c = lbaudio::dsp::design (lbaudio::dsp::Shape::LowPass, 0.4 * fs, 0.0, qs[s], osFs);

    // The bypassed sound is the input, delayed by the latency
    bypassDelay.setSize (2, getLatencySamples());
    bypassDelay.clear();
    bypassPos = 0;

    smoothCoeff = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
    exciteAttack = std::exp (-1.0 / (halation::exciteAttackS * osFs));
    exciteRelease = std::exp (-1.0 / (halation::exciteReleaseS * osFs));

    const auto p = currentParams();
    inGain = juce::Decibels::decibelsToGain (p.inputDb);
    outGain = juce::Decibels::decibelsToGain (p.outputDb);
    mix = p.mix;
    ceilingAmount = p.ceiling ? 1.0f : 0.0f;
    for (int b = 0; b < halation::numBands; ++b)
    {
        excite[(size_t) b] = p.bands[(size_t) b].excite;
        exciteMix[(size_t) b] = p.bands[(size_t) b].exciteMix;
        compThreshold[(size_t) b] = p.bands[(size_t) b].thresholdDb;
        compRatio[(size_t) b] = p.bands[(size_t) b].ratio;
        exciters[(size_t) b].fade = 1.0f;
        exciters[(size_t) b].setTone (p.bands[(size_t) b].tone);
    }
    currentSplits = { -1.0f, -1.0f, -1.0f };
    reset();
}

void HalationAudioProcessor::reset()
{
    if (oversampler) oversampler->reset();
    for (auto& x : crossover) x.reset();
    for (auto& c : compressors) c.reset();
    for (auto& e : exciters) e.reset();
    for (auto& chain : antiAlias) for (auto& f : chain) f.reset();
    antiAliasRunning = false;
    ceilingLimiter.reset();
    ceilingDelay.clear();
    ceilingPos = 0;
    bypassDelay.clear();
    bypassPos = 0;
}

bool HalationAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void HalationAudioProcessor::setSplits (const halation::Splits& s)
{
    currentSplits = s;
    for (auto& x : crossover) x.set (currentSplits, osFs);
    const float edges[halation::numBands + 1] = { 20.0f, s.low, s.mid, s.high, 20000.0f };
    for (int b = 0; b < halation::numBands; ++b) exciters[(size_t) b].setEdges (edges[b], edges[b + 1], osFs);
}

void HalationAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (oversampler == nullptr || channels == 0 || n == 0) return;

    // Keep the input, the latency back, for the bypassed sound: switching to bypass then stays in time
    if (const int len = bypassDelay.getNumSamples(); len > 0)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            float* d = bypassDelay.getWritePointer (ch);
            const float* x = buffer.getReadPointer (ch);
            int pos = bypassPos;
            for (int i = 0; i < n; ++i) { d[pos] = x[i]; if (++pos == len) pos = 0; }
        }
        bypassPos = (bypassPos + n) % len;
    }

    const auto p = currentParams();

    // Split points: ordered here, then they glide (in octaves) a few samples at a time inside processChunk, so
    // moving one never steps the filters by much at once
    targetSplits = halation::orderedSplits (p.splits, fs);
    if (currentSplits.low < 0.0f) setSplits (targetSplits);

    for (int start = 0; start < n; start += maxChunk)
    {
        const int len = juce::jmin (maxChunk, n - start);
        float* ptrs[2] = { buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr };
        processChunk (ptrs, channels, len, p);
    }
}

void HalationAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    const int len = bypassDelay.getNumSamples();
    if (len == 0 || n == 0) return;
    for (int ch = 0; ch < channels; ++ch)
    {
        float* d = bypassDelay.getWritePointer (ch);
        float* x = buffer.getWritePointer (ch);
        int pos = bypassPos;
        for (int i = 0; i < n; ++i)
        {
            const float delayed = d[pos];
            d[pos] = x[i];
            x[i] = delayed;
            if (++pos == len) pos = 0;
        }
    }
    bypassPos = (bypassPos + n) % len;
}

// The limiter always runs, so the latency is the same on or off; with the Ceiling off the output is the same signal
// from a plain delay as long, and switching fades between the two (20 ms) instead of stepping.
void HalationAudioProcessor::applyCeiling (float* const* io, int numChannels, int numSamples, bool on)
{
    const int len = ceilingDelay.getNumSamples();
    if (len == 0) return;
    const int channels = juce::jmin (2, numChannels);
    for (int ch = 0; ch < channels; ++ch)
    {
        float* d = ceilingDelay.getWritePointer (ch);
        float* s = ceilingDry.getWritePointer (ch);
        int pos = ceilingPos;
        for (int i = 0; i < numSamples; ++i) { s[i] = d[pos]; d[pos] = io[ch][i]; if (++pos == len) pos = 0; }
    }
    ceilingPos = (ceilingPos + numSamples) % len;
    ceilingLimiter.process (io[0], channels > 1 ? io[1] : nullptr, numSamples);

    const float target = on ? 1.0f : 0.0f;
    float a = ceilingAmount;
    if (juce::exactlyEqual (a, 1.0f) && on) return;
    for (int i = 0; i < numSamples; ++i)
    {
        a += (target - a) * smoothCoeff;
        if (std::abs (target - a) < 1.0e-6f) a = target;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float dry = ceilingDry.getReadPointer (ch)[i];
            io[ch][i] = a >= 1.0f ? io[ch][i] : (a <= 0.0f ? dry : dry + a * (io[ch][i] - dry));
        }
    }
    ceilingAmount = a;
}

void HalationAudioProcessor::processChunk (float* const* io, int channels, int n, const halation::Params& p)
{
    // 1) Input level
    const float inTarget = juce::Decibels::decibelsToGain (p.inputDb);
    float inPeak = 0.0f;
    {
        float g = inGain;
        for (int i = 0; i < n; ++i)
        {
            g += (inTarget - g) * smoothCoeff;
            for (int ch = 0; ch < channels; ++ch) { io[ch][i] *= g; inPeak = juce::jmax (inPeak, std::abs (io[ch][i])); }
        }
        inGain = g;
    }
    scopeIn.push (io[0], channels > 1 ? io[1] : nullptr, n);

    // 2) Up to 4x, split into four bands (and the phase-matched dry signal). A moved split glides to its new place a
    //    glideStep at a time (about 30 ms, in octaves), so the filters only ever take small steps.
    juce::dsp::AudioBlock<float> block (io, (size_t) channels, (size_t) n);
    auto up = oversampler->processSamplesUp (block);
    const int upN = (int) up.getNumSamples();
    const int steps = (upN + glideStep - 1) / glideStep;
    const float glide = 1.0f - (float) std::exp (-(double) glideStep / (0.03 * osFs));
    std::array<float, halation::numBands> bands {};
    for (int s = 0; s < steps; ++s)
    {
        if (! halation::sameSplits (currentSplits, targetSplits))
        {
            auto toward = [glide] (float cur, float tgt) { return cur * std::pow (tgt / cur, glide); };
            halation::Splits next { toward (currentSplits.low, targetSplits.low), toward (currentSplits.mid, targetSplits.mid),
                                    toward (currentSplits.high, targetSplits.high) };
            if (halation::sameSplits (next, targetSplits)) next = targetSplits;
            currentSplits = next;
            for (auto& x : crossover) x.set (currentSplits, osFs);
        }
        splitsAt[(size_t) s] = currentSplits;

        const int from = s * glideStep, to = juce::jmin (upN, from + glideStep);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* x = up.getChannelPointer ((size_t) ch);
            float* dry = dryBuffer.getWritePointer (ch);
            float* out[halation::numBands];
            for (int b = 0; b < halation::numBands; ++b) out[b] = bandBuffers[(size_t) b].getWritePointer (ch);
            auto& xo = crossover[(size_t) ch];
            for (int i = from; i < to; ++i)
            {
                xo.process (x[i], bands, dry[i]);
                for (int b = 0; b < halation::numBands; ++b) out[b][i] = bands[(size_t) b];
            }
        }
    }
    for (int ch = 0; ch < channels; ++ch) crossover[(size_t) ch].snap();

    // 3) Each band: compress, then excite. The harmonics of the bands you hear collect in their own buffer.
    bool anySolo = false;
    for (const auto& bp : p.bands) anySolo = anySolo || bp.solo;
    for (int ch = 0; ch < 2; ++ch) harmonicsBuffer.clear (ch, 0, upN);
    bool anyHarmonics = false;
    const float osSmooth = 1.0f - (float) std::exp (-1.0 / (0.02 * osFs));
    for (int b = 0; b < halation::numBands; ++b)
    {
        const auto& bp = p.bands[(size_t) b];
        auto& buf = bandBuffers[(size_t) b];
        auto& ex = exciters[(size_t) b];
        auto followSplits = [&ex, b] (const halation::Splits& sp, double rate)
        {
            const float edges[halation::numBands + 1] = { 20.0f, sp.low, sp.mid, sp.high, 20000.0f };
            if (! juce::exactlyEqual (ex.lowEdge, (double) edges[b]) || ! juce::exactlyEqual (ex.highEdge, (double) edges[b + 1]))
                ex.setEdges (edges[b], edges[b + 1], rate);
        };
        if (bp.bypass)
        {
            excite[(size_t) b] = bp.excite;
            exciteMix[(size_t) b] = bp.exciteMix;
            followSplits (splitsAt[(size_t) steps - 1], osFs);
            continue;
        }
        // Compress. Threshold and ratio glide (about 20 ms) so a moved knob can't jump the gain, even at the fastest
        // attack; the compressor smooths its makeup itself.
        {
            auto& comp = compressors[(size_t) b];
            auto cp = halation::compressorParams (bp);
            float& thr = compThreshold[(size_t) b];
            float& ratio = compRatio[(size_t) b];
            float reduction = 0.0f;
            if (std::abs (thr - bp.thresholdDb) < 0.01f && std::abs (ratio - bp.ratio) < 0.001f)
            {
                thr = bp.thresholdDb; ratio = bp.ratio;
                comp.setParams (cp);
                float* ptrs[2] = { buf.getWritePointer (0), buf.getWritePointer (channels > 1 ? 1 : 0) };
                reduction = comp.process (ptrs, channels, nullptr, 0, upN).maxGainReductionDb;
            }
            else
            {
                const float a = 1.0f - (float) std::exp (-(double) glideStep / (0.02 * osFs));
                for (int from = 0; from < upN; from += glideStep)
                {
                    thr += (bp.thresholdDb - thr) * a;
                    ratio += (bp.ratio - ratio) * a;
                    cp.thresholdDb = thr; cp.ratio = ratio;
                    comp.setParams (cp);
                    float* ptrs[2] = { buf.getWritePointer (0, from), buf.getWritePointer (channels > 1 ? 1 : 0, from) };
                    reduction = juce::jmax (reduction, comp.process (ptrs, channels, nullptr, 0, juce::jmin (glideStep, upN - from)).maxGainReductionDb);
                }
            }
            lbaudio::ui::storeMax (meterReduction[(size_t) b], reduction);
        }

        const float target = bp.excite, mixTarget = bp.exciteMix;
        if (target > 0.0f || excite[(size_t) b] > 1.0e-4f)
        {
            const bool heard = ! anySolo || bp.solo;
            anyHarmonics = anyHarmonics || heard;
            float e = excite[(size_t) b], m = exciteMix[(size_t) b];
            const float* x[2] = { buf.getReadPointer (0), buf.getReadPointer (channels > 1 ? 1 : 0) };
            float* harm[2] = { harmonicsBuffer.getWritePointer (0), harmonicsBuffer.getWritePointer (1) };
            double env = ex.env;
            auto envCh = ex.envCh;
            const float fadeStep = (float) (1.0 / (0.015 * osFs)); // a change of tone: 15 ms out, swap, 15 ms in
            const auto* shape = &halation::toneShape (ex.tone);
            for (int s = 0, i = 0; i < upN; ++s)
            {
                followSplits (splitsAt[(size_t) s], osFs);
                for (const int end = juce::jmin (upN, i + glideStep); i < end; ++i)
                {
                    e += (target - e) * osSmooth;
                    m += (mixTarget - m) * osSmooth;
                    if (ex.tone != bp.tone)
                    {
                        ex.fade -= fadeStep;
                        if (ex.fade <= 0.0f) { ex.fade = 0.0f; ex.setTone (bp.tone); shape = &halation::toneShape (ex.tone); }
                    }
                    else if (ex.fade < 1.0f) ex.fade = juce::jmin (1.0f, ex.fade + fadeStep);

                    float level = 0.0f;
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        const double a = std::abs (x[ch][i]);
                        envCh[(size_t) ch] = a + (a > envCh[(size_t) ch] ? exciteAttack : exciteRelease) * (envCh[(size_t) ch] - a);
                        level = juce::jmax (level, (float) a);
                    }
                    env = level + (level > env ? exciteAttack : exciteRelease) * (env - level);
                    const float ref = juce::jmax ((float) env, halation::exciteFloor), amount = e * m * ex.fade;
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        // This channel's share of the curve's DC: all of it at the band's level, none when it's silent
                        const double own = env > 0.0 ? juce::jmin (1.0, envCh[(size_t) ch] / env) : 0.0;
                        const float h = ref * shape->harmonics (x[ch][i] / ref, e, (float) (own * own));
                        const float y = amount * ex.filter (ch, h);
                        if (heard) harm[ch][i] += y;
                    }
                }
            }
            ex.env = env < 1.0e-12 ? 0.0 : env;
            for (auto& v : envCh) if (v < 1.0e-12) v = 0.0;
            ex.envCh = envCh;
            ex.snap();
            excite[(size_t) b] = e;
            exciteMix[(size_t) b] = m;
        }
        else
        {
            excite[(size_t) b] = target;
            exciteMix[(size_t) b] = mixTarget;
            ex.reset();
            if (ex.tone != bp.tone) ex.setTone (bp.tone);
            ex.fade = 1.0f;
            followSplits (splitsAt[(size_t) steps - 1], osFs);
        }
    }

    // 4) The harmonics, low-passed under the host's Nyquist (while there are some, and once more to ring out)
    if (anyHarmonics || antiAliasRunning)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            float* hh = harmonicsBuffer.getWritePointer (ch);
            auto& chain = antiAlias[(size_t) ch];
            for (int i = 0; i < upN; ++i)
            {
                float v = hh[i];
                for (auto& f : chain) v = f.process (v);
                hh[i] = v;
            }
            for (auto& f : chain) f.snapDenormals();
        }
        if (! anyHarmonics)
            for (auto& chain : antiAlias) for (auto& f : chain) f.reset();
        antiAliasRunning = anyHarmonics;
    }

    // 5) Sum: the soloed bands alone, or every band blended with the dry signal by Mix
    for (int ch = 0; ch < channels; ++ch)
    {
        float* y = up.getChannelPointer ((size_t) ch);
        const float* dry = dryBuffer.getReadPointer (ch);
        const float* hh = harmonicsBuffer.getReadPointer (ch);
        float m = mix;
        for (int i = 0; i < upN; ++i)
        {
            float wet = hh[i];
            for (int b = 0; b < halation::numBands; ++b)
                if (! anySolo || p.bands[(size_t) b].solo)
                    wet += bandBuffers[(size_t) b].getReadPointer (ch)[i];
            m += (p.mix - m) * osSmooth;
            y[i] = anySolo ? wet : dry[i] + m * (wet - dry[i]);
        }
        if (ch == channels - 1) mix = m;
    }

    // 6) Back down, output level
    oversampler->processSamplesDown (block);
    const float outTarget = juce::Decibels::decibelsToGain (p.outputDb);
    {
        float g = outGain;
        for (int i = 0; i < n; ++i)
        {
            g += (outTarget - g) * smoothCoeff;
            for (int ch = 0; ch < channels; ++ch) io[ch][i] *= g;
        }
        outGain = g;
    }

    // 7) The ceiling
    applyCeiling (io, channels, n, p.ceiling);
    float outPeak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i) outPeak = juce::jmax (outPeak, std::abs (io[ch][i]));
    scopeOut.push (io[0], channels > 1 ? io[1] : nullptr, n);
    lbaudio::ui::storeMax (meterInputPeak, inPeak);
    lbaudio::ui::storeMax (meterOutputPeak, outPeak);
}

//==============================================================================
int HalationAudioProcessor::getNumPrograms() { return (int) halation::factoryPresets().size(); }

const juce::String HalationAudioProcessor::getProgramName (int index)
{
    const auto& list = halation::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? juce::String (list[(size_t) index].name) : juce::String();
}

void HalationAudioProcessor::setCurrentProgram (int index)
{
    // Hosts often set program 0 while loading a session: leave the restored settings alone then
    if (index == getCurrentProgram() || ! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    applyFactoryPreset (index);
}

void HalationAudioProcessor::applyFactoryPreset (int index)
{
    const auto& list = halation::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
    const auto& preset = list[(size_t) index];
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    for (const auto& [id, value] : preset.values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    currentProgram = index;
    setPresetName (preset.name);
}

void HalationAudioProcessor::setPresetName (const juce::String& name)
{
    const juce::SpinLock::ScopedLockType l (presetLock);
    presetName = name;
}

juce::File HalationAudioProcessor::userPresetFolder()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("LBAudio").getChildFile ("Halation").getChildFile ("Presets");
}

juce::Array<juce::File> HalationAudioProcessor::userPresets() const
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.halationpreset");
    files.sort();
    return files;
}

bool HalationAudioProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    auto folder = userPresetFolder();
    if (! folder.createDirectory()) return false;
    currentProgram = -1;
    setPresetName (name);
    auto state = apvts.copyState();
    state.setProperty ("presetName", name, nullptr);
    juce::XmlElement root ("HalationPreset");
    root.setAttribute ("name", name);
    if (auto xml = state.createXml()) root.addChildElement (xml.release());
    return root.writeTo (folder.getChildFile (name + ".halationpreset"));
}

bool HalationAudioProcessor::loadUserPreset (const juce::File& file)
{
    auto root = juce::XmlDocument::parse (file);
    if (root == nullptr || ! root->hasTagName ("HalationPreset")) return false;
    auto* stateXml = root->getChildByName (apvts.state.getType());
    if (stateXml == nullptr) return false;
    restoreState (juce::ValueTree::fromXml (*stateXml));
    currentProgram = -1;
    setPresetName (root->getStringAttribute ("name", file.getFileNameWithoutExtension()));
    return true;
}

void HalationAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", getPresetName(), nullptr);
    state.setProperty ("program", currentProgram.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HalationAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    const auto state = juce::ValueTree::fromXml (*xml);
    restoreState (state);
    currentProgram = juce::jlimit (-1, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
    setPresetName (state.getProperty ("presetName", "Init").toString());
}

void HalationAudioProcessor::restoreState (const juce::ValueTree& state)
{
    // A session or preset saved before the Ceiling existed (up to 1.2.2) keeps its sound: the ceiling stays off.
    // (Asked before replaceState, which adds whatever the state is missing to the same tree.)
    const bool savedBeforeCeiling = ! state.getChildWithProperty ("id", halation::ids::ceiling).isValid();
    apvts.replaceState (state);
    // The value tree skips a parameter whose stored value already reads the same (a switch sitting at 0.3 reads
    // off, like a stored 0), so line every parameter up with the state exactly
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            const auto child = state.getChildWithProperty ("id", ranged->getParameterID());
            if (! child.isValid() || ! child.hasProperty ("value")) continue;
            const float target = ranged->convertTo0to1 ((float) child.getProperty ("value"));
            if (std::abs (ranged->getValue() - target) > 1.0e-6f)
                ranged->setValueNotifyingHost (target);
        }
    if (savedBeforeCeiling)
        if (auto* ceiling = apvts.getParameter (halation::ids::ceiling))
            ceiling->setValueNotifyingHost (0.0f);
}

juce::AudioProcessorEditor* HalationAudioProcessor::createEditor() { return new HalationAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HalationAudioProcessor(); }
