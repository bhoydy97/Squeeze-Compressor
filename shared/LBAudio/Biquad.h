#pragma once

// Second-order filter sections (RBJ Audio EQ Cookbook designs) shared by LBAudio plugins.
// Plain C++: used by the audio code and by the displays, so what you see is what you hear.

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace lbaudio::dsp
{
constexpr double pi = 3.14159265358979323846;

struct Coeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // normalised so a0 = 1
};

enum class Shape { Bell, LowShelf, HighShelf, HighPass, LowPass };

inline Coeffs design (Shape shape, double freq, double gainDb, double q, double fs)
{
    freq = std::clamp (freq, 1.0, fs * 0.49);
    q = std::max (q, 0.025);
    const double w0 = 2.0 * pi * freq / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double A = std::pow (10.0, gainDb / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (shape)
    {
        case Shape::Bell:
        {
            const double alpha = sw / (2.0 * q);
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
            break;
        }
        case Shape::LowShelf:
        case Shape::HighShelf:
        {
            // q acts as the shelf's Q (0.707 = no overshoot)
            const double alpha = sw / (2.0 * q);
            const double s = 2.0 * std::sqrt (A) * alpha;
            if (shape == Shape::LowShelf)
            {
                b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;        a1 = -2 * ((A - 1) + (A + 1) * cw);    a2 = (A + 1) + (A - 1) * cw - s;
            }
            else
            {
                b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;        a1 = 2 * ((A - 1) - (A + 1) * cw);      a2 = (A + 1) - (A - 1) * cw - s;
            }
            break;
        }
        case Shape::HighPass:
        case Shape::LowPass:
        {
            const double alpha = sw / (2.0 * q);
            if (shape == Shape::HighPass) { b0 = (1 + cw) * 0.5; b1 = -(1 + cw); b2 = b0; }
            else                          { b0 = (1 - cw) * 0.5; b1 = 1 - cw;    b2 = b0; }
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        }
    }

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// Q values of the second-order sections that make an Nth-order Butterworth filter (N even).
inline std::vector<double> butterworthQs (int order)
{
    std::vector<double> qs;
    for (int k = 1; k <= order / 2; ++k)
        qs.push_back (1.0 / (2.0 * std::cos ((2.0 * k - 1.0) * pi / (2.0 * order))));
    return qs;
}

inline std::complex<double> response (const Coeffs& c, double freq, double fs)
{
    const auto z1 = std::polar (1.0, -2.0 * pi * freq / fs);
    const auto z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

inline double magnitudeDb (const Coeffs& c, double freq, double fs)
{
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (response (c, freq, fs))));
}

struct Biquad
{
    Coeffs c;
    double z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }

    float process (float xIn) noexcept // transposed direct form II
    {
        const double x = xIn;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }

    void snapDenormals() noexcept
    {
        if (std::abs (z1) < 1.0e-20) z1 = 0;
        if (std::abs (z2) < 1.0e-20) z2 = 0;
    }
};
} // namespace lbaudio::dsp
