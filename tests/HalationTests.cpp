// Halation tests: the crossover sums flat, Mix stays flat, each band is its own, the compressors' maths, the
// exciters' harmonics (made from their band only, and not aliased), their tones and mix, solo, bypass, presets and state.
//   HalationTests <Halation.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
int expectedLatency()
{
    juce::dsp::Oversampling<float> os (2, (size_t) HalationAudioProcessor::oversamplingStages,
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

//==============================================================================
// Checks that reproduce an outside lab measurement (THE_LAB 1.3.2, Halation 1.2.2) with the settings it used:
// every knob moves smoothly and does something, high notes don't alias, one channel doesn't leak into the other,
// big buffers and high sample rates render the same, and the plugin's bypass keeps its latency.

void labSettings (Harness& h)
{
    defaults (h.proc);
    const std::pair<const char*, const char*> values[] = {
        { "Low Split", "120" }, { "Mid Split", "1000" }, { "High Split", "6000" },
        { "Low Threshold", "-27.1" }, { "Low Ratio", "2.0" }, { "Low Attack", "10" }, { "Low Release", "120" },
        { "Low Makeup", "0" }, { "Low Excite", "53" }, { "Low Excite Tone", "Octave" }, { "Low Excite Mix", "100" },
        { "Low Mid Threshold", "-31.9" }, { "Low Mid Ratio", "5.6" }, { "Low Mid Attack", "0.32" }, { "Low Mid Release", "9.1" },
        { "Low Mid Makeup", "14.5" }, { "Low Mid Excite", "56" }, { "Low Mid Excite Tone", "Tape" }, { "Low Mid Excite Mix", "41" },
        { "High Mid Threshold", "-54.9" }, { "High Mid Ratio", "2.0" }, { "High Mid Attack", "0.10" }, { "High Mid Release", "5.0" },
        { "High Mid Makeup", "8.8" }, { "High Mid Excite", "54" }, { "High Mid Excite Tone", "Bright" }, { "High Mid Excite Mix", "28" },
        { "High Threshold", "-27.6" }, { "High Ratio", "2.5" }, { "High Attack", "0.51" }, { "High Release", "588" },
        { "High Makeup", "8.6" }, { "High Excite", "55" }, { "High Excite Tone", "Warm" }, { "High Excite Mix", "100" },
        { "Input", "-8.4" }, { "Output", "8.4" }, { "Mix", "62" } };
    for (const auto& [name, value] : values) h.set (name, value);
    h.apply();
}

// Plays 'signal' for 'total' samples, setting the parameter to each value at its sample position.
std::vector<float> playWithMoves (Harness& h, Gen signal, int total, juce::AudioProcessorParameter* p,
                                  const std::vector<std::pair<int, float>>& moves)
{
    std::vector<float> out;
    int pos = 0;
    size_t next = 0;
    while (pos < total)
    {
        const int until = next < moves.size() ? juce::jmin (total, moves[next].first) : total;
        if (until > pos)
        {
            const auto part = h.run (until - pos, [signal, pos] (int i) { return signal (pos + i); });
            out.insert (out.end(), part.begin(), part.end());
            pos = until;
        }
        if (next < moves.size() && pos >= moves[next].first) p->setValueNotifyingHost (moves[next++].second);
    }
    return out;
}

// Spectrum of the 65536 samples after 'from', Blackman-Harris windowed: magnitudes per bin (bin = rate / 65536 Hz).
std::vector<float> spectrum (const std::vector<float>& v, size_t from)
{
    constexpr int order = 16, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * (size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * juce::MathConstants<double>::pi * i / (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        data[(size_t) i] = (float) (w * v[from + (size_t) i]);
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    data.resize ((size_t) n / 2);
    return data;
}

// The loudest component that isn't the tone or one of its harmonics below Nyquist (dB re the tone, and where).
std::pair<float, double> worstSpur (const std::vector<float>& mag, int toneBin, double rate)
{
    const int n = (int) mag.size() * 2;
    auto isHarmonic = [&] (int bin)
    {
        if (std::abs (bin - toneBin) <= toneBin / 20 + 4) return true; // the tone and its own sidebands (gain riding)
        for (int k = 2; (long long) k * toneBin < n / 2; ++k)
            if (std::abs (bin - k * toneBin) <= 4) return true;
        return false;
    };
    float tone = 0.0f;
    for (int b = toneBin - 3; b <= toneBin + 3; ++b) tone = std::max (tone, mag[(size_t) b]);
    float worst = 0.0f; int at = 0;
    for (int b = 10; b < (int) mag.size(); ++b)
        if (! isHarmonic (b) && mag[(size_t) b] > worst) { worst = mag[(size_t) b]; at = b; }
    return { toDb (worst / tone), at * rate / n };
}

// Renders 'seconds' of a signal at a sample rate and buffer size of its own (prepared for it), left channel out.
std::vector<float> renderAt (juce::AudioProcessor& proc, double rate, int block, std::function<float (double)> signalAtTime, double seconds)
{
    proc.releaseResources();
    proc.setRateAndBufferSizeDetails (rate, block);
    proc.prepareToPlay (rate, block);
    const int total = (int) (rate * seconds);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int start = 0; start < total; start += block)
    {
        const int len = juce::jmin (block, total - start);
        buffer.setSize (2, len, false, false, true);
        for (int i = 0; i < len; ++i)
            for (int ch = 0; ch < 2; ++ch) buffer.setSample (ch, i, signalAtTime ((start + i) / rate));
        proc.processBlock (buffer, midi);
        for (int i = 0; i < len; ++i) out.push_back (buffer.getSample (0, i));
    }
    return out;
}

void labFindings (juce::AudioProcessor& proc)
{
    Harness h (proc);
    const auto tonePeak = fromDb (-12.0f);
    const Gen tone = sine (-12.0f, 999.76f);
    const Gen threeTones = mixOf (mixOf (sine (-18.0f, 100.0f), sine (-18.0f, 999.76f)), sine (-18.0f, 8000.0f));

    // --- Knob moves: every continuous control, jumped 30 % four times while a tone plays -------------------------
    {
        // Clicks: what the move adds above 5 kHz in the 30 ms after each jump, over what the new setting leaves
        // there for good (measured later), re the tone's peak
        lbaudio::dsp::Biquad hp[4];
        juce::StringArray clicky, inert;
        float worstClick = -200.0f;
        juce::String worstName;
        const int total = (int) (sampleRate * 2.0);
        const int jumpAt[4] = { 24064, 38400, 52736, 67072 }; // on block boundaries, as a host moves them
        for (auto* p : proc.getParameters())
        {
            if (p->isDiscrete() || p->isBoolean() || ! p->isAutomatable()) continue;
            const auto name = p->getName (64);
            if (name.isEmpty() || name.containsIgnoreCase ("bypass")) continue;

            labSettings (h);
            const float v0 = p->getValue(), v1 = v0 <= 0.7f ? v0 + 0.3f : v0 - 0.3f;
            const std::vector<std::pair<int, float>> moves { { jumpAt[0], v1 }, { jumpAt[1], v0 }, { jumpAt[2], v1 }, { jumpAt[3], v0 } };
            float clickDb = -200.0f, changeDb = -200.0f;
            for (int pass = 0; pass < 2; ++pass)
            {
                const Gen signal = pass == 0 ? tone : threeTones;
                labSettings (h);
                const auto still = playWithMoves (h, signal, total, p, {});
                labSettings (h);
                const auto moved = playWithMoves (h, signal, total, p, moves);
                std::vector<float> diff (still.size());
                double sq = 0.0, ref = 0.0;
                for (size_t i = 0; i < diff.size(); ++i)
                {
                    diff[i] = moved[i] - still[i];
                    if (i >= (size_t) jumpAt[0]) { sq += (double) diff[i] * diff[i]; ref += (double) still[i] * still[i]; }
                }
                changeDb = std::max (changeDb, (float) (10.0 * std::log10 ((sq + 1.0e-30) / (ref + 1.0e-30))));
                if (pass == 0)
                {
                    const auto qs = lbaudio::dsp::butterworthQs (8);
                    for (int s = 0; s < 4; ++s) { hp[s].reset(); hp[s].c = lbaudio::dsp::design (lbaudio::dsp::Shape::HighPass, 5000.0, 0.0, qs[(size_t) s], sampleRate); }
                    for (auto& v : diff) for (auto& f : hp) v = f.process (v);
                    for (int j = 0; j < 4; ++j)
                    {
                        const size_t a = (size_t) jumpAt[j], w = (size_t) (0.03 * sampleRate);
                        float burst = 0.0f, after = 0.0f;
                        for (size_t i = a; i < a + w; ++i) burst = std::max (burst, std::abs (diff[i]));
                        const size_t end = j < 3 ? (size_t) jumpAt[j + 1] : diff.size();
                        for (size_t i = a + (size_t) (0.1 * sampleRate); i < end; ++i) after = std::max (after, std::abs (diff[i]));
                        clickDb = std::max (clickDb, toDb (std::max (0.0f, burst - after) / tonePeak));
                    }
                }
            }
            if (clickDb > -35.0f) clicky.add (name + " " + fmt (clickDb, 1) + " dB");
            if (changeDb < -120.0f) inert.add (name);
            if (clickDb > worstClick) { worstClick = clickDb; worstName = name; }
        }
        check (clicky.isEmpty(), "Lab settings: no control clicks when it jumps 30 % (each under -35 dB)",
               clicky.isEmpty() ? "loudest " + worstName + " " + fmt (worstClick, 1) + " dB" : clicky.joinIntoString (", "));
        check (inert.isEmpty(), "Lab settings: every control changes the sound of a tone or three tones",
               inert.isEmpty() ? juce::String ("all do") : "no change from: " + inert.joinIntoString (", "));
    }

    // --- High notes: harmonics above Nyquist mustn't fold back ---------------------------------------------------
    {
        juce::String detail;
        float worst = -200.0f;
        for (int bin : { 1365, 8601, 13653 }) // 1.0, 6.3 and 10 kHz on odd bins, as the lab does
        {
            labSettings (h);
            const float hz = (float) (bin * sampleRate / 65536.0);
            const auto out = h.run ((int) sampleRate + 65536, sine (-6.0f, hz));
            const auto [spur, at] = worstSpur (spectrum (out, (size_t) sampleRate), bin, sampleRate);
            detail << fmt (hz / 1000.0f, 1) << " kHz: " << fmt (spur, 1) << " dB at " << fmt ((float) at / 1000.0f, 1) << " kHz; ";
            worst = std::max (worst, spur);
        }
        check (worst < -90.0f, "Lab settings: no aliasing from high notes (worst spur under -90 dB)", detail);
    }

    // --- Stereo: one channel doesn't leak into the other ---------------------------------------------------------
    {
        labSettings (h);
        const auto leftOnly = h.runStereo ((int) sampleRate * 2, noise (-12.0f, 3), silence());
        const float leak = rmsDb (leftOnly[1], (size_t) sampleRate) - rmsDb (leftOnly[0], (size_t) sampleRate);
        labSettings (h);
        const auto both = h.runStereo ((int) sampleRate, noise (-12.0f, 3), noise (-12.0f, 3));
        float lr = 0.0f;
        for (size_t i = 0; i < both[0].size(); ++i) lr = std::max (lr, std::abs (both[0][i] - both[1][i]));
        check (leak < -120.0f && lr == 0.0f, "Lab settings: left-only in gives nothing on the right, and L and R match exactly",
               "right " + fmt (leak, 1) + " dB re left; L-R " + juce::String (lr));
    }

    // --- Big buffers and high sample rates ----------------------------------------------------------------------
    {
        labSettings (h);
        auto music = [] (double t)
        {
            const double pi2 = 2.0 * juce::MathConstants<double>::pi;
            return (float) (0.25 * std::sin (pi2 * 110.0 * t) + 0.15 * std::sin (pi2 * 1234.5 * t) + 0.1 * std::sin (pi2 * 7000.0 * t))
                   * (float) (0.6 + 0.4 * std::sin (pi2 * 1.3 * t));
        };
        const auto ref = renderAt (proc, sampleRate, 512, music, 2.0);
        juce::String detail;
        bool ok = true;
        for (int block : { 2048, 4096, 8192 })
        {
            const auto out = renderAt (proc, sampleRate, block, music, 2.0);
            std::vector<float> d (ref.size());
            bool finite = true;
            for (size_t i = 0; i < d.size(); ++i) { d[i] = out[i] - ref[i]; finite = finite && std::isfinite (out[i]); }
            const float diff = rmsDb (d) - rmsDb (ref);
            ok = ok && finite && diff < -120.0f;
            detail << block << ": " << fmt (diff, 1) << " dB; ";
        }
        {
            // A 0-sample buffer between ordinary ones
            proc.releaseResources();
            proc.setRateAndBufferSizeDetails (sampleRate, 512);
            proc.prepareToPlay (sampleRate, 512);
            juce::AudioBuffer<float> empty (2, 0);
            juce::MidiBuffer midi;
            proc.processBlock (empty, midi);
        }
        const float refLevel = rmsDb (ref);
        for (double rate : { 176400.0, 192000.0 })
        {
            const auto out = renderAt (proc, rate, 512, music, 1.0);
            bool finite = true;
            for (auto v : out) finite = finite && std::isfinite (v);
            const float level = rmsDb (out, (size_t) (rate * 0.25));
            ok = ok && finite && std::abs (level - refLevel) < 6.0f;
            detail << (int) (rate / 1000.0) << " kHz level " << fmt (level - refLevel, 2) << " dB; ";
        }
        check (ok, "Lab settings: 2048-8192 sample buffers render the same as 512, and 176.4/192 kHz stay level", detail);
        h.prepare();
    }

    // --- The plugin's own bypass keeps the latency, so a bypassed track stays in time ----------------------------
    {
        labSettings (h);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        int arrival = -1;
        for (int b = 0; b < 4 && arrival < 0; ++b)
        {
            buffer.clear();
            if (b == 0) { buffer.setSample (0, 10, 1.0f); buffer.setSample (1, 10, 1.0f); }
            proc.processBlockBypassed (buffer, midi);
            for (int i = 0; i < blockSize && arrival < 0; ++i)
                if (std::abs (buffer.getSample (0, i)) > 0.5f) arrival = b * blockSize + i - 10;
        }
        check (arrival == proc.getLatencySamples(), "Bypassed: the input arrives after the reported latency, as when processing",
               juce::String (arrival) + " samples, latency " + juce::String (proc.getLatencySamples()));
    }
    labSettings (h);
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
    defaults (proc); h.set ("High Mid Excite", "50"); h.apply();
    {
        // The exciter follows the band's level, so a quiet band gets the same colour as a loud one
        auto ratios = [&] (float inDb)
        {
            h.apply(); // each level from a clean start: the exciter takes half a second to come down after a loud tone
            const auto out = h.run ((int) sampleRate, sine (inDb, 2000.0f));
            const float f = toneDb (out, settle, out.size(), 2000.0);
            return std::make_pair (toneDb (out, settle, out.size(), 4000.0) - f, toneDb (out, settle, out.size(), 6000.0) - f);
        };
        const auto loud = ratios (-6.0f), quiet = ratios (-40.0f);
        const float diff = std::max (std::abs (loud.first - quiet.first), std::abs (loud.second - quiet.second));
        check (diff < 1.0f && loud.second > -30.0f && loud.second < -18.0f, "High Mid Excite 50 %: the same harmonics at -6 dB and -40 dB",
               "3rd " + fmt (loud.second, 1) + " / " + fmt (quiet.second, 1) + " dB, 2nd " + fmt (loud.first, 1) + " / " + fmt (quiet.first, 1) + " dB under the tone");
        h.apply();
        const auto out = h.run ((int) sampleRate, sine (-6.0f, 2000.0f));
        const float level = toneDb (out, settle, out.size(), 2000.0) + 6.0f;
        check (std::abs (level) < 0.3f, "...and the tone itself stays at its level", fmt (level) + " dB");
    }
    // --- Excite tones, amount and mix ---------------------------------------------------------------------------
    {
        // Harmonics 2 to 6 of a 1.3 kHz tone in the High Mid band, in dB under the tone, and the tone's own level
        struct Spectrum { float tone; std::array<float, 7> h; float total; };
        auto measure = [&] (const char* tone, const char* amount, const char* mixPct)
        {
            defaults (proc); h.set ("High Mid Excite", amount); h.set ("High Mid Excite Tone", tone); h.set ("High Mid Excite Mix", mixPct); h.apply();
            const auto out = h.run ((int) sampleRate, sine (-12.0f, 1300.0f));
            Spectrum sp {};
            sp.tone = toneDb (out, settle, out.size(), 1300.0) + 12.0f;
            double power = 0.0;
            for (int k = 2; k <= 6; ++k)
            {
                sp.h[(size_t) k] = toneDb (out, settle, out.size(), 1300.0 * k) - (sp.tone - 12.0f);
                power += std::pow (10.0, sp.h[(size_t) k] / 10.0);
            }
            sp.total = (float) (10.0 * std::log10 (power + 1.0e-30));
            return sp;
        };
        const auto warm = measure ("Warm", "100", "100"), tape = measure ("Tape", "100", "100"), bright = measure ("Bright", "100", "100"),
                   crunch = measure ("Crunch", "100", "100"), octave = measure ("Octave", "100", "100");
        bool levels = true; juce::String detail;
        for (const auto* sp : { &warm, &tape, &bright, &crunch, &octave })
        {
            levels = levels && std::abs (sp->tone) < 0.3f && std::abs (sp->total - warm.total) < 2.5f;
            detail << fmt (sp->total, 1) << " ";
        }
        check (levels, "Excite tones: each keeps the tone's level and adds harmonics about as loud as Warm's", "harmonics " + detail + "dB under the tone");
        check (tape.h[2] < -90.0f && tape.h[4] < -90.0f && tape.h[3] - tape.h[5] > 12.0f,
               "Tape: odd harmonics only, mostly the 3rd", "2nd " + fmt (tape.h[2], 0) + ", 3rd " + fmt (tape.h[3], 1) + ", 5th " + fmt (tape.h[5], 1) + " dB");
        check (bright.h[2] < -90.0f && bright.h[5] - warm.h[5] > 5.0f && bright.h[3] - bright.h[5] < 8.0f,
               "Bright: no 2nd, and the upper harmonics well above Warm's", "5th " + fmt (bright.h[5], 1) + " against Warm's " + fmt (warm.h[5], 1) + " dB");
        check (crunch.h[2] > -40.0f && crunch.h[3] > -25.0f && crunch.h[5] - warm.h[5] > 4.0f,
               "Crunch: even and odd, denser than Warm", "2nd " + fmt (crunch.h[2], 1) + ", 3rd " + fmt (crunch.h[3], 1) + ", 5th " + fmt (crunch.h[5], 1) + " dB");
        check (octave.h[2] > -25.0f && octave.h[3] < -90.0f && octave.h[5] < -90.0f,
               "Octave: the octave above, no odd harmonics", "2nd " + fmt (octave.h[2], 1) + ", 3rd " + fmt (octave.h[3], 0) + " dB");

        const auto halfMix = measure ("Warm", "100", "50"), none = measure ("Warm", "100", "0");
        check (std::abs (halfMix.total - warm.total + 6.02f) < 0.5f && none.total < -90.0f && std::abs (none.tone) < 0.3f,
               "Excite Mix: 50 % is 6 dB less, 0 % none, and the tone stays put", fmt (halfMix.total - warm.total) + " dB at 50 %, " + fmt (none.total, 0) + " dB at 0 %");

        const auto gentle = measure ("Warm", "25", "100");
        const float densityLow = gentle.h[3] - gentle.h[2], densityHigh = warm.h[3] - warm.h[2];
        check (densityHigh - densityLow > 8.0f && gentle.total < warm.total - 8.0f,
               "Excite Amount: quieter and gentler turned down, denser turned up",
               "3rd against 2nd " + fmt (densityLow, 1) + " dB at 25 %, " + fmt (densityHigh, 1) + " dB at 100 %");
    }
    {
        // Changing tone while a note plays fades the harmonics out and back in: no step in the waveform
        defaults (proc); h.set ("Low Mid Excite", "100"); h.set ("Low Mid Excite Tone", "Warm"); h.apply();
        auto gen = sine (-12.0f, 150.0f);
        int t0 = 0;
        auto shifted = [&] (int i) { return gen (t0 + i); };
        auto maxStep = [] (const std::vector<float>& v, size_t from, size_t to)
        {
            float m = 0.0f;
            for (size_t i = juce::jmax ((size_t) 1, from); i < juce::jmin (to, v.size()); ++i) m = juce::jmax (m, std::abs (v[i] - v[i - 1]));
            return m;
        };
        const int part = (int) sampleRate / 2;
        const auto before = h.run (part, shifted); t0 += part;
        h.set ("Low Mid Excite Tone", "Crunch");
        const auto after = h.run (part, shifted);
        const float steady = juce::jmax (maxStep (before, (size_t) part / 2, (size_t) part), maxStep (after, (size_t) part / 2, (size_t) part));
        const float change = maxStep (after, 0, (size_t) sampleRate / 10);
        check (change < steady * 1.15f, "Changing tone mid-note: no click", "biggest step " + fmt (change, 4) + " against " + fmt (steady, 4) + " steady");
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
    const auto& presets = halation::factoryPresets();
    check (proc.getNumPrograms() == (int) presets.size(), "Factory presets listed as programs", juce::String (proc.getNumPrograms()) + " programs");
    check (proc.getProgramName (0) == "Init", "Program 0 is Init", proc.getProgramName (0));
    {
        HalationAudioProcessor reference; // for each ID's name and range
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
    defaults (proc); h.set ("Mid Split", "1800"); h.set ("High Mid Excite", "42"); h.setBool ("Low Bypass", true);
    h.set ("High Mid Excite Tone", "Crunch"); h.set ("High Mid Excite Mix", "65"); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    defaults (proc); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Mid Split") == "1.80 kHz" && h.text ("High Mid Excite") == "42 %" && h.text ("Low Bypass") == "On"
               && h.text ("High Mid Excite Tone") == "Crunch" && h.text ("High Mid Excite Mix") == "65 %",
           "State save/restore", "'" + h.text ("Mid Split") + "', '" + h.text ("High Mid Excite") + "', '" + h.text ("Low Bypass") + "', '"
                                 + h.text ("High Mid Excite Tone") + "', '" + h.text ("High Mid Excite Mix") + "'");

    // --- User presets (built-in processor only: they're files on disk) ----------------------------------------
    if (auto* halation = dynamic_cast<HalationAudioProcessor*> (&proc))
    {
        const juce::String name = "HalationTests temporary preset";
        defaults (proc); h.set ("Low Split", "90"); h.set ("High Excite", "33"); h.apply();
        const bool saved = halation->saveUserPreset (name);
        const auto file = HalationAudioProcessor::userPresetFolder().getChildFile (name + ".halationpreset");
        defaults (proc); h.apply();
        const bool listed = halation->userPresets().contains (file);
        const bool loaded = halation->loadUserPreset (file);
        h.apply();
        check (saved && listed && loaded && h.text ("Low Split") == "90 Hz" && h.text ("High Excite") == "33 %"
                   && halation->getPresetName() == name && halation->getPresetIndex() == -1,
               "User preset: save, list and load", "'" + h.text ("Low Split") + "', '" + h.text ("High Excite") + "', '" + halation->getPresetName() + "'");
        // A host setting program 0 while it loads a session mustn't wipe a preset of yours
        proc.setCurrentProgram (0);
        h.apply();
        check (h.text ("High Excite") == "33 %" && halation->getPresetName() == name, "...and a host's program 0 leaves it alone", h.text ("High Excite"));
        file.deleteFile();
        halation->applyFactoryPreset (0);
        check (halation->getPresetName() == "Init" && h.text ("Low Split") == "120 Hz", "Init preset resets everything", halation->getPresetName());
    }

    // --- An outside lab's findings, with its settings ------------------------------------------------------------
    labFindings (proc);
}
} // namespace

int main (int argc, char** argv)
{
    if (auto result = maybeUIVideo<HalationAudioProcessor, HalationAudioProcessorEditor> (argc, argv)) return *result;
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<HalationAudioProcessor>(); },
        [] (const juce::File& png)
        {
            renderSnapshot<HalationAudioProcessor, HalationAudioProcessorEditor> (png, [] (Harness& h)
            {
                h.proc.setCurrentProgram (12); // Master polish: every band working, with Tape on the lows and Bright on the highs
            }, beat (-7.0f), 90);
        });
}
