// Squeeze test tool
//
//   SqueezeTests <path/to/Squeeze.vst3> [ui-snapshot.png]
//
// 1. Loads the compiled VST3 exactly as a DAW would, feeds it test signals and
//    checks gain reduction, timing, mix, makeup, sidechain and state recall
//    against the theoretical values.
// 2. Runs the same checks on the processor compiled directly into this tool.
// 3. Optionally renders the plugin UI to a PNG.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

int failures = 0;

void check (bool ok, const juce::String& what, const juce::String& detail)
{
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << "  " << detail << std::endl;
    if (! ok) ++failures;
}

float toDb (float g) { return g > 1.0e-9f ? 20.0f * std::log10 (g) : -180.0f; }
float fromDb (float db) { return std::pow (10.0f, db / 20.0f); }

float expectedGr (float inDb, float t, float r, float k) { return squeeze::gainReductionDb (inDb, t, r, k); }

using Gen = std::function<float (int)>;

class Harness
{
public:
    explicit Harness (juce::AudioProcessor& p) : proc (p)
    {
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);
    }

    ~Harness() { proc.releaseResources(); }

    juce::AudioProcessorParameter* find (const juce::String& name)
    {
        for (auto* p : proc.getParameters())
            if (p->getName (64).equalsIgnoreCase (name))
                return p;
        std::cout << "  parameter not found: " << name << std::endl;
        ++failures;
        return nullptr;
    }

    void set (const juce::String& name, const juce::String& text)
    {
        if (auto* p = find (name))
            p->setValueNotifyingHost (p->getValueForText (text));
    }

    void setBool (const juce::String& name, bool on)
    {
        if (auto* p = find (name))
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
    }

    juce::String text (const juce::String& name)
    {
        if (auto* p = find (name)) return p->getText (p->getValue(), 64);
        return {};
    }

    void defaults()
    {
        set ("Threshold", "-20"); set ("Ratio", "4"); set ("Knee", "0");
        set ("Attack", "1"); set ("Release", "1000"); set ("Makeup", "0");
        set ("Mix", "100"); set ("SC HPF", "Off");
        setBool ("Auto Makeup", false); setBool ("External Sidechain", false);
    }

    // Push pending parameter changes through, then reset the DSP so each test starts clean.
    void apply()
    {
        run (blockSize, [] (int) { return 0.0f; });
        proc.releaseResources();
        proc.prepareToPlay (sampleRate, blockSize);
    }

    // Returns channel 0 of the main output.
    std::vector<float> run (int numSamples, Gen main, Gen side = nullptr)
    {
        const int totalIn  = proc.getTotalNumInputChannels();
        const int totalOut = proc.getTotalNumOutputChannels();
        const int mainCh   = proc.getMainBusNumInputChannels();
        juce::AudioBuffer<float> buffer (juce::jmax (totalIn, totalOut), blockSize);
        juce::MidiBuffer midi;
        std::vector<float> out;
        out.reserve ((size_t) numSamples);

        for (int start = 0; start < numSamples; start += blockSize)
        {
            const int n = juce::jmin (blockSize, numSamples - start);
            buffer.setSize (buffer.getNumChannels(), n, false, false, true);
            buffer.clear();
            for (int i = 0; i < n; ++i)
            {
                const float m = main (start + i);
                for (int ch = 0; ch < mainCh; ++ch) buffer.setSample (ch, i, m);
                const float s = side ? side (start + i) : 0.0f;
                for (int ch = mainCh; ch < totalIn; ++ch) buffer.setSample (ch, i, s);
            }
            proc.processBlock (buffer, midi);
            for (int i = 0; i < n; ++i) out.push_back (buffer.getSample (0, i));
        }
        return out;
    }

    juce::AudioProcessor& proc;
};

Gen sine (float db, float hz)
{
    const float a = fromDb (db);
    return [a, hz] (int n) { return a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / sampleRate); };
}

Gen dc (float db) { const float a = fromDb (db); return [a] (int) { return a; }; }

float peakDb (const std::vector<float>& v, size_t from)
{
    float p = 0.0f;
    for (size_t i = from; i < v.size(); ++i) p = std::max (p, std::abs (v[i]));
    return toDb (p);
}

juce::String fmt (float v) { return juce::String (v, 2); }

void runSuite (juce::AudioProcessor& proc)
{
    Harness h (proc);
    const int settle = (int) (1.5 * sampleRate);
    const size_t tail = (size_t) (1.2 * sampleRate);

    std::cout << "  inputs=" << proc.getTotalNumInputChannels() << " outputs=" << proc.getTotalNumOutputChannels()
              << " latency=" << proc.getLatencySamples() << std::endl;
    check (proc.getLatencySamples() == 0, "Zero latency", "");

    // --- Static curve, hard knee -------------------------------------------------
    h.defaults(); h.apply();
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
    h.defaults(); h.set ("Ratio", "20"); h.set ("Threshold", "-30"); h.apply();
    {
        const auto out = h.run (settle, sine (-6.0f, 1000.0f));
        const float expected = -6.0f + expectedGr (-6.0f, -30.0f, 20.0f, 0.0f);
        check (std::abs (peakDb (out, tail) - expected) < 0.25f, "20:1 ratio", "out " + fmt (peakDb (out, tail)) + " dB, expected " + fmt (expected));
    }

    // --- Attack / release timing (DC step gives an exact exponential) ----------
    h.defaults(); h.set ("Threshold", "-30"); h.set ("Ratio", "10");
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
    h.defaults(); h.set ("Mix", "0"); h.apply();
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
    h.defaults(); h.set ("Makeup", "6"); h.apply();
    {
        const auto out = h.run (settle, sine (-40.0f, 1000.0f));
        check (std::abs (peakDb (out, tail) - (-34.0f)) < 0.1f, "Manual makeup +6 dB", "out " + fmt (peakDb (out, tail)) + " dB");
    }
    h.defaults(); h.setBool ("Auto Makeup", true); h.apply();
    {
        const auto out = h.run (settle, sine (-40.0f, 1000.0f));
        const float expected = -40.0f + squeeze::autoMakeupDb (-20.0f, 4.0f, 0.0f); // +7.5 dB
        check (std::abs (peakDb (out, tail) - expected) < 0.1f, "Auto makeup (+7.5 dB at T -20, 4:1)", "out " + fmt (peakDb (out, tail)) + " dB");
    }

    // --- Sidechain high-pass -------------------------------------------------------
    h.defaults(); h.apply();
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
        h.defaults(); h.set ("Threshold", "-30"); h.apply();
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
    h.defaults(); h.set ("Threshold", "-33.3"); h.set ("Release", "1.5 s"); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Threshold", "-5"); h.set ("Release", "50"); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Threshold") == "-33.3 dB" && h.text ("Release") == "1.50 s", "State save/restore",
           "threshold '" + h.text ("Threshold") + "', release '" + h.text ("Release") + "'");
}

void renderSnapshot (const juce::File& png)
{
    SqueezeAudioProcessor proc;
    Harness h (proc);
    h.defaults(); h.set ("Threshold", "-24"); h.set ("Ratio", "4"); h.set ("Knee", "8");
    h.set ("Attack", "8"); h.set ("Release", "150"); h.set ("Makeup", "4.5"); h.set ("Mix", "85");
    h.set ("SC HPF", "80"); h.apply();
    h.run ((int) sampleRate / 2, sine (-12.0f, 220.0f));

    SqueezeAudioProcessorEditor editor (proc);
    for (int i = 0; i < 12; ++i)
    {
        h.run (blockSize * 3, sine (-12.0f, 220.0f));
        editor.refreshFromProcessor();
    }

    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    png.deleteFile();
    juce::FileOutputStream os (png);
    juce::PNGImageFormat().writeImageToStream (image, os);
    std::cout << "  wrote " << png.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "usage: SqueezeTests <Squeeze.vst3> [snapshot.png]" << std::endl;
        return 2;
    }

    // 1) The real VST3 binary, loaded like a host would.
    {
        std::cout << "\n== Hosted VST3: " << argv[1] << std::endl;
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName());
        check (types.size() == 1, "VST3 scanned", juce::String (types.size()) + " plugin(s) found");

        if (! types.isEmpty())
        {
            const auto& d = *types[0];
            std::cout << "  " << d.name << " by " << d.manufacturerName << ", category " << d.category << std::endl;

            juce::String error;
            auto instance = format.createInstanceFromDescription (d, sampleRate, blockSize, error);
            check (instance != nullptr, "VST3 instantiated", error);
            if (instance != nullptr)
                runSuite (*instance);
        }
    }

    // 2) Same checks on the processor compiled into this tool.
    {
        std::cout << "\n== Built-in processor" << std::endl;
        SqueezeAudioProcessor proc;
        runSuite (proc);
    }

    if (argc >= 3)
    {
        std::cout << "\n== UI snapshot" << std::endl;
        renderSnapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
