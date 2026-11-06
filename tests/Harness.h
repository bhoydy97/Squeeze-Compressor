#pragma once

#include <optional>

// Shared test harness for LBAudio plugins.
//
// Each <Name>Tests tool:
//   1. loads the compiled VST3 exactly as a DAW would and runs the plugin's checks on it,
//   2. runs the same checks on the processor compiled into the tool,
//   3. optionally renders the plugin window to a PNG.

#include <juce_audio_formats/juce_audio_formats.h>
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

// Renders a plugin window as video frames: the editor, fed an audio file, snapshotted 'fps' times a second.
//   <Name>Tests --ui-video <in.wav> <out-dir> <fps> <width-px> <start-s> <seconds> [Name=Value ...] ["Host BPM=100"]
// Writes <out-dir>/frame-00000.jpg ..., <out-dir>/audio.wav (the plugin's output over the same span,
// latency-compensated) and <out-dir>/params.json (the displayed text of any automated parameter, per frame).
// Meters, graphs, reverb tails and loudness readings are warmed up first: on the 12 s before the span, or for a
// span near the start of the file, by playing the file through once on a loop. Switches take on/off.
// Automation: "Name=Value@Seconds>Value@Seconds>..." moves a parameter through those values (seconds from the
// start of the span), linearly along the knob's travel; choices and switches change at each point.
template <typename Processor, typename Editor>
std::optional<int> maybeUIVideo (int argc, char** argv)
{
    if (argc < 8 || juce::String (argv[1]) != "--ui-video")
        return std::nullopt;

    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto inFile = cwd.getChildFile (argv[2]);
    const auto outDir = cwd.getChildFile (argv[3]);
    const int fps = juce::jmax (1, juce::String (argv[4]).getIntValue());
    const int widthPx = juce::jmax (100, juce::String (argv[5]).getIntValue());
    const double startS = juce::String (argv[6]).getDoubleValue();
    const double seconds = juce::String (argv[7]).getDoubleValue();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr) { std::cerr << "cannot read " << inFile.getFullPathName() << std::endl; return 1; }
    const double sr = reader->sampleRate;
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio (2, n);
    reader->read (&audio, 0, n, 0, true, true);

    Processor proc;
    struct Lane { juce::AudioProcessorParameter* param; std::vector<std::pair<double, float>> keys; juce::StringArray shown; };
    std::vector<Lane> lanes;
    auto toValue = [] (juce::AudioProcessorParameter* p, const juce::String& v)
    {
        if (v.equalsIgnoreCase ("on")) return 1.0f;
        if (v.equalsIgnoreCase ("off")) return 0.0f;
        return p->getValueForText (v);
    };
    // "Host BPM=100" plays the role of a DAW transport: playing, at that tempo (tempo-synced plugins follow it)
    struct HostTransport final : juce::AudioPlayHead
    {
        double bpm = 0.0, rate = 44100.0; long long sample = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            if (bpm <= 0.0) return {};
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (true);
            info.setTimeInSamples ((juce::int64) sample);
            info.setTimeInSeconds ((double) sample / rate);
            return info;
        }
    } transport;
    for (int i = 8; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto name = arg.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = arg.fromFirstOccurrenceOf ("=", false, false).trim();
        if (name.equalsIgnoreCase ("Host BPM")) { transport.bpm = value.getDoubleValue(); continue; }
        juce::AudioProcessorParameter* param = nullptr;
        for (auto* p : proc.getParameters())
            if (p->getName (64).equalsIgnoreCase (name)) param = p;
        if (param == nullptr) { std::cerr << "unknown parameter: " << name << std::endl; return 1; }
        if (value.contains ("@"))
        {
            Lane lane { param, {}, {} };
            for (auto& key : juce::StringArray::fromTokens (value, ">", ""))
                lane.keys.push_back ({ key.fromLastOccurrenceOf ("@", false, false).getDoubleValue(),
                                       toValue (param, key.upToLastOccurrenceOf ("@", false, false).trim()) });
            param->setValueNotifyingHost (lane.keys.front().second);
            lanes.push_back (lane);
        }
        else
        {
            param->setValueNotifyingHost (toValue (param, value));
        }
    }
    auto automate = [&] (double t)
    {
        for (auto& lane : lanes)
        {
            const auto& k = lane.keys;
            float v = k.front().second;
            const bool stepped = lane.param->isDiscrete() || lane.param->isBoolean();
            for (size_t j = 0; j < k.size(); ++j)
            {
                if (t >= k[j].first) v = k[j].second;
                if (! stepped && j + 1 < k.size() && t >= k[j].first && t < k[j + 1].first)
                    v = k[j].second + (k[j + 1].second - k[j].second) * (float) ((t - k[j].first) / (k[j + 1].first - k[j].first));
            }
            if (std::abs (lane.param->getValue() - v) > 1.0e-7f) lane.param->setValueNotifyingHost (v);
        }
    };

    const int spf = juce::roundToInt (sr / fps);
    transport.rate = sr;
    if (transport.bpm > 0.0) proc.setPlayHead (&transport);
    proc.setRateAndBufferSizeDetails (sr, spf);
    proc.prepareToPlay (sr, spf);
    juce::AudioBuffer<float> block (juce::jmax (2, proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels()), spf);
    juce::MidiBuffer midi;
    block.clear();
    proc.processBlock (block, midi); // parameter changes reach the plugin
    proc.reset();
    const int latency = juce::jmax (0, proc.getLatencySamples());

    long long pos = 0; // input position, looping over the file
    auto processChunk = [&] (int len, juce::AudioBuffer<float>* keep)
    {
        block.setSize (block.getNumChannels(), len, false, false, true);
        block.clear();
        for (int i = 0; i < len; ++i, ++pos)
            for (int ch = 0; ch < 2; ++ch)
                block.setSample (ch, i, audio.getSample (ch, (int) (pos % n)));
        transport.sample = pos - len;
        proc.processBlock (block, midi);
        if (keep != nullptr)
        {
            const int at = keep->getNumSamples();
            keep->setSize (2, at + len, true, false, true);
            for (int ch = 0; ch < 2; ++ch) keep->copyFrom (ch, at, block, ch, 0, len);
        }
    };

    // Warm up before the span: the 12 s leading into it, or (near the start of the file) once round the file
    outDir.createDirectory();
    Editor editor (proc);
    const float scale = (float) widthPx / (float) editor.getWidth();
    const double warm = 12.0;
    const long long first = startS >= warm ? (long long) std::llround (startS * sr)
                                           : (long long) n + (long long) std::llround (startS * sr);
    const long long last = first + (long long) std::llround (seconds * sr);
    pos = startS >= warm ? first - (long long) std::llround (warm * sr) : 0;
    automate (-1.0);
    processChunk ((int) ((first - pos) % spf), nullptr); // so the frames below fall exactly on 'first'
    while (pos < first) { processChunk (spf, nullptr); editor.refreshFromProcessor(); }

    juce::AudioBuffer<float> out (2, 0);
    juce::JPEGImageFormat jpeg;
    jpeg.setQuality (0.92f);
    int frame = 0;
    while (pos < last + latency + spf)
    {
        const bool inSpan = pos < last;
        automate ((double) (pos - first) / sr);
        processChunk (spf, &out);
        editor.refreshFromProcessor();
        if (inSpan)
        {
            for (auto& lane : lanes) lane.shown.add (lane.param->getText (lane.param->getValue(), 32));
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, scale);
            auto file = outDir.getChildFile (juce::String::formatted ("frame-%05d.jpg", frame++));
            file.deleteFile();
            juce::FileOutputStream os (file);
            jpeg.writeImageToStream (image, os);
        }
    }

    // Output aligned with the input: skip the plugin's latency
    const int keepN = (int) (last - first);
    juce::AudioBuffer<float> aligned (2, keepN);
    for (int ch = 0; ch < 2; ++ch) aligned.copyFrom (ch, 0, out, ch, latency, keepN);
    auto wavFile = outDir.getChildFile ("audio.wav");
    wavFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wavFile);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2)
                                                  .withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr) { std::cerr << "cannot write audio" << std::endl; return 1; }
    writer->writeFromAudioSampleBuffer (aligned, 0, keepN);
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("frames", frame);
        auto* params = new juce::DynamicObject();
        for (auto& lane : lanes)
        {
            juce::Array<juce::var> texts;
            for (auto& t : lane.shown) texts.add (t);
            params->setProperty (lane.param->getName (64), texts);
        }
        root->setProperty ("params", juce::var (params));
        outDir.getChildFile ("params.json").replaceWithText (juce::JSON::toString (juce::var (root), true));
    }
    std::cout << "frames " << frame << " (" << editor.getWidth() * scale << " px wide), latency " << latency
              << ", wrote " << outDir.getFullPathName() << std::endl;
    return 0;
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
