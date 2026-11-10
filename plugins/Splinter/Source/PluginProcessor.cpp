#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <LBAudio/Params.h>

using namespace lbaudio::params;
namespace ids = splinter::ids;

namespace
{
std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range,
                                                       float def, juce::String (*toText) (float, int), float (*fromText) (const juce::String&))
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText).withValueFromStringFunction (fromText));
}
std::unique_ptr<juce::AudioParameterBool> boolParam (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff));
}
std::unique_ptr<juce::AudioParameterChoice> choiceParam (const juce::String& id, const juce::String& name, juce::StringArray choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def);
}
juce::String formatSemis (float v, int) { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; }

// One step of note repeat, in beats
double repeatBeats (int choice)
{
    switch (choice) { case 1: return 0.5; case 2: return 0.25; case 3: return 1.0 / 6.0; case 4: return 0.125; default: return 0.0; }
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SplinterAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (floatParam (ids::swing, "Swing", juce::NormalisableRange<float> (50.0f, 75.0f, 0.1f), 55.0f, formatPercent, parseNumber));
    layout.add (choiceParam (ids::noteRepeat, "Note Repeat", { "Off", "1/8", "1/16", "1/16T", "1/32" }, 0));
    layout.add (choiceParam (ids::chopMode, "Chop Mode", { "On the hits", "Even", "Tap" }, 0));
    layout.add (choiceParam (ids::flipLength, "Flip Length", { "1 bar", "2 bars", "4 bars" }, 1));
    layout.add (floatParam (ids::flipBusy, "Flip Busy", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f, formatPercent, parseNumber));
    layout.add (boolParam (ids::flipStart, "Start On Chop 1", true));
    layout.add (boolParam (ids::flipPreview, "Flip Preview", true));
    layout.add (floatParam (ids::volume, "Volume", juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f, formatSignedDb, parseNumber));
    for (int p = 0; p < splinter::numPads; ++p)
    {
        const juce::String n = "Pad " + juce::String (p + 1) + " ";
        layout.add (floatParam (ids::pad (p, "pitch"), n + "Pitch", juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, formatSemis, parseNumber));
        layout.add (floatParam (ids::pad (p, "start"), n + "Start", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, formatPercent, parseNumber));
        layout.add (floatParam (ids::pad (p, "end"), n + "End", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, formatPercent, parseNumber));
        layout.add (floatParam (ids::pad (p, "attack"), n + "Attack", skewed (0.0f, 500.0f, 0.1f, 40.0f), 1.0f, formatMs, parseMs));
        layout.add (floatParam (ids::pad (p, "release"), n + "Release", skewed (5.0f, 2000.0f, 0.1f, 200.0f), 120.0f, formatMs, parseMs));
        layout.add (floatParam (ids::pad (p, "filter"), n + "Filter", frequencyRange (60.0f, 20000.0f), 20000.0f, formatHz, parseHz));
        layout.add (floatParam (ids::pad (p, "resonance"), n + "Resonance", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, formatPercent, parseNumber));
        layout.add (floatParam (ids::pad (p, "drive"), n + "Drive", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, formatPercent, parseNumber));
        layout.add (choiceParam (ids::pad (p, "era"), n + "Era", { "Clean", "12-bit grit", "Vintage 26k", "Dusty" }, 0));
        layout.add (boolParam (ids::pad (p, "reverse"), n + "Reverse", false));
        layout.add (boolParam (ids::pad (p, "oneshot"), n + "One-shot", true));
        layout.add (choiceParam (ids::pad (p, "choke"), n + "Choke", { "Off", "1", "2", "3", "4" }, 0));
    }
    return layout;
}

SplinterAudioProcessor::SplinterAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SplinterState", createParameterLayout())
{
    pSwing = apvts.getRawParameterValue (ids::swing);       pRepeat = apvts.getRawParameterValue (ids::noteRepeat);
    pMode = apvts.getRawParameterValue (ids::chopMode);     pLength = apvts.getRawParameterValue (ids::flipLength);
    pBusy = apvts.getRawParameterValue (ids::flipBusy);     pStart = apvts.getRawParameterValue (ids::flipStart);
    pPreview = apvts.getRawParameterValue (ids::flipPreview); pVolume = apvts.getRawParameterValue (ids::volume);
    for (int p = 0; p < splinter::numPads; ++p)
        for (int k = 0; k < ids::numPadControls; ++k)
            pPad[(size_t) p][(size_t) k] = apvts.getRawParameterValue (ids::pad (p, ids::padControls[k]));
    for (auto& h : lastHit) h.store (-100.0);
    for (auto& v : lastVelocity) v.store (0.0f);
    for (auto& c : chopSets) c = std::make_shared<ChopSet>();
}

//==============================================================================
bool SplinterAudioProcessor::loadSample (const juce::File& file)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) return false;
    const int n = (int) std::min (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
    juce::AudioBuffer<float> buf (2, n);
    reader->read (&buf, 0, n, 0, true, true);
    if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, n);
    lastFile = file;
    return loadSample (buf, reader->sampleRate, file.getFileName());
}

bool SplinterAudioProcessor::loadSample (const juce::AudioBuffer<float>& audio, double rate, const juce::String& name)
{
    if (audio.getNumSamples() < 64) return false;
    auto d = std::make_shared<SampleData>();
    d->name = name;
    d->sample.rate = rate;
    const int n = audio.getNumSamples();
    d->sample.l.assign (audio.getReadPointer (0), audio.getReadPointer (0) + n);
    d->sample.r.assign (audio.getReadPointer (juce::jmin (1, audio.getNumChannels() - 1)), audio.getReadPointer (juce::jmin (1, audio.getNumChannels() - 1)) + n);
    const auto span = splinter::soundSpan (d->sample);
    d->soundStart = span.first; d->soundEnd = span.second;
    const auto env = splinter::onsetEnvelope (d->sample);
    d->hits = splinter::chopsOnHits (d->sample, env);
    d->even = splinter::chopsEven (d->sample);
    d->hitRoles = splinter::chopRoles (d->sample, d->hits);
    d->evenRoles = splinter::chopRoles (d->sample, d->even);
    const std::string nm = name.toStdString();
    if (auto t = splinter::tempoFromName (nm)) { d->tempo = *t; d->tempoFromName = true; }
    else d->tempo = splinter::detectTempo (env);
    if (auto k = splinter::keyFromName (nm)) { d->key = *k; d->keyFromName = true; }
    else d->key = splinter::detectKey (d->sample);
    std::atomic_store (&sample, std::shared_ptr<const SampleData> (d));
    taps = { d->soundStart };
    {
        const juce::SpinLock::ScopedLockType l (flipLock);
        history.clear(); historyIndex = -1; locked.clear();
    }
    setPattern (nullptr);
    rebuildChops();
    return true;
}

void SplinterAudioProcessor::rebuildChops()
{
    auto d = sampleData();
    if (d == nullptr) return;
    auto make = [&] (const std::vector<int>& pts, std::vector<splinter::Role> roles)
    {
        auto c = std::make_shared<ChopSet>();
        c->points = pts; c->end = d->soundEnd;
        c->roles = roles.size() == pts.size() ? roles : splinter::chopRoles (d->sample, pts);
        return std::shared_ptr<const ChopSet> (c);
    };
    std::atomic_store (&chopSets[0], make (d->hits, d->hitRoles));
    std::atomic_store (&chopSets[1], make (d->even, d->evenRoles));
    std::atomic_store (&chopSets[2], make (taps, {}));
}

splinter::ChopMode SplinterAudioProcessor::chopMode() const { return (splinter::ChopMode) juce::jlimit (0, 2, (int) pMode->load()); }
std::shared_ptr<const ChopSet> SplinterAudioProcessor::chopsFor (splinter::ChopMode m) const { return std::atomic_load (&chopSets[(int) m]); }
std::shared_ptr<const ChopSet> SplinterAudioProcessor::chops() const { return chopsFor (chopMode()); }

void SplinterAudioProcessor::addTap (int pos)
{
    auto d = sampleData();
    if (d == nullptr || (int) taps.size() >= splinter::numPads) return;
    pos = juce::jlimit (d->soundStart, d->soundEnd - 1, pos);
    for (int t : taps) if (std::abs (t - pos) < (int) (0.05 * d->sample.rate)) return;
    taps.push_back (pos); std::sort (taps.begin(), taps.end());
    rebuildChops();
}

void SplinterAudioProcessor::removeTapNear (int pos)
{
    auto d = sampleData();
    if (d == nullptr || taps.size() <= 1) return;
    auto it = std::min_element (taps.begin() + 1, taps.end(), [pos] (int a, int b) { return std::abs (a - pos) < std::abs (b - pos); });
    if (it != taps.end() && std::abs (*it - pos) < (int) (0.05 * d->sample.rate)) { taps.erase (it); rebuildChops(); }
}

void SplinterAudioProcessor::clearTaps()
{
    if (auto d = sampleData()) { taps = { d->soundStart }; rebuildChops(); }
}

void SplinterAudioProcessor::previewSample (bool on) { previewRequest.store (on ? 1 : 2); }

//==============================================================================
splinter::PadSettings SplinterAudioProcessor::padSettings (int pad) const
{
    const auto& v = pPad[(size_t) juce::jlimit (0, splinter::numPads - 1, pad)];
    splinter::PadSettings s;
    s.pitch = v[0]->load(); s.start = v[1]->load() * 0.01f; s.end = v[2]->load() * 0.01f;
    s.attackMs = v[3]->load(); s.releaseMs = v[4]->load(); s.cutoff = v[5]->load();
    s.resonance = v[6]->load() * 0.01f; s.drive = v[7]->load() * 0.01f;
    s.era = (splinter::Era) juce::jlimit (0, 3, juce::roundToInt (v[8]->load()));
    s.reverse = v[9]->load() > 0.5f; s.oneShot = v[10]->load() > 0.5f;
    s.choke = juce::jlimit (0, 4, juce::roundToInt (v[11]->load()));
    return s;
}

void SplinterAudioProcessor::padDown (int pad, float velocity)
{
    int start1, size1, start2, size2;
    uiFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) uiEvents[(size_t) start1] = { pad, velocity, true };
    else if (size2 > 0) uiEvents[(size_t) start2] = { pad, velocity, true };
    uiFifo.finishedWrite (size1 + size2);
}

void SplinterAudioProcessor::padUp (int pad)
{
    int start1, size1, start2, size2;
    uiFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) uiEvents[(size_t) start1] = { pad, 0.0f, false };
    else if (size2 > 0) uiEvents[(size_t) start2] = { pad, 0.0f, false };
    uiFifo.finishedWrite (size1 + size2);
}

//==============================================================================
splinter::FlipSettings SplinterAudioProcessor::flipSettings() const
{
    splinter::FlipSettings fs;
    const int len = juce::roundToInt (pLength->load());
    fs.bars = len == 0 ? 1 : (len == 1 ? 2 : 4);
    fs.busy = pBusy->load() * 0.01f;
    fs.startOnChop1 = pStart->load() > 0.5f;
    return fs;
}

void SplinterAudioProcessor::setPattern (std::shared_ptr<const splinter::Pattern> p) { std::atomic_store (&current, p); }

void SplinterAudioProcessor::flip()
{
    auto cs = chops();
    if (cs == nullptr || cs->size() == 0) return;
    std::shared_ptr<const splinter::Pattern> next;
    {
        const juce::SpinLock::ScopedLockType l (flipLock);
        const auto keep = historyIndex >= 0 ? history[(size_t) historyIndex] : nullptr;
        auto p = std::make_shared<splinter::Pattern> (splinter::makeFlip (cs->roles, cs->size(), flipSettings(),
                                                                           (unsigned) (history.size() + 1) * 7919u + (unsigned) cs->size(),
                                                                           keep != nullptr ? locked : std::set<int> {}, keep.get()));
        history.push_back (p); historyIndex = (int) history.size() - 1;
        next = p;
    }
    setPattern (next);
}

void SplinterAudioProcessor::stepFlip (int delta)
{
    std::shared_ptr<const splinter::Pattern> next;
    {
        const juce::SpinLock::ScopedLockType l (flipLock);
        if (history.empty()) return;
        historyIndex = juce::jlimit (0, (int) history.size() - 1, historyIndex + delta);
        next = history[(size_t) historyIndex];
    }
    setPattern (next);
}

int SplinterAudioProcessor::flipIndex() const { const juce::SpinLock::ScopedLockType l (flipLock); return historyIndex; }
int SplinterAudioProcessor::flipCount() const { const juce::SpinLock::ScopedLockType l (flipLock); return (int) history.size(); }
std::set<int> SplinterAudioProcessor::lockedRows() const { const juce::SpinLock::ScopedLockType l (flipLock); return locked; }
void SplinterAudioProcessor::setRowLocked (int pad, bool on)
{
    const juce::SpinLock::ScopedLockType l (flipLock);
    if (on) locked.insert (pad); else locked.erase (pad);
}

double SplinterAudioProcessor::tempoForTiming() const
{
    if (hostBpm.load() > 0.0) return hostBpm.load();
    if (auto d = sampleData(); d != nullptr && d->tempo > 0.0) return d->tempo;
    return 120.0;
}

juce::MidiFile SplinterAudioProcessor::patternMidi() const
{
    juce::MidiFile mf; constexpr int tpq = 960;
    mf.setTicksPerQuarterNote (tpq);
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / tempoForTiming())), 0);
    if (auto p = pattern())
    {
        const double swing = pSwing->load() * 0.01;
        for (auto& h : p->hits)
        {
            const double startTicks = (h.step + splinter::swingOffsetSteps (h.step, swing)) * tpq / 4.0;
            const int note = splinter::baseNote + h.pad;
            const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (h.velocity * 127.0f));
            seq.addEvent (juce::MidiMessage::noteOn (1, note, vel), startTicks);
            seq.addEvent (juce::MidiMessage::noteOff (1, note), startTicks + tpq / 4.0 * 0.9);
        }
        seq.addEvent (juce::MidiMessage::endOfTrack(), p->steps * tpq / 4.0);
    }
    seq.updateMatchedPairs();
    mf.addTrack (seq);
    return mf;
}

juce::File SplinterAudioProcessor::writePatternMidi() const
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Splinter Flip " + juce::String (juce::jmax (1, flipIndex() + 1)) + ".mid");
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (out.openedOk()) { patternMidi().writeTo (out); out.flush(); }
    return f;
}

//==============================================================================
void SplinterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    for (auto& sc : scratch) sc.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    evs.reserve (1024);
    reset();
}

void SplinterAudioProcessor::reset()
{
    bank.allOff();
    volGain = juce::Decibels::decibelsToGain (pVolume->load());
    held.fill (false);
    freeBeat = 0.0;
}

bool SplinterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SplinterAudioProcessor::triggerPad (int pad, float vel, const ChopSet& cs, const std::array<splinter::PadSettings, splinter::numPads>& all)
{
    auto d = sampleData();
    if (d == nullptr || pad < 0 || pad >= cs.size()) return;
    const auto [a, b] = cs.span (pad);
    bank.trigger (pad, all[(size_t) pad], all, a, b, vel, d->sample.rate, sr);
    lastHit[(size_t) pad].store (sampleTime);
    lastVelocity[(size_t) pad].store (vel);
}

void SplinterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0) { midi.clear(); return; }
    auto d = sampleData();
    auto cs = chops();
    std::array<splinter::PadSettings, splinter::numPads> all;
    for (int p = 0; p < splinter::numPads; ++p) all[(size_t) p] = padSettings (p);

    // The host's clock
    bool playing = false; double ppq = 0.0, bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) ppq = *q; else playing = false;
        }
    hostBpm.store (bpm); hostPlaying.store (playing);
    const double tempo = bpm > 0.0 ? bpm : (d != nullptr && d->tempo > 0.0 ? d->tempo : 120.0);
    const double beatsPerSample = tempo / 60.0 / sr;
    const double beat0 = playing ? ppq : freeBeat;
    const double swing = pSwing->load() * 0.01;

    // kind 0 on, 1 off, 2 preview start, 3 preview stop, 4 a note-repeat hit
    evs.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int pad = m.getNoteNumber() - splinter::baseNote;
        if (pad < 0 || pad >= splinter::numPads) continue;
        if (m.isNoteOn()) evs.push_back ({ meta.samplePosition, pad, m.getFloatVelocity(), 0 });
        else if (m.isNoteOff()) evs.push_back ({ meta.samplePosition, pad, 0.0f, 1 });
    }
    {
        int s1, b1, s2, b2;
        const int ready = uiFifo.getNumReady();
        uiFifo.prepareToRead (ready, s1, b1, s2, b2);
        for (int i = 0; i < b1; ++i) { auto& e = uiEvents[(size_t) (s1 + i)]; evs.push_back ({ 0, e.pad, e.vel, e.on ? 0 : 1 }); }
        for (int i = 0; i < b2; ++i) { auto& e = uiEvents[(size_t) (s2 + i)]; evs.push_back ({ 0, e.pad, e.vel, e.on ? 0 : 1 }); }
        uiFifo.finishedRead (b1 + b2);
    }
    if (const int pr = previewRequest.exchange (0); pr != 0) evs.push_back ({ 0, -1, 1.0f, pr == 1 ? 2 : 3 });

    // Flip preview: the pattern's steps that fall in this block, while the host plays
    auto pat = pattern();
    int stepNow = -1;
    if (pat != nullptr && playing && pPreview->load() > 0.5f && ! pat->hits.empty())
    {
        const double b1 = beat0 + n * beatsPerSample;
        stepNow = beat0 >= 0.0 ? ((int) std::floor (beat0 * 4.0)) % pat->steps : -1;
        for (long k = (long) std::floor (beat0 * 4.0) - 1; k <= (long) std::floor (b1 * 4.0) + 1; ++k)
        {
            if (k < 0) continue;
            const int step = (int) (k % pat->steps);
            const double at = (k + splinter::swingOffsetSteps (step, swing)) / 4.0;
            if (at < beat0 || at >= b1) continue;
            const int off = juce::jlimit (0, n - 1, (int) ((at - beat0) / beatsPerSample));
            for (auto& h : pat->hits) if (h.step == step) evs.push_back ({ off, h.pad, h.velocity, 0 });
        }
    }
    patternStep.store (stepNow);

    // Note repeat: a held pad plays again on every step of the grid
    const int nr = juce::roundToInt (pRepeat->load());
    const double grid = repeatBeats (nr);
    bool anyHeld = false;
    // (held pads are updated as their events are applied below; repeats come from the state at the block start)
    if (grid > 0.0)
    {
        const double b1 = beat0 + n * beatsPerSample;
        for (int p = 0; p < splinter::numPads; ++p)
        {
            if (! held[(size_t) p]) continue;
            anyHeld = true;
            for (long k = (long) std::floor (beat0 / grid); k <= (long) std::floor (b1 / grid) + 1; ++k)
            {
                const bool swung = (nr == 1 || nr == 2) && (k % 2 == 1);
                const double at = (k + (swung ? (swing - 0.5) * 2.0 : 0.0)) * grid;
                if (at < beat0 || at >= b1 || at - lastRepeatBeat[(size_t) p] < grid * 0.5) continue;
                const int off = juce::jlimit (0, n - 1, (int) ((at - beat0) / beatsPerSample));
                evs.push_back ({ off, p, heldVel[(size_t) p], 4 });
                lastRepeatBeat[(size_t) p] = at;
            }
        }
    }
    repeating.store (anyHeld);
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.at < b.at; });

    float* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0) };
    if ((int) scratch[0].size() < n) for (auto& sc : scratch) sc.resize ((size_t) n);   // only if the host sends a bigger block than it said
    for (auto& sc : scratch) std::fill (sc.begin(), sc.begin() + n, 0.0f);
    float* mix[2] = { scratch[0].data(), scratch[1].data() };
    int cursor = 0;
    auto renderTo = [&] (int upto)
    {
        if (d == nullptr || upto <= cursor) return;
        for (auto& v : bank.voices)
            if (v.active) v.render (d->sample, mix, cursor, upto, sr);
        cursor = upto;
    };
    for (auto& e : evs)
    {
        renderTo (e.at);
        sampleTime = audioTime.load() + e.at / sr;
        if (e.kind == 0 || e.kind == 4)
        {
            if (cs != nullptr) triggerPad (e.pad, e.vel, *cs, all);
            if (e.kind == 0) { held[(size_t) e.pad] = true; heldVel[(size_t) e.pad] = e.vel; lastRepeatBeat[(size_t) e.pad] = beat0 + e.at * beatsPerSample; }
        }
        else if (e.kind == 1) { held[(size_t) e.pad] = false; bank.release (e.pad); }
        else if (e.kind == 2 && d != nullptr)
        {
            splinter::PadSettings ps; ps.releaseMs = 30.0f;
            std::array<splinter::PadSettings, splinter::numPads> none {};
            bank.trigger (splinter::numPads, ps, none, d->soundStart, d->soundEnd, 1.0f, d->sample.rate, sr);
            previewOn.store (true);
        }
        else if (e.kind == 3)
        {
            for (auto& v : bank.voices) if (v.active && v.pad == splinter::numPads) v.choke (sr);
            previewOn.store (false);
        }
    }
    renderTo (n);

    // Out, at the volume
    const float target = juce::Decibels::decibelsToGain (pVolume->load());
    for (int i = 0; i < n; ++i)
    {
        volGain += (target - volGain) * 0.002f;
        out[0][i] = mix[0][i] * volGain;
        if (buffer.getNumChannels() > 1) out[1][i] = mix[1][i] * volGain;
    }

    // For the window: where the newest voice is
    if (const auto* v = bank.newest())
    {
        if (v->pad == splinter::numPads) { previewPos.store (v->pos); playChop.store (-1); playPos.store (v->pos); }
        else { playChop.store (v->pad); playPos.store (v->pos); }
    }
    else { playChop.store (-1); playPos.store (-1.0); }
    bool previewing = false;
    for (auto& v : bank.voices) if (v.active && v.pad == splinter::numPads) previewing = true;
    if (! previewing) { previewOn.store (false); previewPos.store (-1.0); }

    freeBeat += n * beatsPerSample;
    audioTime.store (audioTime.load() + n / sr);
    sampleTime = audioTime.load();
    midi.clear();
}

//==============================================================================
void SplinterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("Splinter");
    root.appendChild (apvts.copyState(), nullptr);
    if (auto d = sampleData())
    {
        juce::ValueTree s ("Sample");
        s.setProperty ("name", d->name, nullptr);
        s.setProperty ("path", lastFile.getFullPathName(), nullptr);
        s.setProperty ("rate", d->sample.rate, nullptr);
        // the audio itself, losslessly compressed, so the session opens anywhere
        juce::MemoryBlock flac;
        {
            std::unique_ptr<juce::OutputStream> mos = std::make_unique<juce::MemoryOutputStream> (flac, false);
            juce::FlacAudioFormat fmt;
            auto w = fmt.createWriterFor (mos, juce::AudioFormatWriterOptions{}.withSampleRate (d->sample.rate).withNumChannels (2).withBitsPerSample (24));
            if (w != nullptr)
            {
                const float* ch[2] = { d->sample.l.data(), d->sample.r.data() };
                w->writeFromFloatArrays (ch, 2, d->sample.length());
            }
        }
        s.setProperty ("flac", juce::var (flac), nullptr);
        juce::StringArray t; for (int x : taps) t.add (juce::String (x));
        s.setProperty ("taps", t.joinIntoString (","), nullptr);
        root.appendChild (s, nullptr);
    }
    {
        const juce::SpinLock::ScopedLockType l (flipLock);
        juce::ValueTree f ("Flips");
        f.setProperty ("index", historyIndex, nullptr);
        juce::StringArray lk; for (int x : locked) lk.add (juce::String (x));
        f.setProperty ("locked", lk.joinIntoString (","), nullptr);
        for (auto& p : history)
        {
            juce::ValueTree pt ("Pattern");
            pt.setProperty ("steps", p->steps, nullptr);
            juce::StringArray hs;
            for (auto& h : p->hits) hs.add (juce::String (h.step) + ":" + juce::String (h.pad) + ":" + juce::String (h.velocity, 3));
            pt.setProperty ("hits", hs.joinIntoString (" "), nullptr);
            f.appendChild (pt, nullptr);
        }
        root.appendChild (f, nullptr);
    }
    root.setProperty ("selected", selectedPad.load(), nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, destData);
}

void SplinterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("Splinter")) return;
    if (auto st = root.getChildWithName (apvts.state.getType()); st.isValid()) apvts.replaceState (st);
    if (auto s = root.getChildWithName ("Sample"); s.isValid())
    {
        bool ok = false;
        if (auto* mb = s["flac"].getBinaryData(); mb != nullptr && mb->getSize() > 0)
        {
            juce::FlacAudioFormat fmt;
            std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (new juce::MemoryInputStream (*mb, false), true));
            if (r != nullptr)
            {
                juce::AudioBuffer<float> buf (2, (int) r->lengthInSamples);
                r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true);
                lastFile = juce::File (s["path"].toString());
                ok = loadSample (buf, r->sampleRate, s["name"].toString());
            }
        }
        if (! ok && juce::File (s["path"].toString()).existsAsFile()) ok = loadSample (juce::File (s["path"].toString()));
        if (ok)
        {
            taps.clear();
            for (auto& t : juce::StringArray::fromTokens (s["taps"].toString(), ",", "")) if (t.isNotEmpty()) taps.push_back (t.getIntValue());
            if (taps.empty()) if (auto d = sampleData()) taps = { d->soundStart };
            rebuildChops();
        }
    }
    if (auto f = root.getChildWithName ("Flips"); f.isValid())
    {
        std::vector<std::shared_ptr<const splinter::Pattern>> h;
        for (auto pt : f)
        {
            auto p = std::make_shared<splinter::Pattern>();
            p->steps = pt["steps"];
            for (auto& tok : juce::StringArray::fromTokens (pt["hits"].toString(), " ", ""))
            {
                auto parts = juce::StringArray::fromTokens (tok, ":", "");
                if (parts.size() == 3) p->hits.push_back ({ parts[0].getIntValue(), parts[1].getIntValue(), parts[2].getFloatValue() });
            }
            h.push_back (p);
        }
        std::shared_ptr<const splinter::Pattern> cur;
        {
            const juce::SpinLock::ScopedLockType l (flipLock);
            history = h;
            historyIndex = juce::jlimit (-1, (int) history.size() - 1, (int) f["index"]);
            locked.clear();
            for (auto& t : juce::StringArray::fromTokens (f["locked"].toString(), ",", "")) if (t.isNotEmpty()) locked.insert (t.getIntValue());
            if (historyIndex >= 0) cur = history[(size_t) historyIndex];
        }
        setPattern (cur);
    }
    selectedPad.store (juce::jlimit (0, splinter::numPads - 1, (int) root.getProperty ("selected", 5)));
}

juce::AudioProcessorEditor* SplinterAudioProcessor::createEditor() { return new SplinterAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SplinterAudioProcessor(); }
