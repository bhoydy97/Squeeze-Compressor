#pragma once

// Drift: tempo-synced tape-style delay. Plain C++ shared by the audio code, the display and the tests.
//
//   Stereo     each side echoes itself
//   Ping-pong  the input (summed to mono) echoes left, right, left...
//
// Every pass through the loop goes through the Low Cut and High Cut filters, so each repeat is a little thinner
// and darker than the one before. Wobble moves the delay time slowly (wow) and quickly (flutter), like tape.
// Duck turns the echoes down while the input is playing and lets them back up in the gaps.

#include <LBAudio/Biquad.h>
#include <LBAudio/DelayLine.h>
#include <array>
#include <cmath>
#include <vector>

namespace drift
{
enum Mode { Stereo = 0, PingPong, NumModes };

struct Division { const char* name; double beats; };
inline const std::array<Division, 14>& divisions()
{
    static const std::array<Division, 14> d {{
        { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/16D", 0.375 },
        { "1/8", 0.5 }, { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 }, { "1/4", 1.0 }, { "1/2T", 4.0 / 3.0 },
        { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/2D", 3.0 }, { "1/1", 4.0 } }};
    return d;
}
inline constexpr int defaultDivision = 7; // 1/8D

inline constexpr double maxDelaySeconds = 4.0;

struct Params
{
    float timeMs = 375.0f;
    bool sync = true;
    int division = defaultDivision;
    float feedback = 0.4f;   // 0..0.95
    int mode = PingPong;
    float lowCutHz = 150.0f, highCutHz = 6000.0f;
    float wobble = 0.15f;    // 0..1
    float duckDb = 0.0f;     // most the echoes are turned down while the input plays
    float mix = 0.3f;
};

// The delay time in milliseconds for these settings at this tempo.
inline double delayMs (const Params& p, double bpm)
{
    if (! p.sync) return p.timeMs;
    const int i = std::clamp (p.division, 0, (int) divisions().size() - 1);
    return std::min (maxDelaySeconds * 1000.0, 60000.0 / std::max (20.0, bpm) * divisions()[(size_t) i].beats);
}

// The loop filters: Butterworth high-pass and low-pass.
inline lbaudio::dsp::Coeffs lowCut (float hz, double fs)  { return lbaudio::dsp::design (lbaudio::dsp::Shape::HighPass, hz, 0.0, 0.7071, fs); }
inline lbaudio::dsp::Coeffs highCut (float hz, double fs) { return lbaudio::dsp::design (lbaudio::dsp::Shape::LowPass, hz, 0.0, 0.7071, fs); }

// How far the echoes are turned down (dB) for an input envelope level (dBFS): none below -40 dBFS, all of Duck
// from -25 dBFS up. With the envelope's 100 ms release, the echoes come back within about 0.2-0.35 s of the
// input stopping.
inline float duckAmountDb (float duckDb, float envelopeDb)
{
    return duckDb * std::clamp ((envelopeDb + 40.0f) / 15.0f, 0.0f, 1.0f);
}

// Keeps the loop bounded: unchanged up to full scale, then a smooth curve (same slope at the join) that never
// passes 1.5.
inline float softLimit (float x) noexcept
{
    const float a = std::abs (x);
    return a <= 1.0f ? x : std::copysign (1.0f + 0.5f * std::tanh (2.0f * (a - 1.0f)), x);
}

using DelayLine = lbaudio::dsp::DelayLine;

// For the display: the first n echoes as (time ms, level dB relative to the input, side 0 = left / 1 = right,
// brightness 0..1: how much of the top end is left after the High Cut passes).
struct Echo { double ms; float db; int side; float brightness; };
inline std::vector<Echo> echoes (const Params& p, double bpm, int n, double fs = 48000.0)
{
    std::vector<Echo> out;
    const double t = delayMs (p, bpm);
    const auto hi = highCut (p.highCutHz, fs), lo = lowCut (p.lowCutHz, fs);
    const double passDb = lbaudio::dsp::magnitudeDb (hi, 1000.0, fs) + lbaudio::dsp::magnitudeDb (lo, 1000.0, fs);
    const double topDb = lbaudio::dsp::magnitudeDb (hi, 6000.0, fs) - lbaudio::dsp::magnitudeDb (hi, 1000.0, fs);
    for (int k = 1; k <= n; ++k)
    {
        const double fbDb = k > 1 ? 20.0 * std::log10 (std::max (1.0e-6f, p.feedback)) * (k - 1) : 0.0;
        const int side = p.mode == PingPong ? ((k - 1) % 2) : 0;
        const float bright = (float) std::clamp (1.0 + topDb * k / 24.0, 0.0, 1.0);
        out.push_back ({ t * k, (float) (fbDb + passDb * k), side, bright });
    }
    return out;
}
} // namespace drift
