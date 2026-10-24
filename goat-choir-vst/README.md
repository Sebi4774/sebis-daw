# Goat Choir (VST3)

The Goat Choir app as a plugin for FL Studio and other Windows DAWs: 16 chord strips played from MIDI keys,
clay goats that sing them with formant synthesis (no AI voices), host tempo sync, MIDI out of the sung notes,
and progressions you record, loop with the song, and drag into FL as MIDI.

`Source/ChoirCore.h` is a C++ port of the app's `main.js` (chords, voicing, voices, the 8 playing styles, chop, MIDI file); `Source/Words.h` ports the app's Words (letter rules, phoneme tables, syllables)
with no JUCE in it. `Source/PluginProcessor.*` wraps it as a plugin; `Source/PluginEditor.*` draws the stage and
controls. `INSTALL.txt` is the user guide that ships in the zip.

## Build

JUCE 7.0.12 (the last JUCE that builds with MinGW). Windows VST3 + standalone, cross-compiled from Linux:

```
sudo apt install g++-mingw-w64-x86-64-posix
git clone --depth 1 -b 7.0.12 https://github.com/juce-framework/JUCE.git ~/src/juce7
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DJUCE_DIR=~/src/juce7 -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --target GoatChoir_VST3 GoatChoir_Standalone -j
```

Add `-DGOAT_DEMO=ON` for the demo edition (strips 9-16, MIDI export and goat dressing locked; its own plugin code).

## Test

```
cmake -B build-lin -DJUCE_DIR=~/src/juce7 -DGOAT_BUILD_TEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-lin --target GoatTest -j
xvfb-run -a build-lin/GoatTest_artefacts/Release/GoatTest out    # WAVs, the .mid and editor.png land in out/
```

`-DGOAT_BUILD_HOSTCHECK=ON` on the Windows build adds `HostCheck.exe`, which loads the built `.vst3` through
JUCE's VST3 host and checks audio, MIDI out, the editor and state (runs under Wine).
