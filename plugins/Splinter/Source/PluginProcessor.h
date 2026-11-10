#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "SplinterDSP.h"

namespace splinter::ids
{
inline constexpr const char* swing = "swing";
inline constexpr const char* noteRepeat = "noterepeat";   // Off, 1/8, 1/16, 1/16T, 1/32
inline constexpr const char* chopMode = "chopmode";       // On the hits, Even, Tap
inline constexpr const char* flipLength = "fliplength";   // 1, 2, 4 bars
inline constexpr const char* flipBusy = "flipbusy";
inline constexpr const char* flipStart = "flipstart";     // start every bar on chop 1
inline constexpr const char* flipPreview = "flippreview"; // play the pattern while the host plays
inline constexpr const char* volume = "volume";
// Per pad: "p1_pitch" ... "p16_choke"
inline juce::String pad (int p, const char* what) { return "p" + juce::String (p + 1) + "_" + what; }
inline const char* const padControls[] = { "pitch", "start", "end", "attack", "release", "filter", "resonance", "drive", "era", "reverse", "oneshot", "choke" };
inline constexpr int numPadControls = 12;
}

// Everything that comes with the loaded sample: the audio, where it can be cut, and what it sounds like
struct SampleData
{
    splinter::Sample sample;
    juce::String name;
    int soundStart = 0, soundEnd = 0;
    std::vector<int> hits, even;
    std::vector<splinter::Role> hitRoles, evenRoles;
    double tempo = 0.0; juce::String key;
    bool tempoFromName = false, keyFromName = false;
};

// The chops in use for one mode
struct ChopSet
{
    std::vector<int> points;            // starts, sorted
    std::vector<splinter::Role> roles;
    int end = 0;                        // where the last one ends
    int size() const { return (int) points.size(); }
    std::pair<int, int> span (int i) const { return { points[(size_t) i], i + 1 < size() ? points[(size_t) i + 1] : end }; }
};

class SplinterAudioProcessor final : public juce::AudioProcessor
{
public:
    SplinterAudioProcessor();
    ~SplinterAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Splinter"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ---- The sample (message thread) ---------------------------------------------------------------------------
    bool loadSample (const juce::File& file);
    bool loadSample (const juce::AudioBuffer<float>& audio, double rate, const juce::String& name);
    std::shared_ptr<const SampleData> sampleData() const { return std::atomic_load (&sample); }
    juce::File samplePath() const { return lastFile; }

    // ---- Chops -------------------------------------------------------------------------------------------------
    splinter::ChopMode chopMode() const;
    std::shared_ptr<const ChopSet> chops() const;             // for the mode in use
    std::shared_ptr<const ChopSet> chopsFor (splinter::ChopMode m) const;
    void addTap (int samplePos);                              // Tap mode: a chop where you tap
    void removeTapNear (int samplePos);
    void clearTaps();
    // Tap mode's chops can be undone and redone, step by step, back to when the sample was loaded
    bool canUndoTaps() const { return ! tapUndo.empty(); }
    bool canRedoTaps() const { return ! tapRedo.empty(); }
    void undoTaps();
    void redoTaps();
    void previewSample (bool on);                             // play the whole sound, to tap along to
    bool isPreviewing() const { return previewOn.load(); }
    double previewPosition() const { return previewPos.load(); }   // in samples of the sample, -1 when not playing

    // ---- Pads --------------------------------------------------------------------------------------------------
    splinter::PadSettings padSettings (int pad) const;
    void padDown (int pad, float velocity);                   // from the window: like a note on
    void padUp (int pad);
    std::atomic<int> selectedPad { 5 };

    // ---- Flip --------------------------------------------------------------------------------------------------
    void flip();                                              // a new pattern; locked rows are kept
    void stepFlip (int delta);                                // back and forward through the ones you've had
    int flipIndex() const;
    int flipCount() const;
    std::shared_ptr<const splinter::Pattern> pattern() const { return std::atomic_load (&current); }
    std::set<int> lockedRows() const;
    void setRowLocked (int pad, bool locked);
    juce::MidiFile patternMidi() const;
    juce::File writePatternMidi() const;                      // to a temporary .mid, for dragging into a DAW
    splinter::FlipSettings flipSettings() const;

    // ---- What the window shows ----------------------------------------------------------------------------------
    std::array<std::atomic<double>, splinter::numPads> lastHit;   // audio time of each pad's last trigger, seconds
    std::array<std::atomic<float>, splinter::numPads> lastVelocity;
    std::atomic<double> audioTime { 0.0 };
    std::atomic<int> playChop { -1 };       // the chop the newest voice is playing
    std::atomic<double> playPos { -1.0 };   // where in the sample it is
    std::atomic<int> patternStep { -1 };    // the step the Flip preview is on, -1 when not playing
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<bool> repeating { false };  // a pad is held with note repeat on
    double tempoForTiming() const;          // the host's tempo, else the sample's, else 120

private:
    void rebuildChops();
    void setTaps (std::vector<int> t);                        // keeps the old ones for undo
    void triggerPad (int pad, float vel, const ChopSet& cs, const std::array<splinter::PadSettings, splinter::numPads>& all);
    void setPattern (std::shared_ptr<const splinter::Pattern> p);

    std::shared_ptr<const SampleData> sample;
    std::shared_ptr<const ChopSet> chopSets[3];
    std::vector<int> taps;
    std::vector<std::vector<int>> tapUndo, tapRedo;
    static constexpr size_t maxTapUndo = 200;
    juce::File lastFile;
    mutable juce::SpinLock flipLock;
    std::vector<std::shared_ptr<const splinter::Pattern>> history;
    int historyIndex = -1;
    std::set<int> locked;
    std::shared_ptr<const splinter::Pattern> current;

    splinter::VoiceBank bank;
    double sr = 48000.0;
    double freeBeat = 0.0;                  // a beat clock for note repeat when the host isn't playing
    std::array<bool, splinter::numPads> held {};
    std::array<float, splinter::numPads> heldVel {};
    std::array<double, splinter::numPads> lastRepeatBeat {};
    std::atomic<bool> previewOn { false };
    std::atomic<double> previewPos { -1.0 };
    int previewVoiceAge = 0;
    double sampleTime = 0.0;

    struct UiEvent { int pad; float vel; bool on; };
    juce::AbstractFifo uiFifo { 256 };
    std::array<UiEvent, 256> uiEvents {};
    std::atomic<int> previewRequest { 0 };  // 1 start, 2 stop

    std::atomic<float>* pSwing = nullptr; std::atomic<float>* pRepeat = nullptr; std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pLength = nullptr; std::atomic<float>* pBusy = nullptr; std::atomic<float>* pStart = nullptr;
    std::atomic<float>* pPreview = nullptr; std::atomic<float>* pVolume = nullptr;
    std::array<std::array<std::atomic<float>*, splinter::ids::numPadControls>, splinter::numPads> pPad {};
    float volGain = 1.0f;
    struct Ev { int at; int pad; float vel; int kind; };
    std::vector<Ev> evs;
    std::array<std::vector<float>, 2> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplinterAudioProcessor)
};
