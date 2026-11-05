#pragma once

// Carve: seven-band parametric EQ.
// High-pass, low shelf, three bells, high shelf, low-pass. Minimum phase, zero latency.
// Parameter moves are smoothed (20 ms) and filters are rebuilt every 32 samples, so sweeps don't zip.
// Band responses come from the same functions the display uses.

#include <LBAudio/Biquad.h>
#include <array>
#include <functional>
#include <vector>

namespace carve
{
using lbaudio::dsp::Coeffs;
using lbaudio::dsp::Shape;

enum Band { HP = 0, LowShelf, Bell1, Bell2, Bell3, HighShelf, LP, NumBands };

struct BandParams
{
    bool  on     = true;
    float freq   = 1000.0f;
    float gainDb = 0.0f;
    float q      = 1.0f;
    int   slope  = 1; // HP/LP only: 0 = 12, 1 = 24, 2 = 48 dB/oct
};

struct Params
{
    std::array<BandParams, NumBands> bands;
    float outputDb = 0.0f;
};

inline int slopeOrder (int slope) { return slope <= 0 ? 2 : (slope == 1 ? 4 : 8); }

// The filter sections that make up one band at given settings.
inline std::vector<Coeffs> bandSections (int band, const BandParams& p, double fs)
{
    std::vector<Coeffs> s;
    switch (band)
    {
        case HP:
        case LP:
            for (double q : lbaudio::dsp::butterworthQs (slopeOrder (p.slope)))
                s.push_back (lbaudio::dsp::design (band == HP ? Shape::HighPass : Shape::LowPass, p.freq, 0.0, q, fs));
            break;
        case LowShelf:  s.push_back (lbaudio::dsp::design (Shape::LowShelf,  p.freq, p.gainDb, 0.7071, fs)); break;
        case HighShelf: s.push_back (lbaudio::dsp::design (Shape::HighShelf, p.freq, p.gainDb, 0.7071, fs)); break;
        default:        s.push_back (lbaudio::dsp::design (Shape::Bell,      p.freq, p.gainDb, p.q,    fs)); break;
    }
    return s;
}

inline bool isPass (int band) { return band == HP || band == LP; }

// Response of one band in dB (0 when the band is off).
inline double bandResponseDb (int band, const BandParams& p, double freq, double fs)
{
    if (isPass (band) && ! p.on) return 0.0;
    double db = 0.0;
    for (const auto& c : bandSections (band, p, fs))
        db += lbaudio::dsp::magnitudeDb (c, freq, fs);
    return db;
}

// Whole-EQ response in dB, including output gain.
inline double responseDb (const Params& p, double freq, double fs)
{
    double db = p.outputDb;
    for (int b = 0; b < NumBands; ++b)
        db += bandResponseDb (b, p.bands[(size_t) b], freq, fs);
    return db;
}

inline Params defaultParams()
{
    Params p;
    p.bands[HP]        = { false, 30.0f,    0.0f, 0.707f, 1 };
    p.bands[LowShelf]  = { true,  100.0f,   0.0f, 0.707f, 1 };
    p.bands[Bell1]     = { true,  250.0f,   0.0f, 1.0f,   1 };
    p.bands[Bell2]     = { true,  1000.0f,  0.0f, 1.0f,   1 };
    p.bands[Bell3]     = { true,  4000.0f,  0.0f, 1.0f,   1 };
    p.bands[HighShelf] = { true,  8000.0f,  0.0f, 0.707f, 1 };
    p.bands[LP]        = { false, 20000.0f, 0.0f, 0.707f, 1 };
    return p;
}

class Equaliser
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int subBlock = 32;
    static constexpr int maxSections = 8;

    void prepare (double newFs, int numChannels)
    {
        fs = newFs > 0 ? newFs : 44100.0;
        channels = std::clamp (numChannels, 1, maxChannels);
        smoothK = 1.0 - std::exp (-(double) subBlock / (0.02 * fs));
        outK = 1.0 - std::exp (-1.0 / (0.02 * fs));
        current = target;
        for (int b = 0; b < NumBands; ++b) passOn[(size_t) b] = target.bands[(size_t) b].on ? 1.0 : 0.0;
        outGain = std::pow (10.0, target.outputDb / 20.0);
        rebuildAll (true);
        reset();
    }

    void reset()
    {
        for (auto& band : filters)
            for (auto& ch : band)
                for (auto& s : ch) s.reset();
    }

    void setParams (const Params& p) { target = p; }

    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min (numChannels, channels);
        for (int start = 0; start < numSamples; start += subBlock)
        {
            const int n = std::min (subBlock, numSamples - start);
            smoothStep();

            for (int b = 0; b < NumBands; ++b)
            {
                const auto& sections = sectionCount[(size_t) b];
                if (sections == 0) continue;

                const bool pass = isPass (b);
                const double onTarget = pass ? (target.bands[(size_t) b].on ? 1.0 : 0.0) : 1.0;
                const double on0 = passOn[(size_t) b];
                const double on1 = pass ? on0 + (onTarget - on0) * smoothK * 2.0 : 1.0;
                passOn[(size_t) b] = std::abs (on1 - onTarget) < 1.0e-4 ? onTarget : on1;

                if (pass && same (on0, 0.0) && same (passOn[(size_t) b], 0.0))
                {
                    // Fully off: keep filter state clear so it starts cleanly when switched on
                    for (int ch = 0; ch < numChannels; ++ch)
                        for (int s = 0; s < sections; ++s) filters[(size_t) b][(size_t) ch][(size_t) s].reset();
                    continue;
                }
                if (! pass && isIdentity[(size_t) b]) continue;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* x = io[ch] + start;
                    auto& chain = filters[(size_t) b][(size_t) ch];
                    for (int i = 0; i < n; ++i)
                    {
                        float y = x[i];
                        for (int s = 0; s < sections; ++s) y = chain[(size_t) s].process (y);
                        if (pass)
                        {
                            const double m = on0 + (passOn[(size_t) b] - on0) * (double) (i + 1) / (double) n;
                            y = (float) (x[i] + m * (y - x[i]));
                        }
                        x[i] = y;
                    }
                    for (int s = 0; s < sections; ++s) chain[(size_t) s].snapDenormals();
                }
            }

            const double outTarget = std::pow (10.0, target.outputDb / 20.0);
            for (int i = 0; i < n; ++i)
            {
                outGain += (outTarget - outGain) * outK;
                for (int ch = 0; ch < numChannels; ++ch)
                    io[ch][start + i] = (float) (io[ch][start + i] * outGain);
            }
        }
    }

    double sampleRate() const noexcept { return fs; }

private:
    static bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }
    static bool same (double a, double b) { return std::equal_to<double>() (a, b); }

    void smoothStep()
    {
        bool changed = false;
        for (int b = 0; b < NumBands; ++b)
        {
            auto& c = current.bands[(size_t) b];
            const auto& t = target.bands[(size_t) b];

            const double lf = std::log (c.freq), lt = std::log (std::max (1.0f, t.freq));
            double nf = lf + (lt - lf) * smoothK;
            if (near (nf, lt, 1.0e-5)) nf = lt;
            double ng = c.gainDb + (t.gainDb - c.gainDb) * smoothK;
            if (near (ng, t.gainDb, 1.0e-4)) ng = t.gainDb;
            const double lq = std::log (c.q), lqt = std::log (std::max (0.01f, t.q));
            double nq = lq + (lqt - lq) * smoothK;
            if (near (nq, lqt, 1.0e-5)) nq = lqt;

            const bool slopeChanged = c.slope != t.slope;
            if (! same (nf, lf) || ! same (ng, c.gainDb) || ! same (nq, lq) || slopeChanged || c.on != t.on)
            {
                c.freq = (float) std::exp (nf);
                c.gainDb = (float) ng;
                c.q = (float) std::exp (nq);
                c.slope = t.slope;
                c.on = t.on;
                rebuild (b, slopeChanged);
                changed = true;
            }
        }
        (void) changed;
    }

    void rebuildAll (bool resetState)
    {
        for (int b = 0; b < NumBands; ++b) rebuild (b, resetState);
    }

    void rebuild (int b, bool resetState)
    {
        const auto& p = current.bands[(size_t) b];
        const auto sections = bandSections (b, p, fs);
        const int count = std::min ((int) sections.size(), maxSections);
        if (count != sectionCount[(size_t) b]) resetState = true;
        sectionCount[(size_t) b] = count;
        const bool identity = ! isPass (b) && std::abs (p.gainDb) < 1.0e-6;
        if (identity && ! isIdentity[(size_t) b]) resetState = true; // settled at 0 dB: clear leftovers
        isIdentity[(size_t) b] = identity;
        for (auto& ch : filters[(size_t) b])
            for (int s = 0; s < count; ++s)
            {
                ch[(size_t) s].c = sections[(size_t) s];
                if (resetState) ch[(size_t) s].reset();
            }
    }

    double fs = 44100.0;
    int channels = 2;
    double smoothK = 0.1, outK = 0.001, outGain = 1.0;
    Params target = defaultParams(), current = defaultParams();
    std::array<double, NumBands> passOn {};
    std::array<int, NumBands> sectionCount {};
    std::array<bool, NumBands> isIdentity {};
    std::array<std::array<std::array<lbaudio::dsp::Biquad, maxSections>, maxChannels>, NumBands> filters {};
};
} // namespace carve
