// Haze tests: decay time, damping, pre-delay, low cut, width and stability, measured from the output.
//   HazeTests <Haze.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void defaults (Harness& h)
{
    h.set ("Mix", "100"); h.set ("Pre-delay", "0"); h.set ("Size", "50"); h.set ("Decay", "2");
    h.set ("Damping", "20 kHz"); h.set ("Low Cut", "20"); h.set ("Width", "100"); h.set ("Modulation", "0");
}

// Energy (L^2 + R^2) of a stereo response, optionally band-limited to [lo, hi] Hz first
std::vector<double> energy (const std::array<std::vector<float>, 2>& out, double lo = 0.0, double hi = 0.0)
{
    std::vector<double> e (out[0].size(), 0.0);
    for (int ch = 0; ch < 2; ++ch)
    {
        std::vector<lbaudio::dsp::Biquad> filters;
        if (lo > 0.0)
            for (int i = 0; i < 2; ++i) { filters.emplace_back(); filters.back().c = lbaudio::dsp::design (lbaudio::dsp::Shape::HighPass, lo, 0, 0.7071, sampleRate); }
        if (hi > 0.0)
            for (int i = 0; i < 2; ++i) { filters.emplace_back(); filters.back().c = lbaudio::dsp::design (lbaudio::dsp::Shape::LowPass, hi, 0, 0.7071, sampleRate); }
        for (size_t i = 0; i < e.size(); ++i)
        {
            float v = out[(size_t) ch][i];
            for (auto& f : filters) v = f.process (v);
            e[i] += (double) v * v;
        }
    }
    return e;
}

// Reverb time from the Schroeder backward integral: time from -5 to -35 dB, doubled
double rt60 (const std::vector<double>& e)
{
    std::vector<double> edc (e.size());
    double acc = 0.0;
    for (size_t i = e.size(); i-- > 0;) { acc += e[i]; edc[i] = acc; }
    size_t start = 0;
    for (size_t i = 0; i < e.size(); ++i) if (e[i] > 0.0) { start = i; break; }
    const double total = edc[start];
    double t5 = -1.0, t35 = -1.0;
    for (size_t i = start; i < e.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (1.0e-300, edc[i] / total));
        if (t5 < 0.0 && db <= -5.0) t5 = (double) i;
        if (db <= -35.0) { t35 = (double) i; break; }
    }
    if (t5 < 0.0 || t35 < 0.0) return -1.0;
    return 2.0 * (t35 - t5) / sampleRate;
}

std::array<std::vector<float>, 2> impulseResponse (Harness& h, double seconds)
{
    const auto imp = impulse (0.0f, 0);
    return h.runStereo ((int) (seconds * sampleRate), imp, imp);
}

double correlation (const std::array<std::vector<float>, 2>& out, size_t from)
{
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = from; i < out[0].size(); ++i)
    {
        lr += (double) out[0][i] * out[1][i];
        ll += (double) out[0][i] * out[0][i];
        rr += (double) out[1][i] * out[1][i];
    }
    return lr / std::sqrt (std::max (1.0e-30, ll * rr));
}

int onset (const std::array<std::vector<float>, 2>& out, float thresholdDb)
{
    float peak = 0.0f;
    for (auto& ch : out) for (auto v : ch) peak = std::max (peak, std::abs (v));
    const float th = peak * fromDb (thresholdDb);
    for (size_t i = 0; i < out[0].size(); ++i)
        if (std::abs (out[0][i]) > th || std::abs (out[1][i]) > th) return (int) i;
    return -1;
}

void runSuite (juce::AudioProcessor& proc)
{
    Harness h (proc);
    runCommonChecks (proc, 0);
    const int second = (int) sampleRate;

    // --- Tail length is reported to the host -------------------------------------
    defaults (h); h.set ("Decay", "4"); h.set ("Pre-delay", "50"); h.apply();
    check (proc.getTailLengthSeconds() >= 4.05, "Tail length reported to the host covers pre-delay + decay",
           fmt ((float) proc.getTailLengthSeconds()) + " s");

    // --- Mix 0 is the untouched dry signal ----------------------------------------
    defaults (h); h.set ("Mix", "0"); h.set ("Decay", "8"); h.set ("Modulation", "100"); h.apply();
    {
        auto in = noise (-6.0f, 3);
        const auto out = h.run (second, in);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < out.size(); ++i) maxDiff = std::max (maxDiff, std::abs (out[i] - in ((int) i)));
        check (! (maxDiff > 0.0f), "Mix 0% is the bit-exact dry signal", "max diff " + juce::String (maxDiff));
    }

    // --- Decay time matches the knob, at every size ----------------------------------
    for (float decay : { 0.5f, 2.0f, 6.0f })
        for (float size : { 0.0f, 50.0f, 100.0f })
        {
            defaults (h); h.set ("Decay", juce::String (decay)); h.set ("Size", juce::String (size)); h.apply();
            const auto ir = impulseResponse (h, decay * 1.6 + 0.6);
            const double measured = rt60 (energy (ir, 700.0, 1400.0));
            const double err = (measured / decay - 1.0) * 100.0;
            check (measured > 0.0 && std::abs (err) < 10.0,
                   "Decay " + fmt (decay, 1) + " s, size " + juce::String ((int) size) + "%: measured RT60 (1 kHz band)",
                   fmt ((float) measured) + " s (" + (err >= 0 ? "+" : "") + fmt ((float) err, 1) + "%)");
        }
    {
        defaults (h); h.set ("Decay", "3"); h.set ("Modulation", "30"); h.apply();
        const double measured = rt60 (energy (impulseResponse (h, 6.0)));
        check (std::abs (measured / 3.0 - 1.0) < 0.15, "Decay 3 s, broadband with modulation: RT60 within 15%", fmt ((float) measured) + " s");
    }

    // --- Damping shortens the highs only ----------------------------------------------
    auto bandRatio = [&] (const char* dampingText)
    {
        defaults (h); h.set ("Decay", "3"); h.set ("Damping", dampingText); h.apply();
        const auto ir = impulseResponse (h, 6.0);
        const double low = rt60 (energy (ir, 350.0, 700.0)), high = rt60 (energy (ir, 7000.0, 10000.0));
        return std::array<double, 2> { low, high };
    };
    {
        const auto open = bandRatio ("20 kHz");
        check (open[1] / open[0] > 0.75, "Damping 20 kHz: highs ring almost as long as lows",
               "500 Hz " + fmt ((float) open[0]) + " s, 8 kHz " + fmt ((float) open[1]) + " s");
        const auto damped = bandRatio ("2 kHz");
        check (damped[1] / damped[0] < 0.45 && std::abs (damped[0] / 3.0 - 1.0) < 0.1,
               "Damping 2 kHz: highs die much faster, lows keep the decay time",
               "500 Hz " + fmt ((float) damped[0]) + " s, 8 kHz " + fmt ((float) damped[1]) + " s");
    }

    // --- Pre-delay moves the start of the reverb by exactly that much --------------------
    {
        defaults (h); h.set ("Decay", "1"); h.apply();
        const int a = onset (impulseResponse (h, 1.0), -50.0f);
        defaults (h); h.set ("Decay", "1"); h.set ("Pre-delay", "100"); h.apply();
        const int b = onset (impulseResponse (h, 1.2), -50.0f);
        check (b - a == (int) (0.1 * sampleRate), "Pre-delay 100 ms starts the reverb 100 ms later",
               juce::String (b - a) + " samples later (onsets " + juce::String (a) + ", " + juce::String (b) + ")");
    }

    // --- Low cut keeps bass out of the reverb --------------------------------------------
    auto wetLevel = [&] (const char* lowCutText, float hz)
    {
        defaults (h); h.set ("Decay", "1"); h.set ("Low Cut", lowCutText); h.apply();
        const auto out = h.runStereo (3 * second, sine (-12.0f, hz), sine (-12.0f, hz));
        return rmsDb (out[0], (size_t) (1.5 * second)) ;
    };
    {
        const float open = wetLevel ("20", 80.0f), cut = wetLevel ("400", 80.0f);
        check (open - cut > 20.0f, "Low cut 400 Hz takes 80 Hz out of the reverb", fmt (open - cut, 1) + " dB less");
        const float mids = wetLevel ("20", 2000.0f) - wetLevel ("400", 2000.0f);
        check (std::abs (mids) < 1.5f, "...and leaves 2 kHz alone", fmt (mids, 2) + " dB difference");
    }

    // --- Width: mono, natural, extra wide --------------------------------------------------
    {
        std::array<double, 3> corr {};
        int idx = 0;
        for (const char* w : { "0", "100", "200" })
        {
            defaults (h); h.set ("Width", w); h.apply();
            const auto n = noise (-12.0f, 9);
            corr[(size_t) idx++] = correlation (h.runStereo (2 * second, n, n), (size_t) (second / 2));
        }
        check (corr[0] > 0.9999, "Width 0%: mono reverb", "L/R correlation " + fmt ((float) corr[0], 4));
        check (std::abs (corr[1]) < 0.3, "Width 100%: wide, decorrelated tail from a mono source", "L/R correlation " + fmt ((float) corr[1], 3));
        check (corr[2] < corr[1] - 0.2, "Width 200%: wider still", "L/R correlation " + fmt ((float) corr[2], 3));
    }

    // --- Level: a sensible wet level, so Mix behaves ------------------------------------
    {
        defaults (h); h.set ("Damping", "6 kHz"); h.set ("Low Cut", "100"); h.set ("Modulation", "30"); h.apply();
        const auto n = noise (-18.0f, 4);
        const auto out = h.runStereo (4 * second, n, n);
        const float gain = rmsDb (out[0], (size_t) (2 * second)) - rmsDb ([&] { std::vector<float> v; for (int i = 0; i < second; ++i) v.push_back (n (i)); return v; }());
        check (std::abs (gain) < 9.0f, "100% wet level stays near the input level (default settings)", fmt (gain, 1) + " dB");
    }

    // --- Stability: longest, biggest, most modulated, then silence -------------------------
    {
        defaults (h); h.set ("Decay", "20"); h.set ("Size", "100"); h.set ("Modulation", "100"); h.apply();
        const auto n = noise (0.0f, 5);
        auto out = h.runStereo (2 * second, n, n);
        float peak = peakDb (out[0]);
        const auto tail = h.runStereo (30 * second, silence(), silence());
        bool finite = true;
        for (auto& ch : tail) for (auto v : ch) finite = finite && std::isfinite (v);
        const float early = rmsDb (tail[0], 0, (size_t) second), late = rmsDb (tail[0], (size_t) (29 * second));
        peak = std::max (peak, peakDb (tail[0]));
        check (finite && peak < 18.0f, "Decay 20 s, size 100%, modulation 100%: stable", "peak " + fmt (peak, 1) + " dBFS");
        // 28 s at 3 dB/s = 84 dB down
        check (early - late > 70.0f, "...and the tail still dies away (no ringing build-up)", fmt (early - late, 1) + " dB down after 28 s");
    }

    // --- Moving every control while audio runs: no blow-ups, no NaNs -------------------------
    {
        defaults (h); h.apply();
        juce::Random rng (7);
        bool finite = true;
        float peak = -200.0f;
        const auto n = noise (-6.0f, 11);
        for (int block = 0; block < 400; ++block)
        {
            for (auto* p : proc.getParameters())
                if (p != proc.getBypassParameter() && rng.nextFloat() < 0.2f) p->setValueNotifyingHost (rng.nextFloat());
            const auto out = h.runStereo (blockSize, n, n);
            for (auto& ch : out) for (auto v : ch) { finite = finite && std::isfinite (v); peak = std::max (peak, toDb (std::abs (v))); }
        }
        check (finite && peak < 24.0f, "Random parameter changes on every block: output stays finite and bounded", "peak " + fmt (peak, 1) + " dBFS");
    }

    // --- Changing Size glides (a pitch bend) rather than clicking ----------------------------
    for (const auto& jump : { std::pair<const char*, const char*> { "0", "100" }, { "100", "0" } })
    {
        defaults (h); h.set ("Size", jump.first); h.apply();
        // A tone that fades in and then runs on unbroken across the two halves
        const Gen tone = [] (int n)
        {
            const float fade = n < 4800 ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) n / 4800.0f) : 1.0f;
            return fade * fromDb (-12.0f) * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) n / (float) sampleRate);
        };
        const Gen later = [tone, second] (int n) { return tone (n + second); };
        auto out = h.runStereo (second, tone, tone);
        h.set ("Size", jump.second);
        const auto after = h.runStereo (second, later, later);
        for (int ch = 0; ch < 2; ++ch) out[(size_t) ch].insert (out[(size_t) ch].end(), after[(size_t) ch].begin(), after[(size_t) ch].end());

        // Energy above 8 kHz in the second half (clicks would be broadband), relative to the signal
        const auto hf = energy (out, 8000.0, 0.0);
        const auto all = energy (out);
        double hfSum = 0.0, allSum = 0.0;
        for (size_t i = (size_t) second; i < hf.size(); ++i) { hfSum += hf[i]; allSum += all[i]; }
        const float ratio = (float) (10.0 * std::log10 (std::max (1.0e-30, hfSum / allSum)));
        check (ratio < -60.0f, juce::String ("Size ") + jump.first + " -> " + jump.second + "% in one jump: no clicks",
               "energy above 8 kHz " + fmt (ratio, 1) + " dB re total");
    }
}

// Bursts of noise, like a snare, for the snapshot
Gen bursts()
{
    auto n = noise (-10.0f, 21);
    return [n] (int i)
    {
        const int pos = i % 19200;
        return pos < 2400 ? n (i) * std::exp (-(float) pos / 600.0f) : 0.0f;
    };
}
} // namespace

int main (int argc, char** argv)
{
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<HazeAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<HazeAudioProcessor, HazeAudioProcessorEditor> (png, [] (Harness& h)
            {
                h.set ("Mix", "30"); h.set ("Pre-delay", "24"); h.set ("Size", "62"); h.set ("Decay", "2.4");
                h.set ("Damping", "5.5 kHz"); h.set ("Low Cut", "180"); h.set ("Width", "120"); h.set ("Modulation", "35");
            }, bursts());
        });
}
