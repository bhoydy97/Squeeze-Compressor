#pragma once

// Spread: stereo width. Plain C++ shared by the audio code, the display and the tests.
//
// The signal is split into mid (what's the same on both sides) and side (the difference):
//   M = (L + R) / 2, S = (L - R) / 2,  then L = M + S * width, R = M - S * width
// Width 0% is mono, 100% leaves the image alone, 200% doubles the side.
// Bass Mono high-passes the side (4th-order Butterworth), so everything below that frequency is centred: tight,
// phase-safe low end, however wide the top. Mono folds the output to M on both sides, to check how it collapses.

#include <LBAudio/Biquad.h>
#include <array>
#include <atomic>
#include <cmath>

namespace spread
{
inline constexpr float bassMonoOff = 20.0f; // the bottom of the Bass Mono range means off

struct Params
{
    float width = 1.0f;          // 0..2
    float bassMonoHz = bassMonoOff;
    float balance = 0.0f;        // -1 (left) .. +1 (right)
    float outputDb = 0.0f;
    bool mono = false;
};

inline bool bassMonoOn (const Params& p) { return p.bassMonoHz > bassMonoOff + 0.5f; }

inline lbaudio::dsp::Coeffs bassMonoSection (float hz, double fs, int section)
{
    static const auto qs = lbaudio::dsp::butterworthQs (4);
    return lbaudio::dsp::design (lbaudio::dsp::Shape::HighPass, hz, 0.0, qs[(size_t) section], fs);
}

// Linear balance: the far side is turned down, the near side stays at unity
inline float balanceGain (float balance, bool right)
{
    return right ? (balance < 0.0f ? 1.0f + balance : 1.0f) : (balance > 0.0f ? 1.0f - balance : 1.0f);
}

// The last few thousand output samples, both sides, for the vectorscope (one writer, one reader).
class StereoScope
{
public:
    static constexpr int size = 1 << 13;
    void push (const float* l, const float* r, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { left[(size_t) w] = l[i]; right[(size_t) w] = r != nullptr ? r[i] : l[i]; w = (w + 1) & (size - 1); }
        writePos.store (w, std::memory_order_release);
    }
    void copyLatest (float* l, float* r, int n) const noexcept
    {
        int p = (writePos.load (std::memory_order_acquire) - n) & (size - 1);
        for (int i = 0; i < n; ++i) { l[i] = left[(size_t) p]; r[i] = right[(size_t) p]; p = (p + 1) & (size - 1); }
    }

private:
    std::array<float, size> left {}, right {};
    std::atomic<int> writePos { 0 };
};
} // namespace spread
