#pragma once

// Delay line with 4-point Hermite interpolation, shared by LBAudio plugins (Drift, Orbit), so a moving delay
// time (wobble, chorus, glides) stays smooth.

#include <algorithm>
#include <cmath>
#include <vector>

namespace lbaudio::dsp
{
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 8) n <<= 1;
        buffer.assign ((size_t) n, 0.0f);
        mask = n - 1; write = 0;
    }
    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); write = 0; }
    void push (float x) noexcept { buffer[(size_t) write] = x; write = (write + 1) & mask; }

    // The sample written 'delay' samples before the most recent one (delay >= 1).
    float read (double delay) const noexcept
    {
        const double pos = (double) (write - 1) - delay;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

private:
    float at (int i) const noexcept { return buffer[(size_t) (i & mask)]; }
    std::vector<float> buffer;
    int mask = 0, write = 0;
};
} // namespace lbaudio::dsp
