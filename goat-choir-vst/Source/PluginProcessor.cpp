#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
const int kChopMul[4] = {0, 2, 4, 8};   // Off, 1/8, 1/16, 1/32
}

GoatChoirProcessor::GoatChoirProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GoatChoir", createLayout()) {
    for (int k = 0; k < goat::kMaxGoats; ++k) {
        voiceType[k] = goat::kCast[k].voice; muted[k] = false; look[k] = goat::kCast[k].look;
        goatLevel[k] = 0; goatNote[k] = -1;
    }
    startTimerHz(30);
}

juce::AudioProcessorValueTreeState::ParameterLayout GoatChoirProcessor::createLayout() {
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const auto pct = AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(std::lround(v * 100)) + "%"; }).withValueFromStringFunction([](const String& t) { return t.getFloatValue() / 100.0f; });
    StringArray keys; for (auto* n : goat::kNames) keys.add(n);
    auto ch = [&](const char* id, const char* name, StringArray opts, int def) { p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, opts, def)); };
    ch("key", "Key", keys, 3);
    ch("scale", "Scale", {"Minor", "Harmonic minor", "Major"}, 0);
    ch("colour", "Colour", {"Triads", "7ths", "9ths"}, 1);
    ch("sing", "Sing", {"Choir", "Unison", "Solo"}, 0);
    ch("style", "Style", {"Block", "Strum", "Arp"}, 0);
    ch("vowel", "Vowel", {"OO", "OH", "AH", "EH", "EE", "Mm"}, 2);
    ch("chop", "Chop", {"Off", "1/8", "1/16", "1/32"}, 0);
    ch("keys", "MIDI keys", {"Chord strips", "Sing my notes"}, 0);
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID{"goats", 1}, "Goats", 1, goat::kMaxGoats, 4));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"room", 1}, "Room", NormalisableRange<float>(0, 1), 0.45f));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"volume", 1}, "Volume", NormalisableRange<float>(-24, 6, 0.1f), 0.0f,
                                                      AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"midiOut", 1}, "MIDI out", true));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"strum", 1}, "Strum", NormalisableRange<float>(0, 1, 0.01f), 0.45f, pct));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"motionOn", 1}, "Motion", false));
    ch("motionSync", "Motion sync", {"Global", "Retrig"}, 0);
    ch("motionLen", "Motion length", {"1/4", "1/2", "1 bar", "2 bars"}, 2);
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"motionDepth", 1}, "Motion depth", NormalisableRange<float>(0, 1, 0.01f), 1.0f, pct));
    p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{"motionChance", 1}, "Motion chance", NormalisableRange<float>(0, 1, 0.01f), 1.0f, pct));
    return {p.begin(), p.end()};
}

bool GoatChoirProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void GoatChoirProcessor::prepareToPlay(double sampleRate, int) {
    sr = sampleRate;
    choir.prepare(sr);
    reverb.setSampleRate(sr);
    reverb.reset();
    compEnv = 0;
    volume.reset(sr, 0.05);
    volume.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("volume")->load()));
    held.fill(false); stripKey = -1;
    midiOutBuf.ensureSize(4096);
}

void GoatChoirProcessor::push(const Cmd& c) {
    const auto scope = cmdFifo.write(1);
    if (scope.blockSize1 > 0) cmdBuf[(size_t)scope.startIndex1] = c;
}

void GoatChoirProcessor::uiStrip(int i, bool down) {
    if (i < 0 || i >= goat::kStrips || stripLocked(i)) return;
    if (down) {
        if (latch && !recording && currentChord.load() == i) push({CmdStop, 0, {}});
        else push({CmdStrip, i, {}});
    } else if (!latch) push({CmdStop, 0, {}});
}
void GoatChoirProcessor::uiStop() { push({CmdStop, 0, {}}); }
void GoatChoirProcessor::uiAudition(int item) {
    if (item < 0 || item >= (int)prog.size()) return;
    push({CmdPlan, prog[(size_t)item].chord, prog[(size_t)item].notes});
}

void GoatChoirProcessor::setRecording(bool on) {
    recording = on;
    if (on) { recTimed = false; recLastQ = -1; }
}
void GoatChoirProcessor::setProgPlaying(bool on) {
    progPlayWanted = on && !prog.empty();
    push({progPlayWanted ? CmdProgStart : CmdProgStop, 0, {}});
}
void GoatChoirProcessor::progChanged() {
    ++progVersion;
    if (prog.size() > (size_t)goat::kMaxProg) prog.resize((size_t)goat::kMaxProg);
    {
        const juce::SpinLock::ScopedLockType l(progLock);
        progSharedN = (int)prog.size();
        for (int i = 0; i < progSharedN; ++i) progShared[(size_t)i] = {prog[(size_t)i].chord, prog[(size_t)i].notes, prog[(size_t)i].bars};
    }
    progDirty = true;
    if (prog.empty() && progPlayWanted) setProgPlaying(false);
}

namespace { const double kMotionLen[4] = {0.25, 0.5, 1, 2}; }
void GoatChoirProcessor::motionChanged() {
    ++motionVersion;
    if (motionPts.size() > (size_t)goat::Motion::kMaxPts) motionPts.resize((size_t)goat::Motion::kMaxPts);
    {
        const juce::SpinLock::ScopedLockType l(motionLock);
        motionSharedN = (int)motionPts.size();
        for (int i = 0; i < motionSharedN; ++i) motionShared[(size_t)i] = motionPts[(size_t)i];
    }
    motionDirty = true;
}
goat::Motion GoatChoirProcessor::motionNow() const {
    goat::Motion m;
    m.setPoints(motionPts);
    m.on = apvts.getRawParameterValue("motionOn")->load() > 0.5f;
    m.sync = choice("motionSync");
    m.lenBars = kMotionLen[choice("motionLen")];
    m.depth = apvts.getRawParameterValue("motionDepth")->load();
    m.chance = apvts.getRawParameterValue("motionChance")->load();
    return m;
}

void GoatChoirProcessor::labels(std::array<goat::ChordLabel, goat::kStrips>& out) const {
    std::array<goat::Chord, goat::kStrips> c;
    goat::buildChords(choice("key"), choice("scale"), choice("colour") + 3, c, &out);
}

void GoatChoirProcessor::timerCallback() {
    // chords the audio thread just played: add them to the progression while Rec is on
    const auto scope = trigFifo.read(trigFifo.getNumReady());
    if (scope.blockSize1 + scope.blockSize2 == 0) return;
    std::array<goat::ChordLabel, goat::kStrips> lab;
    labels(lab);
    bool changed = false;
    auto take = [&](int start, int n) {
        for (int j = 0; j < n; ++j) {
            const auto& e = trigBuf[(size_t)(start + j)];
            if (!recording || e.chord < 0 || (int)prog.size() >= goat::kMaxProg) continue;
            const int q = (int)std::lround(e.beat);
            if (recLastQ < 0) { recTimed = e.hostPlaying; recLastQ = q; }
            else if (recTimed && e.hostPlaying && !prog.empty()) {
                const int qq = std::max(q, recLastQ + 1);
                prog.back().bars = (qq - recLastQ) / 4.0;
                recLastQ = qq;
            }
            prog.push_back({e.chord, juce::String::fromUTF8(lab[(size_t)e.chord].name.c_str()), e.plan, 1.0});
            changed = true;
        }
    };
    take(scope.startIndex1, scope.blockSize1);
    take(scope.startIndex2, scope.blockSize2);
    if (changed) progChanged();
}

void GoatChoirProcessor::handleMidi(const juce::MidiMessage& m) {
    const bool stripsMode = choice("keys") == 0;
    if (m.isAllNotesOff() || m.isAllSoundOff()) { held.fill(false); stripKey = -1; choir.stopAll(); return; }
    if (!m.isNoteOnOrOff()) return;
    const int n = m.getNoteNumber();
    if (stripsMode) {
        const int idx = n - goat::kFirstStripNote;
        if (m.isNoteOn()) {
            if (idx < 0 || idx >= goat::kStrips || stripLocked(idx)) return;
            stripKey = n; choir.playChord(idx);
        } else if (n == stripKey) { stripKey = -1; choir.stopAll(); }
        return;
    }
    held[(size_t)n] = m.isNoteOn();
    int notes[128], nh = 0;
    for (int k = 0; k < 128; ++k) if (held[(size_t)k]) notes[nh++] = k;
    choir.playNotes(notes, nh);
}

void GoatChoirProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // follow the host: tempo always, song position while it plays
    double bpm = hostBpm.load(); bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
            if (playing) if (auto ppq = pos->getPpqPosition()) choir.beatPos = *ppq;
        }
    bpm = juce::jlimit(20.0, 400.0, bpm);
    choir.bpm = bpm; choir.hostPlaying = playing;
    hostBpm = bpm; hostPlaying = playing;

    choir.root = choice("key"); choir.scale = choice("scale"); choir.ext = choice("colour") + 3;
    choir.rebuildIfNeeded();
    choir.mode = choice("sing"); choir.style = choice("style"); choir.chop = kChopMul[choice("chop")];
    choir.setVowel(choice("vowel"));
    choir.sel = selected.load();
    for (int k = 0; k < goat::kMaxGoats; ++k) { choir.setVoiceType(k, voiceType[k].load()); choir.setMuted(k, muted[k].load()); }
    choir.setCount(choice("goats"));
    choir.strum = apvts.getRawParameterValue("strum")->load();
    choir.motion.on = apvts.getRawParameterValue("motionOn")->load() > 0.5f;
    choir.motion.sync = choice("motionSync");
    choir.motion.lenBars = kMotionLen[choice("motionLen")];
    choir.motion.depth = apvts.getRawParameterValue("motionDepth")->load();
    choir.motion.chance = apvts.getRawParameterValue("motionChance")->load();
    if (motionDirty.load()) {
        const juce::SpinLock::ScopedTryLockType l(motionLock);
        if (l.isLocked()) {
            choir.motion.n = motionSharedN;
            for (int i = 0; i < motionSharedN; ++i) choir.motion.pts[(size_t)i] = motionShared[(size_t)i];
            motionDirty = false;
        }
    }
    if (choice("keys") != 0) stripKey = -1;

    if (progDirty.load()) {
        const juce::SpinLock::ScopedTryLockType l(progLock);
        if (l.isLocked()) {
            choir.progN = progSharedN;
            for (int i = 0; i < progSharedN; ++i) choir.prog[(size_t)i] = progShared[(size_t)i];
            choir.progPos = -1;
            progDirty = false;
        }
    }

    choir.midiOut.clear(); choir.nTrig = 0; choir.setOffset(0);
    {
        const auto scope = cmdFifo.read(cmdFifo.getNumReady());
        auto run = [&](int start, int cnt) {
            for (int j = 0; j < cnt; ++j) {
                const auto& c = cmdBuf[(size_t)(start + j)];
                switch (c.type) {
                    case CmdStrip: choir.playChord(c.a); break;
                    case CmdStop: choir.stopProg(); choir.stopAll(); break;
                    case CmdPlan: choir.playPlan(c.plan, c.a, false, true); break;
                    case CmdProgStart: if (choir.progN) choir.startProg(); break;
                    case CmdProgStop: choir.stopProg(); break;
                    default: break;
                }
            }
        };
        run(scope.startIndex1, scope.blockSize1);
        run(scope.startIndex2, scope.blockSize2);
    }

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    juce::HeapBlock<float> monoR;
    if (!R) { monoR.calloc((size_t)n); R = monoR.get(); }
    int pos = 0;
    for (const auto meta : midi) {
        const int s = juce::jlimit(0, n, meta.samplePosition);
        if (s > pos) { choir.render(L + pos, R + pos, s - pos, pos); pos = s; }
        choir.setOffset(s);
        handleMidi(meta.getMessage());
    }
    if (pos < n) choir.render(L + pos, R + pos, n - pos, pos);

    // room: a plate-ish reverb on top of the dry choir
    juce::Reverb::Parameters rp;
    const float room = apvts.getRawParameterValue("room")->load();
    rp.roomSize = 0.84f; rp.damping = 0.45f; rp.width = 1.0f; rp.dryLevel = 0.5f; rp.wetLevel = room * 0.7f * 0.9f;
    reverb.setParameters(rp);
    reverb.processStereo(L, R, n);

    // glue compressor (threshold -18 dB, 3.5:1, like the app) and a soft ceiling
    volume.setTargetValue(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("volume")->load()));
    const double atk = 1 - std::exp(-1 / (0.01 * sr)), rel = 1 - std::exp(-1 / (0.25 * sr));
    for (int i = 0; i < n; ++i) {
        const double x = std::max(std::abs(L[i]), std::abs(R[i]));
        compEnv += (x - compEnv) * (x > compEnv ? atk : rel);
        const double db = 20 * std::log10(compEnv + 1e-9), over = db + 18;
        const double red = over <= -6 ? 0 : over >= 6 ? over * (1 - 1 / 3.5) : (1 - 1 / 3.5) * (over + 6) * (over + 6) / 24;
        const float g = (float)std::pow(10.0, (4.0 - red) / 20) * volume.getNextValue();
        for (float* ch : {L + i, R + i}) {
            float y = *ch * g;
            const float a = std::abs(y);
            if (a > 0.8f) y = std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), y);
            *ch = y;
        }
    }
    if (buffer.getNumChannels() == 1) for (int i = 0; i < n; ++i) L[i] = (L[i] + R[i]) * 0.5f;

    // MIDI out: the notes the goats are singing, to drive any other synth
    midi.clear();
    if (apvts.getRawParameterValue("midiOut")->load() > 0.5f)
        for (const auto& e : choir.midiOut) {
            const int s = juce::jlimit(0, juce::jmax(0, n - 1), e.offset);
            midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)96) : juce::MidiMessage::noteOff(1, e.note), s);
        }

    // report to the editor and the recorder
    for (int k = 0; k < goat::kMaxGoats; ++k) { goatLevel[k] = (float)(choir.voice(k).amp / 0.22); goatNote[k] = choir.soundingNote(k); }
    currentChord = choir.current; motionPhase = (float)choir.motionPhase; progPos = choir.progPlaying ? choir.progPos : -1;
    if (choir.nTrig) {
        const auto scope = trigFifo.write(choir.nTrig);
        for (int j = 0; j < scope.blockSize1; ++j) trigBuf[(size_t)(scope.startIndex1 + j)] = choir.trig[(size_t)j];
        for (int j = 0; j < scope.blockSize2; ++j) trigBuf[(size_t)(scope.startIndex2 + j)] = choir.trig[(size_t)(scope.blockSize1 + j)];
    }
}

// ---------------- MIDI export ----------------
juce::String GoatChoirProcessor::fileBase() const {
    return (juce::String("choir chords ") + goat::kNames[choice("key")] + (choice("scale") == goat::Major ? "" : "m") + " "
            + juce::String((int)std::lround(hostBpm.load())) + "bpm").replace("#", "s");
}
juce::File GoatChoirProcessor::exportFolder() {
    auto f = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Sebi Inc").getChildFile("Goat Choir");
    f.createDirectory();
    return f;
}
bool GoatChoirProcessor::writeMidi(const juce::File& f) const {
    if (kDemo || prog.empty()) return false;
    std::vector<goat::ExportItem> items;
    for (auto& it : prog) items.push_back({it.notes, it.bars});
    const auto mo = motionNow();
    const auto bytes = goat::midiFile(items, hostBpm.load(), choice("style"), apvts.getRawParameterValue("strum")->load(), &mo,
                                      "Goat Choir plugin (c) 2026 Sebi Inc. All rights reserved. v" JucePlugin_VersionString);
    return f.replaceWithData(bytes.data(), bytes.size());
}

// ---------------- state ----------------
void GoatChoirProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    juce::ValueTree cast("Cast");
    cast.setProperty("sel", selected.load(), nullptr);
    cast.setProperty("latch", latch, nullptr);
    for (int k = 0; k < goat::kMaxGoats; ++k) {
        cast.setProperty("voice" + juce::String(k), voiceType[k].load(), nullptr);
        cast.setProperty("mute" + juce::String(k), muted[k].load(), nullptr);
        cast.setProperty("look" + juce::String(k), look[k], nullptr);
    }
    juce::ValueTree pr("Prog");
    for (auto& it : prog) {
        juce::ValueTree c("Item");
        c.setProperty("chord", it.chord, nullptr); c.setProperty("name", it.name, nullptr); c.setProperty("bars", it.bars, nullptr);
        juce::String notes; for (int v : it.notes) notes << v << " ";
        c.setProperty("notes", notes.trim(), nullptr);
        pr.appendChild(c, nullptr);
    }
    juce::ValueTree mo("Motion");
    mo.setProperty("shape", motionShape, nullptr);
    juce::String pts; for (auto& [x, y] : motionPts) pts << x << "," << y << " ";
    mo.setProperty("pts", pts.trim(), nullptr);
    state.removeChild(state.getChildWithName("Motion"), nullptr);
    state.appendChild(mo, nullptr);
    state.removeChild(state.getChildWithName("Cast"), nullptr);
    state.removeChild(state.getChildWithName("Prog"), nullptr);
    state.appendChild(cast, nullptr); state.appendChild(pr, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void GoatChoirProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    const auto cast = state.getChildWithName("Cast"), pr = state.getChildWithName("Prog"), mo = state.getChildWithName("Motion");
    if (mo.isValid()) {
        std::vector<std::pair<float, float>> v;
        for (auto& t : juce::StringArray::fromTokens(mo.getProperty("pts").toString(), " ", ""))
            if (t.contains(",")) v.push_back({juce::jlimit(0.0f, 1.0f, t.upToFirstOccurrenceOf(",", false, false).getFloatValue()),
                                              juce::jlimit(0.0f, 1.0f, t.fromFirstOccurrenceOf(",", false, false).getFloatValue())});
        if (v.size() >= 2) { motionPts = v; motionShape = (int)mo.getProperty("shape", -1); motionChanged(); }
    }
    apvts.replaceState(state);
    if (cast.isValid()) {
        selected = (int)cast.getProperty("sel", 0);
        latch = (bool)cast.getProperty("latch", true);
        for (int k = 0; k < goat::kMaxGoats; ++k) {
            voiceType[k] = juce::jlimit(0, 4, (int)cast.getProperty("voice" + juce::String(k), goat::kCast[k].voice));
            muted[k] = (bool)cast.getProperty("mute" + juce::String(k), false);
            look[k] = juce::jlimit(0, 5, (int)cast.getProperty("look" + juce::String(k), goat::kCast[k].look));
        }
    }
    prog.clear();
    if (pr.isValid())
        for (auto c : pr) {
            Item it{(int)c.getProperty("chord", 0), c.getProperty("name").toString(), goat::emptyPlan(), (double)c.getProperty("bars", 1.0)};
            auto toks = juce::StringArray::fromTokens(c.getProperty("notes").toString(), " ", "");
            for (int k = 0; k < goat::kMaxGoats && k < toks.size(); ++k) it.notes[(size_t)k] = toks[k].getIntValue();
            prog.push_back(it);
        }
    progChanged();
}

juce::AudioProcessorEditor* GoatChoirProcessor::createEditor() { return new GoatChoirEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GoatChoirProcessor(); }
