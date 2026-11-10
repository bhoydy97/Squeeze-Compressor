#pragma once

// Splinter: a sampler that chops one sample across 16 pads, plays the chops, and flips them into new patterns.
// Plain C++ shared by the audio code, the window and the tests (the FFT comes from juce_dsp).
//
// Analysis: a spectral-flux onset envelope (1024-point frames, 256 apart). "On the hits" takes up to 16 of its
// strongest peaks at least ~90 ms apart; "Even" cuts the sound into 16 equal slices; "Tap" uses points you place.
// Leading and trailing silence never become chops. Tempo and key come from the file name when it says (most packs
// do: "loop_110bpm_F_major.wav"), else from the sample: tempo by autocorrelating the onset envelope, key by
// matching a chromagram against the Krumhansl-Kessler profiles.
//
// Playback: up to 24 voices, each a stretch of the sample read at its pitch (Hermite interpolation), shaped by a
// linear attack and exponential release, through a resonant low-pass (TPT state-variable), a drive stage and its
// pad's era. A pad retriggering cuts itself; pads in the same choke group cut each other (4 ms fade).

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace splinter
{
constexpr int numPads = 16;
constexpr int baseNote = 36;                // pad 1 is C1 (36) ... pad 16 is D#2 (51)
constexpr int maxVoices = 24;
constexpr double pi = 3.14159265358979323846;

enum class ChopMode { Hits = 0, Even, Tap };
enum class Era { Clean = 0, Grit12, Vintage26k, Dusty };
inline const char* eraName (int e)
{
    static const char* n[] = { "Clean", "12-bit grit", "Vintage 26k", "Dusty" };
    return n[std::clamp (e, 0, 3)];
}
inline const char* noteName (int pad)
{
    static const char* n[] = { "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1", "C2", "C#2", "D2", "D#2" };
    return n[std::clamp (pad, 0, numPads - 1)];
}

//==============================================================================
// The sample, as loaded: stereo, at its own rate
struct Sample
{
    std::vector<float> l, r;
    double rate = 44100.0;
    int length() const { return (int) l.size(); }
    float mono (int i) const { return 0.5f * (l[(size_t) i] + r[(size_t) i]); }
};

// Where the sound starts and ends (first and last sample within 50 dB of the peak)
inline std::pair<int, int> soundSpan (const Sample& s)
{
    float peak = 0.0f;
    for (int i = 0; i < s.length(); ++i) peak = std::max (peak, std::abs (s.mono (i)));
    const float thr = peak * 0.00316f;
    int a = 0, b = s.length();
    while (a < b && std::abs (s.mono (a)) <= thr) ++a;
    while (b > a && std::abs (s.mono (b - 1)) <= thr) --b;
    return { a, b };
}

//==============================================================================
// Onsets
struct OnsetEnvelope
{
    std::vector<float> flux;   // one value per hop
    int hop = 256, frame = 1024;
    double rate = 44100.0;
    double frameTime (int k) const { return ((double) k * hop + frame * 0.5) / rate; }
};

inline OnsetEnvelope onsetEnvelope (const Sample& s)
{
    OnsetEnvelope env; env.rate = s.rate;
    constexpr int order = 10, n = 1 << order;
    env.frame = n;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) n), buf ((size_t) n * 2), prev ((size_t) n / 2 + 1, 0.0f);
    for (int i = 0; i < n; ++i) window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * pi * i / (n - 1));
    const int frames = std::max (0, (s.length() - n) / env.hop + 1);
    env.flux.assign ((size_t) frames, 0.0f);
    for (int k = 0; k < frames; ++k)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = s.mono (k * env.hop + i) * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        float f = 0.0f;
        for (int b = 1; b <= n / 2; ++b)
        {
            const float m = std::log1p (100.0f * buf[(size_t) b]);
            f += std::max (0.0f, m - prev[(size_t) b]);
            prev[(size_t) b] = m;
        }
        env.flux[(size_t) k] = k == 0 ? 0.0f : f;
    }
    return env;
}

// Where a hit really starts, near a flux peak: the steepest rise in 1 ms loudness, backed up to where the rise begins
inline int refineOnset (const Sample& s, int lo, int hi, int fallback)
{
    const int blk = std::max (16, (int) (0.001 * s.rate));
    lo = std::max (0, lo); hi = std::min (s.length(), hi);
    std::vector<float> e;
    for (int a = lo; a + blk <= hi; a += blk)
    {
        double q = 0.0;
        for (int i = a; i < a + blk; ++i) { const float v = s.mono (i); q += (double) v * v; }
        e.push_back ((float) std::sqrt (q / blk));
    }
    if (e.size() < 6) return fallback;
    int best = -1; float bestRise = 0.0f;
    auto floorBefore = [&] (size_t j) { float m = e[j - 1]; for (size_t k = j - std::min<size_t> (j, 4); k < j; ++k) m = std::min (m, e[k]); return m; };
    for (size_t j = 4; j < e.size(); ++j)
        if (const float r = e[j] - floorBefore (j); r > bestRise) { bestRise = r; best = (int) j; }
    if (best < 0) return fallback;
    const float base = floorBefore ((size_t) best);
    int j = best;
    while (j > 0 && e[(size_t) j - 1] > base + 0.1f * bestRise) --j;
    return std::max (0, lo + j * blk - blk);
}

// Up to maxChops chop points on the strongest hits, in samples, sorted. The first is where the sound starts.
inline std::vector<int> chopsOnHits (const Sample& s, const OnsetEnvelope& env, int maxChops = numPads, double minGapS = 0.09)
{
    const auto [start, end] = soundSpan (s);
    std::vector<int> chops;
    if (end - start < 64) return { 0 };
    const int nf = (int) env.flux.size();
    std::vector<std::pair<float, int>> peaks;   // flux, frame
    float strongest = 0.0f;
    for (int k = 2; k < nf - 2; ++k)
    {
        const float v = env.flux[(size_t) k];
        if (v <= 0.0f) continue;
        bool isMax = true;
        for (int j = std::max (0, k - 3); j <= std::min (nf - 1, k + 3); ++j)
            if (env.flux[(size_t) j] > v) { isMax = false; break; }
        if (! isMax) continue;
        // above the local median, so a dense passage still finds its own hits
        std::vector<float> win;
        for (int j = std::max (0, k - 16); j <= std::min (nf - 1, k + 16); ++j) win.push_back (env.flux[(size_t) j]);
        std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
        if (v < win[win.size() / 2] * 1.4f + 1e-3f) continue;
        peaks.push_back ({ v, k });
        strongest = std::max (strongest, v);
    }
    std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    const int minGap = (int) (minGapS * s.rate);
    chops.push_back (start);
    for (auto& [v, k] : peaks)
    {
        if ((int) chops.size() >= maxChops || v < 0.05f * strongest) break;   // ripples in a decay aren't hits
        const int pos = std::max (start, refineOnset (s, k * env.hop - env.hop, k * env.hop + env.frame, k * env.hop + env.frame / 2 - env.hop));
        if (pos >= end - (int) (0.03 * s.rate)) continue;
        bool clear = true;
        for (int c : chops) if (std::abs (c - pos) < minGap) { clear = false; break; }
        if (clear) chops.push_back (pos);
    }
    std::sort (chops.begin(), chops.end());
    return chops;
}

inline std::vector<int> chopsEven (const Sample& s, int n = numPads)
{
    const auto [start, end] = soundSpan (s);
    std::vector<int> c;
    for (int i = 0; i < n; ++i) c.push_back (start + (int) ((double) (end - start) * i / n));
    return c;
}

//==============================================================================
// Tempo and key
inline std::optional<double> tempoFromName (const std::string& name)
{
    static const std::regex re ("(\\d{2,3}(?:\\.\\d+)?)\\s*[-_ ]?\\s*bpm", std::regex::icase);
    std::smatch m;
    if (std::regex_search (name, m, re))
    {
        const double v = std::stod (m[1].str());
        if (v >= 40.0 && v <= 250.0) return v;
    }
    return std::nullopt;
}

inline std::optional<std::string> keyFromName (const std::string& name)
{
    // "F_major", "F#min", "Bb minor", "Am", "C-maj"
    static const std::regex re ("(?:^|[^A-Za-z])([A-G])(#|b|sharp|flat)?[\\s_-]*(major|minor|maj|min|m)(?![A-Za-z])", std::regex::icase);
    std::smatch m;
    std::string s = name;
    while (std::regex_search (s, m, re))
    {
        std::string root = m[1].str(), acc = m[2].str(), q = m[3].str();
        root[0] = (char) std::toupper ((unsigned char) root[0]);
        std::transform (acc.begin(), acc.end(), acc.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        std::transform (q.begin(), q.end(), q.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        const std::string a = acc == "#" || acc == "sharp" ? "#" : (acc == "b" || acc == "flat" ? "b" : "");
        const bool minor = q == "minor" || q == "min" || q == "m";
        // a lone "m" only counts straight after the note ("Am", not "A_m" in a word)
        if (q == "m" && m[3].first != m[2].second && m[3].first != m[1].second) { s = m.suffix(); continue; }
        return root + a + (minor ? " minor" : " major");
    }
    return std::nullopt;
}

// Beats per minute from the onset envelope, 70-180, by autocorrelation (with a mild preference for 85-160)
inline double detectTempo (const OnsetEnvelope& env)
{
    const int n = (int) env.flux.size();
    if (n < 32) return 0.0;
    std::vector<double> x ((size_t) n);
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += env.flux[(size_t) i];
    mean /= n;
    for (int i = 0; i < n; ++i) x[(size_t) i] = env.flux[(size_t) i] - mean;
    const double hopS = env.hop / env.rate;
    double best = 0.0, bestBpm = 0.0;
    for (double bpm = 70.0; bpm <= 180.0; bpm += 0.5)
    {
        const double lag = 60.0 / bpm / hopS;
        double acc = 0.0; int cnt = 0;
        for (int mult = 1; mult <= 4; ++mult)
        {
            const double L = lag * mult;
            const int li = (int) L; const double fr = L - li;
            if (li + 1 >= n) break;
            double s = 0.0;
            for (int i = 0; i + li + 1 < n; ++i) s += x[(size_t) i] * (x[(size_t) (i + li)] * (1 - fr) + x[(size_t) (i + li + 1)] * fr);
            acc += s / (n - li); ++cnt;
        }
        if (cnt == 0) continue;
        acc /= cnt;
        const double prefer = (bpm >= 85.0 && bpm <= 160.0) ? 1.0 : 0.85;
        if (acc * prefer > best) { best = acc * prefer; bestBpm = bpm; }
    }
    return bestBpm;
}

inline std::string detectKey (const Sample& s)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const double maj[] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mi[] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    constexpr int order = 13, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2);
    std::array<double, 12> chroma {};
    for (int at = 0; at + n <= s.length(); at += n / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = s.mono (at + i) * (0.5f - 0.5f * (float) std::cos (2 * pi * i / (n - 1)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int b = 1; b < n / 2; ++b)
        {
            const double hz = b * s.rate / n;
            if (hz < 60.0 || hz > 2000.0) continue;
            const int pc = ((int) std::lround (12.0 * std::log2 (hz / 440.0)) + 9 + 1200) % 12;
            chroma[(size_t) pc] += (double) buf[(size_t) b] * buf[(size_t) b];
        }
    }
    auto corr = [&] (const double* prof, int root)
    {
        double ma = 0, mb = 0;
        for (int i = 0; i < 12; ++i) { ma += prof[(i - root + 12) % 12]; mb += chroma[(size_t) i]; }
        ma /= 12; mb /= 12;
        double sab = 0, saa = 0, sbb = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = prof[(i - root + 12) % 12] - ma, b = chroma[(size_t) i] - mb;
            sab += a * b; saa += a * a; sbb += b * b;
        }
        return sab / std::sqrt (saa * sbb + 1e-30);
    };
    double best = -2; std::string key = "C major";
    for (int r = 0; r < 12; ++r)
    {
        if (const double c = corr (maj, r); c > best) { best = c; key = std::string (names[r]) + " major"; }
        if (const double c = corr (mi, r); c > best) { best = c; key = std::string (names[r]) + " minor"; }
    }
    return key;
}

//==============================================================================
// What kind of sound starts each chop, so Flip can put kick-like chops on the strong beats and snare-like ones on
// the backbeat: Low (kick), Snap (snare/clap), Tick (hat or quiet hit), Tone (anything sustained or melodic)
enum class Role { Low, Snap, Tick, Tone };

inline std::vector<Role> chopRoles (const Sample& s, const std::vector<int>& chops)
{
    std::vector<Role> roles;
    constexpr int order = 12, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2);
    float loudest = 0.0f;
    std::vector<float> peaks;
    for (size_t c = 0; c < chops.size(); ++c)
    {
        const int a = chops[c], len = std::min ((int) (0.09 * s.rate), s.length() - a);
        float p = 0.0f;
        for (int i = 0; i < len; ++i) p = std::max (p, std::abs (s.mono (a + i)));
        peaks.push_back (p); loudest = std::max (loudest, p);
    }
    for (size_t c = 0; c < chops.size(); ++c)
    {
        const int a = chops[c], len = std::min ({ (int) (0.09 * s.rate), n, s.length() - a });
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < len; ++i) buf[(size_t) i] = s.mono (a + i) * (0.5f - 0.5f * (float) std::cos (2 * pi * i / std::max (1, len - 1)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double low = 0, mid = 0, hi = 0, tot = 0;
        for (int b = 1; b < n / 2; ++b)
        {
            const double hz = b * s.rate / n, v = buf[(size_t) b];
            tot += v;
            if (hz < 150) low += v; else if (hz < 2000) mid += v; else if (hz > 4000) hi += v;
        }
        tot += 1e-12;
        const float rel = loudest > 0 ? peaks[c] / loudest : 0.0f;
        Role r = Role::Tone;
        if (low / tot > 0.18) r = Role::Low;
        else if (hi / tot > 0.5 && rel > 0.6) r = Role::Snap;
        else if (hi / tot > 0.45 && rel <= 0.6) r = Role::Tick;
        roles.push_back (r);
    }
    return roles;
}

//==============================================================================
// Flip: a pattern of 16th-note steps
struct Hit { int step = 0, pad = 0; float velocity = 1.0f; };
struct Pattern
{
    int steps = 32;            // 16 a bar
    std::vector<Hit> hits;
    std::vector<int> rows() const   // the pads it uses, in the order they first play
    {
        std::vector<int> r;
        for (auto& h : hits) if (std::find (r.begin(), r.end(), h.pad) == r.end()) r.push_back (h.pad);
        return r;
    }
    bool has (int step, int pad) const
    {
        for (auto& h : hits) if (h.step == step && h.pad == pad) return true;
        return false;
    }
};

struct FlipSettings { int bars = 2; float busy = 0.6f; bool startOnChop1 = true; };

// A new pattern from the chops. Locked rows (pads) are copied from 'keep'; the rest is made up again from the seed.
// Up to six rows: the first chop (if every bar starts on it), then chops picked by role, then by chance.
inline Pattern makeFlip (const std::vector<Role>& roles, int numChops, const FlipSettings& fs, unsigned seed,
                         const std::set<int>& locked = {}, const Pattern* keep = nullptr)
{
    Pattern p; p.steps = 16 * std::clamp (fs.bars, 1, 4);
    std::mt19937 rng (seed * 2654435761u + 17u);
    auto chance = [&] (double pr) { return std::uniform_real_distribution<double> (0, 1) (rng) < pr; };
    auto vel = [&] (float lo, float hi) { return std::uniform_real_distribution<float> (lo, hi) (rng); };
    const int n = std::clamp (numChops, 1, numPads);
    std::vector<int> rows;
    if (keep != nullptr)
        for (int pad : locked)
            if (pad < n) { rows.push_back (pad); for (auto& h : keep->hits) if (h.pad == pad && h.step < p.steps) p.hits.push_back (h); }
    auto take = [&] (int pad) { if (pad >= 0 && pad < n && (int) rows.size() < 6 && std::find (rows.begin(), rows.end(), pad) == rows.end()) rows.push_back (pad); };
    if (fs.startOnChop1) take (0);
    auto byRole = [&] (Role r)
    {
        std::vector<int> c;
        for (int i = 0; i < n; ++i) if (roles.size() > (size_t) i && roles[(size_t) i] == r) c.push_back (i);
        std::shuffle (c.begin(), c.end(), rng);
        return c;
    };
    auto lows = byRole (Role::Low), snaps = byRole (Role::Snap), ticks = byRole (Role::Tick), tones = byRole (Role::Tone);
    if (! lows.empty()) take (lows[0]);
    if (! snaps.empty()) take (snaps[0]);
    if (! ticks.empty()) take (ticks[0]);
    for (int t : tones) if ((int) rows.size() < 4) take (t);
    std::vector<int> rest;
    for (int i = 0; i < n; ++i) rest.push_back (i);
    std::shuffle (rest.begin(), rest.end(), rng);
    const int want = std::clamp (2 + (int) std::lround (fs.busy * 4), 2, 6);
    for (int i : rest) if ((int) rows.size() < std::min (want, n)) take (i);

    std::set<int> used;                                 // one hit per step keeps the chops from smearing together
    for (auto& h : p.hits) used.insert (h.step);
    auto place = [&] (int step, int pad, float v)
    {
        if (step < 0 || step >= p.steps || used.count (step) || locked.count (pad)) return;
        p.hits.push_back ({ step, pad, v }); used.insert (step);
    };
    const double busy = std::clamp ((double) fs.busy, 0.0, 1.0);
    for (int bar = 0; bar < p.steps / 16; ++bar)
    {
        const int b0 = bar * 16;
        if (fs.startOnChop1) place (b0, 0, 1.0f);
        for (int pad : rows)
        {
            if (locked.count (pad) || (fs.startOnChop1 && pad == 0)) continue;
            const Role r = roles.size() > (size_t) pad ? roles[(size_t) pad] : Role::Tone;
            if (r == Role::Snap)      { place (b0 + 4, pad, vel (0.9f, 1.0f)); place (b0 + 12, pad, vel (0.9f, 1.0f)); }
            else if (r == Role::Low)  { for (int s : { 0, 7, 10 }) if (s == 0 || chance (0.55 + 0.3 * busy)) place (b0 + s, pad, vel (0.85f, 1.0f)); }
            else if (r == Role::Tick) { for (int s = 2; s < 16; s += 2) if (chance (0.25 + 0.45 * busy)) place (b0 + s + (chance (0.2) ? 1 : 0), pad, vel (0.55f, 0.8f)); }
        }
        // the rest of the bar from the tones and anything else, on syncopated steps first. Snare-like chops only as
        // quiet ghost notes, kick-like ones kept off the backbeat, and nothing but a hat twice in a row.
        static const int order[] = { 3, 6, 8, 11, 14, 2, 10, 13, 5, 15, 1, 9 };
        for (int s : order)
        {
            if (! chance (0.18 + 0.62 * busy)) continue;
            std::vector<int> free;
            for (int pad : rows)
            {
                if (locked.count (pad) || (fs.startOnChop1 && pad == 0 && s % 16 != 0)) continue;
                const Role r = roles.size() > (size_t) pad ? roles[(size_t) pad] : Role::Tone;
                if (r == Role::Snap && s != 7 && s != 9 && s != 15) continue;
                if (r == Role::Low && (s == 4 || s == 5 || s == 12 || s == 13)) continue;
                if (r != Role::Tick && p.has (b0 + s - 1, pad)) continue;
                free.push_back (pad);
            }
            if (free.empty()) continue;
            const int pad = free[(size_t) std::uniform_int_distribution<int> (0, (int) free.size() - 1) (rng)];
            const bool ghost = roles.size() > (size_t) pad && roles[(size_t) pad] == Role::Snap;
            place (b0 + s, pad, ghost ? vel (0.4f, 0.55f) : vel (0.7f, 0.95f));
        }
    }
    std::sort (p.hits.begin(), p.hits.end(), [] (const Hit& a, const Hit& b) { return a.step < b.step || (a.step == b.step && a.pad < b.pad); });
    return p;
}

// Swing: every second 16th lands late. 50 % is straight, 66 % is a triplet feel.
inline double swingOffsetSteps (int step, double swing) { return (step % 2 == 1) ? (swing - 0.5) * 2.0 : 0.0; }

//==============================================================================
// One pad's settings, as the voice needs them
struct PadSettings
{
    float pitch = 0.0f;          // semitones
    float start = 0.0f, end = 1.0f;   // of the chop
    float attackMs = 1.0f, releaseMs = 120.0f;
    float cutoff = 20000.0f, resonance = 0.0f, drive = 0.0f;
    Era era = Era::Clean;
    bool reverse = false, oneShot = true;
    int choke = 0;               // 0: none, 1-4: a group
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowPass (double fc, double q, double fs) { set (fc, q, fs, false); }
    void highPass (double fc, double q, double fs) { set (fc, q, fs, true); }
    void set (double fc, double q, double fs, bool high)
    {
        fc = std::clamp (fc, 10.0, fs * 0.45);
        const double w = 2 * pi * fc / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        if (high) { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; }
        else      { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; }
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    float process (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void reset() { z1 = z2 = 0; }
};

inline float quantise (float x, float preGain, int bits)
{
    const float steps = (float) ((1 << (bits - 1)) - 1);
    return std::round (x * preGain * steps) / steps / preGain;
}

struct Voice
{
    bool active = false;
    int pad = -1;
    uint64_t age = 0;
    double pos = 0, inc = 1, begin = 0, end = 0;   // in sample frames
    int dir = 1;
    float gain = 1.0f;
    PadSettings ps;
    // envelope
    float env = 0.0f, attackInc = 1.0f, releaseMul = 0.999f;
    bool releasing = false;
    int chokeFade = -1, chokeLen = 1;
    // filter and era
    float s1[2] {}, s2[2] {};
    double g = 1, k = 2;
    bool filterOn = false;
    Biquad lp1[2], lp2[2], hp[2];
    double holdPhase = 0; float held[2] {};
    double lfo = 0; uint32_t noise = 22222u; float crackle[2] {}, hissZ[2] {};

    void start (int padIndex, const PadSettings& s, int chopStart, int chopEnd, float velocity, double srcRate, double outRate, uint64_t when)
    {
        active = true; pad = padIndex; ps = s; age = when;
        const double len = std::max (1, chopEnd - chopStart);
        begin = chopStart + len * std::clamp ((double) s.start, 0.0, 1.0);
        end = chopStart + len * std::clamp ((double) s.end, 0.0, 1.0);
        if (end <= begin + 8) end = begin + 8;
        dir = s.reverse ? -1 : 1;
        pos = s.reverse ? end - 1 : begin;
        inc = srcRate / outRate * std::pow (2.0, s.pitch / 12.0);
        gain = std::pow (10.0f, (velocity - 1.0f) * 18.0f / 20.0f);
        env = 0.0f; releasing = false; chokeFade = -1;
        attackInc = 1.0f / std::max (1.0f, (float) (std::max (0.5f, s.attackMs) * 0.001 * outRate));
        releaseMul = (float) std::exp (std::log (1.0e-4) / std::max (1.0, s.releaseMs * 0.001 * outRate));
        filterOn = s.cutoff < 19500.0f || s.resonance > 0.001f;
        const double fc = std::clamp ((double) s.cutoff, 20.0, outRate * 0.45);
        g = std::tan (pi * fc / outRate); k = 2.0 - 1.9 * std::clamp ((double) s.resonance, 0.0, 1.0);
        for (int c = 0; c < 2; ++c) { s1[c] = s2[c] = 0; lp1[c].reset(); lp2[c].reset(); hp[c].reset(); held[c] = 0; crackle[c] = 0; hissZ[c] = 0; }
        holdPhase = 1.0; lfo = 0;
        if (s.era == Era::Vintage26k) for (int c = 0; c < 2; ++c) { lp1[c].lowPass (11000, 0.6, outRate); lp2[c].lowPass (11000, 0.6, outRate); }
        if (s.era == Era::Dusty) for (int c = 0; c < 2; ++c) { lp1[c].lowPass (4200, 0.8, outRate); hp[c].highPass (140, 0.7, outRate); }
    }

    void release() { if (! ps.oneShot) releasing = true; }
    void choke (double outRate) { if (chokeFade < 0) { chokeLen = std::max (1, (int) (0.004 * outRate)); chokeFade = chokeLen; } }

    float rnd() { noise = noise * 1664525u + 1013904223u; return (float) (noise >> 8) / 8388608.0f - 1.0f; }

    // Adds this voice into out[0..n), from frame 'from'. Returns false once it's finished.
    bool render (const Sample& smp, float* const* out, int from, int n, double outRate)
    {
        const int len = smp.length();
        const float driveG = 1.0f + 7.0f * ps.drive, driveNorm = std::pow (driveG, -0.7f);
        const double holdInc = 26040.0 / outRate;
        const int endFade = std::max (1, (int) (0.002 * outRate));
        for (int i = from; i < n; ++i)
        {
            // where we are, and how far from the end
            const double left = dir > 0 ? end - pos : pos - begin;
            if (left <= 0) { active = false; return false; }
            float x[2];
            {
                const int i1 = (int) std::floor (pos); const float t = (float) (pos - i1);
                for (int c = 0; c < 2; ++c)
                {
                    const auto& d = c == 0 ? smp.l : smp.r;
                    auto at = [&] (int j) { return d[(size_t) std::clamp (j, 0, len - 1)]; };
                    const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
                    const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                    x[c] = ((c3 * t + c2) * t + c1) * t + c0;
                }
            }
            double step = inc;
            if (ps.era == Era::Dusty)   // wow and flutter
            {
                lfo += 1.0 / outRate;
                step *= 1.0 + 0.0022 * std::sin (2 * pi * 0.55 * lfo) + 0.0007 * std::sin (2 * pi * 5.1 * lfo);
            }
            pos += dir * step;

            // envelope
            if (releasing || chokeFade >= 0) env *= releasing ? releaseMul : 1.0f;
            else env = std::min (1.0f, env + attackInc);
            float e = env;
            if (chokeFade >= 0) { e *= (float) chokeFade / (float) chokeLen; if (--chokeFade <= 0) { active = false; return false; } }
            if (left < endFade) e *= (float) (left / endFade);
            if (releasing && env < 1.0e-4f) { active = false; return false; }

            for (int c = 0; c < 2; ++c)
            {
                float y = x[c];
                if (filterOn)   // TPT state-variable low-pass
                {
                    const double hpv = (y - (g + k) * s1[c] - s2[c]) / (1 + g * (g + k));
                    const double bp = g * hpv + s1[c]; s1[c] = (float) (g * hpv + bp);
                    const double lpv = g * bp + s2[c]; s2[c] = (float) (g * bp + lpv);
                    y = (float) lpv;
                }
                if (ps.drive > 0.001f) y = std::tanh (y * driveG) * driveNorm;
                switch (ps.era)
                {
                    case Era::Clean: break;
                    case Era::Grit12:
                        y = std::tanh (quantise (y, 0.125f, 12) * 1.1f) / 1.1f;   // unity at low level, rounding off the peaks
                        break;
                    case Era::Vintage26k:
                        if (c == 0) { holdPhase += holdInc; }
                        if (holdPhase >= 1.0) { held[c] = quantise (y, 0.25f, 12); if (c == 1) holdPhase -= 1.0; }
                        y = lp2[c].process (lp1[c].process (held[c]));
                        y = std::tanh (y * 1.2f) / 1.2f;
                        break;
                    case Era::Dusty:
                    {
                        y = hp[c].process (lp1[c].process (y));
                        const float h = rnd() * 0.0025f;   // a quiet hiss under it
                        float cr = 0.0f;
                        if ((noise & 0xFFFF) < 14) cr = rnd() * 0.25f;
                        crackle[c] += 0.35f * (cr - crackle[c]);
                        y = std::tanh ((y + h + crackle[c]) * 1.15f) / 1.15f;
                        break;
                    }
                }
                out[c][i] += y * e * gain;
            }
        }
        return true;
    }
};

//==============================================================================
// The voices, and who chokes whom
struct VoiceBank
{
    std::array<Voice, maxVoices> voices;
    uint64_t clock = 0;

    void trigger (int pad, const PadSettings& ps, const std::array<PadSettings, numPads>& all, int chopStart, int chopEnd, float vel,
                  double srcRate, double outRate)
    {
        for (auto& v : voices)
            if (v.active && (v.pad == pad || (ps.choke > 0 && v.pad >= 0 && v.pad < numPads && all[(size_t) v.pad].choke == ps.choke)))
                v.choke (outRate);
        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot == nullptr)   // steal the oldest
        {
            slot = &voices[0];
            for (auto& v : voices) if (v.age < slot->age) slot = &v;
        }
        slot->start (pad, ps, chopStart, chopEnd, vel, srcRate, outRate, ++clock);
    }
    void release (int pad) { for (auto& v : voices) if (v.active && v.pad == pad) v.release(); }
    void allOff() { for (auto& v : voices) v.active = false; }
    // The newest voice still sounding (for the playhead on the waveform)
    const Voice* newest() const
    {
        const Voice* n = nullptr;
        for (auto& v : voices) if (v.active && v.chokeFade < 0 && (n == nullptr || v.age > n->age)) n = &v;
        return n;
    }
};
} // namespace splinter
