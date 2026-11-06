#pragma once

// Orbit: stereo chorus. Plain C++ shared by the audio code, the display and the tests.
//
// Each side has a delay line read by 1-4 voices. Voice v sits on its own phase of one slow LFO (v / voices of a
// turn apart), with its own base delay (Delay x (1 + 0.12 v)), swinging by up to +-4 ms x Depth. The right
// side's voices run Width x a quarter turn behind the left, which is what spreads the chorus across the
// stereo field; at Width 0 a mono input stays exactly mono.
// The voices are summed at 1/sqrt(voices), through Tone (a low-pass on the chorus only), and Feedback sends
// some of the chorus back into the delay lines for a richer, more flanged sound.

#include <LBAudio/Biquad.h>
#include <LBAudio/DelayLine.h>
#include <algorithm>
#include <cmath>

namespace orbit
{
inline constexpr int maxVoices = 4;
inline constexpr double maxDepthMs = 4.0, maxDelayMs = 30.0;

struct Params
{
    float rateHz = 0.6f;
    float depth = 0.5f;      // 0..1
    int voices = 3;          // 1..4
    float delayMs = 12.0f;
    float width = 0.8f;      // 0..1
    float feedback = 0.0f;   // 0..0.7
    float toneHz = 9000.0f;
    float mix = 0.5f;
};

inline double voiceBaseMs (const Params& p, int v) { return p.delayMs * (1.0 + 0.12 * v); }

// A voice's delay (ms) at an LFO phase (turns): never closer than 1 ms
inline double voiceDelayMs (const Params& p, int v, double phase)
{
    const double base = voiceBaseMs (p, v);
    const double depth = std::min (maxDepthMs * p.depth, base - 1.0);
    return base + depth * std::sin (2.0 * 3.14159265358979323846 * phase);
}

inline double voicePhase (const Params& p, double lfoPhase, int v, bool right)
{
    double ph = lfoPhase + (double) v / (double) std::max (1, p.voices) + (right ? 0.25 * p.width : 0.0);
    return ph - std::floor (ph);
}

inline lbaudio::dsp::Coeffs tone (float hz, double fs) { return lbaudio::dsp::design (lbaudio::dsp::Shape::LowPass, std::min ((double) hz, fs * 0.45), 0.0, 0.7071, fs); }

inline float softLimit (float x) noexcept
{
    const float a = std::abs (x);
    return a <= 1.0f ? x : std::copysign (1.0f + 0.5f * std::tanh (2.0f * (a - 1.0f)), x);
}
} // namespace orbit
