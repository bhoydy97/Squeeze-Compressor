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
    return std::make_unique<juce::dsp::Oversampling<float>> ((size_t) channels, (size_t) PrismAudioProcessor::oversamplingStages,
                                                            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PrismAudioProcessor::createParameterLayout()
{
    using namespace prism;
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
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::band (b, "solo"), 1 }, n + " Solo", false,
                                                                juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff)));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::band (b, "bypass"), 1 }, n + " Bypass", false,
                                                                juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff)));
    }

    layout.add (floatParam (ids::input, "Input", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (floatParam (ids::output, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    layout.add (floatParam (ids::mix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, formatPercent, parseNumber));
    return layout;
}

PrismAudioProcessor::PrismAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PrismState", createParameterLayout())
{
    pSplit[0] = apvts.getRawParameterValue (prism::ids::split1);
    pSplit[1] = apvts.getRawParameterValue (prism::ids::split2);
    pSplit[2] = apvts.getRawParameterValue (prism::ids::split3);
    pInput = apvts.getRawParameterValue (prism::ids::input);
    pOutput = apvts.getRawParameterValue (prism::ids::output);
    pMix = apvts.getRawParameterValue (prism::ids::mix);
    for (int b = 0; b < prism::numBands; ++b)
        for (int k = 0; k < 8; ++k)
            pBand[(size_t) b][(size_t) k] = apvts.getRawParameterValue (prism::ids::band (b, prism::ids::bandControls[k]));
    for (auto& m : meterReduction) m.store (0.0f);

    // Report latency from the start: hosts may ask before prepareToPlay. It doesn't depend on the sample rate.
    auto probe = makeOversampler (2);
    probe->initProcessing (512);
    setLatencySamples (juce::roundToInt (probe->getLatencyInSamples()));
}

prism::Params PrismAudioProcessor::currentParams() const
{
    prism::Params p;
    p.splits = { pSplit[0]->load(), pSplit[1]->load(), pSplit[2]->load() };
    for (int b = 0; b < prism::numBands; ++b)
    {
        const auto& v = pBand[(size_t) b];
        auto& band = p.bands[(size_t) b];
        band.thresholdDb = v[0]->load(); band.ratio = v[1]->load(); band.attackMs = v[2]->load();
        band.releaseMs = v[3]->load(); band.makeupDb = v[4]->load(); band.excite = v[5]->load() * 0.01f;
        band.solo = v[6]->load() > 0.5f; band.bypass = v[7]->load() > 0.5f;
    }
    p.inputDb = pInput->load(); p.outputDb = pOutput->load(); p.mix = pMix->load() * 0.01f;
    return p;
}

void PrismAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    osFs = fs * (1 << oversamplingStages);
    maxChunk = juce::jmax (16, samplesPerBlock);
    const int channels = juce::jmax (1, juce::jmin (2, getTotalNumOutputChannels()));

    oversampler = makeOversampler (channels);
    oversampler->initProcessing ((size_t) maxChunk);
    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()));

    const int osMax = maxChunk * (1 << oversamplingStages);
    for (auto& b : bandBuffers) b.setSize (2, osMax);
    dryBuffer.setSize (2, osMax);
    for (auto& c : compressors) c.prepare (osFs, 2);

    smoothCoeff = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
    dcR = std::exp (-2.0 * lbaudio::dsp::pi * 10.0 / osFs);

    const auto p = currentParams();
    inGain = juce::Decibels::decibelsToGain (p.inputDb);
    outGain = juce::Decibels::decibelsToGain (p.outputDb);
    mix = p.mix;
    for (int b = 0; b < prism::numBands; ++b) excite[(size_t) b] = p.bands[(size_t) b].excite;
    currentSplits = { -1.0f, -1.0f, -1.0f };
    reset();
}

void PrismAudioProcessor::reset()
{
    if (oversampler) oversampler->reset();
    for (auto& x : crossover) x.reset();
    for (auto& c : compressors) c.reset();
    for (auto& a : dcX1) a.fill (0.0);
    for (auto& a : dcY1) a.fill (0.0);
}

bool PrismAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void PrismAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int ch = channels; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (oversampler == nullptr || channels == 0 || n == 0) return;

    const auto p = currentParams();

    // Split points: ordered, then glide (in octaves) so dragging them doesn't zip
    const auto target = prism::orderedSplits (p.splits, fs);
    auto next = target;
    if (currentSplits.low > 0.0f)
    {
        const float a = 1.0f - (float) std::exp (-(double) n / (0.03 * fs));
        auto glide = [a] (float cur, float tgt) { return cur * std::pow (tgt / cur, a); };
        next = { glide (currentSplits.low, target.low), glide (currentSplits.mid, target.mid), glide (currentSplits.high, target.high) };
    }
    auto moved = [] (float a, float b) { return std::abs (a - b) > b * 1.0e-4f; };
    if (currentSplits.low < 0.0f || moved (next.low, currentSplits.low) || moved (next.mid, currentSplits.mid) || moved (next.high, currentSplits.high))
    {
        currentSplits = next;
        for (auto& x : crossover) x.set (currentSplits, osFs);
    }

    for (int b = 0; b < prism::numBands; ++b)
        compressors[(size_t) b].setParams (prism::compressorParams (p.bands[(size_t) b]));

    for (int start = 0; start < n; start += maxChunk)
    {
        const int len = juce::jmin (maxChunk, n - start);
        float* ptrs[2] = { buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr };
        processChunk (ptrs, channels, len, p);
    }
}

void PrismAudioProcessor::processChunk (float* const* io, int channels, int n, const prism::Params& p)
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

    // 2) Up to 4x, split into four bands (and the phase-matched dry signal)
    juce::dsp::AudioBlock<float> block (io, (size_t) channels, (size_t) n);
    auto up = oversampler->processSamplesUp (block);
    const int upN = (int) up.getNumSamples();
    std::array<float, prism::numBands> bands {};
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* x = up.getChannelPointer ((size_t) ch);
        float* dry = dryBuffer.getWritePointer (ch);
        float* out[prism::numBands];
        for (int b = 0; b < prism::numBands; ++b) out[b] = bandBuffers[(size_t) b].getWritePointer (ch);
        auto& xo = crossover[(size_t) ch];
        for (int i = 0; i < upN; ++i)
        {
            xo.process (x[i], bands, dry[i]);
            for (int b = 0; b < prism::numBands; ++b) out[b][i] = bands[(size_t) b];
        }
        xo.snap();
    }

    // 3) Each band: compress, then excite
    const float osSmooth = 1.0f - (float) std::exp (-1.0 / (0.02 * osFs));
    for (int b = 0; b < prism::numBands; ++b)
    {
        const auto& bp = p.bands[(size_t) b];
        auto& buf = bandBuffers[(size_t) b];
        if (bp.bypass)
        {
            excite[(size_t) b] = bp.excite;
            continue;
        }
        float* ptrs[2] = { buf.getWritePointer (0), buf.getWritePointer (channels > 1 ? 1 : 0) };
        const auto stats = compressors[(size_t) b].process (ptrs, channels, nullptr, 0, upN);
        lbaudio::ui::storeMax (meterReduction[(size_t) b], stats.maxGainReductionDb);

        const float target = bp.excite;
        if (target > 0.0f || excite[(size_t) b] > 1.0e-4f)
        {
            float e = excite[(size_t) b];
            for (int ch = 0; ch < channels; ++ch)
            {
                float* x = buf.getWritePointer (ch);
                double x1 = dcX1[(size_t) b][(size_t) ch], y1 = dcY1[(size_t) b][(size_t) ch];
                e = excite[(size_t) b];
                for (int i = 0; i < upN; ++i)
                {
                    e += (target - e) * osSmooth;
                    const double h = prism::harmonics (x[i], prism::exciteDrive (e));
                    const double y = h - x1 + dcR * y1; // the harmonics' DC (even ones bring some) is taken out
                    x1 = h; y1 = y;
                    x[i] += prism::exciteMix (e) * (float) y;
                }
                dcX1[(size_t) b][(size_t) ch] = x1;
                dcY1[(size_t) b][(size_t) ch] = std::abs (y1) < 1.0e-20 ? 0.0 : y1;
            }
            excite[(size_t) b] = e;
        }
        else
        {
            excite[(size_t) b] = target;
        }
    }

    // 4) Sum: the soloed bands alone, or every band blended with the dry signal by Mix
    bool anySolo = false;
    for (const auto& bp : p.bands) anySolo = anySolo || bp.solo;
    for (int ch = 0; ch < channels; ++ch)
    {
        float* y = up.getChannelPointer ((size_t) ch);
        const float* dry = dryBuffer.getReadPointer (ch);
        float m = mix;
        for (int i = 0; i < upN; ++i)
        {
            float wet = 0.0f;
            for (int b = 0; b < prism::numBands; ++b)
                if (! anySolo || p.bands[(size_t) b].solo)
                    wet += bandBuffers[(size_t) b].getReadPointer (ch)[i];
            m += (p.mix - m) * osSmooth;
            y[i] = anySolo ? wet : dry[i] + m * (wet - dry[i]);
        }
        if (ch == channels - 1) mix = m;
    }

    // 5) Back down, output level
    oversampler->processSamplesDown (block);
    const float outTarget = juce::Decibels::decibelsToGain (p.outputDb);
    float outPeak = 0.0f;
    {
        float g = outGain;
        for (int i = 0; i < n; ++i)
        {
            g += (outTarget - g) * smoothCoeff;
            for (int ch = 0; ch < channels; ++ch) { io[ch][i] *= g; outPeak = juce::jmax (outPeak, std::abs (io[ch][i])); }
        }
        outGain = g;
    }
    scopeOut.push (io[0], channels > 1 ? io[1] : nullptr, n);
    lbaudio::ui::storeMax (meterInputPeak, inPeak);
    lbaudio::ui::storeMax (meterOutputPeak, outPeak);
}

//==============================================================================
int PrismAudioProcessor::getNumPrograms() { return (int) prism::factoryPresets().size(); }

const juce::String PrismAudioProcessor::getProgramName (int index)
{
    const auto& list = prism::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? juce::String (list[(size_t) index].name) : juce::String();
}

void PrismAudioProcessor::setCurrentProgram (int index)
{
    // Hosts often set program 0 while loading a session: leave the restored settings alone then
    if (index == getCurrentProgram() || ! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    applyFactoryPreset (index);
}

void PrismAudioProcessor::applyFactoryPreset (int index)
{
    const auto& list = prism::factoryPresets();
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

void PrismAudioProcessor::setPresetName (const juce::String& name)
{
    const juce::SpinLock::ScopedLockType l (presetLock);
    presetName = name;
}

juce::File PrismAudioProcessor::userPresetFolder()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("LBAudio").getChildFile ("Prism").getChildFile ("Presets");
}

juce::Array<juce::File> PrismAudioProcessor::userPresets() const
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.prismpreset");
    files.sort();
    return files;
}

bool PrismAudioProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    auto folder = userPresetFolder();
    if (! folder.createDirectory()) return false;
    currentProgram = -1;
    setPresetName (name);
    auto state = apvts.copyState();
    state.setProperty ("presetName", name, nullptr);
    juce::XmlElement root ("PrismPreset");
    root.setAttribute ("name", name);
    if (auto xml = state.createXml()) root.addChildElement (xml.release());
    return root.writeTo (folder.getChildFile (name + ".prismpreset"));
}

bool PrismAudioProcessor::loadUserPreset (const juce::File& file)
{
    auto root = juce::XmlDocument::parse (file);
    if (root == nullptr || ! root->hasTagName ("PrismPreset")) return false;
    auto* stateXml = root->getChildByName (apvts.state.getType());
    if (stateXml == nullptr) return false;
    restoreState (juce::ValueTree::fromXml (*stateXml));
    currentProgram = -1;
    setPresetName (root->getStringAttribute ("name", file.getFileNameWithoutExtension()));
    return true;
}

void PrismAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", getPresetName(), nullptr);
    state.setProperty ("program", currentProgram.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PrismAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    const auto state = juce::ValueTree::fromXml (*xml);
    restoreState (state);
    currentProgram = juce::jlimit (-1, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
    setPresetName (state.getProperty ("presetName", "Init").toString());
}

void PrismAudioProcessor::restoreState (const juce::ValueTree& state)
{
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
}

juce::AudioProcessorEditor* PrismAudioProcessor::createEditor() { return new PrismAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrismAudioProcessor(); }
