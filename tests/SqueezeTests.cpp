// Squeeze tests: checks the compressor against the maths.
//   SqueezeTests <Squeeze.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
float expectedGr (float inDb, float t, float r, float k) { return squeeze::gainReductionDb (inDb, t, r, k); }

void defaults (Harness& h)
{
    h.set ("Threshold", "-20"); h.set ("Ratio", "4"); h.set ("Knee", "0");
    h.set ("Attack", "1"); h.set ("Release", "1000"); h.set ("Makeup", "0");
    h.set ("Mix", "100"); h.set ("SC HPF", "Off");
    h.setBool ("Auto Makeup", false); h.setBool ("External Sidechain", false);
}

void runSuite (juce::AudioProcessor& proc)
{
    runCommonChecks (proc);
    Harness h (proc);
    defaults (h);
    const int settle = (int) (1.5 * sampleRate);
    const size_t tail = (size_t) (1.2 * sampleRate);


    // --- Static curve, hard knee -------------------------------------------------
    defaults (h); h.apply();
    float worst = 0.0f;
    for (float level = -40.0f; level <= 0.01f; level += 5.0f)
    {
        h.apply();
        const auto out = h.run (settle, sine (level, 1000.0f));
        const float expected = level + expectedGr (level, -20.0f, 4.0f, 0.0f);
        worst = std::max (worst, std::abs (peakDb (out, tail) - expected));
    }
    check (worst < 0.25f, "Hard-knee curve (T -20, 4:1), -40..0 dBFS", "max error " + fmt (worst) + " dB");

    // --- Soft knee --------------------------------------------------------------
    h.set ("Knee", "12"); h.apply();
    worst = 0.0f;
    for (float level : { -30.0f, -26.0f, -23.0f, -20.0f, -17.0f, -14.0f, -8.0f })
    {
        h.apply();
        const auto out = h.run (settle, sine (level, 1000.0f));
        const float expected = level + expectedGr (level, -20.0f, 4.0f, 12.0f);
        worst = std::max (worst, std::abs (peakDb (out, tail) - expected));
    }
    check (worst < 0.25f, "Soft-knee curve (12 dB knee)", "max error " + fmt (worst) + " dB");

    // --- Ratio 20:1 near-limiting ---------------------------------------------
    defaults (h); h.set ("Ratio", "20"); h.set ("Threshold", "-30"); h.apply();
    {
        const auto out = h.run (settle, sine (-6.0f, 1000.0f));
        const float expected = -6.0f + expectedGr (-6.0f, -30.0f, 20.0f, 0.0f);
        check (std::abs (peakDb (out, tail) - expected) < 0.25f, "20:1 ratio", "out " + fmt (peakDb (out, tail)) + " dB, expected " + fmt (expected));
    }

    // --- Attack / release timing (DC step gives an exact exponential) ----------
    defaults (h); h.set ("Threshold", "-30"); h.set ("Ratio", "10");
    h.set ("Attack", "10"); h.set ("Release", "100"); h.apply();
    {
        const int stepOn = (int) (0.2 * sampleRate), stepOff = (int) (0.7 * sampleRate), total = (int) (1.5 * sampleRate);
        auto signal = [=] (int n) { return (n >= stepOn && n < stepOff) ? fromDb (-10.0f) : fromDb (-50.0f); };
        const auto out = h.run (total, signal);

        const float finalGr = -expectedGr (-10.0f, -30.0f, 10.0f, 0.0f); // 18 dB
        auto grAt = [&] (int n) { return toDb (signal (n)) - toDb (out[(size_t) n]); };

        int attackN = -1, releaseN = -1;
        for (int n = stepOn; n < stepOff; ++n)
            if (grAt (n) >= finalGr * (1.0f - std::exp (-1.0f))) { attackN = n - stepOn; break; }
        for (int n = stepOff; n < total; ++n)
            if (grAt (n) <= finalGr * std::exp (-1.0f)) { releaseN = n - stepOff; break; }

        const float attackMs = 1000.0f * (float) attackN / (float) sampleRate;
        const float releaseMs = 1000.0f * (float) releaseN / (float) sampleRate;
        check (std::abs (grAt (stepOff - 1) - finalGr) < 0.05f, "Settled GR = 18 dB", "got " + fmt (grAt (stepOff - 1)) + " dB");
        check (std::abs (attackMs - 10.0f) < 0.5f, "Attack time constant 10 ms", "measured " + fmt (attackMs) + " ms");
        check (std::abs (releaseMs - 100.0f) < 2.0f, "Release time constant 100 ms", "measured " + fmt (releaseMs) + " ms");
    }

    // --- Mix ---------------------------------------------------------------------
    defaults (h); h.set ("Mix", "0"); h.apply();
    {
        auto in = sine (-6.0f, 440.0f);
        const auto out = h.run (settle, in);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i) maxDiff = std::max (maxDiff, std::abs (out[i] - in ((int) i)));
        check (maxDiff < 1.0e-6f, "Mix 0% is bit-transparent", "max diff " + juce::String (maxDiff));
    }
    h.set ("Mix", "50"); h.apply();
    {
        const auto out = h.run (settle, sine (-10.0f, 1000.0f));
        const float g = fromDb (expectedGr (-10.0f, -20.0f, 4.0f, 0.0f));
        const float expected = -10.0f + toDb (0.5f + 0.5f * g);
        check (std::abs (peakDb (out, tail) - expected) < 0.25f, "Mix 50% (parallel compression)", "out " + fmt (peakDb (out, tail)) + " dB, expected " + fmt (expected));
    }

    // --- Makeup ------------------------------------------------------------------
    defaults (h); h.set ("Makeup", "6"); h.apply();
    {
        const auto out = h.run (settle, sine (-40.0f, 1000.0f));
        check (std::abs (peakDb (out, tail) - (-34.0f)) < 0.1f, "Manual makeup +6 dB", "out " + fmt (peakDb (out, tail)) + " dB");
    }
    defaults (h); h.setBool ("Auto Makeup", true); h.apply();
    {
        const auto out = h.run (settle, sine (-40.0f, 1000.0f));
        const float expected = -40.0f + squeeze::autoMakeupDb (-20.0f, 4.0f, 0.0f); // +7.5 dB
        check (std::abs (peakDb (out, tail) - expected) < 0.1f, "Auto makeup (+7.5 dB at T -20, 4:1)", "out " + fmt (peakDb (out, tail)) + " dB");
    }

    // --- Sidechain high-pass -------------------------------------------------------
    defaults (h); h.apply();
    {
        const auto out = h.run (settle, sine (-10.0f, 50.0f));
        const float expected = -10.0f + expectedGr (-10.0f, -20.0f, 4.0f, 0.0f);
        check (std::abs (peakDb (out, tail) - expected) < 0.3f, "50 Hz compressed with SC HPF off", "out " + fmt (peakDb (out, tail)) + " dB");
    }
    h.set ("SC HPF", "500"); h.apply();
    {
        const auto out = h.run (settle, sine (-10.0f, 50.0f));
        check (std::abs (peakDb (out, tail) - (-10.0f)) < 0.3f, "50 Hz ignored with SC HPF 500 Hz", "out " + fmt (peakDb (out, tail)) + " dB");
    }

    // --- External sidechain -------------------------------------------------------
    if (proc.getTotalNumInputChannels() > proc.getMainBusNumInputChannels())
    {
        defaults (h); h.set ("Threshold", "-30"); h.apply();
        {
            const auto out = h.run (settle, sine (-20.0f, 1000.0f), dc (-6.0f));
            const float expected = -20.0f + expectedGr (-20.0f, -30.0f, 4.0f, 0.0f);
            check (std::abs (peakDb (out, tail) - expected) < 0.25f, "Ext SC off: detects main input", "out " + fmt (peakDb (out, tail)) + " dB");
        }
        h.setBool ("External Sidechain", true); h.apply();
        {
            const auto out = h.run (settle, sine (-20.0f, 1000.0f), dc (-6.0f));
            const float expected = -20.0f + expectedGr (-6.0f, -30.0f, 4.0f, 0.0f);
            check (std::abs (peakDb (out, tail) - expected) < 0.25f, "Ext SC on: ducks main by sidechain level", "out " + fmt (peakDb (out, tail)) + " dB, expected " + fmt (expected));
        }
    }
    else
    {
        check (false, "Sidechain bus present", "no sidechain inputs");
    }

    // --- State recall -----------------------------------------------------------
    defaults (h); h.set ("Threshold", "-33.3"); h.set ("Release", "1.5 s"); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Threshold", "-5"); h.set ("Release", "50"); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Threshold") == "-33.3 dB" && h.text ("Release") == "1.50 s", "State save/restore",
           "threshold '" + h.text ("Threshold") + "', release '" + h.text ("Release") + "'");
}

} // namespace

int main (int argc, char** argv)
{
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<SqueezeAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<SqueezeAudioProcessor, SqueezeAudioProcessorEditor> (png, [] (Harness& h)
            {
                defaults (h);
                h.set ("Threshold", "-24"); h.set ("Ratio", "4"); h.set ("Knee", "8");
                h.set ("Attack", "8"); h.set ("Release", "150"); h.set ("Makeup", "4.5"); h.set ("Mix", "85");
                h.set ("SC HPF", "80");
            }, sine (-12.0f, 220.0f));
        });
}
