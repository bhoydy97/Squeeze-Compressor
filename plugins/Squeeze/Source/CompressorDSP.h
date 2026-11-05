#pragma once

// Squeeze compressor core.
// Plain C++ with no framework dependency, so it can be unit-tested or reused anywhere.
//
// Design: feed-forward, log-domain, peak-detecting compressor after
// Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design -
// A Tutorial and Analysis" (JAES 2012):
//   detector (optional sidechain high-pass) -> |x| in dB -> soft-knee gain computer
//   -> branching attack/release smoothing in the dB domain -> gain applied to all
//   channels (stereo-linked) -> makeup -> dry/wet mix.

#include <algorithm>
#include <cmath>
#include <vector>

namespace squeeze
{

struct CompressorParams
{
    float thresholdDb = -18.0f;  // dBFS
    float ratio       = 4.0f;    // n:1
    float kneeDb      = 6.0f;    // total knee width
    float attackMs    = 10.0f;
    float releaseMs   = 120.0f;
    float makeupDb    = 0.0f;
    bool  autoMakeup  = false;
    float mix         = 1.0f;    // 0 = dry, 1 = fully compressed
    float scHpfHz     = 20.0f;   // <= 20 Hz means off
};

struct BlockStats
{
    float maxGainReductionDb = 0.0f; // positive number, e.g. 6 = 6 dB of reduction
    float inputPeak  = 0.0f;         // linear
    float outputPeak = 0.0f;         // linear
};

// Static gain computer: returns gain change in dB (<= 0) for a given input level.
inline float gainReductionDb (float inDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float over  = inDb - thresholdDb;
    const float slope = 1.0f / ratio - 1.0f;

    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
    {
        const float t = over + kneeDb * 0.5f;
        return slope * t * t / (2.0f * kneeDb);
    }

    return over > 0.0f ? slope * over : 0.0f;
}

// Auto makeup: restore half of the reduction a 0 dBFS signal would receive.
// Full compensation tends to make things louder than the bypassed signal,
// which fools the ear; half is a common, musical compromise.
inline float autoMakeupDb (float thresholdDb, float ratio, float kneeDb) noexcept
{
    return -0.5f * gainReductionDb (0.0f, thresholdDb, ratio, kneeDb);
}

inline float dbToGain (float db) noexcept { return std::exp (db * 0.11512925464970229f); } // ln(10)/20

class Compressor
{
public:
    void prepare (double newSampleRate, int maxChannels)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        hpf.assign ((size_t) std::max (1, maxChannels), Biquad{});
        hpfActive = false;
        smoothCoeff = 1.0f - (float) std::exp (-1.0 / (0.02 * sampleRate)); // ~20 ms
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        envelopeDb = 0.0f;
        for (auto& f : hpf) f.clearState();
        makeupSmoothed = targetMakeupDb;
        mixSmoothed    = params.mix;
    }

    void setParams (const CompressorParams& p)
    {
        params = p;
        params.ratio  = std::max (1.0f, params.ratio);
        params.kneeDb = std::max (0.0f, params.kneeDb);
        params.mix    = std::clamp (params.mix, 0.0f, 1.0f);
        updateCoefficients();
    }

    const CompressorParams& getParams() const noexcept { return params; }

    // io:  main channels, processed in place
    // sc:  optional external sidechain (nullptr = detect from io)
    BlockStats process (float* const* io, int numChannels,
                        const float* const* sc, int numScChannels,
                        int numSamples) noexcept
    {
        BlockStats stats;
        if (numChannels <= 0 || numSamples <= 0)
            return stats;

        const bool useExternal = sc != nullptr && numScChannels > 0;
        const float* const* det = useExternal ? sc : io;
        const int numDet = std::min (useExternal ? numScChannels : numChannels, (int) hpf.size());
        const bool filterOn = hpfEnabled();

        for (int n = 0; n < numSamples; ++n)
        {
            // 1) Detector: stereo-linked peak of (optionally high-passed) sidechain
            float level = 0.0f;
            for (int ch = 0; ch < numDet; ++ch)
            {
                float s = det[ch][n];
                if (filterOn) s = hpf[(size_t) ch].process (s);
                level = std::max (level, std::abs (s));
            }

            for (int ch = 0; ch < numChannels; ++ch)
                stats.inputPeak = std::max (stats.inputPeak, std::abs (io[ch][n]));

            // 2) Gain computer
            const float levelDb = level > 1.0e-6f ? 20.0f * std::log10 (level) : -120.0f;
            const float targetGr = -gainReductionDb (levelDb, params.thresholdDb, params.ratio, params.kneeDb);

            // 3) Ballistics (branching, in dB domain)
            const float a = targetGr > envelopeDb ? attackCoeff : releaseCoeff;
            envelopeDb = a * envelopeDb + (1.0f - a) * targetGr;
            if (envelopeDb < 1.0e-7f) envelopeDb = 0.0f; // avoid denormals on long releases

            stats.maxGainReductionDb = std::max (stats.maxGainReductionDb, envelopeDb);

            // 4) Makeup + mix (smoothed to avoid zipper noise)
            makeupSmoothed += (targetMakeupDb - makeupSmoothed) * smoothCoeff;
            mixSmoothed    += (params.mix     - mixSmoothed)    * smoothCoeff;

            const float wetGain = dbToGain (makeupSmoothed - envelopeDb);
            const float gain    = 1.0f + mixSmoothed * (wetGain - 1.0f); // dry*(1-m) + wet*m

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float y = io[ch][n] * gain;
                io[ch][n] = y;
                stats.outputPeak = std::max (stats.outputPeak, std::abs (y));
            }
        }

        return stats;
    }

    // For UIs: the full static curve including makeup.
    static float staticOutputDb (float inDb, const CompressorParams& p) noexcept
    {
        const float makeup = p.autoMakeup ? autoMakeupDb (p.thresholdDb, p.ratio, p.kneeDb) : p.makeupDb;
        return inDb + gainReductionDb (inDb, p.thresholdDb, std::max (1.0f, p.ratio), std::max (0.0f, p.kneeDb)) + makeup;
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        void clearState() noexcept { z1 = z2 = 0; }

        void setHighPass (double freq, double fs) noexcept
        {
            const double w0    = 2.0 * 3.14159265358979323846 * freq / fs;
            const double cosw  = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * 0.70710678118654752); // Butterworth Q
            const double a0    = 1.0 + alpha;
            b0 = (1.0 + cosw) * 0.5 / a0;
            b1 = -(1.0 + cosw) / a0;
            b2 = b0;
            a1 = -2.0 * cosw / a0;
            a2 = (1.0 - alpha) / a0;
        }

        float process (float xIn) noexcept // transposed direct form II
        {
            const double x = xIn;
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    bool hpfEnabled() const noexcept { return params.scHpfHz > 20.5f; }

    static float timeToCoeff (float ms, double fs) noexcept
    {
        if (ms <= 0.0f) return 0.0f;
        return (float) std::exp (-1.0 / (0.001 * ms * fs));
    }

    void updateCoefficients()
    {
        attackCoeff  = timeToCoeff (params.attackMs,  sampleRate);
        releaseCoeff = timeToCoeff (params.releaseMs, sampleRate);
        targetMakeupDb = params.autoMakeup ? autoMakeupDb (params.thresholdDb, params.ratio, params.kneeDb)
                                           : params.makeupDb;

        if (hpfEnabled())
        {
            const float f = std::min (params.scHpfHz, (float) (sampleRate * 0.45));
            if (! hpfActive || std::abs (f - currentHpfHz) > 0.01f)
            {
                for (auto& b : hpf) b.setHighPass (f, sampleRate);
                currentHpfHz = f;
                hpfActive = true;
            }
        }
        else if (hpfActive)
        {
            for (auto& b : hpf) b.clearState();
            hpfActive = false;
        }
    }

    CompressorParams params;
    double sampleRate = 44100.0;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float envelopeDb = 0.0f;
    float targetMakeupDb = 0.0f, makeupSmoothed = 0.0f, mixSmoothed = 1.0f;
    float smoothCoeff = 0.001f;
    float currentHpfHz = 0.0f;
    bool hpfActive = false;
    std::vector<Biquad> hpf;
};

} // namespace squeeze
