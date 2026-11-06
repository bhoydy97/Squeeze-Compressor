// Drift tests: echo timing, ping-pong, feedback, loop filters, tempo sync, ducking, wobble and stability.
//   DriftTests <Drift.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void defaults (Harness& h)
{
    h.setBool ("Sync", false); h.set ("Time", "250"); h.set ("Feedback", "0"); h.setChoice ("Mode", drift::Stereo);
    h.set ("Low Cut", "20"); h.set ("High Cut", "20000"); h.set ("Wobble", "0"); h.set ("Duck", "0"); h.set ("Mix", "100");
}

Gen burst (float db, float hz, int length) { auto s = sine (db, hz); return [s, length] (int n) { return n < length ? s (n) : 0.0f; }; }

// Drum-like hits: a decaying noise burst every 'period' samples
Gen hits (float db, int period)
{
    auto nz = noise (db, 9);
    return [nz, period] (int n) { const int k = n % period; return nz (n) * std::exp (-(float) k / 2400.0f); };
}

size_t peakIndex (const std::vector<float>& v)
{
    size_t at = 0;
    for (size_t i = 0; i < v.size(); ++i) if (std::abs (v[i]) > std::abs (v[at])) at = i;
    return at;
}

void runSuite (juce::AudioProcessor& proc)
{
    runCommonChecks (proc, 0);
    Harness h (proc);
    const int second = (int) sampleRate;
    const int ms = second / 1000;

    // --- Echo timing ----------------------------------------------------------------
    defaults (h); h.apply();
    {
        const auto out = h.run (second, impulse (-6.0f, 1000));
        const int at = (int) peakIndex (out);
        check (std::abs (at - (1000 + 250 * ms)) <= 1, "Time 250 ms: the echo lands 250 ms after the hit", "at " + juce::String (at - 1000) + " samples, expected " + juce::String (250 * ms));
        const auto before = peakDb (out, 0, (size_t) (1000 + 240 * ms));
        check (before < -100.0f, "Mix 100%: nothing before the echo", "peak " + fmt (before, 1) + " dBFS");
    }

    // --- Ping-pong ---------------------------------------------------------------------
    defaults (h); h.setChoice ("Mode", drift::PingPong); h.set ("Feedback", "50"); h.set ("Time", "100"); h.apply();
    {
        const auto out = h.runStereo (second, impulse (-6.0f, 1000), impulse (-6.0f, 1000));
        const auto win = [&] (const std::vector<float>& v, int k) { const int c = 1000 + k * 100 * ms; return peakDb (v, (size_t) juce::jmax (0, c - 20), (size_t) (c + 20)); };
        const bool ok = win (out[0], 1) > -10.0f && win (out[1], 1) < -100.0f && win (out[1], 2) > -20.0f && win (out[0], 2) < -100.0f && win (out[0], 3) > -30.0f;
        check (ok, "Ping-pong: echoes go left, right, left", "L1 " + fmt (win (out[0], 1), 1) + ", R1 " + fmt (win (out[1], 1), 1) + ", R2 " + fmt (win (out[1], 2), 1)
               + ", L2 " + fmt (win (out[0], 2), 1) + ", L3 " + fmt (win (out[0], 3), 1) + " dBFS");
    }

    // --- Feedback --------------------------------------------------------------------------
    defaults (h); h.set ("Feedback", "50"); h.set ("Time", "100"); h.apply();
    {
        const auto out = h.run (second, burst (-20.0f, 1000.0f, 30 * ms));
        const float e1 = rmsDb (out, (size_t) (100 * ms + 2 * ms), (size_t) (100 * ms + 28 * ms));
        const float e2 = rmsDb (out, (size_t) (200 * ms + 2 * ms), (size_t) (200 * ms + 28 * ms));
        check (std::abs ((e2 - e1) + 6.02f) < 0.3f, "Feedback 50%: each repeat is 6 dB down", "repeat 2 vs 1: " + fmt (e2 - e1) + " dB");
    }

    // --- The loop filters: each repeat goes through them again ------------------------------
    defaults (h); h.set ("High Cut", "2000"); h.set ("Feedback", "70"); h.set ("Time", "100"); h.apply();
    {
        const auto out = h.run (second, burst (-6.0f, 6000.0f, 40 * ms));
        const float in = -6.0f - 3.0103f;
        const float e1 = rmsDb (out, (size_t) (100 * ms + 10 * ms), (size_t) (100 * ms + 38 * ms)) - in;
        const float e2 = rmsDb (out, (size_t) (200 * ms + 10 * ms), (size_t) (200 * ms + 38 * ms)) - in;
        const double pass = lbaudio::dsp::magnitudeDb (drift::highCut (2000.0f, sampleRate), 6000.0, sampleRate)
                          + lbaudio::dsp::magnitudeDb (drift::lowCut (20.0f, sampleRate), 6000.0, sampleRate);
        const float fb = 20.0f * std::log10 (0.7f);
        check (std::abs (e1 - (float) pass) < 0.5f && std::abs (e2 - (float) (2 * pass + fb)) < 0.8f,
               "High Cut 2 kHz: every repeat is darker by the filter's amount",
               "6 kHz: repeat 1 " + fmt (e1, 1) + " dB (expected " + fmt ((float) pass, 1) + "), repeat 2 " + fmt (e2, 1) + " dB (expected " + fmt ((float) (2 * pass + fb), 1) + ")");
    }

    // --- Tempo sync (no host tempo: 120 BPM) ---------------------------------------------------
    defaults (h); h.setBool ("Sync", true); h.set ("Division", "1/4"); h.apply();
    {
        const auto out = h.run (second, impulse (-6.0f, 1000));
        const int at = (int) peakIndex (out) - 1000;
        check (std::abs (at - 500 * ms) <= 1, "Sync 1/4 at 120 BPM: 500 ms", juce::String (at) + " samples");
    }
    defaults (h); h.setBool ("Sync", true); h.set ("Division", "1/8D"); h.apply();
    {
        const auto out = h.run (second, impulse (-6.0f, 1000));
        const int at = (int) peakIndex (out) - 1000;
        check (std::abs (at - 375 * ms) <= 1, "Sync 1/8 dotted at 120 BPM: 375 ms", juce::String (at) + " samples");
    }

    // --- Duck -----------------------------------------------------------------------------
    auto duckedLevel = [&] (const char* duck)
    {
        defaults (h); h.set ("Time", "50"); h.set ("Duck", duck); h.apply();
        const auto out = h.run (second, sine (-10.0f, 500.0f));
        return rmsDb (out, (size_t) second / 2);
    };
    {
        const float open = duckedLevel ("0"), ducked = duckedLevel ("12");
        check (std::abs ((ducked - open) + 12.0f) < 0.5f, "Duck 12 dB: echoes 12 dB down while the input plays", fmt (ducked - open) + " dB");
    }
    defaults (h); h.set ("Time", "300"); h.set ("Duck", "12"); h.apply();
    {
        const auto out = h.run (second + second / 2, burst (-10.0f, 500.0f, second / 4));
        // The input stops at 250 ms; its echo runs 300-550 ms. The duck releases over ~250 ms after the input stops.
        const float during = rmsDb (out, (size_t) (300 * ms), (size_t) (330 * ms)) - (-13.0f);
        const float after = rmsDb (out, (size_t) (520 * ms), (size_t) (545 * ms)) - (-13.0f);
        check (during < -8.0f && after > during + 6.0f, "Duck lets the echoes back up when the input stops",
               "echo level " + fmt (during, 1) + " dB while playing, " + fmt (after, 1) + " dB after");
    }

    // --- Wobble ------------------------------------------------------------------------------
    auto echoOf = [&] (const char* wobble)
    {
        defaults (h); h.set ("Time", "100"); h.set ("Wobble", wobble); h.apply();
        return h.run (2 * second, sine (-12.0f, 1000.0f));
    };
    {
        const auto still = echoOf ("0"), moving = echoOf ("100");
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = (size_t) second / 2; i < still.size(); ++i) { xy += (double) still[i] * moving[i]; xx += (double) still[i] * still[i]; yy += (double) moving[i] * moving[i]; }
        const float corr = (float) (xy / std::sqrt (xx * yy + 1.0e-30));
        check (corr < 0.5f, "Wobble 100%: the echo wavers in time (wow and flutter)", "correlation with a steady echo " + fmt (corr, 3));
        const float level = rmsDb (moving, (size_t) second / 2) - rmsDb (still, (size_t) second / 2);
        check (std::abs (level) < 0.3f, "Wobble doesn't change the level", fmt (level, 2) + " dB");
    }

    // --- Mix 0 is the dry signal -------------------------------------------------------------
    defaults (h); h.set ("Feedback", "60"); h.set ("Mix", "0"); h.set ("Wobble", "50"); h.apply();
    {
        auto in = noise (-6.0f, 5);
        const auto out = h.run (second, in);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i) maxDiff = std::max (maxDiff, std::abs (out[i] - in ((int) i)));
        check (! (maxDiff > 0.0f), "Mix 0% is the bit-exact dry signal", "max diff " + juce::String (maxDiff));
    }

    // --- Stability at full feedback --------------------------------------------------------------
    defaults (h); h.set ("Feedback", "95"); h.set ("Wobble", "100"); h.set ("Time", "20"); h.apply();
    {
        auto loud = noise (0.0f, 3);
        const auto out = h.run (3 * second, [loud, second] (int n) { return n < second ? loud (n) : 0.0f; });
        bool finite = true; for (auto v : out) finite = finite && std::isfinite (v);
        const float pk = peakDb (out);
        check (finite && pk < 6.0f, "Feedback 95% with a full-scale input stays bounded", "peak " + fmt (pk, 1) + " dBFS");
    }

    check (proc.getTailLengthSeconds() > 0.1, "Reports an effect tail to the host", fmt ((float) proc.getTailLengthSeconds(), 2) + " s");

    // --- State recall ---------------------------------------------------------------------------
    defaults (h); h.setBool ("Sync", true); h.set ("Division", "1/16D"); h.set ("Feedback", "62"); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Division", "1/2"); h.set ("Feedback", "10"); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Division") == "1/16D" && h.text ("Feedback") == "62 %", "State save/restore", "'" + h.text ("Division") + "', '" + h.text ("Feedback") + "'");
}
} // namespace

int main (int argc, char** argv)
{
    if (auto result = maybeUIVideo<DriftAudioProcessor, DriftAudioProcessorEditor> (argc, argv)) return *result;
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<DriftAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<DriftAudioProcessor, DriftAudioProcessorEditor> (png, [] (Harness& h)
            {
                h.setBool ("Sync", true); h.set ("Division", "1/8D"); h.set ("Feedback", "55"); h.setChoice ("Mode", drift::PingPong);
                h.set ("Low Cut", "200"); h.set ("High Cut", "4500"); h.set ("Wobble", "25"); h.set ("Duck", "0"); h.set ("Mix", "35");
            }, hits (-8.0f, (int) (sampleRate * 1.2)), 22);
        });
}
