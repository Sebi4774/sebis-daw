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

static RenderResult render(GoatChoirProcessor& p, FakeHost& host, double sr, double beats, std::vector<Ev> evs, const File& wav, double tailSec = 1.5,
                           AudioBuffer<float>* keep = nullptr) {
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
    if (keep) keep->makeCopyOf(out);
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
    setParam(p, "style", goat::Arp);
    p.setProgPlaying(true);
    r = render(p, host, sr, 16, {}, dir.getChildFile("02_prog_arp.wav"));
    check(r.peak > 0.05f, "arp progression plays");
    check(r.midiOn >= 28, "arp sends an 8th-note line out (" + String(r.midiOn) + " notes)");

    std::cout << "Strum + chop 1/16:\n";
    setParam(p, "style", 1); setParam(p, "chop", 2);
    r = render(p, host, sr, 16, {}, dir.getChildFile("03_prog_strum_chop16.wav"));
    check(r.peak > 0.05f, "strum with chop plays");
    p.setProgPlaying(false);
    setParam(p, "chop", 0);

    std::cout << "All eight playing styles over the progression:\n";
    const char* styleNames[] = {"block", "strum", "down", "arp", "arpdown", "broken", "pulse", "bounce"};
    int styleOns[goat::kStyles] = {};
    p.setProgPlaying(true);
    for (int st = 0; st < goat::kStyles; ++st) {
        setParam(p, "style", st);
        r = render(p, host, sr, 16, {}, dir.getChildFile(String("06_style_") + styleNames[st] + ".wav"));
        styleOns[st] = r.midiOn;
        check(r.peak > 0.05f && r.peak < 1.0f, String(styleNames[st]) + " plays (" + String(r.midiOn) + " notes out)");
    }
    check(styleOns[goat::Pulse] > styleOns[goat::Arp] && styleOns[goat::Bounce] > styleOns[goat::Block], "pulse and bounce retrigger the chord");
    p.setProgPlaying(false);
    setParam(p, "style", 0);
    render(p, host, sr, 2, {}, dir.getChildFile("_flush.wav"), 0.5);

    std::cout << "Playing styles in the MIDI file (one bar of 48 55 55 60):\n";
    {
        goat::Plan pl; pl.fill(-1); pl[0] = 48; pl[1] = 55; pl[2] = 55; pl[3] = 60;
        auto ons = [&](int st, std::vector<std::pair<long, int>>* out = nullptr) {
            const auto f = goat::midiFile({{pl, 1.0}}, 120, st, 0.45, nullptr, "test");
            size_t i = 22; long t = 0; int count = 0; uint8_t run = 0;
            while (i < f.size()) {
                uint32_t d = 0; do d = (d << 7) | (f[i] & 127); while (f[i++] & 128);
                t += (long)d;
                if (f[i] == 0xff) { const uint8_t len = f[i + 2]; i += 3 + len; continue; }
                if (f[i] & 0x80) run = f[i++];
                const int nn = f[i]; i += (run & 0xf0) == 0xc0 || (run & 0xf0) == 0xd0 ? 1 : 2;
                if ((run & 0xf0) == 0x90) { ++count; if (out) out->push_back({t, nn}); }
            }
            return count;
        };
        std::vector<std::pair<long, int>> down;
        check(ons(goat::Block) == 3, "block dedupes the doubled 55 (3 notes)");
        check(ons(goat::Down, &down) == 3 && down.size() == 3 && down[0].second == 60 && down[0].first == 0 && down[2].second == 48 && down[2].first > 0,
              "down rolls from the top note");
        check(ons(goat::Arp) == 8 && ons(goat::ArpDown) == 8 && ons(goat::Broken) == 8, "arp, arp down, broken: one note per 1/8");
        check(ons(goat::Pulse) == 32, "pulse: whole chord every 1/8, doubles kept (" + String(ons(goat::Pulse)) + ")");
        check(ons(goat::Bounce) == 16, "bounce: bass on the beat, the rest on the and (" + String(ons(goat::Bounce)) + ")");
    }

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

    std::cout << "Motion (Gate, 1 bar, Global) and its CC11 in the export:\n";
    p.motionPts = goat::motionShape(7); p.motionShape = 7; p.motionChanged();
    setParam(p, "motionOn", 1);
    {
        AudioBuffer<float> buf;
        host.playing = true; setParam(p, "room", 0);
        render(p, host, sr, 4, strips({0}, 4), dir.getChildFile("06_motion_gate.wav"), 0.2, &buf);
        const int bar = (int)(4 * 60 / host.bpm * sr);
        const float a = buf.getRMSLevel(0, bar / 8, bar / 4), b = buf.getRMSLevel(0, bar * 5 / 8, bar / 4);
        std::cout << "  first half " << Decibels::gainToDecibels(a) << " dB, second half " << Decibels::gainToDecibels(b) << " dB\n";
        check(a > 0.02f && b < a * 0.25f, "Gate shape silences the second half of the bar");
        const auto mid2 = dir.getChildFile("motion.mid");
        check(p.writeMidi(mid2), "export with Motion on");
        FileInputStream in(mid2); MidiFile mf; mf.readFrom(in);
        int cc = 0, lo = 127;
        for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack(t)) if (e->message.isController() && e->message.getControllerNumber() == 11) { ++cc; lo = jmin(lo, e->message.getControllerValue()); }
        check(cc >= 8 && lo == 0, "the file carries the Gate as CC11 (" + String(cc) + " events, low " + String(lo) + ")");
    }
    setParam(p, "motionOn", 0); setParam(p, "room", 0.45f);
    render(p, host, sr, 2, {}, dir.getChildFile("_flush.wav"), 0.5);

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
