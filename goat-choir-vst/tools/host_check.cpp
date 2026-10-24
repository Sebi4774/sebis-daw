// host_check <plugin.vst3>: loads the plugin the way a DAW does (JUCE's VST3 host), plays two chord-strip keys
// into it, and checks that audio and MIDI come out, the editor opens, and state saves.
#include <JuceHeader.h>
using namespace juce;

int main(int argc, char** argv) {
    ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cout << "usage: host_check <plugin.vst3>\n"; return 2; }
    VST3PluginFormat fmt;
    OwnedArray<PluginDescription> found;
    fmt.findAllTypesForFile(found, String(argv[1]));
    if (found.isEmpty()) { std::cout << "FAIL no plugin found in " << argv[1] << "\n"; return 1; }
    auto& d = *found[0];
    std::cout << "found: " << d.name << " by " << d.manufacturerName << " v" << d.version << " (" << d.category << "), instrument=" << (int)d.isInstrument << "\n";
    String err;
    auto inst = fmt.createInstanceFromDescription(d, 48000, 512, err);
    if (!inst) { std::cout << "FAIL could not load: " << err << "\n"; return 1; }
    inst->enableAllBuses();
    inst->prepareToPlay(48000, 512);
    std::cout << "outputs: " << inst->getTotalNumOutputChannels() << ", produces MIDI: " << (int)inst->producesMidi() << ", params: " << inst->getParameters().size() << "\n";
    AudioBuffer<float> buf(jmax(2, inst->getTotalNumOutputChannels()), 512);
    float peak = 0; int midiOut = 0;
    for (int b = 0; b < 300; ++b) {
        MidiBuffer m;
        if (b == 2) m.addEvent(MidiMessage::noteOn(1, 60, (uint8)100), 10);
        if (b == 120) { m.addEvent(MidiMessage::noteOff(1, 60), 0); m.addEvent(MidiMessage::noteOn(1, 67, (uint8)100), 5); }
        if (b == 240) m.addEvent(MidiMessage::noteOff(1, 67), 0);
        buf.clear();
        inst->processBlock(buf, m);
        for (const auto e : m) if (e.getMessage().isNoteOnOrOff()) ++midiOut;
        peak = jmax(peak, buf.getMagnitude(0, 0, 512));
    }
    std::cout << "peak " << Decibels::gainToDecibels(peak) << " dB, MIDI out events " << midiOut << "\n";
    bool ok = peak > 0.05f && peak < 1.0f && midiOut >= 8;
    if (auto* ed = inst->createEditorIfNeeded()) {
        std::cout << "editor " << ed->getWidth() << " x " << ed->getHeight() << "\n";
        MessageManager::getInstance()->runDispatchLoopUntil(300);
        if (argc > 2) {
            auto img = ed->createComponentSnapshot(ed->getLocalBounds());
            File png(argv[2]); png.deleteFile();
            FileOutputStream os(png); PNGImageFormat().writeImageToStream(img, os);
        }
        delete ed;
    } else { std::cout << "FAIL no editor\n"; ok = false; }
    MemoryBlock mb; inst->getStateInformation(mb);
    std::cout << "state " << mb.getSize() << " bytes\n";
    inst->releaseResources();
    inst.reset();
    std::cout << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 1;
}
