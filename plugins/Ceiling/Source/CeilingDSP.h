#pragma once

// Ceiling: a lookahead brickwall limiter with true-peak detection.
//
// For each sample the detector works out the gain needed to keep it under the ceiling. That requirement is held
// for the lookahead time (sliding minimum), released smoothly, then averaged over the lookahead time (box filter).
// Because every value in the average is at or below the requirement, the gain has fully arrived by the time the
// peak leaves the delay line: no overshoot, and no clipping to do the work.
//
// True peak: between every pair of samples the signal is estimated two ways, and the larger is used:
//   - 8x interpolation (Kaiser-windowed sinc, 32 taps), close to the real analogue peak, and
//   - the 4x filter that ITU-R BS.1770 true-peak meters use, so those meters read at or under the ceiling.
// Peaks that fall between samples are the ones that clip in converters and lossy encoders.
//
// Plain C++ (no JUCE) so the tests and the display can use it too.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ceiling
{
constexpr double lookaheadMs = 2.0;
constexpr int tpPhases = 8, tpTaps = 32, tpDelay = tpTaps / 2; // detection needs 16 samples of the future

// ITU-R BS.1770-4 Annex 2: 4x true-peak interpolation filter (4 phases x 12 taps). Its outputs fall 1/8, 3/8,
// 5/8 and 7/8 of the way between input samples 5 and 6 of each 12-sample window.
inline constexpr double ituTaps[4][12] = {
    {  0.0017089843750,  0.0109863281250, -0.0196533203125,  0.0332031250000, -0.0594482421875,  0.1373291015625,
       0.9721679687500, -0.1022949218750,  0.0476074218750, -0.0266113281250,  0.0148925781250, -0.0083007812500 },
    { -0.0291748046875,  0.0292968750000, -0.0517578125000,  0.0891113281250, -0.1665039062500,  0.4650878906250,
       0.7797851562500, -0.2003173828125,  0.1015625000000, -0.0582275390625,  0.0330810546875, -0.0189208984375 },
    { -0.0189208984375,  0.0330810546875, -0.0582275390625,  0.1015625000000, -0.2003173828125,  0.7797851562500,
       0.4650878906250, -0.1665039062500,  0.0891113281250, -0.0517578125000,  0.0292968750000, -0.0291748046875 },
    { -0.0083007812500,  0.0148925781250, -0.0266113281250,  0.0476074218750, -0.1022949218750,  0.9721679687500,
       0.1373291015625, -0.0594482421875,  0.0332031250000, -0.0196533203125,  0.0109863281250,  0.0017089843750 } };
constexpr int ituTapCount = 12, ituCentre = 5; // window x[c-5] .. x[c+6] covers the gap between c and c+1

struct Params
{
    float gainDb = 0.0f;      // 0..24
    float ceilingDb = -1.0f;  // -12..0 dBFS
    float releaseMs = 80.0f;  // 1..1000
    bool truePeak = true;
};

inline int lookaheadSamples (double fs) { return std::max (2, (int) std::ceil (lookaheadMs * 0.001 * fs)); }
inline int latencySamples (double fs)   { return lookaheadSamples (fs) - 1 + tpDelay; }

//==============================================================================
// 8x interpolation filters: phase k estimates the signal k/8 of a sample after x[c], from x[c-15] .. x[c+16].
struct TruePeakTable
{
    std::array<std::array<float, tpTaps>, tpPhases> taps {};

    TruePeakTable()
    {
        auto besselI0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
            return sum;
        };
        constexpr double beta = 8.0, halfSpan = tpTaps / 2 + 0.5, pi = 3.14159265358979323846;
        for (int k = 1; k < tpPhases; ++k)
        {
            double sum = 0.0;
            std::array<double, tpTaps> h {};
            for (int t = 0; t < tpTaps; ++t)
            {
                const double x = (double) (t - (tpDelay - 1)) - (double) k / tpPhases; // position relative to the point
                const double sinc = std::sin (pi * x) / (pi * x);
                const double r = x / halfSpan;
                const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
                h[(size_t) t] = sinc * window;
                sum += h[(size_t) t];
            }
            for (int t = 0; t < tpTaps; ++t) taps[(size_t) k][(size_t) t] = (float) (h[(size_t) t] / sum);
        }
    }
};

inline const TruePeakTable& truePeakTable() { static const TruePeakTable table; return table; }

// Push samples in; get back the sample from 16 pushes ago and the largest |value| between it and the next sample.
class TruePeakDetector
{
public:
    void reset() { history.fill (0.0f); pos = 0; }

    struct Result { float sample, peak; };

    Result push (float x)
    {
        history[(size_t) pos] = history[(size_t) (pos + tpTaps)] = x;
        pos = (pos + 1) % tpTaps;
        const float* w = &history[(size_t) pos]; // w[0] oldest ... w[31] newest; w[15] is the centre sample c
        const float centre = w[tpDelay - 1];
        float peak = std::abs (centre);

        const auto& table = truePeakTable();
        for (int k = 1; k < tpPhases; ++k)
        {
            float v = 0.0f;
            for (int t = 0; t < tpTaps; ++t) v += w[t] * table.taps[(size_t) k][(size_t) t];
            peak = std::max (peak, std::abs (v));
        }

        const float* m = w + (tpDelay - 1 - ituCentre); // x[c-5] .. x[c+6]
        for (const auto& phase : ituTaps)
        {
            double v = 0.0;
            for (int t = 0; t < ituTapCount; ++t) v += m[t] * phase[t];
            peak = std::max (peak, (float) std::abs (v));
        }
        return { centre, peak };
    }

private:
    std::array<float, 2 * tpTaps> history {};
    int pos = 0;
};

//==============================================================================
// Minimum of the last 'length' values, amortised O(1)
class SlidingMin
{
public:
    void setLength (int n) { length = n; values.assign ((size_t) n + 1, 0.0); stamps.assign ((size_t) n + 1, 0); reset(); }
    void reset() { head = tail = 0; counter = 0; }

    double push (double v)
    {
        const int cap = (int) values.size();
        while (head != tail && values[(size_t) ((tail - 1 + cap) % cap)] >= v) tail = (tail - 1 + cap) % cap;
        values[(size_t) tail] = v;
        stamps[(size_t) tail] = counter;
        tail = (tail + 1) % cap;
        if (stamps[(size_t) head] <= counter - length) head = (head + 1) % cap;
        ++counter;
        return values[(size_t) head];
    }

private:
    std::vector<double> values;
    std::vector<int64_t> stamps;
    int length = 1, head = 0, tail = 0;
    int64_t counter = 0;
};

// Mean of the last 'length' values; the running sum is recomputed every lap so it can't drift
class BoxAverage
{
public:
    void setLength (int n) { buffer.assign ((size_t) n, 1.0); reset (1.0); }
    void reset (double value) { std::fill (buffer.begin(), buffer.end(), value); sum = value * (double) buffer.size(); pos = 0; }

    double push (double v)
    {
        sum += v - buffer[(size_t) pos];
        buffer[(size_t) pos] = v;
        if (++pos == (int) buffer.size())
        {
            pos = 0;
            sum = 0.0;
            for (double b : buffer) sum += b;
        }
        return sum / (double) buffer.size();
    }

private:
    std::vector<double> buffer;
    double sum = 0.0;
    int pos = 0;
};

//==============================================================================
class Limiter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lookahead = lookaheadSamples (fs);
        latency = latencySamples (fs);
        for (auto& d : delay) d.assign ((size_t) latency + 1, 0.0f);
        holdMin.setLength (lookahead);
        smooth.setLength (lookahead);
        gainCoeff = 1.0 - std::exp (-1.0 / (0.02 * fs));
        inputGain = targetInputGain ();
        reset();
    }

    void reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        delayPos = 0;
        for (auto& det : detectors) det.reset();
        holdMin.reset();
        smooth.reset (1.0);
        released = 1.0;
        previousPeak = 0.0f;
        inputGain = targetInputGain();
    }

    void setParams (const Params& p)
    {
        params = p;
        ceilingGain = std::pow (10.0, std::min (0.0, (double) p.ceilingDb) / 20.0);
        releaseCoeff = 1.0 - std::exp (-1.0 / (std::max (0.1, (double) p.releaseMs) * 0.001 * fs));
    }

    int getLatency() const { return latency; }

    // What happened in the last process() call, for meters
    struct Stats { float drivenPeak = 0.0f, outputPeak = 0.0f, minGain = 1.0f; int clampedSamples = 0; };
    const Stats& lastStats() const { return stats; }

    // In place; right = nullptr for mono
    void process (float* left, float* right, int numSamples)
    {
        stats = {};
        const int channels = right != nullptr ? 2 : 1;
        float* io[2] = { left, right };
        const double target = targetInputGain();
        const float ceilingF = (float) ceilingGain;

        for (int i = 0; i < numSamples; ++i)
        {
            inputGain += (target - inputGain) * gainCoeff;

            // Detect: the largest (true or sample) peak around the sample 16 pushes ago
            float peakNow = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const float x = (float) (io[ch][i] * inputGain);
                delay[(size_t) ch][(size_t) delayPos] = x;
                stats.drivenPeak = std::max (stats.drivenPeak, std::abs (x));
                const auto r = detectors[(size_t) ch].push (x);
                peakNow = std::max (peakNow, params.truePeak ? r.peak : std::abs (r.sample));
            }
            // With true peak on, a sample also answers for the inter-sample peak just before it, so the gain
            // covers both ends of every interval
            const float peak = params.truePeak ? std::max (peakNow, previousPeak) : peakNow;
            previousPeak = peakNow;

            // (Using the float ceiling keeps the rounded output at or under it)
            const double required = peak > ceilingF ? (double) ceilingF / (double) peak : 1.0;
            const double held = holdMin.push (required);
            released = held < released ? held : released + (held - released) * releaseCoeff;
            double gain = smooth.push (released);
            if (gain > 1.0 - 1.0e-9) gain = 1.0;
            stats.minGain = std::min (stats.minGain, (float) gain);

            // Apply to the delayed audio: 'latency' samples old, lined up with the gain
            const int readPos = (delayPos + 1) % (latency + 1);
            for (int ch = 0; ch < channels; ++ch)
            {
                float y = (float) (delay[(size_t) ch][(size_t) readPos] * gain);
                if (std::abs (y) > ceilingF)
                {
                    // Only float rounding or a sudden ceiling change can get here
                    ++stats.clampedSamples;
                    y = std::clamp (y, -ceilingF, ceilingF);
                }
                io[ch][i] = y;
                stats.outputPeak = std::max (stats.outputPeak, std::abs (y));
            }
            delayPos = readPos;
        }
    }

private:
    double targetInputGain() const { return std::pow (10.0, std::clamp ((double) params.gainDb, 0.0, 48.0) / 20.0); }

    double fs = 48000.0;
    Params params;
    int lookahead = 96, latency = 103, delayPos = 0;
    std::array<std::vector<float>, 2> delay;
    std::array<TruePeakDetector, 2> detectors;
    SlidingMin holdMin;
    BoxAverage smooth;
    double released = 1.0, ceilingGain = 1.0, releaseCoeff = 0.001, inputGain = 1.0, gainCoeff = 0.001;
    float previousPeak = 0.0f;
    Stats stats;
};

//==============================================================================
// Max-hold true-peak meter to ITU-R BS.1770-4 (the 4x filter above), for the output readout
class TruePeakMeter
{
public:
    void reset() { for (auto& h : history) h.fill (0.0f); pos = 0; maximum = 0.0f; }
    void resetPeak() { maximum = 0.0f; }
    float peak() const { return maximum; }

    void process (const float* left, const float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            history[0][(size_t) pos] = history[0][(size_t) (pos + ituTapCount)] = left[i];
            history[1][(size_t) pos] = history[1][(size_t) (pos + ituTapCount)] = right != nullptr ? right[i] : 0.0f;
            pos = (pos + 1) % ituTapCount;
            for (int ch = 0; ch < (right != nullptr ? 2 : 1); ++ch)
            {
                const float* w = &history[(size_t) ch][(size_t) pos];
                for (const auto& phase : ituTaps)
                {
                    double v = 0.0;
                    for (int t = 0; t < ituTapCount; ++t) v += w[t] * phase[t];
                    maximum = std::max (maximum, (float) std::abs (v));
                }
            }
        }
    }

private:
    std::array<std::array<float, 2 * ituTapCount>, 2> history {};
    int pos = 0;
    float maximum = 0.0f;
};
} // namespace ceiling
