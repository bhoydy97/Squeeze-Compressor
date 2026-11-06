#pragma once

// Snap: transient shaper. Plain C++ shared by the audio code, the display and the tests.
//
// Envelope followers compared in dB, so the shaping is the same at any level:
//   attack:  a fast peak follower, and a slow one that rises towards it over Speed but drops with it instantly.
//            On a hit the fast one jumps ahead; the gap (dB) says "this is the front of a hit". As soon as the
//            hit starts to fall the two meet again, so the tail is never counted as attack.
//   sustain: two fast-attack followers with a short and a long release. As a hit dies away the long one stays
//            up; the gap says "this is the tail". It is held off while a hit is still rising.
// Each gap, less a small dead zone (2 dB attack, 1 dB sustain, so the ripple of a steady low note isn't mistaken
// for a hit), becomes a 0..1 weight (1 - e^(-gap/6 dB)); the gain is Attack x attack weight + Sustain x sustain
// weight x (1 - attack weight), in dB. On a steady sound both gaps close and the gain is 0 dB.

#include <array>
#include <atomic>
#include <cmath>

namespace snap
{
struct Params
{
    float attackDb = 0.0f;   // -15..+15
    float sustainDb = 0.0f;  // -15..+15
    float speedMs = 15.0f;   // the slow follower's attack: how long the front of a hit counts as "attack"
    float outputDb = 0.0f;
    float mix = 1.0f;
};

inline constexpr double fastAttackMs = 0.3, attackPairReleaseMs = 120.0;
inline constexpr double sustainShortReleaseMs = 30.0, sustainLongReleaseMs = 300.0;

inline constexpr float attackDeadZoneDb = 2.0f, sustainDeadZoneDb = 1.0f;
inline float weight (float gapDb) { return gapDb > 0.0f ? 1.0f - std::exp (-gapDb / 6.0f) : 0.0f; }

inline float coeff (double ms, double fs) { return 1.0f - (float) std::exp (-1.0 / (ms * 0.001 * fs)); }

struct Follower
{
    float value = 0.0f, attack = 1.0f, release = 0.001f;
    float process (float x) noexcept { value += (x - value) * (x > value ? attack : release); return value; }
};

// Rises towards its input with 'attack', falls with it instantly
struct Riser
{
    float value = 0.0f, attack = 0.001f;
    float process (float x) noexcept { value = x > value ? value + (x - value) * attack : x; return value; }
};

inline float toDb (float v) { return v > 1.0e-7f ? 20.0f * std::log10 (v) : -140.0f; }

// One frame of the scrolling display: the input and output peaks and the gain applied, over ~2.5 ms
struct Frame { float in = 0.0f, out = 0.0f, gainDb = 0.0f; };

class FrameFeed
{
public:
    static constexpr int size = 2048;
    void push (const Frame& f) noexcept
    {
        const int w = writePos.load (std::memory_order_relaxed);
        frames[(size_t) w] = f;
        writePos.store ((w + 1) & (size - 1), std::memory_order_release);
    }
    void copyLatest (Frame* dest, int n) const noexcept
    {
        int p = (writePos.load (std::memory_order_acquire) - n) & (size - 1);
        for (int i = 0; i < n; ++i) { dest[i] = frames[(size_t) p]; p = (p + 1) & (size - 1); }
    }

private:
    std::array<Frame, size> frames {};
    std::atomic<int> writePos { 0 };
};
} // namespace snap
