// Spread tests: passthrough, width, bass mono, mono check, balance, output and the correlation meter.
//   SpreadTests <Spread.vst3> [ui-snapshot.png]

#include "Harness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace lbtest;

namespace
{
void defaults (Harness& h)
{
    h.set ("Width", "100"); h.set ("Bass Mono", "Off"); h.set ("Balance", "0"); h.set ("Output", "0"); h.setBool ("Mono", false);
}

Gen neg (Gen a) { return [a] (int n) { return -a (n); }; }

float maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) d = std::max (d, std::abs (a[i] - b[i]));
    return d;
}

std::vector<float> render (Gen g, int n) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = g (i); return v; }

void runSuite (juce::AudioProcessor& proc)
{
    runCommonChecks (proc, 0);
    Harness h (proc);
    const int second = (int) sampleRate;
    const size_t settle = (size_t) second / 2;
    auto a = noise (-12.0f, 1), b = noise (-12.0f, 2);
    const auto inL = render (a, second), inR = render (b, second);

    // --- Neutral settings leave the signal alone -----------------------------------------------
    defaults (h); h.apply();
    {
        const auto out = h.runStereo (second, a, b);
        const float d = std::max (maxDiff (out[0], inL), maxDiff (out[1], inR));
        check (d < 1.0e-6f, "Width 100%, everything else neutral: the input passes unchanged", "max diff " + juce::String (d));
    }

    // --- Width 0% is mono ----------------------------------------------------------------------
    defaults (h); h.set ("Width", "0"); h.apply();
    {
        const auto out = h.runStereo (second, a, b);
        std::vector<float> mid (inL.size());
        for (size_t i = 0; i < mid.size(); ++i) mid[i] = 0.5f * (inL[i] + inR[i]);
        const float d = std::max (maxDiff (std::vector<float> (out[0].begin() + (long) settle, out[0].end()), std::vector<float> (mid.begin() + (long) settle, mid.end())),
                                  maxDiff (std::vector<float> (out[1].begin() + (long) settle, out[1].end()), std::vector<float> (mid.begin() + (long) settle, mid.end())));
        check (d < 1.0e-6f, "Width 0%: both sides become the mono sum", "max diff " + juce::String (d));
    }

    // --- Width 200% doubles the side ----------------------------------------------------------------
    defaults (h); h.set ("Width", "200"); h.apply();
    {
        const auto out = h.runStereo (second, sine (-12.0f, 1000.0f), silence());
        // L only: M = S = x/2, so L becomes 1.5x and R becomes -0.5x
        const float l = toneDb (out[0], settle, out[0].size(), 1000.0) - (-12.0f), r = toneDb (out[1], settle, out[1].size(), 1000.0) - (-12.0f);
        check (std::abs (l - 3.52f) < 0.05f && std::abs (r + 6.02f) < 0.05f, "Width 200%: the side is doubled", "left " + fmt (l) + " dB (expected +3.52), right " + fmt (r) + " dB (expected -6.02)");
    }

    // --- Bass Mono centres the low end only ------------------------------------------------------------
    defaults (h); h.set ("Bass Mono", "150"); h.apply();
    {
        const auto lowSide = h.runStereo (second, sine (-12.0f, 60.0f), neg (sine (-12.0f, 60.0f)));
        const float lowSideDb = toneDb (lowSide[0], settle, lowSide[0].size(), 60.0) - (-12.0f);
        h.apply();
        const auto highSide = h.runStereo (second, sine (-12.0f, 2000.0f), neg (sine (-12.0f, 2000.0f)));
        const float highSideDb = toneDb (highSide[0], settle, highSide[0].size(), 2000.0) - (-12.0f);
        h.apply();
        const auto lowMid = h.runStereo (second, sine (-12.0f, 60.0f), sine (-12.0f, 60.0f));
        const float lowMidDb = toneDb (lowMid[0], settle, lowMid[0].size(), 60.0) - (-12.0f);
        check (lowSideDb < -24.0f && std::abs (highSideDb) < 0.2f && std::abs (lowMidDb) < 0.05f, "Bass Mono 150 Hz: low side removed, highs and centred bass untouched",
               "60 Hz side " + fmt (lowSideDb, 1) + " dB, 2 kHz side " + fmt (highSideDb) + " dB, 60 Hz mid " + fmt (lowMidDb, 3) + " dB");
    }

    // --- Mono check -------------------------------------------------------------------------------------
    defaults (h); h.setBool ("Mono", true); h.apply();
    {
        const auto out = h.runStereo (second, a, b);
        float d = 0.0f;
        for (size_t i = settle; i < out[0].size(); ++i) d = std::max ({ d, std::abs (out[0][i] - out[1][i]), std::abs (out[0][i] - 0.5f * (inL[i] + inR[i])) });
        check (d < 1.0e-6f, "Mono: both sides play the mono sum", "max diff " + juce::String (d));
    }

    // --- Balance -------------------------------------------------------------------------------------------
    defaults (h); h.set ("Balance", "R 50"); h.apply();
    {
        const auto out = h.runStereo (second, sine (-12.0f, 500.0f), sine (-12.0f, 500.0f));
        const float l = toneDb (out[0], settle, out[0].size(), 500.0) + 12.0f, r = toneDb (out[1], settle, out[1].size(), 500.0) + 12.0f;
        check (std::abs (l + 6.02f) < 0.05f && std::abs (r) < 0.05f, "Balance R 50: left down 6 dB, right unchanged", "left " + fmt (l) + " dB, right " + fmt (r) + " dB");
    }
    defaults (h); h.set ("Balance", "L 100"); h.apply();
    {
        const auto out = h.runStereo (second, sine (-12.0f, 500.0f), sine (-12.0f, 500.0f));
        check (peakDb (out[1], settle) < -100.0f, "Balance L 100: the right side is silent", fmt (peakDb (out[1], settle), 1) + " dBFS");
    }

    // --- Output -----------------------------------------------------------------------------------------------
    defaults (h); h.set ("Output", "-6"); h.apply();
    {
        const auto out = h.runStereo (second, sine (-12.0f, 500.0f), sine (-12.0f, 500.0f));
        const float g = toneDb (out[0], settle, out[0].size(), 500.0) + 12.0f;
        check (std::abs (g + 6.0f) < 0.05f, "Output -6 dB", fmt (g) + " dB");
    }

    // --- Correlation meter (the built-in processor exposes it) ------------------------------------------------
    if (auto* sp = dynamic_cast<SpreadAudioProcessor*> (&proc))
    {
        defaults (h); h.apply();
        h.runStereo (second, a, a);
        const float same = sp->correlation.load();
        h.apply();
        h.runStereo (second, a, neg (a));
        const float opposite = sp->correlation.load();
        h.apply();
        h.runStereo (second, a, b);
        const float unrelated = sp->correlation.load();
        check (same > 0.99f && opposite < -0.99f && std::abs (unrelated) < 0.15f, "Correlation reads +1 for mono, -1 out of phase, ~0 for unrelated sides",
               fmt (same, 3) + ", " + fmt (opposite, 3) + ", " + fmt (unrelated, 3));
    }

    // --- State recall --------------------------------------------------------------------------------------------
    defaults (h); h.set ("Width", "150"); h.set ("Bass Mono", "120"); h.apply();
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    h.set ("Width", "40"); h.set ("Bass Mono", "Off"); h.apply();
    proc.setStateInformation (state.getData(), (int) state.getSize());
    h.apply();
    check (h.text ("Width") == "150 %" && h.text ("Bass Mono") == "120 Hz", "State save/restore", "'" + h.text ("Width") + "', '" + h.text ("Bass Mono") + "'");
}

// A wide stereo pad: different chords and noise on each side
Gen pad (int side)
{
    auto nz = noise (-26.0f, side == 0 ? 21u : 22u);
    return [nz, side] (int n)
    {
        const double t = n / sampleRate, twoPi = 2.0 * juce::MathConstants<double>::pi;
        const double f1 = side == 0 ? 220.0 : 221.7, f2 = side == 0 ? 329.6 : 277.2, f3 = side == 0 ? 440.0 : 659.3;
        const double centre = 0.16 * std::sin (twoPi * 110.0 * t) + 0.08 * std::sin (twoPi * 165.0 * t); // bass and root, the same on both sides
        return (float) (centre + 0.07 * std::sin (twoPi * f1 * t) + 0.05 * std::sin (twoPi * f2 * t + side) + 0.04 * std::sin (twoPi * f3 * t)) + nz (n);
    };
}
} // namespace

int main (int argc, char** argv)
{
    if (auto result = maybeUIVideo<SpreadAudioProcessor, SpreadAudioProcessorEditor> (argc, argv)) return *result;
    return runMain (argc, argv, runSuite,
        [] { return std::make_unique<SpreadAudioProcessor>(); },
        [] (const juce::File& png)
        {
            SpreadAudioProcessor proc;
            Harness h (proc);
            h.set ("Width", "135"); h.set ("Bass Mono", "120"); h.set ("Balance", "0"); h.set ("Output", "-1");
            h.apply();
            auto l = pad (0), r = pad (1);
            int offset = 0;
            auto play = [&] (int n) { h.runStereo (n, [&] (int i) { return l (offset + i); }, [&] (int i) { return r (offset + i); }); offset += n; };
            play ((int) sampleRate / 2);
            SpreadAudioProcessorEditor editor (proc);
            for (int i = 0; i < 14; ++i) { play (blockSize * 3); editor.refreshFromProcessor(); }
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
            png.deleteFile();
            juce::FileOutputStream os (png);
            juce::PNGImageFormat().writeImageToStream (image, os);
            std::cout << "  wrote " << png.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
        });
}
