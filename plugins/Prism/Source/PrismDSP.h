#pragma once

// Prism: four-band compressor with an exciter in every band. Plain C++ shared by the audio code, the display
// and the tests.
//
// Crossover: Linkwitz-Riley 24 dB/oct (two Butterworth sections) at three split points. The middle split comes
// first; each half is then split again, and each half also passes through an all-pass at the other half's split,
// so all four bands carry the same phase. Their sum is an all-pass: with every band left alone, Prism changes no
// level at any frequency. The dry path for Mix goes through the same three all-passes, so blending stays flat too.
//
//   x -> LR(mid) -> low half  -> LR(low)  -> [Low, Low Mid] -> AP(high)
//                -> high half -> AP(low) -> LR(high) -> [High Mid, High]
//
// Compressor: Squeeze's (feed-forward, soft knee, stereo-linked peak detection).
// Exciter: adds the harmonics of a curve with a slope of 1 at zero (Ember's Tube curve, so even and odd
// harmonics) on top of the band; the fundamental is left where it was. Only harmonics are added, so a band's
// level barely moves, and each band's harmonics are generated from that band alone.

#include <LBAudio/Biquad.h>
#include "../../Squeeze/Source/CompressorDSP.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace prism
{
constexpr int numBands = 4;
constexpr double butterworthQ = 0.70710678118654752;

inline const char* bandName (int b)
{
    static const char* names[numBands] = { "Low", "Low Mid", "High Mid", "High" };
    return names[std::clamp (b, 0, numBands - 1)];
}

// Split points keep at least this ratio apart (a third of an octave short of an octave)
constexpr float minSplitRatio = 1.6f;

struct Splits { float low = 120.0f, mid = 1000.0f, high = 6000.0f; };

// Puts the split points in order and keeps them apart, whatever the three knobs say.
inline Splits orderedSplits (Splits s, double fs)
{
    const float top = (float) std::min (20000.0, fs * 0.45);
    s.low  = std::clamp (s.low, 20.0f, top / (minSplitRatio * minSplitRatio));
    s.mid  = std::clamp (s.mid, s.low * minSplitRatio, top / minSplitRatio);
    s.high = std::clamp (s.high, s.mid * minSplitRatio, top);
    return s;
}

inline lbaudio::dsp::Coeffs allPass (double freq, double q, double fs)
{
    freq = std::clamp (freq, 1.0, fs * 0.49);
    const double w0 = 2.0 * lbaudio::dsp::pi * freq / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    return { (1.0 - alpha) / a0, -2.0 * cw / a0, (1.0 + alpha) / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
}

// One Linkwitz-Riley 24 dB/oct split: two identical Butterworth sections each way.
struct LRSplit
{
    lbaudio::dsp::Biquad lp1, lp2, hp1, hp2;

    void set (double freq, double fs)
    {
        lp1.c = lp2.c = lbaudio::dsp::design (lbaudio::dsp::Shape::LowPass, freq, 0.0, butterworthQ, fs);
        hp1.c = hp2.c = lbaudio::dsp::design (lbaudio::dsp::Shape::HighPass, freq, 0.0, butterworthQ, fs);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    void snap() { lp1.snapDenormals(); lp2.snapDenormals(); hp1.snapDenormals(); hp2.snapDenormals(); }

    void process (float x, float& low, float& high) noexcept
    {
        low = lp2.process (lp1.process (x));
        high = hp2.process (hp1.process (x));
    }
};

// The whole four-way crossover for one channel, plus the matching all-pass chain for the dry signal.
struct Crossover
{
    LRSplit mid, low, high;
    lbaudio::dsp::Biquad apHighOnLow, apHighOnLowMid, apLowOnHigh; // phase alignment between the halves
    lbaudio::dsp::Biquad dryLow, dryMid, dryHigh;                   // the dry path's all-passes

    void set (const Splits& s, double fs)
    {
        mid.set (s.mid, fs); low.set (s.low, fs); high.set (s.high, fs);
        apHighOnLow.c = apHighOnLowMid.c = dryHigh.c = allPass (s.high, butterworthQ, fs);
        apLowOnHigh.c = dryLow.c = allPass (s.low, butterworthQ, fs);
        dryMid.c = allPass (s.mid, butterworthQ, fs);
    }

    void reset()
    {
        mid.reset(); low.reset(); high.reset();
        for (auto* b : { &apHighOnLow, &apHighOnLowMid, &apLowOnHigh, &dryLow, &dryMid, &dryHigh }) b->reset();
    }

    void snap()
    {
        mid.snap(); low.snap(); high.snap();
        for (auto* b : { &apHighOnLow, &apHighOnLowMid, &apLowOnHigh, &dryLow, &dryMid, &dryHigh }) b->snapDenormals();
    }

    void process (float x, std::array<float, numBands>& bands, float& dry) noexcept
    {
        float lowHalf, highHalf;
        mid.process (x, lowHalf, highHalf);
        float b0, b1, b2, b3;
        low.process (lowHalf, b0, b1);
        highHalf = apLowOnHigh.process (highHalf);
        high.process (highHalf, b2, b3);
        bands[0] = apHighOnLow.process (b0);
        bands[1] = apHighOnLowMid.process (b1);
        bands[2] = b2;
        bands[3] = b3;
        dry = dryHigh.process (dryMid.process (dryLow.process (x)));
    }
};

// Magnitude of one band's filter (for the display), in dB.
inline double bandResponseDb (int band, const Splits& s, double hz, double fs)
{
    using lbaudio::dsp::Shape; using lbaudio::dsp::design; using lbaudio::dsp::magnitudeDb;
    auto lr = [&] (Shape shape, double f) { return 2.0 * magnitudeDb (design (shape, f, 0.0, butterworthQ, fs), hz, fs); };
    switch (band)
    {
        case 0:  return lr (Shape::LowPass, s.mid) + lr (Shape::LowPass, s.low);
        case 1:  return lr (Shape::LowPass, s.mid) + lr (Shape::HighPass, s.low);
        case 2:  return lr (Shape::HighPass, s.mid) + lr (Shape::LowPass, s.high);
        default: return lr (Shape::HighPass, s.mid) + lr (Shape::HighPass, s.high);
    }
}

//==============================================================================
// Exciter
constexpr float tubeBias = 0.25f;

inline float tube (float x) noexcept
{
    static const float tb = std::tanh (tubeBias);
    static const float norm = 1.0f / (1.0f - tb * tb);
    return (std::tanh (x + tubeBias) - tb) * norm;
}

// Excite 0..1: drive into the curve rises to +16 dB, and the harmonics are added at up to twice their level.
inline float exciteDrive (float amount) noexcept { return 1.0f + 5.3f * amount; }
inline float exciteMix (float amount) noexcept { return 2.0f * amount; }

// The harmonics only: the curve minus its linear part.
inline float harmonics (float x, float drive) noexcept { return tube (x * drive) / drive - x; }

//==============================================================================
struct BandParams
{
    float thresholdDb = 0.0f, ratio = 2.0f, attackMs = 10.0f, releaseMs = 120.0f, makeupDb = 0.0f;
    float excite = 0.0f;    // 0..1
    bool solo = false, bypass = false;
};

constexpr float kneeDb = 6.0f;

inline squeeze::CompressorParams compressorParams (const BandParams& b)
{
    squeeze::CompressorParams c;
    c.thresholdDb = b.thresholdDb; c.ratio = b.ratio; c.kneeDb = kneeDb;
    c.attackMs = b.attackMs; c.releaseMs = b.releaseMs; c.makeupDb = b.makeupDb;
    c.autoMakeup = false; c.mix = 1.0f; c.scHpfHz = 20.0f;
    return c;
}

struct Params
{
    Splits splits;
    std::array<BandParams, numBands> bands;
    float inputDb = 0.0f, outputDb = 0.0f, mix = 1.0f; // mix 0..1
};
} // namespace prism
