#pragma once

// Haze reverb engine: pre-delay -> low cut -> 4-step multichannel diffuser -> 8-line feedback delay network.
//
// - The diffuser spreads each input into dense early echoes. Every step is a set of delays followed by an
//   orthogonal (Hadamard) mix and polarity flips, so it adds no colour and no gain.
// - The feedback network uses a Householder matrix (lossless) with a gain on each line set from the line's
//   length, so the tail falls 60 dB in exactly the Decay time whatever the Size.
// - Damping is a 2nd-order high shelf in each loop, centred on the damping frequency: below it the tail decays
//   at the Decay time, above it the decay is 'hfDecayRatio' times as long. Unlike a plain low-pass in the loop,
//   the damping sounds the same at every Size and Decay setting, and the lows keep their decay time.
// - Lines are modulated by slow, unrelated LFOs and read with cubic interpolation, which breaks up metallic
//   ringing without audible chorus.
//
// Plain C++ (no JUCE), so the tests and the display can use it too.

#include <LBAudio/Biquad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace haze
{
constexpr int numLines = 8;
constexpr int numDiffusionSteps = 4;
constexpr double hfDecayRatio = 0.25;     // above the damping frequency the tail is this fraction as long
constexpr double maxPreDelayMs = 200.0;
constexpr double maxModDepthMs = 1.0;

struct Params
{
    float mix = 0.25f;          // 0..1
    float preDelayMs = 10.0f;   // 0..200
    float size = 0.5f;          // 0..1
    float decayS = 2.0f;        // RT60, 0.2..20 s
    float dampingHz = 6000.0f;  // 1k..20k
    float lowCutHz = 100.0f;    // 20..1000
    float width = 1.0f;         // 0..2
    float modulation = 0.3f;    // 0..1
};

// Size 0..1 -> nominal room delay in ms (15..150 ms, log scaled)
inline double roomMs (double size) { return 15.0 * std::pow (10.0, std::clamp (size, 0.0, 1.0)); }

// Line lengths spread exponentially over one octave of the room delay; the offsets are fixed "random"
// values so no two lines share a common factor.
inline double lineFactor (int i)
{
    static constexpr double jitter[numLines] = { 0.13, 0.71, 0.42, 0.88, 0.27, 0.59, 0.05, 0.93 };
    return std::pow (2.0, ((double) i + jitter[i]) / numLines);
}

inline double lfoRateHz (int i) { return 0.13 + 0.11 * i; }

// Per-pass gain for a line of 'delaySamples' so energy falls 60 dB in rt60 seconds
inline double loopGain (double delaySamples, double rt60, double fs)
{
    return std::pow (10.0, -3.0 * delaySamples / (std::max (0.01, rt60) * fs));
}

// In-place 8-point Hadamard transform, scaled to be orthonormal
inline void hadamard (float* x)
{
    for (int h = 1; h < numLines; h *= 2)
        for (int i = 0; i < numLines; i += 2 * h)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
    constexpr float scale = 0.35355339059327373f; // 1/sqrt(8)
    for (int i = 0; i < numLines; ++i) x[i] *= scale;
}

// In-place Householder reflection I - 2/N * 11^T (orthogonal, every line feeds every other)
inline void householder (float* x)
{
    float sum = 0.0f;
    for (int i = 0; i < numLines; ++i) sum += x[i];
    sum *= -2.0f / numLines;
    for (int i = 0; i < numLines; ++i) x[i] += sum;
}

class DelayLine
{
public:
    void setMaxDelay (int samples)
    {
        int size = 1;
        while (size < samples + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }

    void push (float x) { pos = (pos + 1) & mask; buffer[(size_t) pos] = x; }

    // The sample pushed 'k' pushes ago (k = 0 is the newest)
    float at (int k) const { return buffer[(size_t) ((pos - k) & mask)]; }

    float readLinear (double delay) const
    {
        const int k = (int) delay;
        const float f = (float) (delay - k);
        const float a = at (k), b = at (k + 1);
        return a + f * (b - a);
    }

    // 4-point cubic (Catmull-Rom) read, 'delay' >= 1
    float readCubic (double delay) const
    {
        const int k = (int) delay;
        const float f = (float) (delay - k);
        const float xm1 = at (k - 1), x0 = at (k), x1 = at (k + 1), x2 = at (k + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buffer;
    int mask = 0, pos = 0;
};

// Values that glide towards a target, updated once per sub-block and ramped per sample inside it
struct Ramp
{
    double current = 0.0, target = 0.0, step = 0.0;
    void snap (double v) { current = target = v; step = 0.0; }
};

class Reverb
{
public:
    static constexpr int subBlock = 32;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxRoom = (int) std::ceil (roomMs (1.0) * 0.001 * fs);
        preDelay[0].setMaxDelay ((int) std::ceil (maxPreDelayMs * 0.001 * fs) + 2);
        preDelay[1].setMaxDelay ((int) std::ceil (maxPreDelayMs * 0.001 * fs) + 2);
        for (auto& step : diffusion)
            for (auto& line : step) line.setMaxDelay (maxRoom + 4);
        for (auto& line : lines) line.setMaxDelay (2 * maxRoom + (int) std::ceil (maxModDepthMs * 0.001 * fs) + 8);
        smoothing = 1.0 - std::exp (-(double) subBlock / (0.05 * fs));
        sizeSmoothing = 1.0 - std::exp (-(double) subBlock / (0.25 * fs));
        snapToTargets = true;
        reset();
    }

    void reset()
    {
        for (auto& d : preDelay) d.clear();
        for (auto& step : diffusion) for (auto& line : step) line.clear();
        for (auto& line : lines) line.clear();
        for (auto& b : lowCut) b.reset();
        for (auto& b : damping) b.reset();
        lfoPhase.fill (0.0);
        for (int i = 0; i < numLines; ++i) lfoPhase[(size_t) i] = 0.37 * i; // spread the starting phases
        counter = 0;
    }

    void setParams (const Params& p) { params = p; }

    // In place. Pass right = nullptr for mono (the wet signal is summed back to one channel).
    void process (float* left, float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (counter == 0) updateSubBlock();
            counter = (counter + 1) % subBlock;
            float outL, outR;
            processSample (left[i], right != nullptr ? right[i] : left[i], outL, outR);
            if (right != nullptr) { left[i] = outL; right[i] = outR; }
            else left[i] = 0.5f * (outL + outR);
        }
    }

private:
    void processSample (float inL, float inR, float& outL, float& outR)
    {
        // Pre-delay and low cut
        preDelay[0].push (inL);
        preDelay[1].push (inR);
        preDelayMs.current += preDelayMs.step;
        const double pd = preDelayMs.current * 0.001 * fs;
        const float l = lowCut[0].process (preDelay[0].readLinear (pd));
        const float r = lowCut[1].process (preDelay[1].readLinear (pd));

        // Spread to 8 channels (energy preserving), then diffuse
        std::array<float, numLines> x;
        for (int c = 0; c < numLines; ++c) x[(size_t) c] = 0.5f * ((c & 1) ? r : l);

        room.current += room.step;
        const double roomSamples = room.current * 0.001 * fs;
        double range = roomSamples;
        for (int s = 0; s < numDiffusionSteps; ++s)
        {
            range *= 0.5;
            auto& step = diffusion[(size_t) s];
            for (int c = 0; c < numLines; ++c)
            {
                step[(size_t) c].push (x[(size_t) c]);
                x[(size_t) c] = step[(size_t) c].readLinear (1.0 + range * ((double) c + diffusionOffset (s, c)) / numLines);
            }
            hadamard (x.data());
            for (int c = 0; c < numLines; ++c)
                if (flips (s, c)) x[(size_t) c] = -x[(size_t) c];
        }

        // Feedback network: read, damp, mix, write back with the new input
        modDepth.current += modDepth.step;
        std::array<float, numLines> y;
        for (int i = 0; i < numLines; ++i)
        {
            auto& lfo = lfoValue[(size_t) i];
            lfo.current += lfo.step;
            const double d = roomSamples * lineFactor (i) + modDepth.current * lfo.current;
            y[(size_t) i] = lines[(size_t) i].readCubic (std::max (2.0, d - 1.0));
        }

        std::array<float, numLines> fb;
        for (int i = 0; i < numLines; ++i)
            fb[(size_t) i] = damping[(size_t) i].process (y[(size_t) i]);
        householder (fb.data());
        for (int i = 0; i < numLines; ++i)
            lines[(size_t) i].push (fb[(size_t) i] + x[(size_t) i]);

        // Down to stereo: early (diffused) + late (network), then width and mix
        float wetL = 0.0f, wetR = 0.0f;
        for (int i = 0; i < numLines; i += 2)
        {
            wetL += earlyGain * x[(size_t) i]     + lateGain * y[(size_t) i];
            wetR += earlyGain * x[(size_t) i + 1] + lateGain * y[(size_t) i + 1];
        }

        width.current += width.step;
        const float mid = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * (float) width.current;

        dryGain.current += dryGain.step;
        wetGain.current += wetGain.step;
        outL = (float) dryGain.current * inL + (float) wetGain.current * (mid + side);
        outR = (float) dryGain.current * inR + (float) wetGain.current * (mid - side);
    }

    void updateSubBlock()
    {
        const auto& p = params;
        const double n = subBlock;

        // Targets
        const double roomTarget = roomMs (p.size);
        const double mixTarget = std::clamp ((double) p.mix, 0.0, 1.0);
        const double dryTarget = mixTarget >= 1.0 ? 0.0 : std::cos (mixTarget * lbaudio::dsp::pi * 0.5);
        const double wetTarget = mixTarget <= 0.0 ? 0.0 : std::sin (mixTarget * lbaudio::dsp::pi * 0.5);
        const double depthTarget = std::clamp ((double) p.modulation, 0.0, 1.0) * maxModDepthMs * 0.001 * fs;

        if (snapToTargets)
        {
            room.snap (roomTarget);
            preDelayMs.snap (std::clamp ((double) p.preDelayMs, 0.0, maxPreDelayMs));
            width.snap (p.width);
            dryGain.snap (dryTarget);
            wetGain.snap (wetTarget);
            modDepth.snap (depthTarget);
            decay = p.decayS; dampingHz = p.dampingHz; lowCutHz = p.lowCutHz;
            for (auto& b : lowCut) b.c = lbaudio::dsp::design (lbaudio::dsp::Shape::HighPass, lowCutHz, 0.0, 0.70710678, fs);
            for (int i = 0; i < numLines; ++i) lfoValue[(size_t) i].snap (std::sin (2.0 * lbaudio::dsp::pi * lfoPhase[(size_t) i]));
        }

        // Size glides slowly (a smooth pitch bend rather than clicks); everything else follows in ~50 ms.
        // A ramp that ended on its target lands there exactly, so e.g. Mix 0% stays bit-exact.
        auto glide = [n] (Ramp& r, double target, double coeff, bool logDomain)
        {
            if (same (r.target, target)) { r.current = target; r.step = 0.0; return; }
            double next = logDomain ? r.current * std::pow (target / r.current, coeff)
                                    : r.current + (target - r.current) * coeff;
            if (std::abs (next - target) < 1.0e-5 * std::max (1.0, std::abs (target))) next = target;
            r.target = next;
            r.step = (next - r.current) / n;
        };
        glide (room, roomTarget, sizeSmoothing, true);
        glide (preDelayMs, std::clamp ((double) p.preDelayMs, 0.0, maxPreDelayMs), smoothing, false);
        glide (width, std::clamp ((double) p.width, 0.0, 2.0), smoothing, false);
        glide (dryGain, dryTarget, smoothing, false);
        glide (wetGain, wetTarget, smoothing, false);
        glide (modDepth, depthTarget, smoothing, false);

        decay = snapToTargets ? (double) p.decayS : decay * std::pow (p.decayS / decay, smoothing);
        dampingHz = snapToTargets ? (double) p.dampingHz : dampingHz * std::pow (p.dampingHz / dampingHz, smoothing);
        const double newLowCut = snapToTargets ? (double) p.lowCutHz : lowCutHz * std::pow (p.lowCutHz / lowCutHz, smoothing);
        snapToTargets = false;

        if (std::abs (newLowCut - lowCutHz) > 1.0e-4 * lowCutHz)
        {
            lowCutHz = newLowCut;
            for (auto& b : lowCut) b.c = lbaudio::dsp::design (lbaudio::dsp::Shape::HighPass, lowCutHz, 0.0, 0.70710678, fs);
        }
        // Loop filters from the line lengths at the end of this sub-block: the gain that gives the Decay time
        // for the lows, and a shelf down to the gain that gives the shorter high-frequency decay
        const double roomSamples = room.target * 0.001 * fs;
        double meanGain = 0.0;
        for (int i = 0; i < numLines; ++i)
        {
            const double len = roomSamples * lineFactor (i);
            const double low = loopGain (len, decay, fs), high = loopGain (len, decay * hfDecayRatio, fs);
            auto c = lbaudio::dsp::design (lbaudio::dsp::Shape::HighShelf, std::min (dampingHz, 0.45 * fs),
                                           20.0 * std::log10 (high / low), 0.70710678, fs);
            c.b0 *= low; c.b1 *= low; c.b2 *= low;
            damping[(size_t) i].c = c;
            meanGain += low / numLines;
        }
        // Long decays build up more energy; take back half of that (in dB) so the knob is mostly about length
        lateGain = (float) std::pow (std::max (1.0e-6, 1.0 - meanGain * meanGain), 0.25);

        // LFOs, ramped linearly between sub-blocks
        for (int i = 0; i < numLines; ++i)
        {
            auto& ph = lfoPhase[(size_t) i];
            ph += lfoRateHz (i) * n / fs;
            ph -= std::floor (ph);
            auto& lfo = lfoValue[(size_t) i];
            lfo.target = std::sin (2.0 * lbaudio::dsp::pi * ph);
            lfo.step = (lfo.target - lfo.current) / n;
        }
    }

    static bool same (double a, double b) { return std::equal_to<double>() (a, b); }

    static double diffusionOffset (int step, int channel)
    {
        static constexpr double table[numDiffusionSteps][numLines] = {
            { 0.61, 0.17, 0.89, 0.34, 0.52, 0.08, 0.77, 0.45 },
            { 0.23, 0.68, 0.11, 0.95, 0.39, 0.84, 0.57, 0.02 },
            { 0.48, 0.91, 0.26, 0.63, 0.07, 0.72, 0.35, 0.86 },
            { 0.81, 0.04, 0.55, 0.29, 0.93, 0.41, 0.16, 0.67 } };
        return table[step][channel];
    }

    static bool flips (int step, int channel)
    {
        static constexpr unsigned char masks[numDiffusionSteps] = { 0x5a, 0xa3, 0x36, 0xc9 };
        return (masks[step] >> channel) & 1;
    }

    double fs = 48000.0;
    Params params;
    bool snapToTargets = true;
    int counter = 0;
    double smoothing = 0.01, sizeSmoothing = 0.002;

    std::array<DelayLine, 2> preDelay;
    std::array<lbaudio::dsp::Biquad, 2> lowCut;
    std::array<std::array<DelayLine, numLines>, numDiffusionSteps> diffusion;
    std::array<DelayLine, numLines> lines;

    Ramp room, preDelayMs, width, dryGain, wetGain, modDepth;
    std::array<Ramp, numLines> lfoValue;
    std::array<double, numLines> lfoPhase {};
    std::array<lbaudio::dsp::Biquad, numLines> damping;
    double decay = 2.0, dampingHz = 6000.0, lowCutHz = 100.0;
    float earlyGain = 0.5f, lateGain = 1.0f;
};

//==============================================================================
// For the display: where the tail sits at time t (seconds), in dB re its start
inline double tailDb (const Params& p, double t, bool highs)
{
    const double start = p.preDelayMs * 0.001;
    if (t < start) return -1000.0;
    const double rt = p.decayS * (highs ? hfDecayRatio : 1.0);
    return -60.0 * (t - start) / rt;
}
} // namespace haze
