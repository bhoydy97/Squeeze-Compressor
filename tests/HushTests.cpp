// Hush tests: passthrough, reduction depth, split vs wide, frequency targeting, range, listen and release.
//   HushTests <Hush.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void defaults (Harness& h)
{
    h.set ("Frequency", "6000"); h.set ("Threshold", "-30"); h.set ("Range", "10"); h.setChoice ("Mode", hush::Split); h.setBool ("Listen", false);
}

Gen mixOf (Gen a, Gen b) { return [a, b] (int n) { return a (n) + b (n); }; }

// A voice-like test signal: a 180 Hz buzz with an "s" (high-passed noise) every 0.4 s
Gen voice (float db)
{
    auto nz = noise (db - 4.0f, 11);
    const float a = fromDb (db - 6.0f);
    return [nz, a] (int n)
    {
        const double t = n / sampleRate;
        float v = 0.0f;
        for (int k = 1; k <= 6; ++k) v += a / (float) k * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 180.0 * k * t);
        const int pos = n % (int) (sampleRate * 0.4);
        if (pos < (int) (sampleRate * 0.09)) v += 2.5f * (nz (n) - nz (n > 0 ? n - 1 : 0));
        return v;
    };
}

void runSuite (juce::AudioProcessor& proc)
{
    runCommonChecks (proc, 0);
    Harness h (proc);
    const int second = (int) sampleRate;
    const size_t settle = (size_t) second / 2;
    auto gainAt = [&] (const std::vector<float>& out, double hz, float inDb) { return toneDb (out, settle, out.size(), hz) - inDb; };

    // --- Below the threshold it doesn't touch a thing -----------------------------------------
    defaults (h); h.apply();
    {
        auto in = noise (-60.0f, 4);
        const auto out = h.run (second, in);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i) maxDiff = std::max (maxDiff, std::abs (out[i] - in ((int) i)));
        check (! (maxDiff > 0.0f), "Below the threshold: the input passes bit for bit", "max diff " + juce::String (maxDiff));
    }

    // --- A loud S is turned down by Range -----------------------------------------------------
    defaults (h); h.set ("Frequency", "8000"); h.set ("Threshold", "-40"); h.apply();
    {
        const auto out = h.run (second, sine (-10.0f, 12000.0f));
        const float g = gainAt (out, 12000.0, -10.0f);
        check (g < -9.0f && g > -11.0f, "Range 10 dB: a loud 12 kHz S is turned down 10 dB", fmt (g) + " dB");
    }

    // --- Split leaves the low end alone; Wide doesn't ----------------------------------------------
    for (int m : { hush::Split, hush::Wide })
    {
        defaults (h); h.setChoice ("Mode", m); h.set ("Frequency", "8000"); h.set ("Threshold", "-40"); h.apply();
        const auto out = h.run (second, mixOf (sine (-10.0f, 200.0f), sine (-10.0f, 12000.0f)));
        const float low = gainAt (out, 200.0, -10.0f), high = gainAt (out, 12000.0, -10.0f);
        if (m == hush::Split)
            check (std::abs (low) < 0.2f && high < -8.0f, "Split: the S is turned down, the 200 Hz body isn't", "200 Hz " + fmt (low) + " dB, 12 kHz " + fmt (high) + " dB");
        else
            check (low < -8.0f && high < -8.0f, "Wide: the whole signal dips on the S", "200 Hz " + fmt (low) + " dB, 12 kHz " + fmt (high) + " dB");
    }

    // --- It listens where Frequency says ------------------------------------------------------------
    defaults (h); h.set ("Frequency", "10000"); h.apply();
    {
        const float off = gainAt (h.run (second, sine (-10.0f, 2500.0f)), 2500.0, -10.0f);
        h.apply();
        const float on = gainAt (h.run (second, sine (-10.0f, 10000.0f)), 10000.0, -10.0f);
        check (off > -1.5f && on < -7.0f, "Frequency 10 kHz: reacts to 10 kHz, hardly to 2.5 kHz", "2.5 kHz " + fmt (off) + " dB, 10 kHz " + fmt (on) + " dB");
    }

    // --- Quiet sibilance under the threshold is left alone ---------------------------------------------
    defaults (h); h.set ("Frequency", "8000"); h.apply();
    {
        const float g = gainAt (h.run (second, sine (-50.0f, 9000.0f)), 9000.0, -50.0f);
        check (std::abs (g) < 0.05f, "A quiet S under the threshold is untouched", fmt (g, 3) + " dB");
    }

    // --- Range caps the reduction ----------------------------------------------------------------------
    defaults (h); h.setChoice ("Mode", hush::Wide); h.set ("Threshold", "-60"); h.set ("Range", "6"); h.apply();
    {
        const float g = gainAt (h.run (second, sine (0.0f, 6000.0f)), 6000.0, 0.0f);
        check (std::abs (g + 6.0f) < 0.3f, "Range 6 dB: never more than 6 dB, however loud", fmt (g) + " dB");
    }

    // --- Listen plays the detector band ---------------------------------------------------------------
    defaults (h); h.setBool ("Listen", true); h.apply();
    {
        const float low = gainAt (h.run (second, sine (-10.0f, 200.0f)), 200.0, -10.0f);
        h.apply();
        const float s = gainAt (h.run (second, sine (-10.0f, 6000.0f)), 6000.0, -10.0f);
        check (low < -40.0f && std::abs (s) < 3.0f, "Listen: only the band Hush hears comes through", "200 Hz " + fmt (low, 1) + " dB, 6 kHz " + fmt (s, 1) + " dB");
    }

    // --- It lets go after the S ----------------------------------------------------------------------------
    defaults (h); h.setChoice ("Mode", hush::Wide); h.set ("Frequency", "8000"); h.set ("Threshold", "-40"); h.apply();
    {
        auto s = sine (-10.0f, 10000.0f), body = sine (-20.0f, 200.0f);
        const auto out = h.run (second, [s, body] (int n) { return body (n) + (n < 4800 ? s (n) : 0.0f); });
        const float later = toneDb (out, (size_t) (second * 0.6), out.size(), 200.0) - (-20.0f);
        check (std::abs (later) < 0.1f, "Wide: back to full level once the S has passed", "200 Hz 0.5 s later " + fmt (later, 3) + " dB");
    }

    // --- State recall --------------------------------------------------------------------------------
    defaults (h); h.set ("Frequency", "7200"); h.setChoice ("Mode", hush::Wide); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Frequency", "3000"); h.setChoice ("Mode", hush::Split); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Frequency") == "7.20 kHz" && h.text ("Mode") == "Wide", "State save/restore", "'" + h.text ("Frequency") + "', '" + h.text ("Mode") + "'");
}
} // namespace

int main (int argc, char** argv)
{
    if (auto result = maybeUIVideo<HushAudioProcessor, HushAudioProcessorEditor> (argc, argv)) return *result;
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<HushAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<HushAudioProcessor, HushAudioProcessorEditor> (png, [] (Harness& h)
            {
                h.set ("Frequency", "6800"); h.set ("Threshold", "-34"); h.set ("Range", "9"); h.setChoice ("Mode", hush::Split);
            }, voice (-14.0f), 120);
        });
}
