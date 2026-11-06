// Prism tests: the crossover sums flat, Mix stays flat, each band is its own, the compressors' maths, the
// exciters' harmonics (made from their band only, and not aliased), solo, bypass, presets and state.
//   PrismTests <Prism.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
int expectedLatency()
{
    juce::dsp::Oversampling<float> os (2, (size_t) PrismAudioProcessor::oversamplingStages,
                                       juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    os.initProcessing (512);
    return juce::roundToInt (os.getLatencyInSamples());
}

void defaults (juce::AudioProcessor& proc)
{
    for (auto* p : proc.getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
}

Gen mixOf (Gen a, Gen b) { return [a, b] (int n) { return a (n) + b (n); }; }

// A beat-like test signal for the snapshot: a kick, a bass line, a snare and hats
Gen beat (float db)
{
    auto nz = noise (db - 8.0f, 5);
    const float a = fromDb (db);
    return [nz, a] (int n)
    {
        const double t = n / sampleRate;
        const double beatLen = 0.5, pos = std::fmod (t, beatLen);
        const double pi2 = 2.0 * juce::MathConstants<double>::pi;
        float v = 0.0f;
        v += a * (float) (std::exp (-pos * 14.0) * std::sin (pi2 * (50.0 + 90.0 * std::exp (-pos * 30.0)) * pos));      // kick
        v += 0.35f * a * (float) std::sin (pi2 * 55.0 * t) * (float) std::exp (-std::fmod (t, 0.25) * 4.0);               // bass
        v += 0.25f * a * (float) std::sin (pi2 * 440.0 * t) + 0.15f * a * (float) std::sin (pi2 * 660.0 * t);             // keys
        const double hat = std::fmod (t, 0.125);
        v += (float) std::exp (-hat * 80.0) * 1.6f * (nz (n) - nz (n > 0 ? n - 1 : 0));                                   // hats
        if (std::fmod (t + 0.25, 0.5) < 0.12) v += 0.6f * nz (n) * (float) std::exp (-std::fmod (t + 0.25, 0.5) * 25.0); // snare
        return v;
    };
}

void runSuite (juce::AudioProcessor& proc)
{
    const int reportedEarly = proc.getLatencySamples();
    Harness h (proc);
    const int latency = proc.getLatencySamples();
    // A host only asks a VST3 for its latency once it's active, so the early value may read 0 there
    check (latency == expectedLatency() && (reportedEarly == 0 || reportedEarly == latency), "Oversampling latency is reported",
           juce::String (latency) + " samples (before prepare: " + juce::String (reportedEarly) + ")");
    runCommonChecks (proc, latency);
    const int half = (int) sampleRate / 2;
    const size_t settle = (size_t) sampleRate / 4;
    auto gainAt = [&] (double hz, float inDb = -20.0f)
    {
        const auto out = h.run (half, sine (inDb, (float) hz));
        return toneDb (out, settle, out.size(), hz) - inDb;
    };
    const double sweep[] = { 30.0, 80.0, 120.0, 250.0, 600.0, 1000.0, 2500.0, 6000.0, 9000.0, 15000.0 };
    auto worstDeviation = [&] (float expected = 0.0f)
    {
        float worst = 0.0f;
        for (double hz : sweep) worst = std::max (worst, std::abs (gainAt (hz) - expected));
        return worst;
    };

    // --- Untouched, the four bands add back up to the input ---------------------------------------
    defaults (proc); h.apply();
    {
        const float worst = worstDeviation();
        check (worst < 0.1f, "The four bands sum flat, 30 Hz - 15 kHz", "worst " + fmt (worst, 3) + " dB");
    }

    // --- Mix: the dry path is phase-matched, so blends stay flat -----------------------------------------
    defaults (proc); h.set ("Mix", "50"); h.apply();
    {
        const float worst = worstDeviation();
        check (worst < 0.1f, "Mix 50 % with nothing set is flat too", "worst " + fmt (worst, 3) + " dB");
    }
    defaults (proc); h.set ("High Mid Makeup", "6"); h.apply();
    {
        // In phase, half of a lift of w dB and half of the dry signal is 0.5 + 0.5 * w (out of phase it would be less)
        float worst = 0.0f;
        juce::String detail;
        for (double hz : { 700.0, 1000.0, 2500.0, 6000.0, 9000.0 })
        {
            h.set ("Mix", "100"); h.apply();
            const float wet = gainAt (hz);
            h.set ("Mix", "50"); h.apply();
            const float blend = gainAt (hz), expected = toDb (0.5f + 0.5f * fromDb (wet));
            worst = std::max (worst, std::abs (blend - expected));
            if (hz == 2500.0) detail = "2.5 kHz " + fmt (blend) + " dB, expected " + fmt (expected) + "; ";
        }
        check (worst < 0.05f, "Mix 50 % blends a band's +6 dB with the dry signal in phase", detail + "worst " + fmt (worst, 3) + " dB");
    }

    // --- Each band is its own ------------------------------------------------------------------------------
    defaults (proc); h.set ("Low Mid Makeup", "12"); h.apply();
    {
        const float in = gainAt (350.0), below = gainAt (30.0), above = gainAt (12000.0);
        check (in > 10.5f && std::abs (below) < 0.3f && std::abs (above) < 0.1f,
               "Low Mid +12 dB lifts 350 Hz and leaves 30 Hz and 12 kHz alone",
               "350 Hz " + fmt (in, 1) + ", 30 Hz " + fmt (below) + ", 12 kHz " + fmt (above) + " dB");
    }

    // --- Each band's compressor: threshold, ratio, and nothing in the other bands -------------------------
    defaults (proc); h.set ("High Split", "3000"); h.set ("High Threshold", "-30"); h.set ("High Ratio", "4");
    h.set ("High Attack", "1"); h.apply();
    {
        const auto out = h.run ((int) sampleRate, mixOf (sine (-10.0f, 12000.0f), sine (-10.0f, 100.0f)));
        const size_t from = (size_t) sampleRate / 2;
        const float high = toneDb (out, from, out.size(), 12000.0), low = toneDb (out, from, out.size(), 100.0);
        const float expected = -30.0f + (-10.0f - -30.0f) / 4.0f;
        check (std::abs (high - expected) < 0.6f, "High band, T -30 dB 4:1: a -10 dB tone comes out at -25 dB",
               fmt (high) + " dB, expected " + fmt (expected));
        check (std::abs (low - -10.0f) < 0.1f, "...while a 100 Hz tone in the Low band is untouched", fmt (low) + " dB");
    }
    defaults (proc); h.set ("Low Split", "300"); h.set ("Low Threshold", "-40"); h.set ("Low Ratio", "20"); h.set ("Low Attack", "1");
    h.set ("Low Release", "1000"); h.set ("Low Makeup", "10"); h.apply();
    {
        const auto out = h.run ((int) sampleRate, sine (-10.0f, 30.0f));
        const float got = toneDb (out, (size_t) sampleRate / 2, out.size(), 30.0);
        const float expected = -40.0f + 30.0f / 20.0f + 10.0f;
        check (std::abs (got - expected) < 0.8f, "Low band, T -40 dB 20:1 and +10 dB makeup", fmt (got) + " dB, expected " + fmt (expected));
    }

    // --- Excite: harmonics made from that band only ----------------------------------------------------------
    defaults (proc); h.apply();
    float cleanH2 = 0.0f;
    {
        const auto out = h.run ((int) sampleRate, sine (-12.0f, 60.0f));
        cleanH2 = std::max (toneDb (out, settle, out.size(), 120.0), toneDb (out, settle, out.size(), 180.0));
        check (cleanH2 < -100.0f, "Excite off: no harmonics", "2nd/3rd " + fmt (cleanH2, 1) + " dB");
    }
    defaults (proc); h.set ("Low Excite", "100"); h.apply();
    {
        const auto out = h.run ((int) sampleRate, sine (-12.0f, 60.0f));
        const float h2 = toneDb (out, settle, out.size(), 120.0), h3 = toneDb (out, settle, out.size(), 180.0);
        check (h2 > -45.0f && h3 > -45.0f, "Low Excite 100 %: a 60 Hz tone gains its 2nd and 3rd harmonics",
               "2nd " + fmt (h2, 1) + ", 3rd " + fmt (h3, 1) + " dB");
    }
    defaults (proc); h.set ("High Excite", "100"); h.set ("High Mid Excite", "100"); h.apply();
    {
        const auto out = h.run ((int) sampleRate, sine (-12.0f, 60.0f));
        const float hh = std::max (toneDb (out, settle, out.size(), 120.0), toneDb (out, settle, out.size(), 180.0));
        check (hh < -90.0f, "High and High Mid Excite: a 60 Hz tone (not in their bands) gains nothing", fmt (hh, 1) + " dB");
    }
    defaults (proc); h.set ("High Excite", "100"); h.apply();
    {
        // 9 kHz: its 3rd to 5th harmonics (27, 36, 45 kHz) would fold back to 21, 12 and 3 kHz without oversampling
        const auto out = h.run ((int) sampleRate, sine (-6.0f, 9000.0f));
        const float h2 = toneDb (out, settle, out.size(), 18000.0);
        float alias = -200.0f;
        for (double hz : { 21000.0, 12000.0, 3000.0 }) alias = std::max (alias, toneDb (out, settle, out.size(), hz));
        check (h2 > -40.0f, "High Excite: a 9 kHz tone gains its 18 kHz harmonic", fmt (h2, 1) + " dB");
        check (alias < -75.0f, "...and the harmonics above Nyquist don't fold back (4x oversampling)", "worst alias " + fmt (alias, 1) + " dB");
    }

    // --- Solo and bypass -----------------------------------------------------------------------------------------
    defaults (proc); h.setBool ("Low Mid Solo", true); h.apply();
    {
        const float in = gainAt (350.0), below = gainAt (30.0), above = gainAt (5000.0);
        check (std::abs (in) < 0.5f && below < -40.0f && above < -40.0f, "Low Mid Solo: only that band is heard",
               "350 Hz " + fmt (in) + ", 30 Hz " + fmt (below, 1) + ", 5 kHz " + fmt (above, 1) + " dB");
    }
    defaults (proc); h.setBool ("Low Solo", true); h.setBool ("High Solo", true); h.apply();
    {
        const float low = gainAt (40.0), high = gainAt (15000.0), mid = gainAt (1000.0);
        check (std::abs (low) < 0.3f && std::abs (high) < 0.3f && mid < -25.0f, "Two bands soloed: both heard, the rest not",
               "40 Hz " + fmt (low) + ", 15 kHz " + fmt (high) + ", 1 kHz " + fmt (mid, 1) + " dB");
    }
    defaults (proc); h.set ("High Split", "3000"); h.set ("High Threshold", "-50"); h.set ("High Ratio", "20");
    h.set ("High Excite", "100"); h.setBool ("High Bypass", true); h.apply();
    {
        const float g = gainAt (12000.0, -10.0f);
        check (std::abs (g) < 0.1f, "High Bypass: heavy settings in that band do nothing", fmt (g, 3) + " dB");
    }
    defaults (proc); h.set ("High Threshold", "-50"); h.set ("High Ratio", "20"); h.set ("Mix", "0"); h.apply();
    {
        const float g = gainAt (12000.0, -10.0f);
        check (std::abs (g) < 0.1f, "Mix 0 %: only the dry signal", fmt (g, 3) + " dB");
    }

    // --- Input and Output ---------------------------------------------------------------------------------------
    defaults (proc); h.set ("Input", "6"); h.set ("Output", "-3"); h.apply();
    {
        const float g = gainAt (1000.0);
        check (std::abs (g - 3.0f) < 0.1f, "Input +6 dB, Output -3 dB", fmt (g) + " dB");
    }

    // --- Splits that cross are put back in order, and the sum stays flat ---------------------------------------
    defaults (proc); h.set ("Low Split", "600"); h.set ("Mid Split", "250"); h.set ("High Split", "1500"); h.apply();
    {
        const float worst = worstDeviation();
        check (worst < 0.1f, "Crossed splits: still four ordered bands summing flat", "worst " + fmt (worst, 3) + " dB");
    }
    defaults (proc); h.set ("Low Split", "600"); h.set ("Mid Split", "250"); h.setBool ("Low Mid Solo", true); h.apply();
    {
        // Mid Split 250 can't sit below Low Split 600: it's pushed up above it, so 250 Hz is in the Low band now
        const float g = gainAt (250.0), low = gainAt (40.0);
        check (g < -20.0f && low < -40.0f, "Crossed splits: the Mid split is pushed above the Low split", "Low Mid band at 250 Hz " + fmt (g, 1) + ", 40 Hz " + fmt (low, 1) + " dB");
    }

    // --- Factory presets are the host's programs ---------------------------------------------------------------
    const auto& presets = prism::factoryPresets();
    check (proc.getNumPrograms() == (int) presets.size(), "Factory presets listed as programs", juce::String (proc.getNumPrograms()) + " programs");
    check (proc.getProgramName (0) == "Init", "Program 0 is Init", proc.getProgramName (0));
    {
        PrismAudioProcessor reference; // for each ID's name and range
        int bad = 0;
        juce::String detail;
        for (int k = 1; k < (int) presets.size(); ++k)
        {
            proc.setCurrentProgram (k);
            h.apply();
            for (const auto& [id, value] : presets[(size_t) k].values)
            {
                juce::AudioProcessorParameter* param = nullptr;
                for (auto* p : proc.getParameters())
                    if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                        if (withID->getParameterID() == id) param = p;
                if (param == nullptr)
                {
                    // Hosted: match by the parameter's name instead
                    auto* local = reference.apvts.getParameter (id);
                    for (auto* p : proc.getParameters())
                        if (local != nullptr && p->getName (64) == local->getName (64)) param = p;
                }
                auto* local = reference.apvts.getParameter (id);
                if (param == nullptr || local == nullptr) { ++bad; detail << id << " missing; "; continue; }
                const float got = local->convertFrom0to1 (param->getValue());
                const float tolerance = std::max (0.02f, std::abs (value) * 0.01f);
                if (std::abs (got - value) > tolerance) { ++bad; detail << presets[(size_t) k].name << " " << id << "=" << got << " not " << value << "; "; }
            }
            if (proc.getCurrentProgram() != k) { ++bad; detail << "program " << proc.getCurrentProgram() << " not " << k << "; "; }
        }
        check (bad == 0, "Every factory preset loads its settings", bad == 0 ? juce::String (presets.size()) + " presets" : detail);
    }
    {
        // Each preset stays sane on a loud beat: finite, no runaway level
        bool ok = true;
        juce::String detail;
        for (int k = 0; k < (int) presets.size(); ++k)
        {
            proc.setCurrentProgram (k);
            h.apply();
            const auto out = h.run ((int) sampleRate, beat (-8.0f));
            const float peak = peakDb (out);
            bool finite = true;
            for (auto v : out) finite = finite && std::isfinite (v);
            if (! finite || peak > 6.0f) { ok = false; detail << presets[(size_t) k].name << " peak " << fmt (peak, 1) << "; "; }
        }
        check (ok, "Every factory preset stays finite and under +6 dBFS on a loud beat", ok ? juce::String ("all fine") : detail);
    }

    // --- State recall ------------------------------------------------------------------------------------------
    proc.setCurrentProgram (3);
    defaults (proc); h.set ("Mid Split", "1800"); h.set ("High Mid Excite", "42"); h.setBool ("Low Bypass", true); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    defaults (proc); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Mid Split") == "1.80 kHz" && h.text ("High Mid Excite") == "42 %" && h.text ("Low Bypass") == "On",
           "State save/restore", "'" + h.text ("Mid Split") + "', '" + h.text ("High Mid Excite") + "', '" + h.text ("Low Bypass") + "'");

    // --- User presets (built-in processor only: they're files on disk) ----------------------------------------
    if (auto* prism = dynamic_cast<PrismAudioProcessor*> (&proc))
    {
        const juce::String name = "PrismTests temporary preset";
        defaults (proc); h.set ("Low Split", "90"); h.set ("High Excite", "33"); h.apply();
        const bool saved = prism->saveUserPreset (name);
        const auto file = PrismAudioProcessor::userPresetFolder().getChildFile (name + ".prismpreset");
        defaults (proc); h.apply();
        const bool listed = prism->userPresets().contains (file);
        const bool loaded = prism->loadUserPreset (file);
        h.apply();
        check (saved && listed && loaded && h.text ("Low Split") == "90 Hz" && h.text ("High Excite") == "33 %"
                   && prism->getPresetName() == name && prism->getPresetIndex() == -1,
               "User preset: save, list and load", "'" + h.text ("Low Split") + "', '" + h.text ("High Excite") + "', '" + prism->getPresetName() + "'");
        // A host setting program 0 while it loads a session mustn't wipe a preset of yours
        proc.setCurrentProgram (0);
        h.apply();
        check (h.text ("High Excite") == "33 %" && prism->getPresetName() == name, "...and a host's program 0 leaves it alone", h.text ("High Excite"));
        file.deleteFile();
        prism->applyFactoryPreset (0);
        check (prism->getPresetName() == "Init" && h.text ("Low Split") == "120 Hz", "Init preset resets everything", prism->getPresetName());
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (auto result = maybeUIVideo<PrismAudioProcessor, PrismAudioProcessorEditor> (argc, argv)) return *result;
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<PrismAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<PrismAudioProcessor, PrismAudioProcessorEditor> (png, [] (Harness& h)
            {
                h.proc.setCurrentProgram (1); // Mix glue
            }, beat (-5.0f), 90);
        });
}
