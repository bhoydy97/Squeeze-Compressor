#pragma once

// Prism's factory presets. Each lists only what differs from the defaults (every band untouched, splits at
// 120 Hz, 1 kHz and 6 kHz), using parameter IDs and plain values (Hz, dB, ms, %, 0/1).

#include <utility>
#include <vector>

namespace prism
{
struct Preset
{
    const char* name;
    const char* category;
    std::vector<std::pair<const char*, float>> values;
};

// Parameter IDs: "split1/2/3", "<b>_threshold|ratio|attack|release|makeup|excite|solo|bypass" with b = b1..b4,
// "input", "output", "mix".
inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets = {
        { "Init", "Start", {} },

        // ---- Mix bus ---------------------------------------------------------------------------------------
        { "Mix glue", "Mix bus", {
            { "b1_threshold", -22 }, { "b1_ratio", 2 }, { "b1_attack", 30 }, { "b1_release", 160 }, { "b1_makeup", 1.5f },
            { "b2_threshold", -24 }, { "b2_ratio", 2 }, { "b2_attack", 30 }, { "b2_release", 140 }, { "b2_makeup", 1.5f },
            { "b3_threshold", -24 }, { "b3_ratio", 2 }, { "b3_attack", 20 }, { "b3_release", 120 }, { "b3_makeup", 1.5f },
            { "b4_threshold", -26 }, { "b4_ratio", 2 }, { "b4_attack", 15 }, { "b4_release", 100 }, { "b4_makeup", 1.5f },
            { "b4_excite", 10 } } },
        { "Bright and airy", "Mix bus", {
            { "split3", 7000 },
            { "b3_excite", 15 },
            { "b4_threshold", -30 }, { "b4_ratio", 2 }, { "b4_attack", 5 }, { "b4_release", 80 }, { "b4_makeup", 1.5f },
            { "b4_excite", 45 } } },
        { "Lo-fi warmth", "Mix bus", {
            { "split2", 800 }, { "split3", 5000 },
            { "b1_excite", 25 }, { "b2_excite", 35 },
            { "b3_threshold", -26 }, { "b3_ratio", 3 }, { "b3_attack", 10 }, { "b3_release", 120 },
            { "b4_makeup", -6 }, { "mix", 85 } } },

        // ---- Beats -----------------------------------------------------------------------------------------
        { "808 weight", "Beats", {
            { "split1", 100 },
            { "b1_threshold", -20 }, { "b1_ratio", 4 }, { "b1_attack", 30 }, { "b1_release", 120 }, { "b1_makeup", 3 },
            { "b1_excite", 40 },
            { "b2_threshold", -22 }, { "b2_ratio", 2 }, { "b2_attack", 20 }, { "b2_release", 120 } } },
        { "Punchy drums", "Beats", {
            { "split1", 100 }, { "split2", 1200 }, { "split3", 7000 },
            { "b1_threshold", -18 }, { "b1_ratio", 3 }, { "b1_attack", 25 }, { "b1_release", 80 }, { "b1_makeup", 2 },
            { "b2_threshold", -20 }, { "b2_ratio", 3 }, { "b2_attack", 20 }, { "b2_release", 80 }, { "b2_makeup", 1.5f },
            { "b3_excite", 20 }, { "b4_excite", 25 } } },
        { "Tame the hats", "Beats", {
            { "split3", 7000 },
            { "b3_threshold", -28 }, { "b3_ratio", 3 }, { "b3_attack", 2 }, { "b3_release", 80 },
            { "b4_threshold", -32 }, { "b4_ratio", 6 }, { "b4_attack", 1 }, { "b4_release", 60 } } },

        // ---- Vocals ----------------------------------------------------------------------------------------
        { "Vocal presence", "Vocals", {
            { "split1", 150 }, { "split2", 2500 }, { "split3", 8000 },
            { "b1_threshold", -30 }, { "b1_ratio", 3 }, { "b1_attack", 20 }, { "b1_release", 150 },
            { "b3_excite", 30 }, { "b3_makeup", 2 },
            { "b4_threshold", -30 }, { "b4_ratio", 4 }, { "b4_attack", 1 }, { "b4_release", 60 } } },
        { "Smooth vocal", "Vocals", {
            { "split1", 150 }, { "split2", 2500 }, { "split3", 8000 },
            { "b2_threshold", -22 }, { "b2_ratio", 2 }, { "b2_attack", 15 }, { "b2_release", 120 },
            { "b3_threshold", -26 }, { "b3_ratio", 3 }, { "b3_attack", 5 }, { "b3_release", 100 } } },
        { "Rap vocal upfront", "Vocals", {
            { "split1", 150 }, { "split2", 2000 }, { "split3", 8000 },
            { "b1_threshold", -26 }, { "b1_ratio", 4 }, { "b1_attack", 5 }, { "b1_release", 80 }, { "b1_makeup", 2 },
            { "b2_threshold", -26 }, { "b2_ratio", 4 }, { "b2_attack", 5 }, { "b2_release", 80 }, { "b2_makeup", 3 },
            { "b3_threshold", -26 }, { "b3_ratio", 4 }, { "b3_attack", 5 }, { "b3_release", 80 }, { "b3_makeup", 3 },
            { "b3_excite", 25 },
            { "b4_threshold", -28 }, { "b4_ratio", 4 }, { "b4_attack", 2 }, { "b4_release", 60 }, { "b4_makeup", 1 } } },

        // ---- Instruments -----------------------------------------------------------------------------------
        { "Bass control", "Instruments", {
            { "split1", 80 }, { "split2", 400 }, { "split3", 3000 },
            { "b1_threshold", -20 }, { "b1_ratio", 4 }, { "b1_attack", 20 }, { "b1_release", 150 }, { "b1_makeup", 2 },
            { "b2_threshold", -22 }, { "b2_ratio", 3 }, { "b2_attack", 15 }, { "b2_release", 120 },
            { "b2_excite", 30 } } },
        { "Keys and guitar sparkle", "Instruments", {
            { "split1", 200 }, { "split3", 5000 },
            { "b1_threshold", -24 }, { "b1_ratio", 2 }, { "b1_attack", 20 }, { "b1_release", 150 },
            { "b3_excite", 15 }, { "b4_excite", 35 } } },

        // ---- Mastering -------------------------------------------------------------------------------------
        { "Master polish", "Mastering", {
            { "split1", 100 }, { "split2", 900 }, { "split3", 7000 },
            { "b1_threshold", -18 }, { "b1_ratio", 1.5f }, { "b1_attack", 30 }, { "b1_release", 200 }, { "b1_makeup", 1 },
            { "b2_threshold", -20 }, { "b2_ratio", 1.5f }, { "b2_attack", 25 }, { "b2_release", 200 }, { "b2_makeup", 1 },
            { "b3_threshold", -20 }, { "b3_ratio", 1.5f }, { "b3_attack", 20 }, { "b3_release", 180 }, { "b3_makeup", 1 },
            { "b4_threshold", -20 }, { "b4_ratio", 1.5f }, { "b4_attack", 15 }, { "b4_release", 150 }, { "b4_makeup", 1 },
            { "b1_excite", 10 }, { "b4_excite", 20 } } },
        { "Loud and clear", "Mastering", {
            { "split1", 110 }, { "split2", 1000 }, { "split3", 6500 },
            { "b1_threshold", -20 }, { "b1_ratio", 2.5f }, { "b1_attack", 15 }, { "b1_release", 100 }, { "b1_makeup", 3 },
            { "b2_threshold", -20 }, { "b2_ratio", 2.5f }, { "b2_attack", 10 }, { "b2_release", 100 }, { "b2_makeup", 3 },
            { "b3_threshold", -20 }, { "b3_ratio", 2.5f }, { "b3_attack", 10 }, { "b3_release", 100 }, { "b3_makeup", 3 },
            { "b4_threshold", -22 }, { "b4_ratio", 2.5f }, { "b4_attack", 5 }, { "b4_release", 80 }, { "b4_makeup", 3 },
            { "b4_excite", 15 } } },
    };
    return presets;
}
} // namespace prism
