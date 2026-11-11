// Splinter's tests: the VST3 as a DAW loads it, then the processor itself.
//   SplinterTests <Splinter.vst3> [snapshot.png]
//   SplinterTests --promo <script.json> <out-dir>     (renders the promo's plugin footage and audio)

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Harness.h"
#include "SplinterVideo.h"

using namespace lbtest;

namespace
{
constexpr double kitRate = 44100.0;
// nine decaying tones, then a short click-like hit (for timing note repeat)
const float kitFreqs[] = { 200.0f, 300.0f, 450.0f, 675.0f, 1000.0f, 1500.0f, 2250.0f, 3000.0f, 12000.0f, 800.0f };
const double kitDecay[] = { 0.08, 0.08, 0.08, 0.08, 0.08, 0.08, 0.08, 0.08, 0.08, 0.008 };
constexpr int kitHits = 10, clickPad = 9;
constexpr double kitGap = 0.25, kitLead = 0.05;
const juce::String kitName ("test kit 92bpm A minor.wav");

// Ten hits a quarter-second apart, each a decaying tone at its own pitch
juce::AudioBuffer<float> makeKit()
{
    const int n = (int) (kitRate * (kitLead + kitGap * kitHits + 0.2));
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    const int len = (int) (kitGap * kitRate) - 400, fade = (int) (0.01 * kitRate);
    for (int h = 0; h < kitHits; ++h)
    {
        const int at = (int) std::lround ((kitLead + kitGap * h) * kitRate);
        for (int i = 0; i < len && at + i < n; ++i)
        {
            const double t = i / kitRate;
            const float f = (float) juce::jmin (1, len - i) * juce::jmin (1.0f, (float) (len - i) / (float) fade);
            const float v = 0.5f * f * (float) (std::exp (-t / kitDecay[h]) * std::sin (2.0 * juce::MathConstants<double>::pi * kitFreqs[h] * t));
            b.setSample (0, at + i, v);
            b.setSample (1, at + i, v);
        }
    }
    return b;
}

bool isLocal (juce::AudioProcessor& p) { return dynamic_cast<SplinterAudioProcessor*> (&p) != nullptr; }

// A session's state. A hosted VST3 wraps the plugin's own state in the host's XML, so unwrap and wrap it here.
juce::MemoryBlock stateOf (juce::AudioProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    if (isLocal (p)) return mb;
    juce::MemoryBlock inner;
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
        if (auto* c = xml->getChildByName ("IComponent")) inner.fromBase64Encoding (c->getAllSubText());
    return inner;
}

// The kit loaded (and optionally a Flip made), as a saved session
juce::MemoryBlock kitState (bool withFlip)
{
    SplinterAudioProcessor p;
    p.loadSample (makeKit(), kitRate, kitName);
    if (withFlip) p.flip();
    return stateOf (p);
}

void restore (juce::AudioProcessor& p, const juce::MemoryBlock& mb)
{
    if (isLocal (p)) { p.setStateInformation (mb.getData(), (int) mb.getSize()); return; }
    juce::XmlElement x ("VST3PluginState");
    x.createNewChildElement ("IComponent")->addTextElement (mb.toBase64Encoding());
    juce::MemoryBlock wrapped;
    juce::AudioProcessor::copyXmlToBinary (x, wrapped);
    p.setStateInformation (wrapped.getData(), (int) wrapped.getSize());
}

// A DAW's transport
struct Transport final : juce::AudioPlayHead
{
    double bpm = 0.0, rate = 48000.0; bool playing = false; juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (bpm <= 0.0) return {};
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setTimeInSamples (sample);
        info.setTimeInSeconds ((double) sample / rate);
        info.setPpqPosition ((double) sample / rate * bpm / 60.0);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

struct Note { double t; int pad; float vel; bool on; };

// Plays notes into the plugin (already prepared) for 'seconds'; returns left and right
std::array<std::vector<float>, 2> play (juce::AudioProcessor& p, double seconds, std::vector<Note> notes, Transport* tp = nullptr)
{
    const int total = (int) std::lround (seconds * sampleRate);
    juce::AudioBuffer<float> buffer (juce::jmax (2, p.getTotalNumOutputChannels()), blockSize);
    std::array<std::vector<float>, 2> out;
    if (tp != nullptr) { tp->rate = sampleRate; tp->sample = 0; p.setPlayHead (tp); }
    for (int start = 0; start < total; start += blockSize)
    {
        const int n = juce::jmin (blockSize, total - start);
        buffer.setSize (buffer.getNumChannels(), n, false, false, true);
        buffer.clear();
        juce::MidiBuffer midi;
        for (auto& e : notes)
        {
            const int at = (int) std::lround (e.t * sampleRate) - start;
            if (at < 0 || at >= n) continue;
            midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, splinter::baseNote + e.pad, e.vel) : juce::MidiMessage::noteOff (1, splinter::baseNote + e.pad), at);
        }
        if (tp != nullptr) tp->sample = start;
        p.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            out[0].push_back (buffer.getSample (0, i));
            out[1].push_back (buffer.getSample (buffer.getNumChannels() > 1 ? 1 : 0, i));
        }
    }
    if (tp != nullptr) p.setPlayHead (nullptr);
    return out;
}

std::vector<float> hit (juce::AudioProcessor& p, int pad, double seconds = 0.4, float vel = 1.0f)
{
    return play (p, seconds, { { 0.0, pad, vel, true }, { 0.3, pad, 0.0f, false } })[0];
}

size_t at (double s) { return (size_t) std::lround (s * sampleRate); }

// Where new hits start: 5 ms windows (a whole cycle of the 200 Hz pad) that jump up
std::vector<double> onsets (const std::vector<float>& v, float floorDb = -40.0f)
{
    constexpr int w = 240;
    std::vector<double> res;
    float prev = 0.0f;
    for (size_t i = 0; i + w <= v.size(); i += w)
    {
        double s = 0; for (size_t j = i; j < i + w; ++j) s += (double) v[j] * v[j];
        const float r = (float) std::sqrt (s / w);
        const double t = (double) i / sampleRate;
        if (r > fromDb (floorDb) && r > 2.2f * prev + 1.0e-6f && (res.empty() || t - res.back() > 0.03)) res.push_back (t);
        prev = r;
    }
    return res;
}

// A fresh session with the kit; then 'tweak' sets parameters
Harness& fresh (std::unique_ptr<Harness>& holder, juce::AudioProcessor& p, const juce::MemoryBlock& state, std::function<void (Harness&)> tweak = nullptr)
{
    holder.reset();
    restore (p, state);
    holder = std::make_unique<Harness> (p);
    if (tweak) tweak (*holder);
    holder->apply();
    return *holder;
}

//==============================================================================
void runSuite (juce::AudioProcessor& p)
{
    runCommonChecks (p);
    check (p.acceptsMidi() && p.getTotalNumInputChannels() == 0 && p.getTotalNumOutputChannels() == 2, "An instrument: MIDI in, stereo out, no audio in",
           juce::String (p.getTotalNumInputChannels()) + " in / " + juce::String (p.getTotalNumOutputChannels()) + " out");

    const auto kit = kitState (false), kitFlip = kitState (true);
    std::unique_ptr<Harness> h;

    // ---- Each pad plays its own chop ---------------------------------------------------------------------------
    fresh (h, p, kit);
    {
        const auto a = hit (p, 0), b = hit (p, 4);
        const float a200 = toneDb (a, at (0.01), at (0.12), 200), a300 = toneDb (a, at (0.01), at (0.12), 300);
        const float b1k = toneDb (b, at (0.01), at (0.12), 1000), b200 = toneDb (b, at (0.01), at (0.12), 200);
        check (a200 > -20 && a200 - a300 > 30, "Pad 1 plays chop 1 (its 200 Hz hit)", fmt (a200) + " dB at 200 Hz, " + fmt (a300) + " at 300");
        check (b1k > -20 && b1k - b200 > 30, "Pad 5 plays chop 5 (its 1 kHz hit)", fmt (b1k) + " dB at 1 kHz, " + fmt (b200) + " at 200");
        const auto none = hit (p, 12);
        check (peakDb (none) < -120, "A pad with no chop is silent", fmt (peakDb (none)) + " dB");
        const auto late = hit (p, 0);
        check (peakDb (late, at (0.27)) < -80, "The chop ends where the next one starts", fmt (peakDb (late, at (0.27))) + " dB after 270 ms");
        const auto soft = hit (p, 0, 0.4, 0.5f);
        const float dv = rmsDb (a, 0, at (0.2)) - rmsDb (soft, 0, at (0.2));
        check (dv > 6 && dv < 12, "Softer hits are quieter", "half velocity " + fmt (dv) + " dB down");
    }

    // ---- Pitch -------------------------------------------------------------------------------------------------
    fresh (h, p, kit, [] (Harness& x) { x.set ("Pad 5 Pitch", "12"); x.set ("Pad 6 Pitch", "-12"); });
    {
        const auto up = hit (p, 4), down = hit (p, 5);
        const float u = toneDb (up, at (0.005), at (0.1), 2000), u0 = toneDb (up, at (0.005), at (0.1), 1000);
        const float d = toneDb (down, at (0.01), at (0.2), 750), d0 = toneDb (down, at (0.01), at (0.2), 1500);
        check (u - u0 > 30, "Pitch +12 plays an octave up", fmt (u) + " dB at 2 kHz vs " + fmt (u0) + " at 1 kHz");
        check (d - d0 > 30, "Pitch -12 plays an octave down", fmt (d) + " dB at 750 Hz vs " + fmt (d0) + " at 1.5 kHz");
        const auto longer = play (p, 0.7, { { 0.0, 5, 1.0f, true } })[0];
        check (peakDb (longer, at (0.27), at (0.45)) > -45 && peakDb (longer, at (0.52)) < -80, "An octave down plays for twice as long",
               fmt (peakDb (longer, at (0.27), at (0.45))) + " dB at 270-450 ms, " + fmt (peakDb (longer, at (0.52))) + " after 520");
    }

    // ---- Reverse, start and end --------------------------------------------------------------------------------
    fresh (h, p, kit, [] (Harness& x) { x.setBool ("Pad 1 Reverse", true); x.set ("Pad 2 Start", "50"); x.set ("Pad 3 End", "20"); });
    {
        const auto r = hit (p, 0);
        const float first = rmsDb (r, 0, at (0.03)), last = rmsDb (r, at (0.2), at (0.235));
        check (last - first > 15, "Reverse plays the chop backwards (swelling up)", "first 30 ms " + fmt (first) + " dB, last " + fmt (last));
        fresh (h, p, kit);
        const auto full = hit (p, 1);
        fresh (h, p, kit, [] (Harness& x) { x.set ("Pad 2 Start", "50"); x.set ("Pad 3 End", "20"); });
        const auto half = hit (p, 1), cut = hit (p, 2);
        const float dStart = peakDb (full, 0, at (0.02)) - peakDb (half, 0, at (0.02));
        check (dStart > 8, "Start moves into the chop (later, quieter part of the hit)", fmt (dStart) + " dB down");
        check (peakDb (cut, at (0.06)) < -80 && peakDb (cut, 0, at (0.04)) > -20, "End cuts the chop short (20 % of 250 ms)", fmt (peakDb (cut, at (0.06))) + " dB after 60 ms");
    }

    // ---- One-shot and gate -------------------------------------------------------------------------------------
    fresh (h, p, kit, [] (Harness& x) { x.setBool ("Pad 1 One-shot", false); x.set ("Pad 1 Release", "20"); });
    {
        const auto gate = play (p, 0.3, { { 0.0, 0, 1.0f, true }, { 0.03, 0, 0.0f, false } })[0];
        fresh (h, p, kit);
        const auto shot = play (p, 0.3, { { 0.0, 0, 1.0f, true }, { 0.03, 0, 0.0f, false } })[0];
        check (peakDb (gate, at (0.09), at (0.2)) < -70, "Gate: letting go of the pad stops it (20 ms release)", fmt (peakDb (gate, at (0.09), at (0.2))) + " dB");
        check (peakDb (shot, at (0.09), at (0.2)) > -25, "One-shot: the chop plays out after you let go", fmt (peakDb (shot, at (0.09), at (0.2))) + " dB");
    }

    // ---- Choke -------------------------------------------------------------------------------------------------
    {
        const std::vector<Note> two { { 0.0, 0, 1.0f, true }, { 0.05, 1, 1.0f, true } };
        fresh (h, p, kit);
        const auto open = play (p, 0.3, two)[0];
        fresh (h, p, kit, [] (Harness& x) { x.setChoice ("Pad 1 Choke", 1); x.setChoice ("Pad 2 Choke", 1); });
        const auto choked = play (p, 0.3, two)[0];
        const float o = toneDb (open, at (0.07), at (0.2), 200), c = toneDb (choked, at (0.07), at (0.2), 200);
        const float o3 = toneDb (open, at (0.07), at (0.2), 300), c3 = toneDb (choked, at (0.07), at (0.2), 300);
        check (o - c > 40, "Choke: pad 2 cuts off pad 1 in the same group", "pad 1 " + fmt (o) + " dB open, " + fmt (c) + " choked");
        check (std::abs (o3 - c3) < 0.5f, "...and pad 2 plays as normal", fmt (o3) + " / " + fmt (c3) + " dB");
        fresh (h, p, kit);
        const auto again = play (p, 0.4, { { 0.0, 0, 1.0f, true }, { 0.1, 0, 1.0f, true } })[0];
        const float k = peakDb (again, at (0.1), at (0.11)), k0 = peakDb (again, at (0.09), at (0.1));
        check (k - k0 > 6, "Hitting a pad again restarts it", fmt (k0) + " dB before, " + fmt (k) + " after");
    }

    // ---- Filter, resonance, drive ------------------------------------------------------------------------------
    {
        fresh (h, p, kit);
        const auto open = hit (p, 6);
        fresh (h, p, kit, [] (Harness& x) { x.set ("Pad 7 Filter", "400"); x.set ("Pad 1 Drive", "100"); x.set ("Pad 2 Filter", "300"); x.set ("Pad 2 Resonance", "100"); });
        const auto shut = hit (p, 6);
        const float dF = toneDb (open, at (0.01), at (0.1), 2250) - toneDb (shut, at (0.01), at (0.1), 2250);
        check (dF > 24, "Filter: down to 400 Hz takes the 2.25 kHz hit down", fmt (dF) + " dB");
        const auto res = hit (p, 1);
        fresh (h, p, kit);
        const auto plain = hit (p, 1);
        const float dR = toneDb (res, at (0.01), at (0.1), 300) - toneDb (plain, at (0.01), at (0.1), 300);
        check (dR > 10, "Resonance lifts the cutoff (a 300 Hz cutoff on a 300 Hz hit)", "+" + fmt (dR) + " dB");
        const auto clean = hit (p, 0);
        fresh (h, p, kit, [] (Harness& x) { x.set ("Pad 1 Drive", "100"); });
        const auto driven = hit (p, 0);
        const float hc = toneDb (clean, at (0.005), at (0.05), 600) - toneDb (clean, at (0.005), at (0.05), 200);
        const float hd = toneDb (driven, at (0.005), at (0.05), 600) - toneDb (driven, at (0.005), at (0.05), 200);
        check (hd - hc > 20, "Drive adds harmonics", "3rd harmonic " + fmt (hc) + " dB clean, " + fmt (hd) + " driven");
        check (peakDb (driven) < 0.5f, "...and stays under full scale", fmt (peakDb (driven)) + " dB");
    }

    // ---- Eras --------------------------------------------------------------------------------------------------
    {
        fresh (h, p, kit);
        const auto clean = hit (p, 8), clean1 = hit (p, 0);
        const float c12k = toneDb (clean, at (0.005), at (0.05), 12000);
        auto withEra = [&] (int era, int pad) { fresh (h, p, kit, [era, pad] (Harness& x) { x.setChoice ("Pad " + juce::String (pad + 1) + " Era", era); }); return hit (p, pad); };
        const auto grit = withEra (1, 0);
        // the grit: what's left once the 200 Hz tone (and its harmonics) are taken out
        auto residue = [&] (const std::vector<float>& v)
        {
            double s = 0, t = 0;
            for (size_t i = at (0.005); i < at (0.1); ++i) { const double d = v[i] - clean1[i]; s += d * d; t += (double) clean1[i] * clean1[i]; }
            return (float) (10.0 * std::log10 (s / t));
        };
        check (residue (grit) > -30 && residue (grit) < -6, "12-bit grit roughens the sound, enough to hear", fmt (residue (grit)) + " dB of grit");
        check (std::abs (rmsDb (grit, 0, at (0.1)) - rmsDb (clean1, 0, at (0.1))) < 1.5f, "...at about the same level", fmt (rmsDb (grit, 0, at (0.1)) - rmsDb (clean1, 0, at (0.1))) + " dB");
        const auto vin = withEra (2, 8), dusty = withEra (3, 8);
        const float v12k = toneDb (vin, at (0.005), at (0.05), 12000), d12k = toneDb (dusty, at (0.005), at (0.05), 12000);
        check (c12k - v12k > 6, "Vintage 26k rolls off the top (12 kHz)", fmt (c12k - v12k) + " dB down");
        check (c12k - d12k > 15, "Dusty darkens the top (12 kHz)", fmt (c12k - d12k) + " dB down");
        const auto vin1 = withEra (2, 0), dusty1 = withEra (3, 0);
        const float vBody = toneDb (vin1, at (0.01), at (0.1), 200) - toneDb (clean1, at (0.01), at (0.1), 200);
        const float dBody = toneDb (dusty1, at (0.01), at (0.1), 200) - toneDb (clean1, at (0.01), at (0.1), 200);
        check (std::abs (vBody) < 1.5f && std::abs (dBody) < 2.5f, "Vintage 26k and Dusty keep the body (200 Hz)", fmt (vBody) + " / " + fmt (dBody) + " dB");
        bool finite = true; for (auto* v : { &grit, &vin, &dusty }) for (float x : *v) finite = finite && std::isfinite (x);
        check (finite, "Every era stays finite", "");
        check (peakDb (dusty1, at (0.3)) < -40, "Dusty's hiss stops with the chop", fmt (peakDb (dusty1, at (0.3))) + " dB after");
    }

    // ---- Volume ------------------------------------------------------------------------------------------------
    {
        fresh (h, p, kit);
        const auto a = hit (p, 0);
        fresh (h, p, kit, [] (Harness& x) { x.set ("Volume", "-12"); });
        const auto b = hit (p, 0);
        const float d = rmsDb (a, at (0.02), at (0.2)) - rmsDb (b, at (0.02), at (0.2));
        check (std::abs (d - 12.0f) < 0.3f, "Volume -12 dB", fmt (d) + " dB down");
    }

    // ---- Note repeat -------------------------------------------------------------------------------------------
    {
        auto repeatCount = [&] (int choice, float swing, double bpm, std::vector<double>* times = nullptr)
        {
            fresh (h, p, kit, [=] (Harness& x) { x.setChoice ("Note Repeat", choice); x.set ("Swing", juce::String (swing)); });
            Transport tp; tp.bpm = bpm; tp.playing = true;
            const auto out = play (p, 1.2, { { 0.0, clickPad, 1.0f, true }, { 0.999, clickPad, 0.0f, false } }, &tp)[0];
            auto o = onsets (out);
            int k = 0; for (double t : o) if (t < 1.0) ++k;
            if (times != nullptr) *times = o;
            return k;
        };
        const int n16 = repeatCount (2, 50, 120), n8 = repeatCount (1, 50, 120), n16t = repeatCount (3, 50, 120), n32 = repeatCount (4, 50, 120);
        check (n16 == 8, "Note repeat 1/16 at 120 BPM: 8 hits a second", juce::String (n16));
        check (n8 == 4, "Note repeat 1/8: 4 a second", juce::String (n8));
        check (n16t == 12, "Note repeat 1/16T: 12 a second", juce::String (n16t));
        check (n32 == 16, "Note repeat 1/32: 16 a second", juce::String (n32));
        const int off = repeatCount (0, 50, 120);
        check (off == 1, "Note repeat off: one hit", juce::String (off));
        std::vector<double> t;
        repeatCount (2, 66, 120, &t);
        check (t.size() > 3 && std::abs (t[1] - 0.165) < 0.007 && std::abs (t[2] - 0.25) < 0.007, "Swing 66 % pushes every other 1/16 late (by 40 ms at 120)",
               t.size() > 2 ? fmt ((float) t[1] * 1000.0f, 1) + " ms, " + fmt ((float) t[2] * 1000.0f, 1) + " ms" : juce::String ("too few hits"));
        const int free = [&] { fresh (h, p, kit, [] (Harness& x) { x.setChoice ("Note Repeat", 2); }); return (int) onsets (play (p, 1.2, { { 0.0, clickPad, 1.0f, true }, { 0.999, clickPad, 0.0f, false } })[0]).size(); }();
        check (free >= 5 && free <= 7, "Note repeat keeps time without a host (at the sample's 92 BPM)", juce::String (free) + " hits");
    }

    // ---- Flip plays along with the host ------------------------------------------------------------------------
    {
        fresh (h, p, kitFlip);
        Transport tp; tp.bpm = 92.0; tp.playing = true;
        const auto out = play (p, 2.7, {}, &tp)[0];
        const auto o = onsets (out, -50.0f);
        check (o.size() >= 6 && o.front() < 0.006, "The Flip plays in time while the host plays (from chop 1 on the downbeat)",
               juce::String ((int) o.size()) + " hits, first at " + (o.empty() ? juce::String ("-") : fmt ((float) o.front() * 1000.0f, 1) + " ms"));
        fresh (h, p, kitFlip);
        Transport stopped; stopped.bpm = 92.0; stopped.playing = false;
        check (peakDb (play (p, 1.0, {}, &stopped)[0]) < -120, "...and is quiet when the host stops", "");
        fresh (h, p, kitFlip, [] (Harness& x) { x.setBool ("Flip Preview", false); });
        check (peakDb (play (p, 1.0, {}, &tp)[0]) < -120, "Flip Preview off: the pattern doesn't play", "");
    }

    // ---- Saving -------------------------------------------------------------------------------------------------
    {
        fresh (h, p, kitFlip, [] (Harness& x) { x.set ("Pad 3 Pitch", "5"); x.setChoice ("Pad 4 Era", 2); });
        const auto saved = stateOf (p);
        SplinterAudioProcessor q;
        restore (q, saved);
        auto d = q.sampleData();
        const auto ref = makeKit();
        float worst = 0.0f;
        if (d != nullptr) for (int i = 0; i < ref.getNumSamples(); ++i) worst = juce::jmax (worst, std::abs (d->sample.l[(size_t) i] - ref.getSample (0, i)));
        check (d != nullptr && d->sample.length() == ref.getNumSamples() && worst < 1.0e-6f && d->name == kitName,
               "The sample is saved in the session (24-bit FLAC, opens without the file)", d != nullptr ? "max error " + juce::String (worst) : juce::String ("no sample"));
        check (q.chops()->size() == kitHits, "...with its chops", juce::String (q.chops()->size()));
        check (q.pattern() != nullptr && ! q.pattern()->hits.empty() && q.flipCount() == 1, "...and the Flip", "");
        check (q.apvts.getParameter (splinter::ids::pad (2, "pitch"))->getCurrentValueAsText() == "+5 st" && juce::roundToInt (q.apvts.getRawParameterValue (splinter::ids::pad (3, "era"))->load()) == 2,
               "...and every pad's settings", q.apvts.getParameter (splinter::ids::pad (2, "pitch"))->getCurrentValueAsText());
    }

    // ---- The processor itself: chopping, Flip, MIDI -------------------------------------------------------------
    auto* sp = dynamic_cast<SplinterAudioProcessor*> (&p);
    if (sp == nullptr) return;
    h.reset();
    restore (p, kit);
    {
        auto d = sp->sampleData();
        auto c = sp->chops();
        bool near = c->size() == kitHits;
        double worstMs = 0;
        for (int i = 0; near && i < kitHits; ++i)
        {
            const double ms = std::abs (c->points[(size_t) i] / kitRate - (kitLead + kitGap * i)) * 1000.0;
            worstMs = juce::jmax (worstMs, ms); near = ms < 12.0;
        }
        juce::StringArray where; for (int pt : c->points) where.add (juce::String (juce::roundToInt (pt / kitRate * 1000.0)));
        check (near, "Chop on the hits: finds all 10 hits", juce::String (c->size()) + " chops, worst " + fmt ((float) worstMs, 1) + " ms out" + (near ? juce::String() : " (at " + where.joinIntoString (" ") + " ms)"));
        auto e = sp->chopsFor (splinter::ChopMode::Even);
        bool even = e->size() == 16;
        for (int i = 1; even && i < 16; ++i) even = std::abs ((e->points[(size_t) i] - e->points[(size_t) i - 1]) - (d->soundEnd - d->soundStart) / 16) <= 1;
        check (even, "Even: 16 equal chops across the sound", juce::String (e->size()));
        check (d->tempoFromName && std::abs (d->tempo - 92.0) < 0.01 && d->keyFromName && d->key == "A minor", "Tempo and key read from the file name",
               juce::String (d->tempo) + " BPM, " + d->key);
    }
    {
        Harness x (p); x.setChoice ("Chop Mode", 2); x.apply();
        check (sp->chops()->size() == 1, "Tap: starts as one chop", juce::String (sp->chops()->size()));
        sp->addTap ((int) (0.8 * kitRate)); sp->addTap ((int) (1.6 * kitRate));
        sp->addTap ((int) (0.82 * kitRate));   // too close to the last: ignored
        check (sp->chops()->size() == 3 && sp->chops()->points[1] == (int) (0.8 * kitRate), "Tap: a chop where you tap (taps too close together count once)", juce::String (sp->chops()->size()));
        sp->removeTapNear ((int) (1.61 * kitRate));
        check (sp->chops()->size() == 2, "Tap: right-click takes one away", juce::String (sp->chops()->size()));
        // undo and redo, step by step
        sp->undoTaps();
        const bool backTo3 = sp->chops()->size() == 3 && sp->chops()->points[2] == (int) (1.6 * kitRate);
        sp->undoTaps();
        const bool backTo2 = sp->chops()->size() == 2 && sp->chops()->points[1] == (int) (0.8 * kitRate);
        sp->undoTaps(); sp->undoTaps(); sp->undoTaps();   // past the start: stays at the start
        const bool atStart = sp->chops()->size() == 1 && ! sp->canUndoTaps() && sp->canRedoTaps();
        check (backTo3 && backTo2 && atStart, "Tap: undo takes chops back one at a time, to where you started",
               juce::String (backTo3 ? "" : "remove not undone ") + (backTo2 ? "" : "tap not undone ") + (atStart ? "" : "didn't stop at the start"));
        sp->redoTaps(); sp->redoTaps();
        const bool redone = sp->chops()->size() == 3;
        sp->redoTaps();
        const bool redone2 = sp->chops()->size() == 2 && ! sp->canRedoTaps();
        check (redone && redone2, "Tap: redo puts them back", juce::String (sp->chops()->size()));
        sp->undoTaps(); sp->addTap ((int) (2.4 * kitRate));
        check (! sp->canRedoTaps() && sp->chops()->size() == 4, "Tap: a new chop after undo starts a new branch (nothing left to redo)", juce::String (sp->chops()->size()));
        sp->clearTaps(); sp->undoTaps();
        check (sp->chops()->size() == 4, "Tap: clearing can be undone too", juce::String (sp->chops()->size()));
        x.setChoice ("Chop Mode", 0); x.apply();
    }
    // Tempo and key, heard (no hints in the name)
    {
        const double sr = 44100.0, bpm = 100.0, beat = 60.0 / bpm;
        juce::AudioBuffer<float> loop (2, (int) (sr * beat * 16)); loop.clear();
        juce::Random rng (7);
        const double cMajor[] = { 261.63, 329.63, 392.0, 261.63, 349.23, 440.0, 392.0, 493.88, 587.33, 261.63, 329.63, 392.0 };
        for (int i = 0; i < loop.getNumSamples(); ++i)
        {
            const double t = i / sr, inBeat = std::fmod (t, beat), inEighth = std::fmod (t, beat / 2);
            float v = (float) (0.6 * std::exp (-inBeat / 0.06) * std::sin (2 * juce::MathConstants<double>::pi * 60.0 * inBeat));
            v += (float) (0.12 * std::exp (-inEighth / 0.012) * (rng.nextFloat() * 2 - 1));
            const int bar = (int) (t / (beat * 4));
            const double* chord = cMajor + 3 * (bar % 4);
            for (int k = 0; k < 3; ++k)
                for (int hm = 1; hm <= 3; ++hm) v += (float) (0.05 / hm * std::sin (2 * juce::MathConstants<double>::pi * chord[k] * hm * t));
            v += (float) (0.08 * std::sin (2 * juce::MathConstants<double>::pi * 130.81 * t));
            loop.setSample (0, i, v); loop.setSample (1, i, v);
        }
        SplinterAudioProcessor q;
        q.loadSample (loop, sr, "loop.wav");
        auto d = q.sampleData();
        check (! d->tempoFromName && std::abs (d->tempo - bpm) < 1.5, "Tempo heard from the audio", juce::String (d->tempo, 1) + " BPM");
        check (! d->keyFromName && d->key == "C major", "Key heard from the audio", d->key);
    }
    // Flip
    {
        sp->flip();
        auto a = sp->pattern();
        bool onChop1 = a != nullptr && a->has (0, 0) && a->has (16, 0);
        bool inRange = a != nullptr, oneEach = a != nullptr;
        if (a != nullptr)
        {
            std::set<int> seen;
            for (auto& hh : a->hits) { inRange = inRange && hh.pad >= 0 && hh.pad < kitHits && hh.step >= 0 && hh.step < a->steps; oneEach = oneEach && seen.insert (hh.step).second; }
        }
        check (a != nullptr && a->steps == 32 && ! a->hits.empty(), "FLIP makes a 2-bar pattern", a != nullptr ? juce::String ((int) a->hits.size()) + " hits" : juce::String());
        check (inRange && oneEach, "...from the chops there are, one hit a step", "");
        check (onChop1, "...every bar starting on chop 1", "");
        check (a != nullptr && (int) a->rows().size() <= 6, "...on at most six pads", a != nullptr ? juce::String ((int) a->rows().size()) : juce::String());
        sp->flip();
        auto b = sp->pattern();
        bool differ = false;
        if (a != nullptr && b != nullptr) { std::vector<std::pair<int, int>> x, y; for (auto& hh : a->hits) x.push_back ({ hh.step, hh.pad }); for (auto& hh : b->hits) y.push_back ({ hh.step, hh.pad }); differ = x != y; }
        check (differ, "FLIP again makes a different one", "");
        sp->stepFlip (-1);
        check (sp->pattern() == a && sp->flipIndex() == 0 && sp->flipCount() == 2, "Back through the flips you've had", "");
        sp->stepFlip (1);
        // lock a row
        const int row = b->rows().size() > 1 ? b->rows()[1] : b->rows()[0];
        sp->setRowLocked (row, true);
        sp->flip();
        auto c = sp->pattern();
        auto stepsOf = [] (const splinter::Pattern& pt, int pad) { std::vector<int> s; for (auto& hh : pt.hits) if (hh.pad == pad) s.push_back (hh.step); return s; };
        check (stepsOf (*c, row) == stepsOf (*b, row) && ! stepsOf (*b, row).empty(), "A locked row stays when you flip again", "pad " + juce::String (row + 1));
        sp->setRowLocked (row, false);
        Harness x (p);
        x.setChoice ("Flip Length", 2); x.set ("Flip Busy", "100"); x.apply();
        sp->flip();
        const auto busy = sp->pattern();
        x.set ("Flip Busy", "0"); x.setBool ("Start On Chop 1", false); x.apply();
        sp->flip();
        const auto sparse = sp->pattern();
        check (busy->steps == 64 && sparse->steps == 64, "Length: 4 bars", juce::String (busy->steps) + " steps");
        check (busy->hits.size() > sparse->hits.size() + 8, "Busy: more hits", juce::String ((int) sparse->hits.size()) + " at 0 %, " + juce::String ((int) busy->hits.size()) + " at 100 %");
        x.setChoice ("Flip Length", 1); x.set ("Flip Busy", "60"); x.setBool ("Start On Chop 1", true); x.set ("Swing", "60"); x.apply();
        sp->flip();
        // MIDI out
        const auto mf = sp->patternMidi();
        const auto pat = sp->pattern();
        int notesOn = 0; bool right = mf.getNumTracks() == 1 && mf.getTimeFormat() == 960;
        double tempoMeta = 0;
        if (right)
            for (auto* ev : *mf.getTrack (0))
            {
                const auto& m = ev->message;
                if (m.isTempoMetaEvent()) tempoMeta = 60.0 / m.getTempoSecondsPerQuarterNote();
                if (! m.isNoteOn()) continue;
                ++notesOn;
                const int pad = m.getNoteNumber() - splinter::baseNote;
                const double step = ev->message.getTimeStamp() / 240.0;
                const int s = (int) std::floor (step + 0.01);
                const double expected = s + splinter::swingOffsetSteps (s, 0.6);
                right = right && pat->has (s, pad) && std::abs (step - expected) < 1e-6;
            }
        check (right && notesOn == (int) pat->hits.size() && std::abs (tempoMeta - 92.0) < 0.01, "Drag MIDI: the pattern as notes (pads 1-16 on C1 up, with the swing, at 92 BPM)",
               juce::String (notesOn) + " notes, " + juce::String (tempoMeta, 2) + " BPM");
        const auto file = sp->writePatternMidi();
        check (file.existsAsFile() && file.getSize() > 30 && file.hasFileExtension ("mid"), "...written to a .mid to drag into the DAW", file.getFileName());
    }
    // The window
    {
        SplinterAudioProcessorEditor ed (*sp);
        check (ed.getWidth() == 1200 && ed.getHeight() == 760, "The window opens at 1200 x 760", "");
        ed.select (2);
        check (sp->selectedPad.load() == 2, "Clicking a pad selects it for editing", "");
        ed.pressFlip();
        check (sp->flipCount() > 0, "The FLIP button flips", "");
        ed.setFlipWindowOpen (true);
        check (ed.flipWindowOpen(), "Clicking the lanes opens the Flip window", "");
        ed.setFlipWindowOpen (false);
        // The Flip's speaker: dragging the pattern out mutes it (the DAW plays it from then on); FLIP and < > unmute it
        if (ed.dragBox().onDragOut) ed.dragBox().onDragOut();
        const bool mutedByDrag = ! ed.flipSounds();
        ed.pressFlip();
        const bool backOnFlip = ed.flipSounds();
        ed.setFlipSounds (false); ed.stepFlip (-1);
        const bool backOnStep = ed.flipSounds();
        ed.setFlipSounds (false);
        const bool speakerOff = juce::approximatelyEqual (sp->apvts.getRawParameterValue (splinter::ids::flipPreview)->load(), 0.0f);
        check (mutedByDrag && backOnFlip && backOnStep && speakerOff, "The Flip mutes when dragged into the DAW or with its speaker; FLIP and < > unmute it",
               juce::String (mutedByDrag ? "" : "drag didn't mute ") + (backOnFlip ? "" : "FLIP didn't unmute ") + (backOnStep ? "" : "< > didn't unmute ") + (speakerOff ? "" : "speaker didn't mute"));
        {
            Transport tp; tp.bpm = 92.0; tp.playing = true;
            const auto own = play (p, 1.0, { { 0.1, 0, 0.9f, true } }, &tp);   // your own playing, from a controller, while the DAW plays
            const auto none = play (p, 1.0, {}, &tp);
            check (peakDb (own[0]) > -40 && peakDb (none[0]) < -120, "With the Flip muted, your own pads still play (and nothing else does)",
                   fmt (peakDb (own[0])) + " dB with a pad, " + fmt (peakDb (none[0])) + " dB without");
        }
        ed.setFlipSounds (true);
        // Cmd/Ctrl+Z and Shift+Cmd/Ctrl+Z in Tap mode; in the other modes the keys go back to the host
        const juce::KeyPress undoKey ('z', juce::ModifierKeys::commandModifier, 0), redoKey ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        const bool othersPassed = ! ed.keyPressed (undoKey);
        Harness x (p); x.setChoice ("Chop Mode", 2); x.apply();
        const int before = sp->chops()->size();
        sp->addTap ((int) (2.0 * kitRate));
        const bool added = sp->chops()->size() == before + 1;
        const bool undone = ed.keyPressed (undoKey) && sp->chops()->size() == before;
        const bool redone = ed.keyPressed (redoKey) && sp->chops()->size() == before + 1;
        const bool plainZ = ! ed.keyPressed (juce::KeyPress ('z'));
        check (added && othersPassed && undone && redone && plainZ, "Cmd/Ctrl+Z undoes a chop in Tap mode, Shift redoes it; other keys go to the host",
               juce::String (added ? "" : "the tap didn't land ") + (othersPassed ? "" : "kept the key outside Tap mode ") + (undone ? "" : "no undo ")
               + (redone ? "" : "no redo ") + (plainZ ? "" : "kept a plain Z"));
        x.setChoice ("Chop Mode", 0); x.apply();
    }
}

// The window as it looks with the kit loaded, a Flip made and pad 6 just hit
void snapshot (const juce::File& png)
{
    SplinterAudioProcessor proc;
    proc.loadSample (makeKit(), kitRate, "Dusty soul loop 92bpm A minor.wav");
    proc.flip();
    Harness h (proc);
    h.setChoice ("Pad 6 Era", 3); h.set ("Pad 6 Filter", "6000"); h.set ("Pad 6 Drive", "30");
    h.apply();
    proc.selectedPad.store (5);
    Transport tp; tp.bpm = 92.0; tp.playing = true;
    play (proc, 0.5, { { 0.45, 5, 0.9f, true } }, &tp);
    SplinterAudioProcessorEditor editor (proc);
    editor.refreshFromProcessor();
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    png.deleteFile();
    juce::FileOutputStream os (png);
    juce::PNGImageFormat().writeImageToStream (image, os);
    std::cout << "  wrote " << png.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
}
} // namespace

int main (int argc, char** argv)
{
    if (auto r = splinter::video::maybeRun (argc, argv)) return *r;
    return runMain (argc, argv, runSuite, [] { return std::make_unique<SplinterAudioProcessor>(); }, snapshot);
}
