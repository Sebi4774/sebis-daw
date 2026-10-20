// goat_test <outdir>: renders the choir through the real processor (host tempo, MIDI in, progression player),
// checks MIDI out and the MIDI export, and snapshots the editor to PNG.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

struct FakeHost : AudioPlayHead {
    double bpm = 140, ppq = 0; bool playing = false;
    Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setBpm(bpm); p.setIsPlaying(playing); p.setPpqPosition(ppq); return p;
    }
};

static int failures = 0;
static void check(bool ok, const String& what) { std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n"; if (!ok) ++failures; }
static void setParam(GoatChoirProcessor& p, const char* id, float v) {
    auto* prm = p.apvts.getParameter(id); prm->setValueNotifyingHost(prm->convertTo0to1(v));
}

struct Ev { double beat; MidiMessage m; };
struct RenderResult { float peak = 0, rms = 0; int midiOn = 0, midiOff = 0; };

static RenderResult render(GoatChoirProcessor& p, FakeHost& host, double sr, double beats, std::vector<Ev> evs, const File& wav, double tailSec = 1.5) {
    const int block = 512;
    const int total = (int)(beats * 60 / host.bpm * sr + tailSec * sr);
    AudioBuffer<float> out(2, total), buf(2, block);
    RenderResult r; double sum = 0;
    std::stable_sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.beat < b.beat; });
    size_t next = 0;
    for (int pos = 0; pos < total; pos += block) {
        const int n = jmin(block, total - pos);
        buf.setSize(2, n, false, false, true);
        MidiBuffer midi;
        const double b0 = pos / sr * host.bpm / 60, b1 = (pos + n) / sr * host.bpm / 60;
        host.ppq = b0;
        while (next < evs.size() && evs[next].beat < b1) {
            const int off = jlimit(0, n - 1, (int)((evs[next].beat - b0) * 60 / host.bpm * sr));
            midi.addEvent(evs[next].m, off); ++next;
        }
        p.processBlock(buf, midi);
        for (const auto m : midi) { if (m.getMessage().isNoteOn()) ++r.midiOn; else if (m.getMessage().isNoteOff()) ++r.midiOff; }
        for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
    }
    for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) { const float v = out.getSample(c, i); r.peak = jmax(r.peak, std::abs(v)); sum += v * v; }
    r.rms = (float)std::sqrt(sum / (2.0 * total));
    wav.deleteFile();
    if (auto os = std::unique_ptr<FileOutputStream>(wav.createOutputStream())) {
        WavAudioFormat fmt;
        if (auto w = std::unique_ptr<AudioFormatWriter>(fmt.createWriterFor(os.get(), sr, 2, 24, {}, 0))) { os.release(); w->writeFromAudioSampleBuffer(out, 0, total); }
    }
    std::cout << "  " << wav.getFileName() << ": peak " << Decibels::gainToDecibels(r.peak) << " dB, rms " << Decibels::gainToDecibels(r.rms)
              << " dB, MIDI out " << r.midiOn << " on / " << r.midiOff << " off\n";
    return r;
}

static std::vector<Ev> strips(std::initializer_list<int> idx, double beatsEach) {
    std::vector<Ev> e; double b = 0;
    for (int i : idx) { e.push_back({b, MidiMessage::noteOn(1, 60 + i, (uint8)100)}); e.push_back({b + beatsEach - 0.05, MidiMessage::noteOff(1, 60 + i)}); b += beatsEach; }
    return e;
}

int main(int argc, char** argv) {
    ScopedJuceInitialiser_GUI init;
    const File dir = File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "goat_test_out");
    dir.createDirectory();
    const double sr = argc > 2 ? String(argv[2]).getDoubleValue() : 48000;
    GoatChoirProcessor p; FakeHost host; p.setPlayHead(&host);
    p.prepareToPlay(sr, 512);
    auto pump = [] { MessageManager::getInstance()->runDispatchLoopUntil(150); };

    std::cout << "Labels (D#m, 7ths):\n  ";
    std::array<goat::ChordLabel, goat::kStrips> lab; p.labels(lab);
    for (auto& l : lab) std::cout << l.name << " (" << l.roman << ")  ";
    std::cout << "\n";
    check(lab[0].name == "D#m7" && lab[7].name == "A#7", "strip 1 is D#m7 and strip 8 is A#7");

    std::cout << "Strips from MIDI keys, recorded with the song playing:\n";
    host.playing = true;
    p.setRecording(true);
    auto r = render(p, host, sr, 16, strips({0, 2, 1, 7}, 4), dir.getChildFile("01_block_i_vi_iv_V.wav"));
    pump();
    p.setRecording(false);
    check(r.peak > 0.05f && r.peak < 1.0f, "block chords are audible and below 0 dBFS");
    check(r.midiOn >= 12 && r.midiOn == r.midiOff, "MIDI out sends the sung notes and closes them all");
    check(p.prog.size() == 4, "Rec captured 4 chords (got " + String((int)p.prog.size()) + ")");
    if (p.prog.size() == 4) check(p.prog[0].bars == 1 && p.prog[1].bars == 1 && p.prog[2].bars == 1, "chord lengths follow the beat (1 bar each)");

    std::cout << "Progression player, arp, locked to the song:\n";
    setParam(p, "style", 2);
    p.setProgPlaying(true);
    r = render(p, host, sr, 16, {}, dir.getChildFile("02_prog_arp.wav"));
    check(r.peak > 0.05f, "arp progression plays");
    check(r.midiOn >= 28, "arp sends an 8th-note line out (" + String(r.midiOn) + " notes)");

    std::cout << "Strum + chop 1/16:\n";
    setParam(p, "style", 1); setParam(p, "chop", 2);
    r = render(p, host, sr, 16, {}, dir.getChildFile("03_prog_strum_chop16.wav"));
    check(r.peak > 0.05f, "strum with chop plays");
    p.setProgPlaying(false);
    setParam(p, "chop", 0); setParam(p, "style", 0);
    render(p, host, sr, 2, {}, dir.getChildFile("_flush.wav"), 0.5);

    std::cout << "MIDI export:\n";
    const auto mid = dir.getChildFile(p.fileBase() + ".mid");
    check(p.writeMidi(mid), "wrote " + mid.getFileName());
    {
        FileInputStream in(mid); MidiFile mf;
        check(in.openedOk() && mf.readFrom(in), "the file reads back as a standard MIDI file");
        int ons = 0; double end = 0;
        for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack(t)) { if (e->message.isNoteOn()) ++ons; end = jmax(end, e->message.getTimeStamp()); }
        check(ons >= 12, String(ons) + " notes in the file");
        check(std::abs(end - 16 * 96) < 1, "the file is 4 bars long");
    }

    std::cout << "Sing my notes (held Cm9 voicing), Unison then Choir:\n";
    setParam(p, "keys", 1);
    std::vector<Ev> held;
    for (int nn : {48, 55, 58, 62, 63}) { held.push_back({0, MidiMessage::noteOn(1, nn, (uint8)100)}); held.push_back({7.9, MidiMessage::noteOff(1, nn)}); }
    r = render(p, host, sr, 8, held, dir.getChildFile("04_sing_my_notes.wav"));
    check(r.peak > 0.05f && r.midiOn == r.midiOff, "held notes are sung and released");
    setParam(p, "keys", 0);

    std::cout << "Six goats, 9ths, vowel EE, strum, stopped transport:\n";
    host.playing = false;
    setParam(p, "goats", 6); setParam(p, "colour", 2); setParam(p, "vowel", 4); setParam(p, "style", 1);
    r = render(p, host, sr, 8, strips({0, 10}, 4), dir.getChildFile("05_six_goats_9ths_EE.wav"));
    check(r.peak > 0.05f && r.peak < 1.0f, "six goats stay under 0 dBFS");
    setParam(p, "goats", 4); setParam(p, "colour", 1); setParam(p, "vowel", 2); setParam(p, "style", 0);

    std::cout << "State save and load:\n";
    MemoryBlock mb; p.getStateInformation(mb);
    GoatChoirProcessor q; q.setStateInformation(mb.getData(), (int)mb.getSize());
    check(q.prog.size() == p.prog.size() && q.choice("key") == p.choice("key"), "progression and settings come back");

    std::cout << "Editor snapshot:\n";
    host.playing = true;
    render(p, host, sr, 0.5, {{0, MidiMessage::noteOn(1, 62, (uint8)100)}}, dir.getChildFile("_warm.wav"), 0.3);
    std::unique_ptr<AudioProcessorEditor> ed(p.createEditor());
    pump();
    Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    auto png = dir.getChildFile("editor.png"); png.deleteFile();
    { FileOutputStream os(png); PNGImageFormat().writeImageToStream(img, os); }
    check(img.getWidth() == 1060, "editor is 1060 x " + String(img.getHeight()));
    ed.reset();
    dir.getChildFile("_flush.wav").deleteFile(); dir.getChildFile("_warm.wav").deleteFile();
    std::cout << (failures ? String(failures) + " FAILED" : String("all passed")) << "\n";
    return failures ? 1 : 0;
}
