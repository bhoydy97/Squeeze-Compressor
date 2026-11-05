#pragma once

// Loudness to ITU-R BS.1770-4 / EBU R128: K-weighting, momentary (400 ms), short-term (3 s) and gated integrated
// loudness in LUFS. Plain C++, so the tests can check it directly.

#include <LBAudio/Biquad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace lbaudio
{
// K-weighting for any sample rate, from the analogue prototypes behind the 48 kHz coefficients in BS.1770.
// Stage 1: the head's high-frequency boost (shelf). Stage 2: the RLB high-pass.
inline dsp::Coeffs kWeightingShelf (double fs)
{
    const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
    const double k = std::tan (dsp::pi * f0 / fs);
    const double vh = std::pow (10.0, gainDb / 20.0);
    const double vb = std::pow (vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    dsp::Coeffs c;
    c.b0 = (vh + vb * k / q + k * k) / a0;
    c.b1 = 2.0 * (k * k - vh) / a0;
    c.b2 = (vh - vb * k / q + k * k) / a0;
    c.a1 = 2.0 * (k * k - 1.0) / a0;
    c.a2 = (1.0 - k / q + k * k) / a0;
    return c;
}

inline dsp::Coeffs kWeightingHighPass (double fs)
{
    const double f0 = 38.13547087602444, q = 0.5003270373238773;
    const double k = std::tan (dsp::pi * f0 / fs);
    const double a0 = 1.0 + k / q + k * k;
    dsp::Coeffs c;
    c.b0 = 1.0; c.b1 = -2.0; c.b2 = 1.0;
    c.a1 = 2.0 * (k * k - 1.0) / a0;
    c.a2 = (1.0 - k / q + k * k) / a0;
    return c;
}

inline double energyToLufs (double meanSquare)
{
    return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -std::numeric_limits<double>::infinity();
}

class LoudnessMeter
{
public:
    static constexpr double absoluteGate = -70.0;  // LUFS
    static constexpr double relativeGate = -10.0;  // LU below the ungated level
    static constexpr double binWidth = 0.02;       // LU per histogram bin for the integrated measurement
    static constexpr double histogramTop = 10.0;   // LUFS

    void prepare (double sampleRate)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            shelf[(size_t) ch].c = kWeightingShelf (sampleRate);
            highPass[(size_t) ch].c = kWeightingHighPass (sampleRate);
        }
        blockLength = std::max (1, (int) std::lround (sampleRate * 0.1));
        histogramCount.assign ((size_t) std::ceil ((histogramTop - absoluteGate) / binWidth), 0);
        histogramEnergy.assign (histogramCount.size(), 0.0);
        reset();
    }

    void reset()
    {
        for (auto& f : shelf) f.reset();
        for (auto& f : highPass) f.reset();
        subBlocks.fill (0.0);
        subBlocksSeen = 0;
        accumulator = 0.0;
        inBlock = 0;
        momentary = shortTerm = -std::numeric_limits<double>::infinity();
        resetIntegrated();
    }

    void resetIntegrated()
    {
        std::fill (histogramCount.begin(), histogramCount.end(), 0);
        std::fill (histogramEnergy.begin(), histogramEnergy.end(), 0.0);
        integrated = -std::numeric_limits<double>::infinity();
        gatingBlocksSeen = 0;
    }

    // Pass right = nullptr for a mono signal
    void process (const float* left, const float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const double l = highPass[0].process (shelf[0].process (left[i]));
            double z = l * l;
            if (right != nullptr)
            {
                const double r = highPass[1].process (shelf[1].process (right[i]));
                z += r * r;
            }
            accumulator += z;
            if (++inBlock == blockLength) endSubBlock();
        }
        for (auto& f : shelf) f.snapDenormals();
        for (auto& f : highPass) f.snapDenormals();
    }

    double momentaryLufs() const  { return momentary; }
    double shortTermLufs() const  { return shortTerm; }
    double integratedLufs() const { return integrated; }

private:
    void endSubBlock()
    {
        subBlocks[(size_t) (subBlocksSeen % (long long) subBlocks.size())] = accumulator / blockLength;
        ++subBlocksSeen;
        accumulator = 0.0;
        inBlock = 0;

        auto meanOfLast = [this] (int count)
        {
            const int have = (int) std::min<long long> (count, subBlocksSeen);
            double sum = 0.0;
            for (int k = 0; k < have; ++k)
                sum += subBlocks[(size_t) ((subBlocksSeen - 1 - k) % (long long) subBlocks.size())];
            return sum / count;
        };

        const double m = meanOfLast (4);
        momentary = energyToLufs (m);
        shortTerm = energyToLufs (meanOfLast (30));

        // Each 400 ms block, every 100 ms (75% overlap), feeds the gated integrated measurement
        if (subBlocksSeen >= 4 && momentary >= absoluteGate)
        {
            const auto bin = (size_t) std::clamp ((int) ((momentary - absoluteGate) / binWidth), 0, (int) histogramCount.size() - 1);
            ++histogramCount[bin];
            histogramEnergy[bin] += m;
            ++gatingBlocksSeen;
            updateIntegrated();
        }
    }

    void updateIntegrated()
    {
        double energy = 0.0;
        long long count = 0;
        for (size_t b = 0; b < histogramCount.size(); ++b) { energy += histogramEnergy[b]; count += histogramCount[b]; }
        if (count == 0) return;
        const double threshold = energyToLufs (energy / (double) count) + relativeGate;

        energy = 0.0; count = 0;
        for (size_t b = 0; b < histogramCount.size(); ++b)
        {
            if (histogramCount[b] == 0) continue;
            // Each bin's blocks share (to within binWidth) one loudness: its mean
            const double binLoudness = energyToLufs (histogramEnergy[b] / histogramCount[b]);
            if (binLoudness > threshold) { energy += histogramEnergy[b]; count += histogramCount[b]; }
        }
        integrated = count > 0 ? energyToLufs (energy / (double) count) : -std::numeric_limits<double>::infinity();
    }

    std::array<dsp::Biquad, 2> shelf, highPass;
    int blockLength = 4800, inBlock = 0;
    double accumulator = 0.0;
    std::array<double, 30> subBlocks {};
    long long subBlocksSeen = 0, gatingBlocksSeen = 0;
    double momentary = 0.0, shortTerm = 0.0, integrated = 0.0;
    std::vector<long long> histogramCount;
    std::vector<double> histogramEnergy;
};
} // namespace lbaudio
