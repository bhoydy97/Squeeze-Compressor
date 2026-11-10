#pragma once

// Splinter on film: real sessions, played from a script, for the promo.
//
//   SplinterTests --analyse <file> [start-s length-s]
//       loads the file (or a stretch of it) and prints what Splinter makes of it: tempo, key, and every chop
//
//   SplinterTests --promo <script.json> <out-dir>
//       plays the script through one or more Splinters (one DAW transport, MIDI and window actions at set times)
//       and writes each one's audio (<id>.wav, 48 kHz float) and, for the spans it's on camera, its window as
//       frames (<id>/f-00000.jpg ...), plus layout.json: where each part of the window is, for pointers and zooms.
//
// Script:
//   { "fps": 30, "seconds": 66, "bpm": 90, "width": 2400,
//     "instances": [ { "id": "vox", "file": "...wav", "start": 0, "length": 4, "name": "shown name.wav" } ],
//     "events": [ { "t": 1.5, "i": "vox", "do": "note", "pad": 0, "vel": 0.9, "len": 0.25 }, ... ],
//     "shots": [ { "from": 0, "to": 20, "i": "vox" }, ... ] }
//   do: load | note | param (name, value) | ramp (name, value, dur) | select (pad) | flip | flipAgain | step (delta)
//       | lock (pad, on) | flipWindow (open) | preview (on) | tap (at: seconds into the sample) | untap (at)
//       | transport (playing) | drag (glow 0-1)

#include <optional>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace splinter::video
{
struct Host final : juce::AudioPlayHead
{
    double bpm = 90.0, rate = 48000.0; bool playing = false; juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setTimeInSamples (sample);
        info.setTimeInSeconds ((double) sample / rate);
        info.setPpqPosition ((double) sample / rate * bpm / 60.0);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

inline bool readStretch (const juce::File& f, double start, double length, juce::AudioBuffer<float>& out, double& rate)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return false;
    rate = r->sampleRate;
    const auto s0 = (juce::int64) std::llround (start * rate);
    const auto n = (int) (length > 0 ? std::llround (length * rate) : r->lengthInSamples - s0);
    out.setSize (2, n);
    r->read (&out, 0, n, s0, true, true);
    if (r->numChannels == 1) out.copyFrom (1, 0, out, 0, 0, n);
    if (start > 0 || length > 0)   // a stretch of a longer file: no clicks at its ends
    {
        const int f0 = juce::jmin (n / 4, (int) (0.003 * rate));
        out.applyGainRamp (0, f0, 0.0f, 1.0f);
        out.applyGainRamp (n - f0, f0, 1.0f, 0.0f);
    }
    return true;
}

inline juce::String noteOf (double hz)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (hz <= 0) return "-";
    const double m = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int k = (int) std::lround (m);
    return juce::String (names[(k % 12 + 12) % 12]) + juce::String (k / 12 - 1) + juce::String::formatted (" %+.0fc", (m - k) * 100.0);
}

// The strongest pitch near the start of a stretch (autocorrelation), 0 if there isn't a clear one
inline double pitchOf (const splinter::Sample& s, int a, int b)
{
    const int n = juce::jmin (b - a, (int) (0.12 * s.rate));
    if (n < 512) return 0.0;
    const int skip = juce::jmin ((b - a) / 4, (int) (0.02 * s.rate));
    std::vector<double> x ((size_t) n);
    for (int i = 0; i < n && a + skip + i < s.length(); ++i) x[(size_t) i] = s.mono (a + skip + i);
    const int lo = (int) (s.rate / 1000.0), hi = juce::jmin (n / 2, (int) (s.rate / 40.0));
    double r0 = 0; for (double v : x) r0 += v * v;
    if (r0 <= 1e-9) return 0.0;
    double best = 0; int bestK = 0;
    for (int k = lo; k < hi; ++k)
    {
        double r = 0; for (int i = 0; i + k < n; ++i) r += x[(size_t) i] * x[(size_t) (i + k)];
        r /= (n - k) / (double) n;
        if (r > best) { best = r; bestK = k; }
    }
    return best > 0.45 * r0 && bestK > 0 ? s.rate / bestK : 0.0;
}

inline int analyse (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    const double start = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 0.0, length = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 0.0;
    juce::AudioBuffer<float> buf; double rate = 0;
    if (! readStretch (file, start, length, buf, rate)) { std::cerr << "cannot read " << file.getFullPathName() << std::endl; return 1; }
    SplinterAudioProcessor p;
    p.loadSample (buf, rate, argc > 5 ? juce::String (argv[5]) : file.getFileName());
    auto d = p.sampleData();
    std::cout << d->name << ": " << juce::String (d->sample.length() / rate, 3) << " s, sound " << juce::String (d->soundStart / rate, 3) << "-"
              << juce::String (d->soundEnd / rate, 3) << " s, " << juce::String (d->tempo, 1) << " BPM" << (d->tempoFromName ? " (name)" : " (heard)")
              << ", " << d->key << (d->keyFromName ? " (name)" : " (heard)") << std::endl;
    static const char* roles[] = { "Low", "Snap", "Tick", "Tone" };
    for (auto m : { splinter::ChopMode::Hits, splinter::ChopMode::Even })
    {
        auto c = p.chopsFor (m);
        std::cout << (m == splinter::ChopMode::Hits ? "On the hits" : "Even") << ": " << c->size() << " chops" << std::endl;
        for (int i = 0; i < c->size(); ++i)
        {
            const auto [a, b] = c->span (i);
            float pk = 0; for (int k = a; k < b; ++k) pk = juce::jmax (pk, std::abs (d->sample.mono (k)));
            std::cout << "  " << juce::String (i + 1).paddedLeft (' ', 2) << "  " << juce::String (a / rate, 3) << " s  +" << juce::String ((b - a) / rate, 3)
                      << " s  " << roles[(int) c->roles[(size_t) i]] << "  peak " << juce::String (juce::Decibels::gainToDecibels (pk), 1) << " dB  "
                      << noteOf (pitchOf (d->sample, a, b)) << std::endl;
        }
    }
    return 0;
}

//==============================================================================
struct Instance
{
    juce::String id, name;
    juce::AudioBuffer<float> sample; double rate = 44100.0;
    std::unique_ptr<SplinterAudioProcessor> proc;
    std::unique_ptr<SplinterAudioProcessorEditor> editor;
    std::vector<float> outL, outR;
    struct Off { double t; int pad; };
    std::vector<Off> offs;
    struct Ramp { juce::AudioProcessorParameter* p; float from, to; double t0, t1; };
    std::vector<Ramp> ramps;

    juce::AudioProcessorParameter* param (const juce::String& which) const
    {
        for (auto* p : proc->getParameters()) if (p->getName (64).equalsIgnoreCase (which)) return p;
        std::cerr << "unknown parameter " << which << std::endl;
        return nullptr;
    }
};

inline void writeWav (const juce::File& f, const std::vector<float>& l, const std::vector<float>& r, double rate)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (32)
                                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (w == nullptr) return;
    const float* ch[2] = { l.data(), r.data() };
    w->writeFromFloatArrays (ch, 2, (int) l.size());
}

inline int promo (char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto script = juce::JSON::parse (cwd.getChildFile (argv[2]));
    const auto outDir = cwd.getChildFile (argv[3]);
    outDir.createDirectory();
    const int fps = script.getProperty ("fps", 30);
    const double seconds = script.getProperty ("seconds", 60.0), rate = 48000.0;
    const int widthPx = script.getProperty ("width", 2400);
    const int spf = (int) std::lround (rate / fps), frames = (int) std::ceil (seconds * fps);
    Host host; host.bpm = script.getProperty ("bpm", 90.0); host.rate = rate;

    std::vector<std::unique_ptr<Instance>> insts;
    auto find = [&] (const juce::String& id) -> Instance* { for (auto& i : insts) if (i->id == id) return i.get(); return nullptr; };
    if (auto* arr = script["instances"].getArray())
        for (auto& v : *arr)
        {
            auto in = std::make_unique<Instance>();
            in->id = v["id"].toString(); in->name = v["name"].toString();
            if (! readStretch (juce::File (v["file"].toString()), v.getProperty ("start", 0.0), v.getProperty ("length", 0.0), in->sample, in->rate))
            { std::cerr << "cannot read " << v["file"].toString() << std::endl; return 1; }
            in->proc = std::make_unique<SplinterAudioProcessor>();
            in->proc->setPlayHead (&host);
            in->proc->setRateAndBufferSizeDetails (rate, spf);
            in->proc->prepareToPlay (rate, spf);
            in->editor = std::make_unique<SplinterAudioProcessorEditor> (*in->proc);
            auto* p = in->proc.get();
            in->editor->buttonClock = [p] { return p->audioTime.load(); };
            outDir.getChildFile (in->id).createDirectory();
            insts.push_back (std::move (in));
        }

    struct Ev { double t; Instance* in; juce::var v; };
    std::vector<Ev> evs;
    if (auto* arr = script["events"].getArray())
        for (auto& v : *arr) evs.push_back ({ (double) v["t"], find (v["i"].toString()), v });
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t; });
    struct Shot { double from, to; Instance* in; };
    std::vector<Shot> shots;
    if (auto* arr = script["shots"].getArray())
        for (auto& v : *arr) shots.push_back ({ (double) v["from"], (double) v["to"], find (v["i"].toString()) });

    // Where everything is in the window (for the pointer and the zooms)
    {
        auto* root = new juce::DynamicObject();
        for (auto& [name, r] : insts.front()->editor->layout())
            root->setProperty (name, juce::Array<juce::var> { r.getX(), r.getY(), r.getWidth(), r.getHeight() });
        outDir.getChildFile ("layout.json").replaceWithText (juce::JSON::toString (juce::var (root), true));
    }

    juce::JPEGImageFormat jpeg; jpeg.setQuality (0.93f);
    const float scale = (float) widthPx / (float) SplinterAudioProcessorEditor::defaultWidth;
    juce::AudioBuffer<float> block (2, spf);
    size_t next = 0;
    std::map<Instance*, int> written;
    for (int f = 0; f < frames; ++f)
    {
        const double t0 = (double) f / fps, t1 = (double) (f + 1) / fps;
        std::map<Instance*, juce::MidiBuffer> midi;
        // what happens in this frame: window actions at its start, notes where they fall
        for (; next < evs.size() && evs[next].t < t1; ++next)
        {
            auto& e = evs[next];
            auto* in = e.in;
            const auto what = e.v["do"].toString();
            const int at = juce::jlimit (0, spf - 1, (int) std::lround ((e.t - t0) * rate));
            if (what == "transport") { host.playing = (bool) e.v["playing"]; continue; }
            if (in == nullptr) { std::cerr << "event with no instance at " << e.t << std::endl; continue; }
            auto& p = *in->proc; auto& ed = *in->editor;
            if (what == "note")
            {
                const int pad = e.v["pad"];
                midi[in].addEvent (juce::MidiMessage::noteOn (1, splinter::baseNote + pad, (float) (double) e.v.getProperty ("vel", 0.9)), at);
                in->offs.push_back ({ e.t + (double) e.v.getProperty ("len", 0.2), pad });
            }
            else if (what == "load") { p.loadSample (in->sample, in->rate, in->name); ed.refreshFromProcessor(); }
            else if (what == "param" || what == "ramp")
            {
                if (auto* q = in->param (e.v["name"].toString()))
                {
                    const auto target = q->getValueForText (e.v["value"].toString());
                    if (what == "param") q->setValueNotifyingHost (target);
                    else in->ramps.push_back ({ q, q->getValue(), target, e.t, e.t + (double) e.v.getProperty ("dur", 0.5) });
                }
            }
            else if (what == "select") ed.select (e.v["pad"]);
            else if (what == "flip") ed.pressFlip();
            else if (what == "flipAgain") ed.pressFlipAgain();
            else if (what == "step") p.stepFlip (e.v["delta"]);
            else if (what == "lock") ed.pressLock (e.v["pad"], e.v.getProperty ("on", true));
            else if (what == "flipWindow") ed.setFlipWindowOpen (e.v["open"]);
            else if (what == "preview") p.previewSample (e.v["on"]);
            else if (what == "tap") p.addTap ((int) std::lround ((double) e.v["at"] * in->rate));
            else if (what == "untap") p.removeTapNear ((int) std::lround ((double) e.v["at"] * in->rate));
            else if (what == "drag") ed.dragBox().glow = (float) (double) e.v["glow"];
            else std::cerr << "unknown action " << what << std::endl;
        }
        host.sample = (juce::int64) f * spf;
        for (auto& in : insts)
        {
            // parameters on the move
            for (auto& r : in->ramps)
            {
                const double k = juce::jlimit (0.0, 1.0, (t0 - r.t0) / juce::jmax (1e-6, r.t1 - r.t0));
                if (t0 >= r.t0 && t0 <= r.t1 + 0.1) r.p->setValueNotifyingHost (r.from + (r.to - r.from) * (float) k);
            }
            in->ramps.erase (std::remove_if (in->ramps.begin(), in->ramps.end(), [t0] (auto& r) { return t0 > r.t1 + 0.1; }), in->ramps.end());
            // note-offs due in this frame
            auto& mb = midi[in.get()];
            for (auto it = in->offs.begin(); it != in->offs.end();)
                if (it->t < t1)
                {
                    mb.addEvent (juce::MidiMessage::noteOff (1, splinter::baseNote + it->pad), juce::jlimit (0, spf - 1, (int) std::lround ((it->t - t0) * rate)));
                    it = in->offs.erase (it);
                }
                else ++it;
            block.clear();
            in->proc->processBlock (block, mb);
            in->outL.insert (in->outL.end(), block.getReadPointer (0), block.getReadPointer (0) + spf);
            in->outR.insert (in->outR.end(), block.getReadPointer (1), block.getReadPointer (1) + spf);
        }
        // the windows on camera
        for (auto& s : shots)
        {
            if (s.in == nullptr || t0 < s.from || t0 >= s.to) continue;
            auto& ed = *s.in->editor;
            ed.refreshFromProcessor();
            const auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, scale);
            auto file = outDir.getChildFile (s.in->id).getChildFile (juce::String::formatted ("f-%05d.jpg", f));
            file.deleteFile();
            juce::FileOutputStream os (file);
            jpeg.writeImageToStream (img, os);
            ++written[s.in];
        }
        if (f % (fps * 5) == 0) std::cout << "  " << juce::String (t0, 1) << " s" << std::endl;
    }
    for (auto& in : insts)
    {
        writeWav (outDir.getChildFile (in->id + ".wav"), in->outL, in->outR, rate);
        std::cout << in->id << ": " << written[in.get()] << " frames, " << juce::String (in->outL.size() / rate, 2) << " s of audio" << std::endl;
    }
    return 0;
}

// --flips <file> <start> <length> <busy %> <bars> <actions>: f flip, b back, n next, lN lock pad N, uN unlock
//   prints each pattern as it changes
inline int flips (int, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioBuffer<float> buf; double rate = 0;
    if (! readStretch (juce::File (argv[2]), juce::String (argv[3]).getDoubleValue(), juce::String (argv[4]).getDoubleValue(), buf, rate)) return 1;
    SplinterAudioProcessor p;
    p.loadSample (buf, rate, "x.wav");
    auto set = [&] (const char* n, const juce::String& v) { for (auto* q : p.getParameters()) if (q->getName (64) == n) q->setValueNotifyingHost (q->getValueForText (v)); };
    set ("Flip Busy", argv[5]);
    set ("Flip Length", juce::String (argv[6]) + (juce::String (argv[6]) == "1" ? " bar" : " bars"));
    for (auto& a : juce::StringArray::fromTokens (argv[7], ",", ""))
    {
        if (a == "f") p.flip();
        else if (a == "b") p.stepFlip (-1);
        else if (a == "n") p.stepFlip (1);
        else if (a.startsWith ("l")) { p.setRowLocked (a.substring (1).getIntValue() - 1, true); continue; }
        else if (a.startsWith ("u")) { p.setRowLocked (a.substring (1).getIntValue() - 1, false); continue; }
        auto pat = p.pattern();
        std::cout << a << " -> Flip " << p.flipIndex() + 1 << " of " << p.flipCount() << ": " << pat->hits.size() << " hits" << std::endl;
        for (int row : pat->rows())
        {
            juce::String line = juce::String (row + 1).paddedLeft (' ', 3) + " ";
            for (int st = 0; st < pat->steps; ++st) line << (st % 16 == 0 ? "|" : "") << (pat->has (st, row) ? "x" : (st % 4 == 0 ? "-" : "."));
            std::cout << line << std::endl;
        }
    }
    return 0;
}

inline std::optional<int> maybeRun (int argc, char** argv)
{
    if (argc >= 8 && juce::String (argv[1]) == "--flips") return flips (argc, argv);
    if (argc >= 3 && juce::String (argv[1]) == "--analyse") return analyse (argc, argv);
    if (argc >= 4 && juce::String (argv[1]) == "--promo") return promo (argv);
    return std::nullopt;
}
} // namespace splinter::video
