#pragma once
#include <JuceHeader.h>
#include "ChoirCore.h"

#ifndef GOAT_DEMO
 #define GOAT_DEMO 0
#endif

// Goat Choir as a plugin: chord strips played from MIDI keys (or the mouse), host tempo sync,
// the sung notes sent out as MIDI, and progressions you can record, loop with the song, and drag into FL.
class GoatChoirProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    static constexpr bool kDemo = GOAT_DEMO != 0;
    static bool stripLocked(int i) { return kDemo && i >= 8; }

    GoatChoirProcessor();
    ~GoatChoirProcessor() override { stopTimer(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    int choice(const char* id) const { return (int)apvts.getRawParameterValue(id)->load(); }

    // ---- the editor (message thread) talks to the audio thread through these ----
    void uiStrip(int i, bool down);
    void uiStop();
    void uiAudition(int item);
    bool latch = true;   // mouse clicks on strips keep singing until you click again

    // the goats: voice type, mute and scarf per goat; the selected goat sings Solo
    std::array<std::atomic<int>, goat::kMaxGoats> voiceType;
    std::array<std::atomic<bool>, goat::kMaxGoats> muted;
    std::array<int, goat::kMaxGoats> look{};
    std::atomic<int> selected{0};

    // what the editor draws
    std::array<std::atomic<float>, goat::kMaxGoats> goatLevel;
    std::array<std::atomic<int>, goat::kMaxGoats> goatNote;
    std::atomic<int> currentChord{-1}, progPos{-1};
    std::atomic<double> hostBpm{140.0};
    std::atomic<bool> hostPlaying{false};

    // progression: recorded on the message thread from the chords the audio thread reports
    struct Item { int chord; juce::String name; goat::Plan notes; double bars; };
    std::vector<Item> prog;
    int progVersion = 0;
    bool recording = false;
    void setRecording(bool on);
    void setProgPlaying(bool on);
    bool isProgPlaying() const { return progPlayWanted; }
    void progChanged();   // call after editing prog
    void labels(std::array<goat::ChordLabel, goat::kStrips>& out) const;

    // Motion curve (message thread); the audio thread gets a copy
    std::vector<std::pair<float, float>> motionPts = goat::motionShape(5);
    int motionShape = 5;   // preset index, -1 = custom drawing
    int motionVersion = 0;
    void motionChanged();
    goat::Motion motionNow() const;   // the curve + its parameters, for export and drawing
    std::atomic<float> motionPhase{-1.0f};

    // MIDI export: the progression as the choir sings it in the current style
    juce::String fileBase() const;
    bool writeMidi(const juce::File& f) const;
    static juce::File exportFolder();

private:
    void timerCallback() override;
    void handleMidi(const juce::MidiMessage& m);

    goat::ChoirEngine choir;
    struct Cmd { int type, a; goat::Plan plan; };
    enum { CmdStrip, CmdStop, CmdPlan, CmdProgStart, CmdProgStop };
    juce::AbstractFifo cmdFifo{128};
    std::array<Cmd, 128> cmdBuf;
    void push(const Cmd& c);
    juce::AbstractFifo trigFifo{128};
    std::array<goat::TriggerEvt, 128> trigBuf;

    juce::SpinLock progLock;
    std::array<goat::ProgItem, goat::kMaxProg> progShared;
    int progSharedN = 0;
    std::atomic<bool> progDirty{false};
    juce::SpinLock motionLock;
    std::array<std::pair<float, float>, goat::Motion::kMaxPts> motionShared{};
    int motionSharedN = 0;
    std::atomic<bool> motionDirty{true};
    bool progPlayWanted = false;
    int recLastQ = 0; bool recTimed = false;

    // keys held in "Sing my notes" mode, and the strip key that is sounding
    std::array<bool, 128> held{};
    int stripKey = -1;

    juce::Reverb reverb;
    double sr = 48000, compEnv = 0;
    juce::LinearSmoothedValue<float> volume;
    juce::MidiBuffer midiOutBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GoatChoirProcessor)
};
