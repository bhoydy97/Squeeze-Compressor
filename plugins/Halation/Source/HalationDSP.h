#pragma once

// Halation: four-band compressor with an exciter in every band. Plain C++ shared by the audio code, the display
// and the tests.
//
// Crossover: Linkwitz-Riley 24 dB/oct (two Butterworth sections) at three split points. The middle split comes
// first; each half is then split again, and each half also passes through an all-pass at the other half's split,
// so all four bands carry the same phase. Their sum is an all-pass: with every band left alone, Halation changes no
// level at any frequency. The dry path for Mix goes through the same three all-passes, so blending stays flat too.
//
//   x -> LR(mid) -> low half  -> LR(low)  -> [Low, Low Mid] -> AP(high)
//                -> high half -> AP(low) -> LR(high) -> [High Mid, High]
//
// Compressor: Squeeze's (feed-forward, soft knee, stereo-linked peak detection).
// Exciter: each band, scaled by its own envelope to a steady level, goes through the curve of its tone (Warm is
// Ember's Tube curve); what the curve adds, minus its fundamental and DC, is scaled back and added to the band. So the
// harmonics sit at the same distance below the band whether it's loud or quiet, the band's own level
// doesn't move, and each band's harmonics are made from that band alone. They're high-passed at the band's
// lower edge, so nothing the envelope does can thump below the band.

#include <LBAudio/Biquad.h>
#include "../../Squeeze/Source/CompressorDSP.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace halation
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
//
// Each band can pick a tone: the curve its harmonics come from, and how they're filtered.
//   Warm    the tube curve (even and odd harmonics), full and round: the original
//   Tape    a soft, symmetric curve (mostly the 3rd harmonic), with the top of the harmonics rolled off
//   Bright  the soft curve driven much harder, for crisp odd harmonics that reach higher (no 2nd), high-passed
//   Crunch  an asymmetric, harder clip: dense even and odd harmonics, with an edge
//   Octave  an even curve, so the harmonics start an octave above the note: weight on small speakers, or shimmer
//
// Amount sets how loud the harmonics are, as before, and how hard the band is driven into the curve: low settings
// give mostly the 2nd and 3rd, high settings get denser. Each tone's level is matched to Warm's at every amount.
// Mix then blends the harmonics in (100 % is the full amount).
constexpr float tubeBias = 0.25f;

inline float tube (float x) noexcept
{
    static const float tb = std::tanh (tubeBias);
    static const float norm = 1.0f / (1.0f - tb * tb);
    return (std::tanh (x + tubeBias) - tb) * norm;
}

constexpr float crunchBias = 0.2f;
inline float hardClip (float x) noexcept { const float x2 = x * x; return x / std::sqrt (std::sqrt (1.0f + x2 * x2)); }
inline float crunch (float x) noexcept
{
    static const float hb = hardClip (crunchBias);
    static const float slope = 1.0f / std::pow (1.0f + std::pow (crunchBias, 4.0f), 1.25f);
    return (hardClip (x + crunchBias) - hb) / slope;
}
inline float softTape (float x) noexcept { return std::tanh (x); }
inline float evenOctave (float x) noexcept { return x * std::tanh (x); }

enum class Tone { Warm, Tape, Bright, Crunch, Octave };
constexpr int numTones = 5;

inline const char* toneName (int t)
{
    static const char* names[numTones] = { "Warm", "Tape", "Bright", "Crunch", "Octave" };
    return names[std::clamp (t, 0, numTones - 1)];
}
inline const char* toneDescription (int t)
{
    static const char* text[numTones] = {
        "Full and round: tube harmonics, even and odd",
        "Soft and smooth: mostly the 3rd harmonic, the top rolled off",
        "Presence and air: crisp harmonics that reach higher",
        "An edge: dense harmonics, like a driven amp",
        "An octave up: weight on small speakers, or shimmer on top" };
    return text[std::clamp (t, 0, numTones - 1)];
}

constexpr float exciteFloor = 0.001f;          // -60 dB: quieter than this, a band isn't lifted to full level
constexpr double exciteAttackS = 0.001, exciteReleaseS = 0.12;
constexpr float exciteMinDrive = 0.15f;         // of the tone's full drive, at the bottom of Amount
constexpr float exciteWarmGain = 1.5f;          // Warm at 100 %: the 3rd harmonic of a steady tone ~17 dB under it

// One tone's curve and filters, and tables over its drive: what to take away so only harmonics are left (the
// fundamental and DC a full-level sine gets from the curve), and how much to lift them to a steady level.
struct ToneShape
{
    float (*curve) (float) = tube;
    float maxDrive = 2.0f;
    float highPassAt = 1.0f;   // times the band's lower edge
    bool steepHighPass = false; // 24 dB/oct rather than 12
    float lowPassAt = 0.0f;    // times the band's upper edge; 0 for none
    float gain = 1.0f;         // matches the tone's level at full drive to Warm's
    static constexpr int tableSize = 65;
    std::array<float, tableSize> fundamental {}, dc {}, level {};

    float driveFor (float amount) const noexcept { return maxDrive * (exciteMinDrive + (1.0f - exciteMinDrive) * std::clamp (amount, 0.0f, 1.0f)); }

    // The harmonics of a band scaled to about 1 (u is the band over its level), at 'amount'
    float harmonics (float u, float amount) const noexcept
    {
        const float drive = driveFor (amount);
        const float pos = (drive / maxDrive - exciteMinDrive) / (1.0f - exciteMinDrive) * (float) (tableSize - 1);
        const int i = std::clamp ((int) pos, 0, tableSize - 2);
        const float f = std::clamp (pos - (float) i, 0.0f, 1.0f);
        auto at = [i, f] (const std::array<float, tableSize>& t) { return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]); };
        const float raw = curve (u * drive) / drive - u + at (fundamental) * u - at (dc);
        return gain * at (level) * raw;
    }
};

namespace detail
{
// What the curve adds to a full-level sine at 'drive', with the fundamental and DC removed: (fundamental, dc, rms)
inline std::array<double, 3> analyseCurve (float (*curve) (float), float drive)
{
    constexpr int n = 2048;
    double fund = 0.0, mean = 0.0;
    std::array<double, n> r {};
    for (int i = 0; i < n; ++i)
    {
        const double u = std::sin (2.0 * lbaudio::dsp::pi * i / n);
        r[(size_t) i] = curve ((float) u * drive) / drive - u;
        fund += r[(size_t) i] * u;
        mean += r[(size_t) i];
    }
    fund = -2.0 * fund / n; mean /= n;
    double sq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double u = std::sin (2.0 * lbaudio::dsp::pi * i / n);
        const double h = r[(size_t) i] + fund * u - mean;
        sq += h * h;
    }
    return { fund, mean, std::sqrt (sq / n) };
}
} // namespace detail

inline const ToneShape& toneShape (int t)
{
    static const std::array<ToneShape, numTones> shapes = []
    {
        std::array<ToneShape, numTones> s;
        s[0].curve = tube;       s[0].maxDrive = 2.0f;
        s[1].curve = softTape;   s[1].maxDrive = 1.6f;                                      s[1].lowPassAt = 1.5f;
        s[2].curve = softTape;   s[2].maxDrive = 4.0f; s[2].highPassAt = 2.5f; s[2].steepHighPass = true;
        s[3].curve = crunch;     s[3].maxDrive = 3.5f;
        s[4].curve = evenOctave; s[4].maxDrive = 2.0f; s[4].highPassAt = 1.5f; s[4].steepHighPass = true; s[4].lowPassAt = 4.0f;
        const double warmRms = detail::analyseCurve (tube, 2.0f)[2];
        for (auto& shape : s)
        {
            const double fullRms = detail::analyseCurve (shape.curve, shape.maxDrive)[2];
            for (int i = 0; i < ToneShape::tableSize; ++i)
            {
                const float drive = shape.maxDrive * (exciteMinDrive + (1.0f - exciteMinDrive) * (float) i / (ToneShape::tableSize - 1));
                const auto a = detail::analyseCurve (shape.curve, drive);
                shape.fundamental[(size_t) i] = (float) a[0];
                shape.dc[(size_t) i] = (float) a[1];
                shape.level[(size_t) i] = (float) (fullRms / std::max (1.0e-9, a[2]));
            }
            shape.gain = (float) (exciteWarmGain * warmRms / fullRms);
        }
        s[2].gain *= 1.15f; // Bright's high-pass trims a little of its level: lift it back level with the rest
        return s;
    }();
    return shapes[(size_t) std::clamp (t, 0, numTones - 1)];
}

// One band's exciter: its envelope (stereo-linked), its tone, and the filters on its harmonics, per channel.
// A change of tone fades the harmonics out, swaps, and fades them back in, so it never clicks.
struct Exciter
{
    double env = 0.0;
    int tone = 0;
    float fade = 1.0f;
    double lowEdge = 20.0, highEdge = 20000.0, fs = 48000.0;
    struct Chain { lbaudio::dsp::Biquad hp1, hp2, lp; bool steep = false, lowPass = false; };
    std::array<Chain, 2> chain;

    void reset() noexcept
    {
        env = 0.0;
        for (auto& c : chain) { c.hp1.reset(); c.hp2.reset(); c.lp.reset(); }
    }

    void setEdges (double lowHz, double highHz, double sampleRate)
    {
        lowEdge = lowHz; highEdge = highHz; fs = sampleRate;
        configure();
    }

    void setTone (int t)
    {
        tone = std::clamp (t, 0, numTones - 1);
        configure();
        for (auto& c : chain) { c.hp1.reset(); c.hp2.reset(); c.lp.reset(); }
    }

    void configure()
    {
        using lbaudio::dsp::Shape; using lbaudio::dsp::design;
        const auto& shape = toneShape (tone);
        const double nyquistish = fs * 0.45;
        const auto hp = design (Shape::HighPass, std::clamp (lowEdge * shape.highPassAt, 20.0, nyquistish), 0.0, butterworthQ, fs);
        const bool lowPass = shape.lowPassAt > 0.0f && highEdge * shape.lowPassAt < nyquistish;
        const auto lp = design (Shape::LowPass, lowPass ? highEdge * shape.lowPassAt : nyquistish, 0.0, butterworthQ, fs);
        for (auto& c : chain)
        {
            c.hp1.c = c.hp2.c = hp; c.lp.c = lp;
            c.steep = shape.steepHighPass; c.lowPass = lowPass;
        }
    }

    float filter (int ch, float h) noexcept
    {
        auto& c = chain[(size_t) ch];
        h = c.hp1.process (h);
        if (c.steep) h = c.hp2.process (h);
        if (c.lowPass) h = c.lp.process (h);
        return h;
    }

    void snap() noexcept { for (auto& c : chain) { c.hp1.snapDenormals(); c.hp2.snapDenormals(); c.lp.snapDenormals(); } }
};

//==============================================================================
struct BandParams
{
    float thresholdDb = 0.0f, ratio = 2.0f, attackMs = 10.0f, releaseMs = 120.0f, makeupDb = 0.0f;
    float excite = 0.0f;    // 0..1
    int tone = 0;           // halation::Tone
    float exciteMix = 1.0f; // 0..1
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
} // namespace halation
