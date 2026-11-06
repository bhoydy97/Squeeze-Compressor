// Snap tests: neutral passthrough, steady sounds untouched, attack and sustain shaping, level independence.
//   SnapTests <Snap.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void defaults (Harness& h)
{
    h.set ("Attack", "0"); h.set ("Sustain", "0"); h.set ("Speed", "15"); h.set ("Output", "0"); h.set ("Mix", "100");
}

constexpr int period = 24000; // a hit every 0.5 s at 48 kHz

// Drum-like hits: a decaying noise burst (40 ms time constant) every 0.5 s
Gen hits (float db)
{
    auto nz = noise (db, 31);
    return [nz] (int n) { const int k = n % period; return nz (n) * std::exp (-(float) k / 1920.0f); };
}

// Level change (dB) of out vs in over [a, b) samples after each hit, averaged over the hits after the first
float changeAfterHit (const std::vector<float>& in, const std::vector<float>& out, int a, int b)
{
    double ei = 0, eo = 0;
    for (int start = period; start + b < (int) out.size(); start += period)
        for (int i = start + a; i < start + b; ++i) { ei += (double) in[(size_t) i] * in[(size_t) i]; eo += (double) out[(size_t) i] * out[(size_t) i]; }
    return (float) (10.0 * std::log10 ((eo + 1e-30) / (ei + 1e-30)));
}

std::vector<float> render (Gen g, int n) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = g (i); return v; }

void runSuite (juce::AudioProcessor& proc)
{
    runCommonChecks (proc, 0);
    Harness h (proc);
    const int second = (int) sampleRate;
    const int ms = second / 1000;
    const int len = 4 * period;
    const auto hitsIn = render (hits (-6.0f), len);

    // --- Neutral settings: bit for bit --------------------------------------------------------
    defaults (h); h.apply();
    {
        const auto out = h.run (len, hits (-6.0f));
        float d = 0.0f; for (size_t i = 0; i < out.size(); ++i) d = std::max (d, std::abs (out[i] - hitsIn[i]));
        check (! (d > 0.0f), "Attack and Sustain at 0: the input passes bit for bit", "max diff " + juce::String (d));
    }

    // --- A steady sound is left alone --------------------------------------------------------------
    defaults (h); h.set ("Attack", "12"); h.set ("Sustain", "12"); h.apply();
    {
        const auto out = h.run (second, sine (-12.0f, 200.0f));
        const float g = toneDb (out, (size_t) second / 2, out.size(), 200.0) - (-12.0f);
        check (std::abs (g) < 0.3f, "A steady tone comes out unchanged", fmt (g) + " dB");
    }

    // --- Attack: the front of each hit, not the tail -------------------------------------------------------
    defaults (h); h.set ("Attack", "12"); h.apply();
    {
        const auto out = h.run (len, hits (-6.0f));
        const float front = changeAfterHit (hitsIn, out, 0, 5 * ms), tail = changeAfterHit (hitsIn, out, 150 * ms, 400 * ms);
        check (front > 4.0f && std::abs (tail) < 1.5f, "Attack +12 dB: the front of each hit jumps out, the tail doesn't",
               "first 5 ms " + fmt (front, 1) + " dB, 150-400 ms " + fmt (tail, 1) + " dB");
    }
    defaults (h); h.set ("Attack", "-12"); h.apply();
    {
        const auto out = h.run (len, hits (-6.0f));
        const float front = changeAfterHit (hitsIn, out, 0, 5 * ms);
        check (front < -4.0f, "Attack -12 dB: softer hits", "first 5 ms " + fmt (front, 1) + " dB");
    }

    // --- Sustain: the tail, not the front ---------------------------------------------------------------------
    defaults (h); h.set ("Sustain", "-12"); h.apply();
    {
        const auto out = h.run (len, hits (-6.0f));
        const float front = changeAfterHit (hitsIn, out, 0, 2 * ms), tail = changeAfterHit (hitsIn, out, 60 * ms, 300 * ms);
        check (tail < -5.0f && std::abs (front) < 1.5f, "Sustain -12 dB: tighter tails, the hit itself untouched",
               "first 2 ms " + fmt (front, 1) + " dB, 60-300 ms " + fmt (tail, 1) + " dB");
    }
    defaults (h); h.set ("Sustain", "12"); h.apply();
    {
        const auto out = h.run (len, hits (-6.0f));
        const float tail = changeAfterHit (hitsIn, out, 60 * ms, 300 * ms);
        check (tail > 5.0f, "Sustain +12 dB: longer, louder tails", "60-300 ms " + fmt (tail, 1) + " dB");
    }

    // --- The same shaping at any level --------------------------------------------------------------------------
    auto frontAt = [&] (float db)
    {
        defaults (h); h.set ("Attack", "12"); h.apply();
        const auto in = render (hits (db), len);
        const auto out = h.run (len, hits (db));
        return changeAfterHit (in, out, 0, 5 * ms);
    };
    {
        const float loud = frontAt (-6.0f), quiet = frontAt (-36.0f);
        check (std::abs (loud - quiet) < 0.5f, "Level-independent: a hit 30 dB quieter is shaped the same", "loud " + fmt (loud) + " dB, quiet " + fmt (quiet) + " dB");
    }

    // --- Mix ---------------------------------------------------------------------------------------------------
    defaults (h); h.set ("Attack", "12"); h.set ("Sustain", "-12"); h.set ("Mix", "0"); h.apply();
    {
        const auto out = h.run (len, hits (-6.0f));
        float d = 0.0f; for (size_t i = 0; i < out.size(); ++i) d = std::max (d, std::abs (out[i] - hitsIn[i]));
        check (! (d > 0.0f), "Mix 0%: the dry signal, bit for bit", "max diff " + juce::String (d));
    }
    defaults (h); h.set ("Attack", "12"); h.set ("Mix", "50"); h.apply();
    {
        const auto out = h.run (len, hits (-6.0f));
        const float front = changeAfterHit (hitsIn, out, 0, 5 * ms);
        h.set ("Mix", "100"); h.apply();
        const auto full = h.run (len, hits (-6.0f));
        const float fullFront = changeAfterHit (hitsIn, full, 0, 5 * ms);
        check (front > 1.0f && front < fullFront - 1.0f, "Mix 50%: part of the effect", fmt (front, 1) + " dB vs " + fmt (fullFront, 1) + " dB at 100%");
    }

    // --- Output ------------------------------------------------------------------------------------------------
    defaults (h); h.set ("Output", "-6"); h.apply();
    {
        const auto out = h.run (second, sine (-12.0f, 200.0f));
        const float g = toneDb (out, (size_t) second / 2, out.size(), 200.0) - (-12.0f);
        check (std::abs (g + 6.0f) < 0.05f, "Output -6 dB", fmt (g) + " dB");
    }

    // --- State recall ---------------------------------------------------------------------------------------------
    defaults (h); h.set ("Attack", "7.5"); h.set ("Speed", "25"); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Attack", "-3"); h.set ("Speed", "5"); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Attack") == "+7.5 dB" && h.text ("Speed") == "25.0 ms", "State save/restore", "'" + h.text ("Attack") + "', '" + h.text ("Speed") + "'");
}

// A drum loop for the snapshot: kick-ish thumps and snare-ish noise
Gen loop()
{
    auto nz = noise (-10.0f, 41);
    return [nz] (int n)
    {
        const int beat = (int) (sampleRate * 0.3);
        const int k = n % beat, which = (n / beat) % 2;
        const float env = std::exp (-(float) k / (which == 0 ? 3000.0f : 1600.0f));
        const float body = which == 0 ? 0.6f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * k / sampleRate) : 0.0f;
        return env * (body + (which == 0 ? 0.25f : 1.0f) * nz (n));
    };
}
} // namespace

int main (int argc, char** argv)
{
    if (auto result = maybeUIVideo<SnapAudioProcessor, SnapAudioProcessorEditor> (argc, argv)) return *result;
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<SnapAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<SnapAudioProcessor, SnapAudioProcessorEditor> (png, [] (Harness& h)
            {
                h.set ("Attack", "9"); h.set ("Sustain", "-6"); h.set ("Speed", "15"); h.set ("Mix", "100"); h.set ("Output", "-1");
            }, loop(), 24);
        });
}
