#pragma once

// Ember: saturator. Plain C++ curves and helpers shared by the audio code and the display.
//
//   Tape  symmetric tanh soft clip (odd harmonics, smooth)
//   Tube  biased tanh: asymmetric, adds even harmonics; DC removed afterwards
//   Clip  hard clip with a short rounded knee
//
// Every curve has a slope of exactly 1 at zero, so quiet signals pass at unity before drive.
// Auto gain picks the output trim that keeps a -12 dBFS sine at the same RMS level.

#include <LBAudio/Biquad.h>
#include <algorithm>
#include <cmath>

namespace ember
{
enum Mode { Tape = 0, Tube, Clip, NumModes };

constexpr float tubeBias = 0.25f;

inline float shape (int mode, float x) noexcept
{
    switch (mode)
    {
        case Tube:
        {
            static const float tb = std::tanh (tubeBias);
            static const float norm = 1.0f / (1.0f - tb * tb);
            return (std::tanh (x + tubeBias) - tb) * norm;
        }
        case Clip:
        {
            constexpr float knee = 0.2f; // rounded between 0.8 and 1.2
            const float u = std::abs (x);
            float y;
            if (u <= 1.0f - knee)      y = u;
            else if (u >= 1.0f + knee) y = 1.0f;
            else { const float d = u - (1.0f - knee); y = u - d * d / (4.0f * knee); }
            return std::copysign (y, x);
        }
        default:
            return std::tanh (x);
    }
}

// Output trim that keeps a -12 dBFS sine at the same RMS after driving it into the curve.
inline float autoGainFor (int mode, float driveDb)
{
    constexpr int points = 256;
    const double amp = std::pow (10.0, -12.0 / 20.0);
    const double drive = std::pow (10.0, driveDb / 20.0);
    double sumIn = 0.0, sumOut = 0.0, mean = 0.0;
    double ys[points];
    for (int i = 0; i < points; ++i)
    {
        const double x = amp * std::sin (2.0 * lbaudio::dsp::pi * (i + 0.5) / points);
        ys[i] = shape (mode, (float) (x * drive));
        mean += ys[i];
        sumIn += x * x;
    }
    mean /= points; // the DC blocker removes this part afterwards
    for (int i = 0; i < points; ++i) sumOut += (ys[i] - mean) * (ys[i] - mean);
    return (float) std::sqrt (sumIn / std::max (sumOut, 1.0e-20));
}

struct Params
{
    float driveDb  = 12.0f;
    int   mode     = Tape;
    float toneDb   = 0.0f;   // tilt: + brighter, - darker (pivot 1 kHz)
    float mix      = 1.0f;   // 0..1
    float outputDb = 0.0f;
    bool  autoGain = true;
};

// The tilt filter: low shelf at -tone/2 and high shelf at +tone/2 around 1 kHz.
inline lbaudio::dsp::Coeffs tiltLow (float toneDb, double fs)  { return lbaudio::dsp::design (lbaudio::dsp::Shape::LowShelf,  1000.0, -toneDb * 0.5, 0.5, fs); }
inline lbaudio::dsp::Coeffs tiltHigh (float toneDb, double fs) { return lbaudio::dsp::design (lbaudio::dsp::Shape::HighShelf, 1000.0,  toneDb * 0.5, 0.5, fs); }

// Static input -> output curve as drawn on screen (linear amplitude).
inline float displayCurve (const Params& p, float x)
{
    const float drive = std::pow (10.0f, p.driveDb / 20.0f);
    const float trim = p.autoGain ? autoGainFor (p.mode, p.driveDb) : 1.0f;
    return shape (p.mode, x * drive) * trim;
}
} // namespace ember
