#pragma once

// LBAudio shared parameter helpers: value formatting and parsing that every
// plugin uses, so typed values like "1.2 s", "250 ms", "2.5k" or "off" work everywhere.

#include <juce_audio_processors/juce_audio_processors.h>

namespace lbaudio::params
{
using juce::String;

inline String formatDb (float v, int)      { return String (v, 1) + " dB"; }
inline String formatSignedDb (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; }
inline String formatPercent (float v, int) { return String (juce::roundToInt (v)) + " %"; }
inline String formatOnOff (bool b, int)    { return b ? "On" : "Off"; }

inline String formatMs (float v, int)
{
    if (v >= 1000.0f) return String (v / 1000.0f, 2) + " s";
    if (v >= 100.0f)  return String (juce::roundToInt (v)) + " ms";
    if (v >= 1.0f)    return String (v, 1) + " ms";
    return String (v, 2) + " ms";
}

inline String formatSeconds (float v, int)
{
    return v < 10.0f ? String (v, 2) + " s" : String (v, 1) + " s";
}

inline String formatHz (float v, int)
{
    if (v >= 10000.0f) return String (v / 1000.0f, 1) + " kHz";
    if (v >= 1000.0f)  return String (v / 1000.0f, 2) + " kHz";
    return String (juce::roundToInt (v)) + " Hz";
}

inline String formatQ (float v, int) { return String (v, 2); }

inline float parseNumber (const String& t) { return t.retainCharacters ("-+.0123456789").getFloatValue(); }

inline float parseMs (const String& t)
{
    const auto lower = t.trim().toLowerCase();
    const auto value = parseNumber (lower);
    return (lower.endsWith ("s") && ! lower.endsWith ("ms")) ? value * 1000.0f : value;
}

inline float parseSeconds (const String& t)
{
    const auto lower = t.trim().toLowerCase();
    const auto value = parseNumber (lower);
    return lower.endsWith ("ms") ? value / 1000.0f : value;
}

inline float parseHz (const String& t)
{
    const auto lower = t.trim().toLowerCase().removeCharacters (" ");
    const auto value = parseNumber (lower);
    return (lower.endsWith ("k") || lower.endsWith ("khz")) ? value * 1000.0f : value;
}

inline juce::NormalisableRange<float> skewed (float start, float end, float interval, float centre)
{
    juce::NormalisableRange<float> r (start, end, interval);
    r.setSkewForCentre (centre);
    return r;
}

// Logarithmic frequency range: equal knob travel per octave.
// Logarithmic range (equal knob travel per octave/decade), values snapped to 1/stepsPerUnit
inline juce::NormalisableRange<float> logRange (float lo, float hi, float stepsPerUnit)
{
    return { lo, hi,
             [] (float s, float e, float p) { return s * std::pow (e / s, p); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [stepsPerUnit] (float s, float e, float v) { return juce::jlimit (s, e, (float) juce::roundToInt (v * stepsPerUnit) / stepsPerUnit); } };
}

inline juce::NormalisableRange<float> frequencyRange (float lo, float hi) { return logRange (lo, hi, 10.0f); }

inline std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const String& name,
                                                             juce::NormalisableRange<float> range, float def,
                                                             String (*toText) (float, int),
                                                             float (*fromText) (const String&))
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (toText)
            .withValueFromStringFunction (fromText));
}

inline std::unique_ptr<juce::AudioParameterBool> makeBool (const char* id, const String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id, 1 }, name, def,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (formatOnOff));
}

inline std::unique_ptr<juce::AudioParameterChoice> makeChoice (const char* id, const String& name,
                                                               const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def);
}

// Standard state save/restore through the value tree.
inline void saveState (juce::AudioProcessorValueTreeState& apvts, juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, dest);
}

inline void loadState (juce::AudioProcessorValueTreeState& apvts, const void* data, int size)
{
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace lbaudio::params
