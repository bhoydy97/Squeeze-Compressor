// Ceiling tests: the ceiling holds (sample and true peak), transparency, alignment, release timing, distortion,
// and loudness metering against ITU-R BS.1770 / EBU Tech 3341.
//   CeilingTests <Ceiling.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void defaults (Harness& h)
{
    h.set ("Gain", "0"); h.set ("Ceiling", "-1"); h.set ("Release", "80"); h.setBool ("True Peak", true);
}

// Independent true-peak reference: 16x, 128-tap Kaiser-windowed sinc per phase
float referenceTruePeakDb (const std::vector<float>& x, size_t from)
{
    constexpr int half = 64, os = 16;
    auto i0 = [] (double v) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (v / (2 * k)) * (v / (2 * k)); s += t; } return s; };
    static std::vector<std::vector<double>> taps;
    if (taps.empty())
    {
        taps.assign (os, std::vector<double> (2 * half));
        for (int k = 0; k < os; ++k)
            for (int t = 0; t < 2 * half; ++t)
            {
                const double d = (t - half + 1) - (double) k / os;
                const double sinc = std::abs (d) < 1.0e-12 ? 1.0 : std::sin (lbaudio::dsp::pi * d) / (lbaudio::dsp::pi * d);
                const double r = d / (half + 0.5);
                taps[(size_t) k][(size_t) t] = sinc * i0 (10.0 * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (10.0);
            }
    }
    double peak = 0.0;
    for (size_t c = std::max (from, (size_t) half); c + half < x.size(); ++c)
        for (int k = 0; k < os; ++k)
        {
            double v = 0.0;
            for (int t = 0; t < 2 * half; ++t) v += x[c - half + 1 + (size_t) t] * taps[(size_t) k][(size_t) t];
            peak = std::max (peak, std::abs (v));
        }
    return toDb ((float) peak);
}

// BS.1770 4x true-peak meter reading (the standard meter), after letting it settle
float ituTruePeakDb (const std::array<std::vector<float>, 2>& out, size_t from)
{
    ceiling::TruePeakMeter meter;
    meter.process (out[0].data() + from - 64, out[1].data() + from - 64, 64);
    meter.resetPeak();
    meter.process (out[0].data() + from, out[1].data() + from, (int) (out[0].size() - from));
    return toDb (meter.peak());
}

float samplePeakDb (const std::array<std::vector<float>, 2>& out, size_t from)
{
    return std::max (peakDb (out[0], from), peakDb (out[1], from));
}

// White noise through an 8th-order 20 kHz low-pass: like program material, which stops around 20 kHz
Gen bandLimitedNoise (float db, unsigned seed)
{
    auto n = noise (db, seed);
    auto filters = std::make_shared<std::array<lbaudio::dsp::Biquad, 4>>();
    const auto qs = lbaudio::dsp::butterworthQs (8);
    for (size_t i = 0; i < 4; ++i) (*filters)[i].c = lbaudio::dsp::design (lbaudio::dsp::Shape::LowPass, 20000.0, 0, qs[i], sampleRate);
    auto last = std::make_shared<int> (-1);
    auto value = std::make_shared<float> (0.0f);
    return [n, filters, last, value] (int i)
    {
        if (i < *last) { *last = -1; for (auto& f : *filters) f.reset(); } // a new run starts from 0
        while (*last < i) { ++*last; float v = n (*last); for (auto& f : *filters) v = f.process (v); *value = v; }
        return *value;
    };
}

void loudnessChecks()
{
    std::cout << "\n== Loudness meter (ITU-R BS.1770-4, EBU Tech 3341)" << std::endl;
    {
        const auto shelf = lbaudio::kWeightingShelf (48000.0), hp = lbaudio::kWeightingHighPass (48000.0);
        // The 48 kHz coefficients published in BS.1770-4
        const double err = std::max ({ std::abs (shelf.b0 - 1.53512485958697), std::abs (shelf.b1 + 2.69169618940638),
                                       std::abs (shelf.b2 - 1.19839281085285), std::abs (shelf.a1 + 1.69065929318241),
                                       std::abs (shelf.a2 - 0.73248077421585), std::abs (hp.a1 + 1.99004745483398),
                                       std::abs (hp.a2 - 0.99007225036621) });
        check (err < 1.0e-6, "K-weighting matches the BS.1770 coefficients at 48 kHz", "max difference " + juce::String (err, 10));
    }

    // Stereo 1 kHz tones, a list of (dBFS, seconds) sections, fed in 512-sample blocks
    auto measure = [] (std::initializer_list<std::pair<float, double>> sections, double rate = 48000.0)
    {
        lbaudio::LoudnessMeter meter;
        meter.prepare (rate);
        std::vector<float> l (512), r (512);
        long long n = 0;
        for (const auto& [db, seconds] : sections)
        {
            const float a = fromDb (db);
            for (long long left = (long long) std::llround (seconds * rate); left > 0;)
            {
                const int len = (int) std::min<long long> (512, left);
                for (int i = 0; i < len; ++i) l[(size_t) i] = r[(size_t) i] = a * (float) std::sin (2.0 * lbaudio::dsp::pi * 1000.0 * (double) (n + i) / rate);
                meter.process (l.data(), r.data(), len);
                n += len; left -= len;
            }
        }
        return std::array<double, 3> { meter.momentaryLufs(), meter.shortTermLufs(), meter.integratedLufs() };
    };

    {
        const auto m = measure ({ { -23.0f, 20.0 } });
        check (std::abs (m[0] + 23.0) < 0.1 && std::abs (m[1] + 23.0) < 0.1 && std::abs (m[2] + 23.0) < 0.1,
               "EBU 3341 case 1: -23 dBFS stereo 1 kHz reads -23.0 LUFS (M, S, I)",
               fmt ((float) m[0]) + ", " + fmt ((float) m[1]) + ", " + fmt ((float) m[2]));
    }
    {
        const auto m = measure ({ { -33.0f, 20.0 } });
        check (std::abs (m[2] + 33.0) < 0.1, "EBU 3341 case 2: -33 dBFS reads -33.0 LUFS integrated", fmt ((float) m[2]));
    }
    {
        const auto m = measure ({ { -36.0f, 10.0 }, { -23.0f, 60.0 }, { -36.0f, 10.0 } });
        check (std::abs (m[2] + 23.0) < 0.1, "EBU 3341 case 3: relative gate (-36/-23/-36) gives -23.0 LUFS", fmt ((float) m[2]));
    }
    {
        const auto m = measure ({ { -72.0f, 10.0 }, { -36.0f, 10.0 }, { -23.0f, 60.0 }, { -36.0f, 10.0 }, { -72.0f, 10.0 } });
        check (std::abs (m[2] + 23.0) < 0.1, "EBU 3341 case 4: absolute + relative gates give -23.0 LUFS", fmt ((float) m[2]));
    }
    {
        const auto m = measure ({ { -26.0f, 20.0 }, { -20.0f, 20.1 }, { -26.0f, 20.0 } });
        check (std::abs (m[2] + 23.0) < 0.1, "EBU 3341 case 5: -26/-20/-26 gives -23.0 LUFS", fmt ((float) m[2]));
    }
    {
        const auto m = measure ({ { -23.0f, 20.0 } }, 44100.0);
        check (std::abs (m[2] + 23.0) < 0.1, "...and at 44.1 kHz", fmt ((float) m[2]));
    }
}

void runSuite (juce::AudioProcessor& proc)
{
    Harness h (proc);
    const int latency = ceiling::latencySamples (sampleRate);
    runCommonChecks (proc, latency);
    const int second = (int) sampleRate;
    auto* local = dynamic_cast<CeilingAudioProcessor*> (&proc);

    // --- Below the ceiling: untouched, just delayed -----------------------------------
    defaults (h); h.apply();
    {
        auto in = noise (-20.0f, 3);
        const auto out = h.run (second, in);
        float maxDiff = 0.0f;
        for (size_t i = (size_t) latency; i < out.size(); ++i) maxDiff = std::max (maxDiff, std::abs (out[i] - in ((int) i - latency)));
        check (! (maxDiff > 0.0f), "Below the ceiling: bit-exact (delayed by the lookahead)", "max diff " + juce::String (maxDiff));
    }
    h.apply();
    {
        const auto out = h.run (4000, impulse (-30.0f, 1000));
        size_t at = 0;
        for (size_t i = 0; i < out.size(); ++i) if (std::abs (out[i]) > std::abs (out[at])) at = i;
        check ((int) at == 1000 + latency, "Impulse comes out exactly 'latency' samples later", "at " + juce::String ((int) at) + ", expected " + juce::String (1000 + latency));
    }
    defaults (h); h.set ("Gain", "6"); h.apply();
    {
        const auto out = h.run (second, sine (-30.0f, 1000.0f));
        const float g = rmsDb (out, (size_t) second / 2) - (-30.0f - 3.0103f);
        check (std::abs (g - 6.0f) < 0.01f, "Gain +6 dB (no limiting needed)", fmt (g, 3) + " dB");
    }

    // --- The ceiling holds: sample peaks ------------------------------------------------
    struct Case { const char* name; Gen gen; const char* gain; };
    const std::vector<Case> cases {
        { "noise +18 dB",           noise (-12.0f, 5),             "18" },
        { "band-limited noise +24", bandLimitedNoise (-18.0f, 6), "24" },
        { "1 kHz sine +12",         sine (-6.0f, 1000.0f),         "12" },
        { "50 Hz sine +18",         sine (-12.0f, 50.0f),          "18" },
        { "clicks",                 [] (int i) { return i % 4800 == 0 ? 4.0f : 0.0f; }, "0" },
    };
    for (bool tp : { false, true })
        for (const auto& c : cases)
        {
            defaults (h); h.set ("Gain", c.gain); h.setBool ("True Peak", tp); h.apply();
            const auto out = h.runStereo (2 * second, c.gen, c.gen);
            const float sp = samplePeakDb (out, (size_t) latency);
            check (sp <= -1.0f + 1.0e-4f, juce::String ("Ceiling -1 dB holds (sample peak), true peak ") + (tp ? "on: " : "off: ") + c.name,
                   "peak " + fmt (sp, 4) + " dBFS");
        }

    // --- True peak: inter-sample peaks -------------------------------------------------------
    {
        // A tone at a quarter of the sample rate, sampled at 45 degrees: every sample is 3 dB under the real peak
        const Gen quarter = [] (int i) { return 0.5f * (float) std::sin (lbaudio::dsp::pi * 0.5 * i + lbaudio::dsp::pi * 0.25); };
        defaults (h); h.set ("Gain", "12"); h.setBool ("True Peak", false); h.apply();
        auto out = h.runStereo (second, quarter, quarter);
        const float off = ituTruePeakDb (out, (size_t) second / 2);
        check (off > 1.5f, "True peak off: sample peaks held, inter-sample peaks get through (as expected)", "true peak " + fmt (off, 2) + " dBTP");
        defaults (h); h.set ("Gain", "12"); h.apply();
        out = h.runStereo (second, quarter, quarter);
        const float on = ituTruePeakDb (out, (size_t) second / 2);
        const float ref = referenceTruePeakDb (out[0], (size_t) second / 2);
        check (on <= -1.0f + 0.01f && ref <= -1.0f + 0.05f, "True peak on: inter-sample peaks held under the ceiling",
               "BS.1770 meter " + fmt (on, 3) + " dBTP, 16x reference " + fmt (ref, 3) + " dBFS");
    }
    {
        defaults (h); h.set ("Gain", "24"); h.apply();
        const auto n = bandLimitedNoise (-18.0f, 8);
        const auto out = h.runStereo (2 * second, n, n);
        const float on = ituTruePeakDb (out, (size_t) second);
        const float ref = referenceTruePeakDb (out[0], (size_t) (3 * second / 2));
        check (on <= -1.0f + 0.01f && ref <= -1.0f + 0.1f, "True peak on, noise driven 24 dB into the limiter: held under the ceiling",
               "BS.1770 meter " + fmt (on, 3) + " dBTP, 16x reference " + fmt (ref, 3) + " dBFS");
    }

    // --- Limiting a steady tone is clean ---------------------------------------------------------
    defaults (h); h.set ("Gain", "12"); h.apply();
    {
        const auto out = h.run (2 * second, sine (-6.0f, 1000.0f));
        const size_t a = (size_t) second, b = out.size();
        const float f = toneDb (out, a, b, 1000.0), h2 = toneDb (out, a, b, 2000.0), h3 = toneDb (out, a, b, 3000.0);
        check (std::abs (peakDb (out, a) + 1.0f) < 0.05f && std::max (h2, h3) - f < -80.0f,
               "1 kHz tone 12 dB into the limiter: sits at the ceiling, no distortion",
               "peak " + fmt (peakDb (out, a), 3) + " dBFS, H2 " + fmt (h2 - f, 1) + " dB, H3 " + fmt (h3 - f, 1) + " dB re fundamental");
    }

    // --- Release: the level comes back with the set time constant --------------------------------
    for (float releaseMs : { 20.0f, 80.0f, 300.0f })
    {
        defaults (h); h.set ("Release", juce::String (releaseMs)); h.apply();
        // 12 dB of limiting, then a quiet tone: watch the quiet tone's level recover
        const int switchAt = second / 2;
        const float quiet = fromDb (-40.0f);
        const Gen gen = [switchAt, quiet] (int i)
        {
            const float a = i < switchAt ? fromDb (11.0f) : quiet;
            return a * (float) std::sin (2.0 * lbaudio::dsp::pi * 1000.0 * i / sampleRate);
        };
        const auto out = h.run (switchAt + 2 * second, gen);
        // Gain from the quiet tone's envelope (peak over each 1 ms period)
        const double target = 1.0 - (1.0 - fromDb (-12.0f)) * std::exp (-1.0); // one time constant
        double crossed = -1.0;
        for (int i = switchAt + latency + 48; i + 48 < (int) out.size(); i += 4)
        {
            float env = 0.0f;
            for (int k = 0; k < 48; ++k) env = std::max (env, std::abs (out[(size_t) (i - 48 + k)]));
            if (env / quiet >= target) { crossed = (double) (i - switchAt - latency) / sampleRate * 1000.0; break; }
        }
        // The lookahead's averaging centres the gain change half a lookahead (1 ms) later
        const double expected = releaseMs + 0.5 * ceiling::lookaheadMs;
        check (crossed > 0.0 && std::abs (crossed / expected - 1.0) < 0.1, "Release " + juce::String ((int) releaseMs) + " ms: recovery time constant",
               fmt ((float) crossed, 1) + " ms (expected about " + fmt ((float) expected, 1) + " ms)");
    }

    // --- Loudness and true-peak readouts in the plugin ----------------------------------------
    if (local != nullptr)
    {
        defaults (h); h.set ("Ceiling", "0"); h.apply();
        h.runStereo (5 * second, sine (-20.0f, 1000.0f), sine (-20.0f, 1000.0f));
        const float m = local->momentaryLufs.load(), s = local->shortTermLufs.load(), i = local->integratedLufs.load();
        check (std::abs (m + 20.0f) < 0.1f && std::abs (s + 20.0f) < 0.1f && std::abs (i + 20.0f) < 0.1f,
               "Plugin readouts: stereo -20 dBFS 1 kHz reads -20.0 LUFS (M, S, I)", fmt (m) + ", " + fmt (s) + ", " + fmt (i));
        const float tp = toDb (local->truePeakMax.load());
        check (std::abs (tp + 20.0f) < 0.1f, "Plugin readout: true peak of a -20 dBFS tone", fmt (tp) + " dBTP");
        h.runStereo (blockSize, silence(), silence()); // let the meter's filter window empty
        local->resetMeasurementsRequested.store (true);
        h.runStereo (blockSize, silence(), silence());
        check (local->truePeakMax.load() < 1.0e-6f && local->integratedLufs.load() <= -99.0f, "Reset clears integrated loudness and true peak", "");
        check (local->clampedSamples.load() == 0, "The safety clamp was never needed: the gain did all the work",
               juce::String (local->clampedSamples.load()) + " samples clamped");
    }

    // --- Settings are saved and restored ------------------------------------------------------
    {
        defaults (h); h.set ("Gain", "7.5"); h.set ("Ceiling", "-0.3"); h.set ("Release", "250"); h.setBool ("True Peak", false); h.apply();
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        defaults (h); h.apply();
        proc.setStateInformation (state.getData(), (int) state.getSize());
        const auto got = h.text ("Gain") + " / " + h.text ("Ceiling") + " / " + h.text ("Release") + " / " + h.text ("True Peak");
        check (got == "7.5 dB / -0.3 dB / 250 ms / Off", "Settings saved and restored", got);
    }

    // --- Moving every control while audio runs ------------------------------------------------
    {
        defaults (h); h.apply();
        juce::Random rng (7);
        bool finite = true;
        float peak = -200.0f;
        const auto n = noise (0.0f, 11);
        for (int block = 0; block < 400; ++block)
        {
            for (auto* p : proc.getParameters())
                if (p != proc.getBypassParameter() && rng.nextFloat() < 0.2f) p->setValueNotifyingHost (rng.nextFloat());
            const auto out = h.runStereo (blockSize, n, n);
            for (auto& ch : out) for (auto v : ch) { finite = finite && std::isfinite (v); peak = std::max (peak, toDb (std::abs (v))); }
        }
        check (finite && peak <= 0.0f, "Random parameter changes on every block: finite, never over 0 dBFS", "peak " + fmt (peak, 2) + " dBFS");
    }
}
} // namespace

int main (int argc, char** argv)
{
    loudnessChecks();
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<CeilingAudioProcessor>(); },
        [] (const juce::File& png)
        {
            // Something drum-like: kick on the beat, a noisy snare on the off-beat, a quieter bed underneath
            const auto bed = bandLimitedNoise (-30.0f, 4);
            const auto snareNoise = noise (-8.0f, 9);
            const Gen music = [bed, snareNoise] (int i)
            {
                const int pos = i % 24000, snarePos = (i + 12000) % 24000;
                const float t = (float) pos / 48000.0f;
                const float kick = pos < 9000 ? 0.9f * std::exp (-(float) pos / 2400.0f) * std::sin (2.0f * 3.14159265f * (50.0f * t + 90.0f * (1.0f - std::exp (-t * 40.0f)) / 40.0f)) : 0.0f;
                const float snare = snarePos < 6000 ? snareNoise (i) * std::exp (-(float) snarePos / 1800.0f) : 0.0f;
                const float swell = 1.0f + 0.5f * std::sin ((float) i * 0.00011f);
                return kick + snare + bed (i) * swell;
            };
            renderSnapshot<CeilingAudioProcessor, CeilingAudioProcessorEditor> (png, [] (Harness& h)
            {
                defaults (h); h.set ("Gain", "6"); h.set ("Ceiling", "-1"); h.set ("Release", "120");
            }, music, 220);
        });
}
