// Ember tests: alignment, level, harmonics, aliasing and tone against the maths.
//   EmberTests <Ember.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void defaults (Harness& h)
{
    h.set ("Drive", "0"); h.setChoice ("Character", ember::Tape); h.set ("Tone", "0");
    h.set ("Mix", "100"); h.set ("Output", "0"); h.setBool ("Auto Gain", false);
}

const char* modeName (int m) { return m == ember::Tape ? "Tape" : (m == ember::Tube ? "Tube" : "Clip"); }

void runSuite (juce::AudioProcessor& proc)
{
    const int reportedEarly = proc.getLatencySamples();
    Harness h (proc);
    const int latency = proc.getLatencySamples();
    // A host only asks a VST3 for its latency once it's active, so the early value may read 0 there
    check (latency > 0 && latency < 200 && (reportedEarly == 0 || reportedEarly == latency), "Oversampling latency is reported",
           juce::String (latency) + " samples (before prepare: " + juce::String (reportedEarly) + ")");
    runCommonChecks (proc, latency);
    const int second = (int) sampleRate;

    // --- Output lines up with the reported latency -------------------------------
    defaults (h); h.apply();
    {
        const auto out = h.run (4000, impulse (-30.0f, 1000));
        size_t at = 0;
        for (size_t i = 0; i < out.size(); ++i) if (std::abs (out[i]) > std::abs (out[at])) at = i;
        check ((int) at == 1000 + latency, "Impulse comes out exactly 'latency' samples later", "at " + juce::String ((int) at) + ", expected " + juce::String (1000 + latency));
    }

    // --- Mix 0 is the dry signal, delayed to match --------------------------------
    defaults (h); h.set ("Drive", "30"); h.set ("Mix", "0"); h.apply();
    {
        auto in = noise (-6.0f, 5);
        const auto out = h.run (second, in);
        float maxDiff = 0.0f;
        for (size_t i = (size_t) latency; i < out.size(); ++i) maxDiff = std::max (maxDiff, std::abs (out[i] - in ((int) i - latency)));
        check (maxDiff < 1.0e-6f, "Mix 0% is the bit-exact dry signal", "max diff " + juce::String (maxDiff));
    }

    // --- Quiet signals pass at unity with no drive --------------------------------
    defaults (h); h.apply();
    {
        const auto out = h.run (second, sine (-40.0f, 1000.0f));
        const float g = rmsDb (out, (size_t) second / 2) - (-40.0f - 3.0103f);
        check (std::abs (g) < 0.05f, "Drive 0 dB, quiet signal: unity gain", "gain " + fmt (g, 3) + " dB");
    }

    // --- Auto gain keeps a -12 dBFS signal level ------------------------------------
    for (int m : { ember::Tape, ember::Tube, ember::Clip })
    {
        defaults (h); h.setChoice ("Character", m); h.set ("Drive", "24"); h.setBool ("Auto Gain", true); h.apply();
        const auto out = h.run (second, sine (-12.0f, 1000.0f));
        const float g = rmsDb (out, (size_t) second / 2) - (-12.0f - 3.0103f);
        check (std::abs (g) < 0.5f, juce::String ("Auto gain holds level: ") + modeName (m) + ", drive 24 dB", "level change " + fmt (g) + " dB");
    }

    // --- Harmonics --------------------------------------------------------------------
    auto harmonics = [&] (int m, float driveDb)
    {
        defaults (h); h.setChoice ("Character", m); h.set ("Drive", juce::String (driveDb)); h.setBool ("Auto Gain", true); h.apply();
        const auto out = h.run (second, sine (-12.0f, 1000.0f));
        const size_t a = (size_t) second / 2, b = out.size();
        const float f = toneDb (out, a, b, 1000.0), h2 = toneDb (out, a, b, 2000.0), h3 = toneDb (out, a, b, 3000.0);
        return std::array<float, 2> { h2 - f, h3 - f };
    };
    {
        const auto tape = harmonics (ember::Tape, 24.0f);
        check (tape[1] > -30.0f && tape[0] < -70.0f, "Tape: strong odd (3rd) harmonic, no even (2nd)",
               "H2 " + fmt (tape[0], 1) + " dB, H3 " + fmt (tape[1], 1) + " dB re fundamental");
        const auto tube = harmonics (ember::Tube, 24.0f);
        check (tube[0] > -40.0f, "Tube: adds even (2nd) harmonic", "H2 " + fmt (tube[0], 1) + " dB, H3 " + fmt (tube[1], 1) + " dB");
        const auto clean = harmonics (ember::Tape, 0.0f);
        check (clean[1] < tape[1] - 20.0f, "More drive gives more harmonics", "H3 at 0 dB drive " + fmt (clean[1], 1) + " dB vs 24 dB drive " + fmt (tape[1], 1) + " dB");
    }

    // --- Clip mode can't go much past full scale -----------------------------------------
    defaults (h); h.setChoice ("Character", ember::Clip); h.set ("Drive", "36"); h.apply();
    {
        const auto out = h.run (second, sine (0.0f, 100.0f));
        const float pk = peakDb (out, (size_t) latency + 2000);
        check (pk < 2.0f, "Clip at +36 dB drive: only band-limited overshoot past 0 dBFS", "peak " + fmt (pk) + " dBFS");
    }

    // --- Oversampling keeps aliasing down ------------------------------------------------
    defaults (h); h.set ("Drive", "30"); h.setBool ("Auto Gain", true); h.apply();
    {
        const auto out = h.run (second, sine (-6.0f, 9000.0f));
        const size_t a = (size_t) second / 2, b = out.size();
        const float f = toneDb (out, a, b, 9000.0);
        // Without oversampling the 5th harmonic (45 kHz) folds to 3 kHz and the 7th (63 kHz) to 15 kHz
        const float a3 = toneDb (out, a, b, 3000.0) - f, a15 = toneDb (out, a, b, 15000.0) - f, a21 = toneDb (out, a, b, 21000.0) - f;
        std::cout << "  alias levels re fundamental: 3 kHz " << fmt (a3, 1) << ", 15 kHz " << fmt (a15, 1) << ", 21 kHz " << fmt (a21, 1) << std::endl;
        check (std::max (a3, a15) < -70.0f, "Aliasing below 16 kHz from a hard-driven 9 kHz tone", fmt (std::max (a3, a15), 1) + " dB re fundamental");
    }

    // --- Tone tilt matches its filter maths ------------------------------------------------
    defaults (h); h.set ("Tone", "6"); h.apply();
    {
        float worst = 0.0f;
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            h.apply();
            const auto out = h.run (second, sine (-40.0f, hz));
            const float measured = rmsDb (out, (size_t) second / 2) - (-40.0f - 3.0103f);
            const float expected = (float) (lbaudio::dsp::magnitudeDb (ember::tiltLow (6.0f, sampleRate), hz, sampleRate)
                                            + lbaudio::dsp::magnitudeDb (ember::tiltHigh (6.0f, sampleRate), hz, sampleRate));
            worst = std::max (worst, std::abs (measured - expected));
        }
        check (worst < 0.1f, "Tone +6 dB tilt matches the shelf maths", "worst error " + fmt (worst, 3) + " dB");
    }

    // --- Output level --------------------------------------------------------------
    defaults (h); h.set ("Output", "-6"); h.apply();
    {
        const auto out = h.run (second, sine (-40.0f, 1000.0f));
        const float g = rmsDb (out, (size_t) second / 2) - (-40.0f - 3.0103f);
        check (std::abs (g + 6.0f) < 0.05f, "Output -6 dB", "gain " + fmt (g) + " dB");
    }

    // --- State recall ---------------------------------------------------------------
    defaults (h); h.set ("Drive", "17.5"); h.setChoice ("Character", ember::Tube); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Drive", "3"); h.setChoice ("Character", ember::Clip); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Drive") == "17.5 dB" && h.text ("Character") == "Tube", "State save/restore", "'" + h.text ("Drive") + "', '" + h.text ("Character") + "'");
}
} // namespace

int main (int argc, char** argv)
{
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<EmberAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<EmberAudioProcessor, EmberAudioProcessorEditor> (png, [] (Harness& h)
            {
                defaults (h);
                h.set ("Drive", "14"); h.setChoice ("Character", ember::Tube); h.set ("Tone", "-1.5");
                h.set ("Mix", "80"); h.set ("Output", "-1"); h.setBool ("Auto Gain", true);
            }, sine (-9.0f, 110.0f));
        });
}
