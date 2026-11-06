// Orbit tests: dry passthrough, a still chorus is a clean delay, modulation, stereo width, level, tone, feedback.
//   OrbitTests <Orbit.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void defaults (Harness& h)
{
    h.set ("Rate", "0.6"); h.set ("Depth", "50"); h.setChoice ("Voices", 2); h.set ("Delay", "12"); h.set ("Width", "80");
    h.set ("Feedback", "0"); h.set ("Tone", "20000"); h.set ("Mix", "100");
}

float correlation (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double xy = 0, xx = 0, yy = 0;
    for (size_t i = from; i < a.size(); ++i) { xy += (double) a[i] * b[i]; xx += (double) a[i] * a[i]; yy += (double) b[i] * b[i]; }
    return (float) (xy / std::sqrt (xx * yy + 1.0e-30));
}

void runSuite (juce::AudioProcessor& proc)
{
    runCommonChecks (proc, 0);
    Harness h (proc);
    const int second = (int) sampleRate;
    const size_t settle = (size_t) second / 2;

    // --- Mix 0 is the dry signal ----------------------------------------------------------------
    defaults (h); h.set ("Mix", "0"); h.set ("Feedback", "50"); h.apply();
    {
        auto in = noise (-6.0f, 7);
        const auto out = h.run (second, in);
        float d = 0.0f; for (size_t i = 0; i < out.size(); ++i) d = std::max (d, std::abs (out[i] - in ((int) i)));
        check (! (d > 0.0f), "Mix 0%: the dry signal, bit for bit", "max diff " + juce::String (d));
    }

    // --- With no depth, one voice is a clean 12 ms delay ------------------------------------------------
    defaults (h); h.set ("Depth", "0"); h.setChoice ("Voices", 0); h.apply();
    {
        const auto out = h.run (second / 2, impulse (-6.0f, 1000));
        size_t at = 0; for (size_t i = 0; i < out.size(); ++i) if (std::abs (out[i]) > std::abs (out[at])) at = i;
        const int expected = 1000 + (int) std::lround (0.012 * sampleRate);
        check (std::abs ((int) at - expected) <= 1, "Depth 0, one voice: a clean delay of exactly Delay", "at " + juce::String ((int) at - 1000) + " samples, expected " + juce::String (expected - 1000));
    }

    // --- Depth moves the pitch --------------------------------------------------------------------------
    auto purity = [&] (const char* depth)
    {
        defaults (h); h.set ("Depth", depth); h.setChoice ("Voices", 0); h.set ("Rate", "2"); h.set ("Width", "0"); h.apply();
        const auto out = h.run (2 * second, sine (-12.0f, 1000.0f));
        return toneDb (out, settle, out.size(), 1000.0) - (rmsDb (out, settle) + 3.0103f);
    };
    {
        const float still = purity ("0"), moving = purity ("100");
        check (std::abs (still) < 0.1f && moving < -3.0f, "Depth 100%: the voice's pitch swings (a pure tone spreads out)",
               "1 kHz share of the energy: " + fmt (still, 2) + " dB still, " + fmt (moving, 1) + " dB moving");
    }

    // --- Width ----------------------------------------------------------------------------------------------
    defaults (h); h.set ("Width", "0"); h.apply();
    {
        const auto out = h.runStereo (second, noise (-12.0f, 3), noise (-12.0f, 3));
        float d = 0.0f; for (size_t i = 0; i < out[0].size(); ++i) d = std::max (d, std::abs (out[0][i] - out[1][i]));
        check (! (d > 0.0f), "Width 0%: a mono input stays mono", "max L-R difference " + juce::String (d));
    }
    defaults (h); h.set ("Width", "100"); h.set ("Depth", "100"); h.apply();
    {
        const auto out = h.runStereo (2 * second, noise (-12.0f, 3), noise (-12.0f, 3));
        const float c = correlation (out[0], out[1], settle);
        check (c < 0.9f, "Width 100%: the two sides move differently (wide stereo)", "L/R correlation " + fmt (c, 3));
    }

    // --- Level holds as voices are added ----------------------------------------------------------------------
    for (int v : { 0, 3 })
    {
        defaults (h); h.setChoice ("Voices", v); h.apply();
        const auto out = h.run (2 * second, noise (-12.0f, 5));
        const float g = rmsDb (out, settle) - (-12.0f);
        check (std::abs (g) < 3.0f, "Mix 100%, " + juce::String (v + 1) + " voice(s): level within 3 dB of the input", fmt (g) + " dB");
    }

    // --- Tone ----------------------------------------------------------------------------------------------
    defaults (h); h.set ("Depth", "0"); h.setChoice ("Voices", 0); h.set ("Tone", "3000"); h.apply();
    {
        const auto out = h.run (second, sine (-12.0f, 12000.0f));
        const float g = toneDb (out, settle, out.size(), 12000.0) - (-12.0f);
        const float expected = (float) lbaudio::dsp::magnitudeDb (orbit::tone (3000.0f, sampleRate), 12000.0, sampleRate);
        check (std::abs (g - expected) < 0.2f, "Tone 3 kHz: the chorus's top end follows the filter maths", fmt (g, 1) + " dB at 12 kHz (expected " + fmt (expected, 1) + ")");
    }

    // --- Full feedback stays bounded --------------------------------------------------------------------------
    defaults (h); h.set ("Feedback", "70"); h.set ("Depth", "100"); h.set ("Rate", "5"); h.apply();
    {
        auto loud = noise (0.0f, 9);
        const auto out = h.run (2 * second, [loud, second] (int n) { return n < second ? loud (n) : 0.0f; });
        bool finite = true; for (auto v : out) finite = finite && std::isfinite (v);
        const float pk = peakDb (out);
        check (finite && pk < 6.0f, "Feedback 70% with a full-scale input stays bounded", "peak " + fmt (pk, 1) + " dBFS");
    }

    // --- State recall --------------------------------------------------------------------------------------------
    defaults (h); h.set ("Rate", "1.5"); h.setChoice ("Voices", 3); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Rate", "0.1"); h.setChoice ("Voices", 0); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Rate") == "1.5 Hz" && h.text ("Voices") == "4", "State save/restore", "'" + h.text ("Rate") + "', '" + h.text ("Voices") + "'");
}

// A soft chord for the snapshot
Gen chord()
{
    return [] (int n)
    {
        const double t = n / sampleRate, twoPi = 2.0 * juce::MathConstants<double>::pi;
        return (float) (0.12 * std::sin (twoPi * 196.0 * t) + 0.09 * std::sin (twoPi * 246.9 * t) + 0.08 * std::sin (twoPi * 293.7 * t)
                        + 0.05 * std::sin (twoPi * 392.0 * t));
    };
}
} // namespace

int main (int argc, char** argv)
{
    if (auto result = maybeUIVideo<OrbitAudioProcessor, OrbitAudioProcessorEditor> (argc, argv)) return *result;
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<OrbitAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<OrbitAudioProcessor, OrbitAudioProcessorEditor> (png, [] (Harness& h)
            {
                h.set ("Rate", "0.45"); h.set ("Depth", "65"); h.setChoice ("Voices", 2); h.set ("Delay", "14");
                h.set ("Width", "90"); h.set ("Feedback", "10"); h.set ("Tone", "8000"); h.set ("Mix", "45");
            }, chord(), 30);
        });
}
