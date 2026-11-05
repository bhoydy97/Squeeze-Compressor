#pragma once

// Lock-free audio feed from the audio thread to a display (spectrum analysers, scopes).
// One writer (audio thread), one reader (UI). The reader may occasionally see a block
// that is being overwritten; for a display that is harmless.

#include <array>
#include <atomic>

namespace lbaudio
{
class ScopeBuffer
{
public:
    static constexpr int size = 1 << 14;

    // Pushes the mono sum of up to two channels.
    void push (const float* left, const float* right, int numSamples) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            data[(size_t) w] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            w = (w + 1) & (size - 1);
        }
        writePos.store (w, std::memory_order_release);
    }

    // Copies the most recent numSamples (<= size) into dest, oldest first.
    void copyLatest (float* dest, int numSamples) const noexcept
    {
        const int w = writePos.load (std::memory_order_acquire);
        int r = (w - numSamples) & (size - 1);
        for (int i = 0; i < numSamples; ++i)
        {
            dest[i] = data[(size_t) r];
            r = (r + 1) & (size - 1);
        }
    }

private:
    std::array<float, size> data {};
    std::atomic<int> writePos { 0 };
};
} // namespace lbaudio
