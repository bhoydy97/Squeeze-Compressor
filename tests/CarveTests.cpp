// Carve tests: checks the EQ's measured response against the filter maths.
//   CarveTests <Carve.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void flat (Harness& h)
{
    h.setBool ("High-pass On", false); h.set ("High-pass Freq", "30"); h.setChoice ("High-pass Slope", 1);
    h.set ("Low shelf Freq", "100");  h.set ("Low shelf Gain", "0");
    h.set ("Bell 1 Freq", "250");  h.set ("Bell 1 Gain", "0"); h.set ("Bell 1 Q", "1");
    h.set ("Bell 2 Freq", "1000"); h.set ("Bell 2 Gain", "0"); h.set ("Bell 2 Q", "1");
    h.set ("Bell 3 Freq", "4000"); h.set ("Bell 3 Gain", "0"); h.set ("Bell 3 Q", "1");
    h.set ("High shelf Freq", "8000"); h.set ("High shelf Gain", "0");
    h.setBool ("Low-pass On", false); h.set ("Low-pass Freq", "20000"); h.setChoice ("Low-pass Slope", 1);
    h.set ("Output", "0");
}

// Steady-state gain of a sine through the plugin, in dB
float measureGain (Harness& h, float hz)
{
    h.apply();
    const int len = (int) (sampleRate * (hz < 100.0f ? 1.5 : 0.6));
    const auto out = h.run (len, sine (-20.0f, hz));
    return rmsDb (out, (size_t) len / 2) - (-20.0f - 3.0103f);
}

void runSuite (juce::AudioProcessor& proc)
{
    runCommonChecks (proc);
    Harness h (proc);

    // --- Flat settings are transparent --------------------------------------------
    flat (h); h.apply();
    {
        auto in = noise (-12.0f, 7);
        const auto out = h.run ((int) sampleRate, in);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i) maxDiff = std::max (maxDiff, std::abs (out[i] - in ((int) i)));
        check (maxDiff < 1.0e-6f, "All bands at 0 dB is bit-transparent", "max diff " + juce::String (maxDiff));
    }

    // --- Single bell -------------------------------------------------------------------
    flat (h); h.set ("Bell 2 Gain", "12");
    {
        const float g = measureGain (h, 1000.0f);
        check (std::abs (g - 12.0f) < 0.05f, "Bell +12 dB at 1 kHz", "measured " + fmt (g) + " dB");
    }

    // --- Output gain ----------------------------------------------------------------
    flat (h); h.set ("Output", "6");
    {
        const float g = measureGain (h, 1000.0f);
        check (std::abs (g - 6.0f) < 0.05f, "Output +6 dB", "measured " + fmt (g) + " dB");
    }

    // --- A full curve matches the maths at 14 frequencies --------------------------
    flat (h);
    h.setBool ("High-pass On", true); h.set ("High-pass Freq", "80"); h.setChoice ("High-pass Slope", 1);
    h.set ("Low shelf Freq", "120"); h.set ("Low shelf Gain", "4");
    h.set ("Bell 1 Freq", "400");  h.set ("Bell 1 Gain", "-6"); h.set ("Bell 1 Q", "2");
    h.set ("Bell 2 Freq", "2000"); h.set ("Bell 2 Gain", "3");  h.set ("Bell 2 Q", "0.7");
    h.set ("Bell 3 Freq", "6000"); h.set ("Bell 3 Gain", "-3"); h.set ("Bell 3 Q", "4");
    h.set ("High shelf Freq", "10000"); h.set ("High shelf Gain", "5");
    h.setBool ("Low-pass On", true); h.set ("Low-pass Freq", "16000"); h.setChoice ("Low-pass Slope", 0);
    {
        auto p = carve::defaultParams();
        p.bands[carve::HP] = { true, 80.0f, 0.0f, 0.707f, 1 };
        p.bands[carve::LowShelf] = { true, 120.0f, 4.0f, 0.707f, 1 };
        p.bands[carve::Bell1] = { true, 400.0f, -6.0f, 2.0f, 1 };
        p.bands[carve::Bell2] = { true, 2000.0f, 3.0f, 0.7f, 1 };
        p.bands[carve::Bell3] = { true, 6000.0f, -3.0f, 4.0f, 1 };
        p.bands[carve::HighShelf] = { true, 10000.0f, 5.0f, 0.707f, 1 };
        p.bands[carve::LP] = { true, 16000.0f, 0.0f, 0.707f, 0 };

        float worst = 0.0f; juce::String worstAt;
        for (float hz : { 30.0f, 60.0f, 90.0f, 150.0f, 300.0f, 400.0f, 700.0f, 1500.0f, 2000.0f, 4000.0f, 6000.0f, 9000.0f, 14000.0f, 19000.0f })
        {
            const float measured = measureGain (h, hz);
            const float expected = (float) carve::responseDb (p, hz, sampleRate);
            if (std::abs (measured - expected) > worst) { worst = std::abs (measured - expected); worstAt = juce::String (hz) + " Hz"; }
        }
        check (worst < 0.15f, "Full curve matches the filter maths (14 frequencies)", "worst error " + fmt (worst, 3) + " dB at " + worstAt);
    }

    // --- High-pass slopes ----------------------------------------------------------
    for (int slope : { 0, 1, 2 })
    {
        flat (h); h.setBool ("High-pass On", true); h.set ("High-pass Freq", "1000"); h.setChoice ("High-pass Slope", slope);
        auto p = carve::defaultParams();
        p.bands[carve::HP] = { true, 1000.0f, 0.0f, 0.707f, slope };
        const float measured = measureGain (h, 250.0f);
        const float expected = (float) carve::responseDb (p, 250.0, sampleRate);
        check (std::abs (measured - expected) < 0.2f, "High-pass " + juce::String (12 << slope) + " dB/oct, 2 octaves below",
               "measured " + fmt (measured) + " dB, expected " + fmt (expected));
    }

    // --- No zipper noise when a band is swept fast ------------------------------------
    flat (h); h.apply();
    {
        auto in = sine (-20.0f, 1000.0f);
        auto first = h.run ((int) (0.2 * sampleRate), in);
        h.set ("Bell 2 Gain", "18");
        auto second = h.run ((int) (0.3 * sampleRate), [&] (int n) { return in (n + (int) first.size()); });
        first.insert (first.end(), second.begin(), second.end());
        float maxStep = 0.0f, peak = 0.0f;
        for (size_t i = 1; i < first.size(); ++i) { maxStep = std::max (maxStep, std::abs (first[i] - first[i - 1])); peak = std::max (peak, std::abs (first[i])); }
        const float allowed = 1.1f * peak * 2.0f * juce::MathConstants<float>::pi * 1000.0f / (float) sampleRate;
        check (maxStep <= allowed, "Sudden +18 dB change stays smooth (no clicks)", "largest step " + fmt (maxStep, 4) + ", sine limit " + fmt (allowed, 4));
    }

    // --- State recall ------------------------------------------------------------
    flat (h); h.set ("Bell 1 Gain", "-7.5"); h.set ("Bell 3 Freq", "3.2k"); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Bell 1 Gain", "2"); h.set ("Bell 3 Freq", "500"); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Bell 1 Gain") == "-7.5 dB" && h.text ("Bell 3 Freq") == "3.20 kHz", "State save/restore",
           "'" + h.text ("Bell 1 Gain") + "', '" + h.text ("Bell 3 Freq") + "'");
}
} // namespace

int main (int argc, char** argv)
{
    if (auto result = maybeUIVideo<CarveAudioProcessor, CarveAudioProcessorEditor> (argc, argv)) return *result;
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<CarveAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<CarveAudioProcessor, CarveAudioProcessorEditor> (png, [] (Harness& h)
            {
                flat (h);
                h.setBool ("High-pass On", true); h.set ("High-pass Freq", "40");
                h.set ("Low shelf Freq", "90"); h.set ("Low shelf Gain", "3");
                h.set ("Bell 1 Freq", "300"); h.set ("Bell 1 Gain", "-4"); h.set ("Bell 1 Q", "1.4");
                h.set ("Bell 2 Freq", "1800"); h.set ("Bell 2 Gain", "2.5"); h.set ("Bell 2 Q", "0.9");
                h.set ("Bell 3 Freq", "5000"); h.set ("Bell 3 Gain", "-3"); h.set ("Bell 3 Q", "3");
                h.set ("High shelf Freq", "11000"); h.set ("High shelf Gain", "4");
            }, noise (-14.0f, 3));
        });
}
