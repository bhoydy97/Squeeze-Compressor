#pragma once

// Shared test harness for LBAudio plugins.
//
// Each <Name>Tests tool:
//   1. loads the compiled VST3 exactly as a DAW would and runs the plugin's checks on it,
//   2. runs the same checks on the processor compiled into the tool,
//   3. optionally renders the plugin window to a PNG.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <iostream>
#include <random>

namespace lbtest
{
inline double sampleRate = 48000.0;
inline constexpr int blockSize = 512;
inline int failures = 0;

inline void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << "  " << detail << std::endl;
    if (! ok) ++failures;
}

inline float toDb (float g)   { return g > 1.0e-9f ? 20.0f * std::log10 (g) : -180.0f; }
inline float fromDb (float db) { return std::pow (10.0f, db / 20.0f); }
inline juce::String fmt (float v, int decimals = 2) { return juce::String (v, decimals); }

using Gen = std::function<float (int)>;

inline Gen sine (float db, float hz)
{
    const float a = fromDb (db);
    return [a, hz] (int n) { return a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / sampleRate); };
}

inline Gen dc (float db) { const float a = fromDb (db); return [a] (int) { return a; }; }

inline Gen silence() { return [] (int) { return 0.0f; }; }

// Deterministic white noise (same sequence every run)
inline Gen noise (float db, unsigned seed = 1)
{
    auto data = std::make_shared<std::vector<float>>();
    const float a = fromDb (db) * std::sqrt (3.0f); // uniform noise with that RMS
    return [data, a, seed] (int n) mutable
    {
        if ((size_t) n >= data->size())
        {
            std::mt19937 rng (seed);
            std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
            data->resize ((size_t) n + 48000 * 4);
            for (auto& v : *data) v = dist (rng);
        }
        return a * (*data)[(size_t) n];
    };
}

inline Gen impulse (float db = 0.0f, int at = 0) { const float a = fromDb (db); return [a, at] (int n) { return n == at ? a : 0.0f; }; }

inline float peakDb (const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    float p = 0.0f;
    for (size_t i = from; i < (to ? to : v.size()); ++i) p = std::max (p, std::abs (v[i]));
    return toDb (p);
}

inline float rmsDb (const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < (to ? to : v.size()); ++i) { s += (double) v[i] * v[i]; ++n; }
    return n ? toDb ((float) std::sqrt (s / (double) n)) : -180.0f;
}

// Amplitude (dBFS) of one frequency in v[from, to), using a 4-term Blackman-Harris window
// (side lobes ~92 dB down, so harmonics far below the fundamental can be measured).
inline float toneDb (const std::vector<float>& v, size_t from, size_t to, double hz)
{
    const size_t n = to - from;
    double s = 0.0, c = 0.0, wsum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        const double ph = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / sampleRate;
        s += w * v[from + i] * std::sin (ph);
        c += w * v[from + i] * std::cos (ph);
        wsum += w;
    }
    return toDb ((float) (2.0 * std::sqrt (s * s + c * c) / wsum));
}

class Harness
{
public:
    explicit Harness (juce::AudioProcessor& p) : proc (p) { prepare(); }
    ~Harness() { proc.releaseResources(); }

    void prepare()
    {
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);
    }

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

    void setChoice (const juce::String& name, int index)
    {
        if (auto* p = find (name))
            p->setValueNotifyingHost ((float) index / (float) juce::jmax (1, p->getNumSteps() - 1));
    }

    juce::String text (const juce::String& name)
    {
        if (auto* p = find (name)) return p->getText (p->getValue(), 64);
        return {};
    }

    // Push pending parameter changes through, then reset the DSP so each test starts clean.
    void apply()
    {
        run (blockSize, silence());
        proc.releaseResources();
        prepare();
    }

    // Mono-in-both-channels convenience: returns channel 0 of the main output.
    std::vector<float> run (int numSamples, Gen main, Gen side = nullptr)
    {
        auto out = runStereo (numSamples, main, main, side);
        return out[0];
    }

    // Independent left/right inputs; returns both output channels.
    std::array<std::vector<float>, 2> runStereo (int numSamples, Gen left, Gen right, Gen side = nullptr)
    {
        const int totalIn  = proc.getTotalNumInputChannels();
        const int totalOut = proc.getTotalNumOutputChannels();
        const int mainIn   = proc.getMainBusNumInputChannels();
        const int mainOut  = proc.getMainBusNumOutputChannels();
        juce::AudioBuffer<float> buffer (juce::jmax (totalIn, totalOut), blockSize);
        juce::MidiBuffer midi;
        std::array<std::vector<float>, 2> out;
        out[0].reserve ((size_t) numSamples);
        out[1].reserve ((size_t) numSamples);

        for (int start = 0; start < numSamples; start += blockSize)
        {
            const int n = juce::jmin (blockSize, numSamples - start);
            buffer.setSize (buffer.getNumChannels(), n, false, false, true);
            buffer.clear();
            for (int i = 0; i < n; ++i)
            {
                const float l = left (start + i), r = right (start + i);
                for (int ch = 0; ch < mainIn; ++ch) buffer.setSample (ch, i, ch == 1 ? r : l);
                const float s = side ? side (start + i) : 0.0f;
                for (int ch = mainIn; ch < totalIn; ++ch) buffer.setSample (ch, i, s);
            }
            proc.processBlock (buffer, midi);
            for (int i = 0; i < n; ++i)
            {
                out[0].push_back (buffer.getSample (0, i));
                out[1].push_back (buffer.getSample (mainOut > 1 ? 1 : 0, i));
            }
        }
        return out;
    }

    juce::AudioProcessor& proc;
};

// Common checks every plugin must pass.
inline void runCommonChecks (juce::AudioProcessor& proc, int expectedLatency = 0)
{
    std::cout << "  inputs=" << proc.getTotalNumInputChannels() << " outputs=" << proc.getTotalNumOutputChannels()
              << " latency=" << proc.getLatencySamples() << std::endl;
    check (proc.getLatencySamples() == expectedLatency, "Reported latency", juce::String (proc.getLatencySamples()) + " samples");

    // Silence in, silence out (no DC, noise or denormal junk)
    Harness h (proc);
    const auto out = h.run ((int) sampleRate, silence());
    float worst = 0.0f;
    for (auto v : out) worst = std::max (worst, std::abs (v));
    check (worst < 1.0e-6f, "Silence in gives silence out", "max " + juce::String (worst));

    // Extreme input stays finite
    bool finite = true;
    const auto loud = h.run ((int) sampleRate / 2, sine (12.0f, 60.0f));
    for (auto v : loud) finite = finite && std::isfinite (v);
    check (finite, "+12 dBFS input stays finite", "");
}

// Renders a plugin window to PNG after feeding it some audio.
template <typename Processor, typename Editor>
void renderSnapshot (const juce::File& png, std::function<void (Harness&)> setup, Gen signal, int ticks = 14)
{
    Processor proc;
    Harness h (proc);
    setup (h);
    h.apply();
    // Play the signal continuously across the runs
    int offset = 0;
    auto play = [&] (int n) { h.run (n, [&signal, offset] (int i) { return signal (offset + i); }); offset += n; };
    play ((int) sampleRate / 2);

    Editor editor (proc);
    for (int i = 0; i < ticks; ++i)
    {
        play (blockSize * 3);
        editor.refreshFromProcessor();
    }

    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    png.deleteFile();
    juce::FileOutputStream os (png);
    juce::PNGImageFormat().writeImageToStream (image, os);
    std::cout << "  wrote " << png.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
}

// main() for every <Name>Tests tool.
inline int runMain (int argc, char** argv,
                    std::function<void (juce::AudioProcessor&)> suite,
                    std::function<std::unique_ptr<juce::AudioProcessor>()> makeLocal,
                    std::function<void (const juce::File&)> snapshot)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout << "usage: " << argv[0] << " <Plugin.vst3> [snapshot.png]" << std::endl;
        return 2;
    }

    {
        std::cout << "\n== Hosted VST3: " << argv[1] << std::endl;
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName());
        check (types.size() == 1, "VST3 scanned", juce::String (types.size()) + " plugin(s) found");

        if (! types.isEmpty())
        {
            const auto& d = *types[0];
            std::cout << "  " << d.name << " by " << d.manufacturerName << " " << d.version << ", category " << d.category << std::endl;
            juce::String error;
            auto instance = format.createInstanceFromDescription (d, sampleRate, blockSize, error);
            check (instance != nullptr, "VST3 instantiated", error);
            if (instance != nullptr)
                suite (*instance);
        }
    }

    {
        std::cout << "\n== Built-in processor" << std::endl;
        auto proc = makeLocal();
        suite (*proc);
    }

    if (argc >= 3)
    {
        std::cout << "\n== UI snapshot" << std::endl;
        snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
} // namespace lbtest
