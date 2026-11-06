#pragma once

// Hush: de-esser. Plain C++ shared by the audio code, the display and the tests.
//
// A detector listens to a band around Frequency (high-pass at f/1.3, low-pass at f*1.6). When that band goes
// over Threshold, Hush turns down by 3 dB for every 4 dB over, up to Range:
//   Split  only the top end is turned down. y = g * x + (1 - g) * low(x), where low is a 4th-order Butterworth
//          low-pass at f/1.5: below the split the two parts add back to the input, above it only g * x is left.
//          (A plain "subtract a turned-down high band" split partly cancels itself through the filter's phase.)
//   Wide   the whole signal is turned down: y = g * x
// With no reduction (g = 1) both pass the input bit for bit.

#include <LBAudio/Biquad.h>
#include <algorithm>
#include <cmath>

namespace hush
{
enum Mode { Split = 0, Wide, NumModes };

struct Params
{
    float frequencyHz = 6000.0f;
    float thresholdDb = -30.0f;
    float rangeDb = 10.0f;
    int mode = Split;
    bool listen = false;
};

inline constexpr float slope = 0.75f; // dB of reduction per dB over the threshold (4:1)

inline lbaudio::dsp::Coeffs detectHighPass (float f, double fs) { return lbaudio::dsp::design (lbaudio::dsp::Shape::HighPass, f / 1.3, 0.0, 0.7071, fs); }
inline lbaudio::dsp::Coeffs detectLowPass (float f, double fs)  { return lbaudio::dsp::design (lbaudio::dsp::Shape::LowPass, std::min (f * 1.6, fs * 0.45), 0.0, 0.7071, fs); }
// The two sections of the 4th-order split low-pass
inline lbaudio::dsp::Coeffs splitLowPass (float f, double fs, int section)
{
    static const auto qs = lbaudio::dsp::butterworthQs (4);
    return lbaudio::dsp::design (lbaudio::dsp::Shape::LowPass, f / 1.5, 0.0, qs[(size_t) section], fs);
}

// Reduction (dB, positive) for a detector level (dBFS)
inline float reductionDb (const Params& p, float detectorDb)
{
    return std::clamp ((detectorDb - p.thresholdDb) * slope, 0.0f, p.rangeDb);
}

// The detector band's response (dB) at a frequency: for the display
inline double detectorResponseDb (const Params& p, double hz, double fs)
{
    return lbaudio::dsp::magnitudeDb (detectHighPass (p.frequencyHz, fs), hz, fs)
         + lbaudio::dsp::magnitudeDb (detectLowPass (p.frequencyHz, fs), hz, fs);
}
} // namespace hush
