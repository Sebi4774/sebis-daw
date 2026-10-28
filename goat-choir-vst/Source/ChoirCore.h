// Goat Choir - the sound and the chord logic, with no JUCE in it (so the test tool can drive it directly).
// A C++ port of the Goat Choir app (goat-choir/source/src/main.js): same 16 chord strips, same voicing,
// same formant goats (glottal-style wave into five resonators, no AI voices), same strum, playing styles and chop.
// Sebi picks the chords; the choir only voices them.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "Words.h"

namespace goat {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxGoats = 6, kStrips = 16, kMaxProg = 64;
constexpr int kFirstStripNote = 60;   // MIDI 60-75 play strips 1-16 (C5-D#6 in FL Studio)

inline const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
// FL Studio calls middle C (MIDI 60) C5, so the plugin does too
inline std::string noteName(int n) { return std::string(kNames[((n % 12) + 12) % 12]) + std::to_string(n / 12); }

// ---------------- voices and cast ----------------
// sex: 'm' male, 'f' female, 'k' kid; swap = the same part in the other voice (Male / Female). Mezzo and Soprano
// come last so voice numbers saved by earlier versions still mean the same voice.
struct VoiceType { const char* label; int lo, hi; double fs; char sex; int swap; };
enum { Bass, Baritone, Tenor, Alto, Kid, Mezzo, Soprano, kVoiceTypes };
inline const std::array<VoiceType, kVoiceTypes> kVoices{{
    {"Bass", 38, 62, 0.86, 'm', Alto}, {"Baritone", 43, 67, 0.92, 'm', Mezzo}, {"Tenor", 48, 72, 1.0, 'm', Soprano},
    {"Alto", 53, 75, 1.12, 'f', Bass}, {"Kid", 60, 84, 1.28, 'k', Soprano}, {"Mezzo", 56, 78, 1.16, 'f', Baritone},
    {"Soprano", 60, 81, 1.2, 'f', Tenor}}};
// a female voice is breathier and rounder at the source; a male one buzzes a little more
struct SexInfo { double breath, vib; };
inline SexInfo sexInfo(char sex) { return sex == 'm' ? SexInfo{0.012, 42} : sex == 'f' ? SexInfo{0.026, 36} : SexInfo{0.016, 40}; }
// Male / Female for a goat: its partner voice, or Tenor / Soprano from a kid
inline int swapVoice(int v, char to) {
    const auto& V = kVoices[(size_t)v];
    if (V.sex == to) return v;
    if (to == 'f') return V.sex == 'm' ? V.swap : Soprano;
    return V.sex == 'f' ? V.swap : Tenor;
}

// what each goat sounds like: three voices, or an instrument it holds and plays. All plain subtractive synthesis.
// instruments: wave (0 saw, 1 triangle), filter sweep f0 -> f1 over fa then settling at f2, Q (dB, like WebAudio),
// level, attack/release, vibrato (cents), body resonance (peak Hz, dB), breath hiss
struct SoundDef { const char* label; bool inst, whisper; int wave; double f0, f1, f2, fa, q, gain, att, rel, vib, peakF, peakG, breath, lvl; };
enum { SndVoice, SndOpera, SndWhisper, SndTrumpet, SndViolin, SndFlute, SndTuba, kSounds };
inline const std::array<SoundDef, kSounds> kSoundDefs{{
    {"Voice", false, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {"Opera", false, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 72, 0, 0, 0, 1.1},
    {"Whisper", false, true, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {"Trumpet", true, false, 0, 650, 3400, 2300, 0.05, 2.2, 0.55, 0.02, 0.07, 16, 1400, 4, 0, 1},
    {"Violin", true, false, 0, 2600, 4800, 4200, 0.12, 0.7, 0.42, 0.09, 0.2, 26, 2900, 7, 0.006, 1},
    {"Flute", true, false, 1, 1400, 2400, 1900, 0.05, 0.5, 0.9, 0.05, 0.12, 14, 1500, 0, 0.03, 1},
    {"Tuba", true, false, 0, 380, 1050, 800, 0.06, 1.4, 0.75, 0.05, 0.12, 8, 300, 5, 0, 1}}};
// the Band button hands out instruments by voice type
inline int bandSound(int voice) {
    static const int band[kVoiceTypes] = {SndTuba, SndViolin, SndTrumpet, SndFlute, SndTrumpet, SndViolin, SndFlute};
    return band[std::clamp(voice, 0, kVoiceTypes - 1)];
}

struct Look { const char* name; uint32_t c; };
inline const std::array<Look, 6> kLooks{{{"Red scarf", 0xc2382f}, {"Mustard scarf", 0xd9a530}, {"Teal scarf", 0x2f8f8a},
                                         {"Plum scarf", 0x7a3a78}, {"Blue scarf", 0x3557a8}, {"Green scarf", 0x4f8a3a}}};

struct CastMember { const char* name; int voice; uint32_t fur; int look; float size; float pan; };
inline const std::array<CastMember, kMaxGoats> kCast{{{"Billy", Bass, 0xf1ece0, 0, 1.06f, -0.15f},
                                                     {"Clover", Alto, 0xf6f1e6, 2, 0.96f, 0.35f},
                                                     {"Gruff", Baritone, 0xe6dfd0, 1, 1.02f, -0.45f},
                                                     {"Pip", Kid, 0xfaf6ee, 3, 0.8f, 0.55f},
                                                     {"Hazel", Tenor, 0xeee6d6, 4, 0.98f, 0.1f},
                                                     {"Horace", Bass, 0xdcd3c2, 5, 1.1f, -0.6f}}};

// F1..F5 in Hz, bandwidths, and how open and round the mouth is
struct Vowel { double f[5], b[5]; float open, round; };
inline const std::array<Vowel, 6> kVowels{{
    {{310, 760, 2250, 3200, 3700}, {55, 80, 120, 180, 250}, 0.32f, 1.0f},   // OO
    {{470, 840, 2450, 3250, 3750}, {60, 85, 120, 180, 250}, 0.62f, 0.65f},  // OH
    {{720, 1100, 2500, 3300, 3800}, {80, 95, 130, 190, 260}, 1.0f, 0.1f},   // AH
    {{540, 1780, 2500, 3350, 3800}, {70, 90, 130, 190, 260}, 0.62f, 0.0f},  // EH
    {{290, 2250, 2950, 3450, 3900}, {50, 90, 130, 190, 260}, 0.36f, 0.0f},  // EE
    {{250, 1050, 2350, 3200, 3700}, {40, 140, 200, 260, 300}, 0.02f, 0.25f}, // Mm
}};
inline const char* const kVowelNames[6] = {"OO", "OH", "AH", "EH", "EE", "Mm"};
constexpr double kFGain[5] = {1, 0.62, 0.32, 0.18, 0.1};

// ---------------- chords ----------------
enum Scale { Minor, Harmonic, Major };
enum Fn { Tonic, Pre, Dom };
enum Mode { Choir, Unison, Solo };
// Block/Strum/Down sing the chord at once (Strum low to high, Down high to low); the rest step every 1/8 note
enum Style { Block, Strum, Down, Arp, ArpDown, Broken, Pulse, Bounce, kStyles };
inline bool patterned(int style) { return style >= Arp; }
constexpr int kScales[3][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 4, 5, 7, 9, 11}};

// prefs: which notes the upper goats take first (set by a flavor), else the 3rd, 7th, 5th and 9th
struct Chord { int rootPc = 0, n = 0, fn = 0; int iv[6]{}, pcs[6]{}; int prefs[5]{}, nPrefs = 0; };
struct ChordLabel { std::string name, roman; };

inline int chordFn(int deg) { return (deg == 0 || deg == 2 || deg == 5) ? Tonic : (deg == 1 || deg == 3) ? Pre : Dom; }

inline const char* quality(const int* iv, int m) {
    struct Q { int a, b, c; const char* s; };
    static const Q q[] = {{4, 7, -1, ""}, {3, 7, -1, "m"}, {3, 6, -1, "\xc2\xb0"}, {4, 8, -1, "+"}, {4, 7, 11, "maj7"},
                          {3, 7, 10, "m7"}, {4, 7, 10, "7"}, {3, 6, 10, "\xc3\xb8" "7"}, {3, 6, 9, "\xc2\xb0" "7"},
                          {3, 7, 11, "m(maj7)"}, {4, 8, 11, "+maj7"}};
    for (auto& x : q) {
        const int len = x.c < 0 ? 2 : 3;
        if (len != m - 1) continue;
        if (iv[1] == x.a && iv[2] == x.b && (len == 2 || iv[3] == x.c)) return x.s;
    }
    return "";
}

inline Chord makeChord(int root, int scale, int deg, int count, bool forceMajor, ChordLabel* lab) {
    const int* sc = kScales[scale];
    Chord c; c.n = count;
    for (int k = 0; k < count; ++k) { const int d = deg + k * 2; c.iv[k] = sc[d % 7] + 12 * (d / 7) - sc[deg]; }
    if (forceMajor) { c.iv[1] = 4; if (count > 3) c.iv[3] = 10; }
    c.rootPc = (root + sc[deg]) % 12;
    for (int k = 0; k < count; ++k) c.pcs[k] = (c.rootPc + c.iv[k]) % 12;
    c.fn = forceMajor ? Dom : chordFn(deg);
    if (lab) {
        static const char* const roman[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
        lab->name = std::string(kNames[c.rootPc]) + quality(c.iv, std::min(4, count));
        std::string r = roman[deg];
        if (c.iv[1] == 4) for (auto& ch : r) ch = (char)std::toupper((unsigned char)ch);
        if (c.iv[2] == 6) r += "\xc2\xb0";
        if (count >= 4) r += "7";
        if (count == 5) { lab->name += c.iv[4] == 13 ? "(b9)" : "(9)"; r += "(9)"; }
        lab->roman = r;
    }
    return c;
}

// borrowed and colour chords, built from a semitone offset above the key root
enum Shape { ShMaj, ShMin, ShDom, ShSus, ShDim7, ShMM7 };
inline Chord colorChord(int root, int ext, int off, int shape, const char* roman, int fn, int minN, ChordLabel* lab) {
    static const int shapes[6][5] = {{0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 10, 14}, {0, 5, 7, 10, 14}, {0, 3, 6, 9, 14}, {0, 3, 7, 11, 14}};
    static const char* const sfx[6][3] = {{"", "maj7", "maj9"}, {"m", "m7", "m9"}, {"", "7", "9"}, {"sus4", "7sus4", "9sus4"},
                                          {"\xc2\xb0", "\xc2\xb0" "7", "\xc2\xb0" "7"}, {"m(maj7)", "m(maj7)", "m(maj9)"}};
    Chord c; c.n = std::max(minN, ext); c.rootPc = (root + off) % 12; c.fn = fn;
    for (int k = 0; k < c.n; ++k) { c.iv[k] = shapes[shape][k]; c.pcs[k] = (c.rootPc + c.iv[k]) % 12; }
    if (lab) { lab->name = std::string(kNames[c.rootPc]) + sfx[shape][c.n - 3]; lab->roman = roman; }
    return c;
}

// chord flavors: the same strips, recoloured (sus2, sus4, add9, 6 or 6/9, 11, 13). Only major, minor and dominant
// shapes change; diminished chords stay as they are. full = the chord's root-position stack up to the 9th.
enum Flavor { FlPlain, FlSus2, FlSus4, FlAdd9, Fl6, Fl11, Fl13, kFlavors };
inline const char* const kFlavorNames[kFlavors] = {"Plain", "sus2", "sus4", "add9", "6", "11", "13"};
inline void applyFlavor(Chord& ch, const int* full, int flavor, int ext, ChordLabel* lab) {
    if (flavor == FlPlain || full[2] != 7 || (full[1] != 3 && full[1] != 4)) return;
    const bool mn = full[1] == 3, has7 = ext >= 4, nine = ext >= 5;
    const int sev = full[3]; const bool maj7 = sev == 11;
    int iv[6], n = 0;
    std::vector<int> pref;
    std::string sfx;
    auto add = [&](std::initializer_list<int> xs) { for (int x : xs) iv[n++] = x; };
    if (flavor == FlSus2 || flavor == FlSus4) {
        add({0, flavor == FlSus2 ? 2 : 5, 7});
        if (has7) add({sev});
        if (nine && flavor == FlSus4) add({14});
        sfx = std::string(has7 ? (maj7 ? "maj7" : "7") : "") + (flavor == FlSus2 ? "sus2" : "sus4"); pref = {1, 3, 2, 4};
    } else if (flavor == FlAdd9) { add({0, full[1], 7, 14}); sfx = std::string(mn ? "m" : "") + "add9"; pref = {1, 3, 2}; }
    else if (flavor == Fl6) { add({0, full[1], 7, 9}); if (nine) add({14}); sfx = std::string(mn ? "m" : "") + (nine ? "6/9" : "6"); pref = {1, 3, 4, 2}; }
    else if (flavor == Fl11) { add({0, full[1], 7, sev, 14, mn ? 17 : 18}); sfx = mn ? "m11" : maj7 ? "maj7(#11)" : "7(#11)"; pref = {1, 3, 5, 4, 2}; }
    else { add({0, full[1], 7, sev, 14, 21}); sfx = mn ? "m13" : maj7 ? "maj13" : "13"; pref = {1, 3, 5, 4, 2}; }
    ch.n = n;
    for (int k = 0; k < n; ++k) { ch.iv[k] = iv[k]; ch.pcs[k] = (ch.rootPc + iv[k]) % 12; }
    ch.nPrefs = 0;
    for (int k : pref) if (k < n && ch.nPrefs < 5) ch.prefs[ch.nPrefs++] = ch.pcs[k];
    if (lab) {
        lab->name = std::string(kNames[ch.rootPc]) + sfx;
        std::string r = lab->roman;
        for (size_t at; (at = r.find("(9)")) != std::string::npos;) r.erase(at, 3);
        r.erase(std::remove(r.begin(), r.end(), '7'), r.end());
        lab->roman = r + " " + kFlavorNames[flavor];
    }
}

// strips 1-8: the key's own chords (i iv vi vii v ii iii, then V with a major third); 9-16: borrowed and colour chords
inline void buildChords(int root, int scale, int ext, std::array<Chord, kStrips>& out, std::array<ChordLabel, kStrips>* labs, int flavor = FlPlain) {
    static const int degs[7] = {0, 3, 5, 6, 4, 1, 2};
    for (int i = 0; i < 8; ++i) {
        const int deg = i < 7 ? degs[i] : 4; const bool force = i == 7;
        out[i] = makeChord(root, scale, deg, ext, force, labs ? &(*labs)[i] : nullptr);
        applyFlavor(out[i], makeChord(root, scale, deg, 5, force, nullptr).iv, flavor, ext, labs ? &(*labs)[i] : nullptr);
    }
    struct More { int off, shape; const char* roman; int fn, minN; };
    static const More major[8] = {{5, ShMin, "iv", Pre, 3}, {10, ShMaj, "bVII", Dom, 3}, {8, ShMaj, "bVI", Pre, 3},
                                  {3, ShMaj, "bIII", Tonic, 3}, {4, ShDom, "V/vi", Dom, 3}, {2, ShDom, "V/V", Dom, 3},
                                  {0, ShDom, "V/IV", Dom, 3}, {7, ShSus, "Vsus", Dom, 3}};
    static const More minor[8] = {{1, ShMaj, "bII", Pre, 3}, {5, ShMaj, "IV", Pre, 3}, {7, ShSus, "Vsus", Dom, 3},
                                  {11, ShDim7, "vii\xc2\xb0" "7", Dom, 4}, {2, ShDom, "V/V", Dom, 3}, {0, ShDom, "V/iv", Dom, 3},
                                  {0, ShMM7, "i(maj7)", Tonic, 4}, {6, ShDim7, "#iv\xc2\xb0" "7", Pre, 4}};
    const More* m = scale == Major ? major : minor;
    static const int full[3][5] = {{0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 10, 14}};   // maj, min, dom
    for (int i = 0; i < 8; ++i) {
        out[8 + i] = colorChord(root, ext, m[i].off, m[i].shape, m[i].roman, m[i].fn, m[i].minN, labs ? &(*labs)[8 + i] : nullptr);
        if (m[i].shape <= ShDom) applyFlavor(out[8 + i], full[m[i].shape], flavor, ext, labs ? &(*labs)[8 + i] : nullptr);
    }
}

// ---------------- voicing ----------------
using Plan = std::array<int, kMaxGoats>;   // the note each goat sings, -1 = silent
inline Plan emptyPlan() { Plan p; p.fill(-1); return p; }

struct SingerInfo { int voice = Tenor, sound = SndVoice; bool muted = false; int last = -1, lastTop = -1; };

inline int nearest(int pc, int voice, double target, int floor) {
    const auto& V = kVoices[voice];
    int best = -1;
    for (int n = V.lo; n <= V.hi; ++n) {
        if (n % 12 != pc || n < floor) continue;
        if (best < 0 || std::abs(n - target) < std::abs(best - target)) best = n;
    }
    if (best < 0) for (int n = V.hi; n >= V.lo; --n) if (n % 12 == pc) { best = n; break; }
    return best;
}

inline int activeOrder(const std::array<SingerInfo, kMaxGoats>& s, int count, int* order) {
    int na = 0;
    for (int k = 0; k < count; ++k) if (!s[k].muted) order[na++] = k;
    std::stable_sort(order, order + na, [&](int a, int b) {
        const auto &A = kVoices[s[a].voice], &B = kVoices[s[b].voice];
        return A.lo != B.lo ? A.lo < B.lo : A.hi < B.hi;
    });
    return na;
}

// the lowest goat takes the root, the rest take the 3rd, 7th, 5th and 9th, each moving to the nearest note in
// their range (smooth voice leading, no crossing). Unison and Solo sing one top line instead.
inline Plan assignChord(const Chord& ch, std::array<SingerInfo, kMaxGoats>& singers, int count, int mode, int sel) {
    Plan out = emptyPlan();
    int order[kMaxGoats];
    const int na = activeOrder(singers, count, order);
    if (!na) return out;
    if (mode == Choir) {
        int prefs[5], np = 0;
        if (ch.nPrefs) for (int p = 0; p < ch.nPrefs; ++p) prefs[np++] = ch.prefs[p];
        else for (int idx : {1, 3, 2, 4}) if (idx < ch.n) prefs[np++] = ch.pcs[idx];
        int floor = 0, used[8], nu = 0;
        auto isUsed = [&](int pc) { for (int u = 0; u < nu; ++u) if (used[u] == pc) return true; return false; };
        for (int k = 0; k < na; ++k) {
            const auto& s = singers[order[k]];
            const auto& V = kVoices[s.voice];
            const double prev = s.last >= 0 ? s.last : (V.lo + V.hi) / 2.0;
            int pick;
            if (k == 0) pick = nearest(ch.rootPc, s.voice, std::min(prev, V.lo + 7.0), 0);
            else {
                int pool[6], npool = 0;
                for (int p = 0; p < np; ++p) if (!isUsed(prefs[p])) pool[npool++] = prefs[p];
                if (!npool) for (int p = 0; p < ch.n; ++p) pool[npool++] = ch.pcs[p];
                int bestN = -1, bestPc = -1; double bestCost = 1e9;
                for (int rank = 0; rank < npool; ++rank) {
                    const int n = nearest(pool[rank], s.voice, prev, floor);
                    if (n < 0) continue;
                    const double cost = std::abs(n - prev) + rank * 1.6 + (n < floor ? 20 : 0);
                    if (bestN < 0 || cost < bestCost) { bestN = n; bestCost = cost; bestPc = pool[rank]; }
                }
                pick = bestN >= 0 ? bestN : (int)std::lround(prev);
                if (bestN >= 0 && nu < 8) used[nu++] = bestPc;
            }
            floor = pick; out[order[k]] = pick;
        }
    } else {
        const int ref = singers[order[0]].lastTop >= 0 ? singers[order[0]].lastTop : 64;
        int top = -1;
        for (int p = 0; p < ch.n; ++p)
            for (int n = 55; n <= 74; ++n)
                if (n % 12 == ch.pcs[p] && (top < 0 || std::abs(n - ref) < std::abs(top - ref))) top = n;
        for (int k = 0; k < na; ++k) singers[order[k]].lastTop = top;
        for (int k = 0; k < na; ++k) {
            const int g = order[k];
            if (mode == Solo && g != sel) continue;
            out[g] = nearest(top % 12, singers[g].voice, top, 0);
        }
    }
    return out;
}

// "Sing my notes": the goats share out the keys you hold, low goats on low keys, each in its own octave range
inline Plan assignNotes(const int* held, int nh, const std::array<SingerInfo, kMaxGoats>& singers, int count, int mode, int sel) {
    Plan out = emptyPlan();
    int order[kMaxGoats];
    const int na = activeOrder(singers, count, order);
    if (!na || !nh) return out;
    for (int k = 0; k < na; ++k) {
        const int g = order[k];
        if (mode == Solo && g != sel) continue;
        const int idx = mode != Choir ? nh - 1 : na == 1 ? nh - 1 : (int)std::lround(k * (nh - 1) / double(na - 1));
        const int target = held[idx];
        out[g] = nearest(target % 12, singers[g].voice, target, 0);
    }
    return out;
}

// The stepped styles: each 1/8-note step is a list of (goat, note) that sing; gate = how much of the step they hold.
// arp, arp down and broken go one goat at a time and skip doubled pitches; pulse and bounce use every goat.
struct Pattern {
    static constexpr int kMaxSteps = kMaxGoats * 2;
    int n = 0; double gate = 1;
    int size[kMaxSteps]{};
    std::pair<int, int> step[kMaxSteps][kMaxGoats];
    void one(const std::pair<int, int>& x) { size[n] = 1; step[n++][0] = x; }
};
// seq = (goat, note) sorted by pitch
inline void buildPattern(int style, const std::pair<int, int>* seq, int n, Pattern& P) {
    P = {};
    if (!n) return;
    int nu = 0;
    std::pair<int, int> u[kMaxGoats];
    for (int k = 0; k < n; ++k) if (k == 0 || seq[k].second != seq[k - 1].second) u[nu++] = seq[k];
    if (style == Arp) {
        for (int k = 0; k < nu; ++k) P.one(u[k]);
        if (nu > 2) for (int k = nu - 2; k >= 1; --k) P.one(u[k]);
    } else if (style == ArpDown) {
        for (int k = nu - 1; k >= 0; --k) P.one(u[k]);
    } else if (style == Broken) {
        if (nu < 3) for (int k = 0; k < nu; ++k) P.one(u[k]);
        else for (int i : {0, nu - 1, nu - 2, nu - 1}) P.one(u[i]);
    } else if (style == Pulse) {
        P.gate = 0.55; P.n = 1; P.size[0] = n;
        for (int k = 0; k < n; ++k) P.step[0][k] = seq[k];
    } else if (style == Bounce) {
        P.gate = 0.85;
        if (n < 2) { P.n = 1; P.size[0] = n; P.step[0][0] = seq[0]; }
        else {
            P.n = 2; P.size[0] = 1; P.step[0][0] = seq[0]; P.size[1] = n - 1;
            for (int k = 1; k < n; ++k) P.step[1][k - 1] = seq[k];
        }
    }
}
inline int sortedPlan(const Plan& p, std::pair<int, int>* out) {
    int n = 0;
    for (int k = 0; k < kMaxGoats; ++k) if (p[k] >= 0) out[n++] = {k, p[k]};
    std::stable_sort(out, out + n, [](auto& a, auto& b) { return a.second < b.second; });
    return n;
}

// ---------------- DSP ----------------
// The glottal-style wave of the original, band-limited per octave so high goats don't alias. Three sources, each
// normalised like a WebAudio PeriodicWave: normal (k^-1.2), soft for female voices (k^-1.5), bright for words (k^-0.78)
enum Source { SrcNormal, SrcSoft, SrcBright };
struct Wavetables {
    static constexpr int N = 2048, T = 7, S = 3;
    static constexpr int H[T] = {47, 32, 22, 15, 10, 6, 3};
    float tab[S][T][N + 1];
    Wavetables() {
        static const double ex[S] = {-1.2, -1.5, -0.78}, tilt[S] = {0.5, 0.6, 0.35};
        for (int s = 0; s < S; ++s) {
            double peak = 0;
            for (int t = 0; t < T; ++t)
                for (int i = 0; i <= N; ++i) {
                    double v = 0;
                    for (int h = 1; h <= H[t]; ++h) v += std::pow(h, ex[s]) * (1 - h / 48.0 * tilt[s]) * std::sin(2 * kPi * h * i / N);
                    tab[s][t][i] = (float)v;
                    if (t == 0) peak = std::max(peak, std::abs(v));
                }
            for (auto& row : tab[s]) for (auto& v : row) v = (float)(v / peak);
        }
    }
    static const Wavetables& get() { static const Wavetables w; return w; }
    static int pick(double f, double sr) {
        const double maxH = 0.45 * sr / std::max(1.0, f);
        for (int t = 0; t < T; ++t) if (H[t] <= maxH) return t;
        return T - 1;
    }
};
struct Biquad {
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bandpass(double f, double q, double sr) {   // constant 0 dB peak, the same as WebAudio's "bandpass"
        const double w = 2 * kPi * std::min(f, sr * 0.49) / sr, al = std::sin(w) / (2 * q), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * std::cos(w) / a0; a2 = (1 - al) / a0;
    }
    void lowpass(double f, double q, double sr) {
        const double w = 2 * kPi * f / sr, al = std::sin(w) / (2 * q), cw = std::cos(w), a0 = 1 + al;
        b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    void highpass(double f, double q, double sr) {
        const double w = 2 * kPi * f / sr, al = std::sin(w) / (2 * q), cw = std::cos(w), a0 = 1 + al;
        b0 = (1 + cw) / 2 / a0; b1 = -(1 + cw) / a0; b2 = b0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    void peaking(double f, double q, double gainDb, double sr) {   // WebAudio's "peaking"
        const double A = std::pow(10.0, gainDb / 40), w = 2 * kPi * std::min(f, sr * 0.49) / sr, al = std::sin(w) / (2 * q), cw = std::cos(w), a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * cw / a0; b2 = (1 - al * A) / a0; a1 = -2 * cw / a0; a2 = (1 - al / A) / a0;
    }
    double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

inline double mtof(int n) { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

inline double sylLevel(double n) { return 0.22 * (n < 45 ? 1.35 : n < 52 ? 1.15 : 1); }

// One goat: wave -> amp -> five formant band-passes, plus a little breath noise; or hiss through the same mouth
// (Whisper); or its own instrument chain (saw or triangle -> swept low-pass -> body resonance). Each change glides the
// way WebAudio's setTargetAtTime did in the app. With Words on, the consonants are timed changes (events) on this goat.
struct Voice {
    static constexpr double kFric = 0.3, kBreath = 0.012;
    static constexpr double kFGainSpeech[5] = {1.3, 1.45, 1.15, 0.78, 0.5};   // words: the upper formants lifted
    enum EvKind { EvAmp = 1, EvNg = 2, EvForm = 4, EvNf = 8, EvFreq = 16, EvVib = 32, EvIlp = 64 };
    struct Ev { long long at; int kind; double v, v2, tau; const Ph* ph; bool useE; };
    double sr = 48000, phase = 0;
    double freq = 220, freqT = 220, freqC = 0;
    double amp = 0, ampT = 0, ampC = 0;
    double lfo = 0, lfoRate = 6.8;
    // vibrato: 1 = linear ramps through a few (time, cents) points from the note start, 2/3 = glide to vibTgt
    int vibMode = 0; double vib = 0, vibT = 0, vibC = 0, vibTgt = 0; double vpT[4]{}, vpV[4]{}; int vpN = 0;
    double ff[5]{}, ffT[5]{}, q[5]{}, qT[5]{}, ffC = 0, qC = 0; int ffDelay = 0, coefCount = 0, table = 0;
    double fg[5]{}, fgT[5]{}, fgC = 1;   // formant gains: singing, or speech
    int src = SrcNormal, baseSrc = SrcNormal; bool speech = false;
    Biquad bp[5], nbp;
    double ng = 0, ngT = 0, ngC = 0;
    double wg = 0, wgT = 0, wgC = 0;   // Whisper
    Biquad wlp1, wlp2, whp;            // Whisper hiss shaping: two low-passes and a high-pass, so it never screeches
    double breath = 0.012, vibEnd = 42;
    // instrument
    const SoundDef* snd = &kSoundDefs[SndVoice];
    double iphase = 0, iFreq = 220, iFreqT = 220, iFreqC = 0, iamp = 0, iampT = 0, iampC = 0, ilpF = 1200, ilpT = 1200, ilpC = 0, ilpQ = 1;
    Biquad ilp, ipk;
    uint32_t rng = 1;
    double gL = 0.7, gR = 0.7;
    bool singing = false; int note = -1;
    // Words
    std::vector<Ev> evs; size_t evHead = 0; long long clock = 0, sayEnd = -1;
    double fsNow = 1;
    const Ph* coda[Syl::kMaxCons]{}; int nCoda = 0; long long sylId = -1;

    double coef(double tau) const { return tau > 0 ? 1 - std::exp(-1 / (tau * sr)) : 1; }
    void prepare(double rate, uint32_t seed, float pan, double vibRate) {
        sr = rate; rng = seed; lfoRate = vibRate;
        const double x = (std::clamp(pan, -1.0f, 1.0f) + 1) / 2;
        gL = std::cos(x * kPi / 2); gR = std::sin(x * kPi / 2);
        nbp.bandpass(2600, 0.8, sr);
        wlp1.lowpass(2000, std::pow(10.0, 0.5 / 20), sr); wlp2.lowpass(3000, std::pow(10.0, 0.5 / 20), sr);
        whp.highpass(200, std::pow(10.0, 1.0 / 20), sr);   // WebAudio's default Q of 1 dB
        for (int i = 0; i < 5; ++i) {
            ff[i] = ffT[i] = kVowels[2].f[i]; q[i] = qT[i] = kVowels[2].f[i] / kVowels[2].b[i]; bp[i].bandpass(ff[i], q[i], sr);
            fg[i] = fgT[i] = kFGain[i];
        }
        amp = ampT = ng = ngT = wg = wgT = iamp = iampT = vib = 0; singing = false; speech = false; src = baseSrc; note = -1; vibMode = 0;
        ilp.lowpass(1200, 1, sr); ipk.peaking(1500, 1.2, 0, sr);
        evs.clear(); evs.reserve(8192); evHead = 0; clock = 0; sayEnd = -1; nCoda = 0; sylId = -1;
    }
    void setType(const VoiceType& V) {
        baseSrc = V.sex == 'f' ? SrcSoft : SrcNormal;
        if (!speech) src = baseSrc;
        const auto si = sexInfo(V.sex);
        breath = si.breath; vibEnd = si.vib;
    }
    // a goat changing its sound lets go quickly first
    void setSound(int k) {
        const auto* s = &kSoundDefs[(size_t)std::clamp(k, 0, kSounds - 1)];
        if (s == snd) return;
        if (singing) quiet(0.05);
        snd = s;
    }
    void setFormants(int from, int to, double fs, bool jump) {
        const auto &A = kVowels[from], &B = kVowels[to];
        for (int i = 0; i < 5; ++i) {
            if (jump) ff[i] = A.f[i] * fs;
            ffT[i] = B.f[i] * fs; q[i] = qT[i] = B.f[i] / B.b[i];
        }
        ffC = coef(0.09); ffDelay = jump ? (int)(0.03 * sr) : 0;
    }
    void vibRamp(double v0, std::initializer_list<std::pair<double, double>> pts) {
        vibMode = 1; vibT = 0; vpN = 0;
        vpT[vpN] = 0; vpV[vpN++] = v0;
        for (auto& p : pts) if (vpN < 4) { vpT[vpN] = p.first; vpV[vpN++] = p.second; }
    }
    // words need a clearer voice than humming: brighter source, the upper formants lifted
    void speechTone(bool on) {
        if (speech == on) return;
        speech = on; src = on ? SrcBright : baseSrc;
        for (int i = 0; i < 5; ++i) fgT[i] = on ? kFGainSpeech[i] : kFGain[i];
        fgC = coef(0.02);
    }
    // syl: the syllable to sing (Words on), id: which use of it, so a re-voiced chord keeps the word without new consonants;
    // shift: the Pitch knob in semitones; it moves the pitch only, so the vowels keep their colour
    void sing(int n, int vowel, double fs, const Syl* syl = nullptr, long long id = -1, int shift = 0) {
        const bool was = singing;
        cancel(EvAmp | EvNg | EvForm | EvFreq | EvVib | EvIlp); sayEnd = -1;
        const double f = mtof(n + shift);
        if (!was) freq = f * 0.985;
        freqT = f; freqC = coef(was ? 0.055 : 0.03);
        ampT = sylLevel(n); ampC = coef(was ? 0.05 : 0.08);
        vibRamp(was ? vib : 0, {{0.12, was ? 30 : 12}, {0.6, vibEnd}});
        ngT = breath; ngC = coef(0.05);
        setFormants(was ? vowel : (vowel == 5 ? 5 : 0), vowel, fs, !was);
        fsNow = fs;
        const auto& S = *snd;
        if (!S.inst) { iampT = 0; iampC = coef(0.03); }
        if (!S.whisper) { wgT = 0; wgC = coef(0.03); }
        if (S.inst) playInst(f, was, n);
        else if (S.whisper) { ampT = 0; ampC = coef(0.02); wgT = 0.22 * (n < 50 ? 1.3 : 1); wgC = coef(0.06); nCoda = 0; sylId = -1; speechTone(false); }
        else {
            if (S.vib > 0) { vibRamp(was ? vib : 0, {{0.12, was ? 30 : 12}, {0.6, vibEnd}, {0.7, S.vib}}); ampT = sylLevel(n) * S.lvl; }
            if (syl && syl->nu) {
                speechTone(true);
                const double L = sylLevel(n);
                if (was && id == sylId) vowelAt(*syl->nu, 0, L);
                else {
                    // what is left of the last word first (its final consonants), then this syllable's
                    double u = 0;
                    if (was) for (int j = 0; j < nCoda; ++j) u = phonAt(*coda[j], u, L, 0.8, syl->nu);
                    for (int j = 0; j < syl->nOn; ++j) u = phonAt(*syl->on[j], u, L, 0.8, syl->nu);
                    vowelAt(*syl->nu, u, L);
                }
                nCoda = syl->nCo;
                for (int j = 0; j < nCoda; ++j) coda[j] = syl->co[j];
                sylId = id;
            } else { nCoda = 0; sylId = -1; speechTone(false); }
        }
        singing = true; note = n;
    }
    // an instrument note: the goat's voice stays silent, the instrument chain plays
    void playInst(double hz, bool was, int n) {
        const auto& I = *snd;
        ampT = 0; ampC = coef(0.01); wgT = 0; wgC = coef(0.02);
        if (!was) iFreq = hz * 0.99;
        iFreqT = hz; iFreqC = coef(was ? 0.04 : 0.015);
        iampT = I.gain * (n < 45 ? 1.2 : 1); iampC = coef(was ? 0.04 : I.att);
        ilpQ = std::pow(10.0, I.q / 20);
        if (!was) ilpF = I.f0;
        ilpT = I.f1; ilpC = coef(I.fa);
        schedule(EvIlp, I.fa * 3, I.f2, 0.25);
        ipk.peaking(I.peakF, 1.2, I.peakG, sr);
        vibRamp(was ? vib : 0, {{0.45, I.vib}});
        ngT = I.breath; ngC = coef(0.03);
        if (I.breath > 0) nbp.bandpass(std::min(8000.0, hz * 2.5), 1.5, sr);
        nCoda = 0; sylId = -1;
    }
    // let go; with Words on the goat finishes the word (its last consonants) first
    void quiet(double rel = 0.14) {
        if (!singing) return;
        iampT = 0; iampC = coef(snd->inst ? std::min(rel, snd->rel) : 0.03);
        wgT = 0; wgC = coef(0.08);
        cancel(EvAmp | EvNg | EvVib);
        double u = 0;
        if (nCoda) {
            cancel(EvForm);
            for (int j = 0; j < nCoda; ++j) u = phonAt(*coda[j], u, sylLevel(note), 0.9, nullptr);
            nCoda = 0;
        }
        if (u > 0) { schedule(EvAmp, u, 0, rel); schedule(EvNg, u, 0, 0.1); }
        else { ampT = 0; ampC = coef(rel); ngT = 0; ngC = coef(0.1); }
        vibMode = 2; vibC = coef(0.2);
        singing = false; sylId = -1;
    }
    // the Pitch knob moved: a singing goat glides to its new pitch
    void retune(int shift) {
        if (!singing || sayEnd >= 0) return;
        freqT = mtof(note + shift); freqC = coef(0.04);
        iFreqT = freqT; iFreqC = coef(0.04);
    }
    bool idle() const { return !singing && amp < 1e-5 && ng < 1e-6 && wg < 1e-6 && iamp < 1e-6 && evHead >= evs.size(); }
    // how loud the goat is, 0..about 1, for drawing
    double level() const { return std::max({amp / 0.22, wg / 0.22, iamp / 0.6}); }

    // ---- Words: timed changes, the app's setTargetAtTime calls; u is seconds from now ----
    void schedule(int kind, double u, double v, double tau, double v2 = 0, const Ph* ph = nullptr, bool useE = false) {
        if (evs.size() >= evs.capacity()) {
            if (!evHead) return;
            evs.erase(evs.begin(), evs.begin() + (long)evHead); evHead = 0;
        }
        evs.push_back({clock + (long long)std::llround(u * sr), kind, v, v2, tau, ph, useE});
        for (size_t j = evs.size() - 1; j > evHead && evs[j - 1].at > evs[j].at; --j) std::swap(evs[j - 1], evs[j]);
    }
    void cancel(int mask) {
        evs.erase(std::remove_if(evs.begin() + (long)evHead, evs.end(), [mask](const Ev& e) { return (e.kind & mask) != 0; }), evs.end());
    }
    void apply(const Ev& e) {
        switch (e.kind) {
            case EvAmp: ampT = e.v; ampC = coef(e.tau); break;
            case EvNg: ngT = e.v; ngC = coef(e.tau); break;
            case EvNf: nbp.bandpass(e.v, e.v2, sr); break;
            case EvFreq: freqT = e.v; freqC = coef(e.tau); break;
            case EvVib: vibMode = 3; vibTgt = e.v; vibC = coef(e.tau); break;
            case EvIlp: ilpT = e.v; ilpC = coef(e.tau); break;
            case EvForm:
                // words use slightly wider bands than the sung vowels (Q = f / (b * 1.2)), so they read clearer
                for (int i = 0; i < 5; ++i) {
                    if (e.useE && i >= 3) break;
                    const double f = i < 3 ? (e.useE ? e.ph->e[i] : e.ph->f[i]) : kVowels[2].f[i], b = i < 3 ? e.ph->b[i] : kVowels[2].b[i];
                    if (f <= 0) continue;
                    ffT[i] = f * fsNow;
                    if (!e.useE) qT[i] = f / (b * 1.2);
                }
                ffC = coef(e.tau); if (!e.useE) qC = coef(e.tau);
                ffDelay = 0;
                break;
            default: break;
        }
    }
    // one sound from u: formants, voicing, hiss, and a burst for stops; returns when it ends
    double phonAt(const Ph& p, double u, double L, double k, const Ph* nucleus) {
        const double d = std::max(0.03, p.d * k);
        schedule(EvForm, u, 0, 0.012, 0, p.f[0] > 0 ? &p : nucleus ? nucleus : &p);
        if (p.t == 'H') { schedule(EvNf, u, 1600, 0, 0.5); schedule(EvNg, u, kFric * 0.5, 0.01); schedule(EvAmp, u, 0, 0.015); }
        else if (p.t == 'S') {
            // closure, then a short burst of hiss at the release
            schedule(EvAmp, u, L * p.av * 0.4, 0.008); schedule(EvNg, u, 0, 0.006);
            const double r = u + d * 0.6;
            schedule(EvNf, r, p.fh, 0, p.fh / p.fb);
            schedule(EvNg, r, kFric * p.af * 1.5, 0.003); schedule(EvNg, r + 0.026, 0, 0.014);
        } else {
            schedule(EvAmp, u, L * p.av, 0.012);
            if (p.af > 0) schedule(EvNf, u, p.fh, 0, p.fh / p.fb);
            schedule(EvNg, u, p.af > 0 ? kFric * p.af : kBreath, 0.01);
        }
        return u + d;
    }
    // the vowel of a syllable from u, gliding for diphthongs (ay, ow, ...)
    void vowelAt(const Ph& p, double u, double L) {
        schedule(EvForm, u, 0, 0.02, 0, &p);
        if (p.hasE) schedule(EvForm, u + 0.12, 0, 0.09, 0, &p, true);
        schedule(EvAmp, u, L, 0.02); schedule(EvNg, u, kBreath, 0.03);
    }
    // Say: speak a line in a talking voice, with a falling speech melody (rising on a question)
    void say(const Syl* syl, int n, const VoiceType& V, int shift = 0) {
        cancel(~0); nCoda = 0; sylId = -1;
        fsNow = V.fs;
        speechTone(true);
        iampT = 0; iampC = coef(0.02); wgT = 0; wgC = coef(0.02);
        const double base = (V.lo + V.hi) / 2.0 - 1 + shift, L = sylLevel(base - shift) * 1.5;
        schedule(EvVib, 0, 4, 0.05);
        amp = ampT = 0; ffDelay = 0;
        // each syllable's place in its phrase (phrases end at punctuation)
        std::vector<double> posIn((size_t)n, 0);
        for (int i = 0, a = 0; i < n; ++i)
            if (syl[i].end > 0.1 || i == n - 1) { for (int j = a; j <= i; ++j) posIn[(size_t)j] = i > a ? double(j - a) / (i - a) : 0; a = i + 1; }
        double u = 0;
        for (int i = 0; i < n; ++i) {
            const auto& x = syl[i];
            if (!x.nu) continue;
            const double note = base + 2.5 - 4 * posIn[(size_t)i] + (x.first ? 0.8 : 0) + (x.ask && x.end > 0 ? 4 : 0);
            const double hz = 440 * std::pow(2.0, (note - 69) / 12);
            if (i == 0) freq = freqT = hz;
            schedule(EvFreq, u, hz, 0.04);
            for (int j = 0; j < x.nOn; ++j) u = phonAt(*x.on[j], u, L, 1.2, x.nu);
            vowelAt(*x.nu, u, L);
            u += std::max(0.11, x.nu->d * 1.45) + (x.end > 0.1 ? 0.08 : 0);
            if (x.ask && x.end > 0) schedule(EvFreq, u - 0.12, hz * std::pow(2.0, 4 / 12.0), 0.05);
            for (int j = 0; j < x.nCo; ++j) u = phonAt(*x.co[j], u, L, 1.2, nullptr);
            if (x.end > 0) { schedule(EvAmp, u, x.end > 0.1 ? 0 : L * 0.4, 0.025); schedule(EvNg, u, 0, 0.02); u += x.end; }
        }
        schedule(EvAmp, u, 0, 0.04); schedule(EvNg, u, 0, 0.03); schedule(EvVib, u, 0, 0.1);
        singing = true; note = (int)std::lround(base - shift);
        sayEnd = clock + (long long)((u + 0.12) * sr);
    }
    bool saying() const { return sayEnd >= 0; }
    float noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng / 4294967295.0f) * 2.0f - 1.0f; }

    double tick(double extraCents) {
        while (evHead < evs.size() && evs[evHead].at <= clock) apply(evs[evHead++]);
        if (evHead && evHead >= evs.size()) { evs.clear(); evHead = 0; }
        if (sayEnd >= 0 && clock >= sayEnd) { singing = false; sayEnd = -1; }
        ++clock;
        freq += (freqT - freq) * freqC;
        amp += (ampT - amp) * ampC;
        ng += (ngT - ng) * ngC;
        wg += (wgT - wg) * wgC;
        if (vibMode == 1) {
            vibT += 1 / sr;
            int i = 0;
            while (i + 1 < vpN && vibT >= vpT[i + 1]) ++i;
            vib = i + 1 < vpN ? vpV[i] + (vpV[i + 1] - vpV[i]) * (vibT - vpT[i]) / std::max(1e-9, vpT[i + 1] - vpT[i]) : vpV[vpN - 1];
        } else if (vibMode == 2) vib += (0 - vib) * vibC;
        else if (vibMode == 3) vib += (vibTgt - vib) * vibC;
        lfo += lfoRate / sr; if (lfo >= 1) lfo -= 1;
        const double cents = std::sin(2 * kPi * lfo) * vib + extraCents, det = std::exp2(cents / 1200);
        const double f = freq * det;
        phase += f / sr; if (phase >= 1) phase -= std::floor(phase);
        if (ffDelay > 0) --ffDelay;
        else for (int i = 0; i < 5; ++i) ff[i] += (ffT[i] - ff[i]) * ffC;
        if (qC > 0) for (int i = 0; i < 5; ++i) q[i] += (qT[i] - q[i]) * qC;
        for (int i = 0; i < 5; ++i) fg[i] += (fgT[i] - fg[i]) * fgC;
        const bool instOn = iamp > 1e-7 || iampT > 0;
        if (--coefCount <= 0) {
            coefCount = 16;
            for (int i = 0; i < 5; ++i) bp[i].bandpass(ff[i], q[i], sr);
            table = Wavetables::pick(f * 1.03, sr);
            if (instOn) ilp.lowpass(std::min(ilpF, sr * 0.45), ilpQ, sr);
        }
        const auto& W = Wavetables::get();
        const double pos = phase * Wavetables::N;
        const int i0 = (int)pos; const double fr = pos - i0;
        const float* tb = W.tab[src][table];
        const double hiss = noise();
        const double wn = (wg > 1e-7 || wgT > 0) ? whp.process(wlp2.process(wlp1.process(hiss))) : 0;
        const double x = (tb[i0] + (tb[i0 + 1] - tb[i0]) * fr) * amp + wn * wg;
        double y = 0;
        for (int i = 0; i < 5; ++i) y += bp[i].process(x) * fg[i] * 7;
        y += nbp.process(hiss) * ng;
        if (instOn) {
            iamp += (iampT - iamp) * iampC;
            iFreq += (iFreqT - iFreq) * iFreqC;
            ilpF += (ilpT - ilpF) * ilpC;
            const double fi = iFreq * det, dt = fi / sr;
            iphase += dt; if (iphase >= 1) iphase -= std::floor(iphase);
            double o;
            if (snd->wave == 1) o = 4 * std::abs(iphase - 0.5) - 1;   // triangle
            else {
                o = 2 * iphase - 1;   // saw with a polyBLEP edge
                if (iphase < dt) { const double t = iphase / dt; o -= t + t - t * t - 1; }
                else if (iphase > 1 - dt) { const double t = (iphase - 1) / dt; o -= t * t + t + t + 1; }
            }
            y += ipk.process(ilp.process(o)) * iamp;
        }
        return y;
    }
};

struct NoteEvt { int offset, note; bool on; };
struct TriggerEvt { int chord; Plan plan; double beat; bool hostPlaying; };
struct ProgItem { int chord = -1; Plan notes = emptyPlan(); double bars = 1; };

// ---------------- Motion: a drawn curve rides the choir's level, locked to the beat ----------------
// points are (x, y) in 0..1 over one cycle; between points the curve eases (cosine), like the app
inline const char* const kMotionShapes[10] = {"Flat", "Swell", "Fade", "Wave", "Rise", "Breathe", "Pump", "Gate", "Stutter", "Steps"};
inline std::vector<std::pair<float, float>> motionShape(int i) {
    switch (i) {
        case 0: return {{0, 1}, {1, 1}};
        case 1: return {{0, 0}, {1, 1}};
        case 2: return {{0, 1}, {1, 0}};
        case 3: return {{0, .5f}, {.25f, 1}, {.75f, 0}, {1, .5f}};
        case 4: return {{0, 0}, {.5f, 1}, {1, 0}};
        case 6: return {{0, 0}, {.38f, 1}, {1, 1}};
        case 7: return {{0, 1}, {.48f, 1}, {.5f, 0}, {.98f, 0}, {1, 1}};
        case 8: {
            std::vector<std::pair<float, float>> v;
            for (int k = 0; k < 4; ++k) { const float b = k / 4.0f; v.insert(v.end(), {{b, 1}, {b + .11f, 1}, {b + .125f, .05f}, {b + .235f, .05f}}); }
            v.push_back({1, 1});
            return v;
        }
        case 9: return {{0, 1}, {.24f, 1}, {.25f, .66f}, {.49f, .66f}, {.5f, .33f}, {.74f, .33f}, {.75f, .1f}, {1, .1f}};
        default: return {{0, .05f}, {.22f, 1}, {.45f, 1}, {.72f, .05f}, {1, .05f}};   // Breathe
    }
}
struct Motion {
    static constexpr int kMaxPts = 32;
    enum { Global, Retrigger };
    bool on = false; int sync = Global; double lenBars = 1, depth = 1, chance = 1;
    std::array<std::pair<float, float>, kMaxPts> pts{}; int n = 0;
    Motion() { setPoints(motionShape(5)); }
    void setPoints(const std::vector<std::pair<float, float>>& v) {
        n = (int)std::min<size_t>(v.size(), kMaxPts);
        for (int i = 0; i < n; ++i) pts[(size_t)i] = v[(size_t)i];
    }
    double at(double x) const {
        x -= std::floor(x);
        for (int i = 1; i < n; ++i)
            if (x <= pts[(size_t)i].first) {
                const auto [x0, y0] = pts[(size_t)i - 1];
                const auto [x1, y1] = pts[(size_t)i];
                const double u = x1 > x0 ? (x - x0) / (x1 - x0) : 1;
                return y0 + (y1 - y0) * (1 - std::cos(kPi * u)) / 2;
            }
        return n ? pts[(size_t)n - 1].second : 1;
    }
    // a fixed coin per cycle, so Chance gives the same pattern every time (same hash as the app)
    static double coin(long long seed) {
        uint32_t h = (uint32_t)seed * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        return h / 4294967296.0;
    }
    // gBeats = beats since the song start, lBeats = beats since chord k started
    double gain(double gBeats, double lBeats, int k) const {
        if (!on || n < 2) return 1;
        const double cyc = lenBars * 4, t = sync == Global ? gBeats : lBeats;
        const long long c = (long long)std::floor(t / cyc + 1e-9), seed = sync == Global ? c : k * 997LL + c;
        if (chance < 1 && coin(seed + 1) >= chance) return 1;
        return 1 - depth * (1 - at(t / cyc));
    }
};

// The whole choir: six goats, the chord strips, strum/arp/chop timing, the progression player and MIDI out.
// Everything that follows the beat reads beatPos, which the plugin locks to the host's song position.
class ChoirEngine {
public:
    // settings, written by the owner before each block
    int root = 3, scale = Minor, ext = 4, mode = Choir, style = Block, vowel = 2, chop = 0, count = 4, sel = 0;
    int flavor = FlPlain;
    int pitch = 0;   // Pitch knob, -12..+12 semitones on everything the goats sing (set it with setPitch)
    double bpm = 140, beatPos = 0;
    bool hostPlaying = false;
    double strum = 0.45;   // Strum knob: gap between goats, low to high = strum * a beat * 0.3
    Motion motion;
    double motionPhase = -1;   // where the Motion playhead is in its cycle (for the editor), -1 = off
    std::array<SingerInfo, kMaxGoats> singers;
    std::array<Chord, kStrips> chords;
    // results, read by the owner after each block
    std::vector<NoteEvt> midiOut;
    std::array<TriggerEvt, 32> trig; int nTrig = 0;
    int current = -1;
    // progression player
    std::array<ProgItem, kMaxProg> prog; int progN = 0; bool progPlaying = false; double progOrigin = 0; int progPos = -1;
    double progBeat = -1;   // where the player is, in beats from the progression's start (for the note graph)
    // Words: the line the goats sing, one syllable per chord (or per step); lastWord = the word being sung, for the editor
    std::array<Syl, kMaxSyl> words; int nWords = 0; bool wordsOn = false; int lastWord = -1;
    std::array<Syl, kMaxSyl> sayBuf; int nSay = 0;

    ChoirEngine() {
        for (int k = 0; k < kMaxGoats; ++k) singers[k].voice = kCast[k].voice;
        midiOut.reserve(1024);
        rebuild();
    }
    void prepare(double rate) {
        sr = rate;
        for (int k = 0; k < kMaxGoats; ++k) {
            voices[k].setType(kVoices[(size_t)singers[k].voice]);
            voices[k].prepare(sr, 0x9E3779B9u * (k + 1), kCast[k].pan, 6.4 + 1.2 * ((k * 37) % 10) / 10.0);
            voices[k].snd = &kSoundDefs[(size_t)singers[k].sound];
        }
        chopLp.lowpass(70, 1.122, sr);
        chopC = 1 - std::exp(-1 / (0.03 * sr));
        motionC = 1 - std::exp(-1 / (0.006 * sr));
        sounding.fill(-1); noteRef.fill(0); pend.fill({-1, 0});
        arpOn = false; current = -1;
    }
    int chordKey() const { return ((root * 3 + scale) * 8 + ext) * kFlavors + flavor; }
    void rebuild() { buildChords(root, scale, ext, chords, nullptr, flavor); builtKey = chordKey(); }
    // a new key or colour stops the choir; a new flavor re-sings the chord that is sounding, like the app
    void rebuildIfNeeded() {
        if (builtKey == chordKey()) return;
        const bool flavorOnly = builtKey % kFlavors != flavor && builtKey / kFlavors == chordKey() / kFlavors;
        const int c = current;
        if (!flavorOnly) stopAll();
        rebuild();
        if (flavorOnly && c >= 0 && !progPlaying) playPlan(assignChord(chords[(size_t)c], singers, count, mode, sel), c, false, true);
    }

    void setVowel(int v) {
        if (v == vowel) return;
        vowel = v;
        for (int k = 0; k < kMaxGoats; ++k) if (voices[k].singing) voices[k].setFormants(v, v, kVoices[singers[k].voice].fs, false);
    }
    void setPitch(int p) {
        p = std::clamp(p, -12, 12);
        if (p == pitch) return;
        pitch = p;
        for (int k = 0; k < kMaxGoats; ++k) {
            voices[k].retune(p);
            if (sounding[k] >= 0 && voices[k].singing) {   // the notes sent out follow
                noteOff(sounding[k]);
                sounding[k] = std::clamp(voices[k].note + p, 0, 127);
                noteOn(sounding[k]);
            }
        }
    }
    void setCount(int c) {
        c = std::clamp(c, 1, kMaxGoats);
        if (c == count) return;
        count = c;
        for (int k = c; k < kMaxGoats; ++k) quietGoat(k);
    }
    void setMuted(int k, bool m) { if (singers[k].muted == m) return; singers[k].muted = m; if (m) quietGoat(k); }
    void setVoiceType(int k, int v) {
        v = std::clamp(v, 0, kVoiceTypes - 1);
        if (singers[k].voice == v) return;
        singers[k].voice = v; singers[k].last = -1;
        voices[k].setType(kVoices[(size_t)v]);
    }
    void setSound(int k, int snd) {
        snd = std::clamp(snd, 0, kSounds - 1);
        if (singers[k].sound == snd) return;
        singers[k].sound = snd;
        if (voices[k].singing && sounding[k] >= 0) { noteOff(sounding[k]); sounding[k] = -1; }
        voices[k].setSound(snd);
    }
    // a goat is speaking (Say): the room dips so the echo doesn't smear the consonants
    bool speaking() const { for (auto& v : voices) if (v.saying()) return true; return false; }

    void playChord(int idx) {
        if (idx < 0 || idx >= kStrips) return;
        playPlan(assignChord(chords[idx], singers, count, mode, sel), idx, true, true);
    }
    // noteOn: a key went down (a new syllable, unless it lands with the keys of the same chord); otherwise one came up
    void playNotes(const int* held, int nh, bool noteOn = true) {
        if (!nh) { stopAll(); return; }
        const bool newSyl = noteOn && samples - lastSylAt > (long long)(0.04 * sr);
        playPlan(assignNotes(held, nh, singers, count, mode, sel), -1, false, false, newSyl);
    }
    void setWords(const Syl* s, int n, bool on) {
        nWords = std::clamp(n, 0, kMaxSyl);
        for (int i = 0; i < nWords; ++i) words[(size_t)i] = s[i];
        wordsOn = on && nWords > 0; wordsI = 0; curSyl = -1; lastWord = -1;
    }
    // Say: the selected goat speaks the line in sayBuf
    void sayLine() {
        if (!nSay) return;
        stopProg(); stopAll();
        const int k = sel < count ? sel : 0;
        voices[k].say(sayBuf.data(), nSay, kVoices[singers[k].voice], pitch);
    }
    void playPlan(const Plan& plan, int chordIdx, bool record, bool allowStrum, bool newSyl = true) {
        arpOn = false;
        for (auto& p : pend) p.note = -1;
        std::pair<int, int> seq[kMaxGoats];
        const int n = sortedPlan(plan, seq);
        for (int k = 0; k < kMaxGoats; ++k) if (plan[k] >= 0) singers[k].last = plan[k];
        if (patterned(style) && n) {
            for (int k = 0; k < kMaxGoats; ++k) quietGoat(k);
            buildPattern(style, seq, n, pattern);
            arpBase = (long long)std::floor(beatPos * 2); arpStep = -1; gateOff = false; arpOn = true;
        } else {
            for (int k = 0; k < kMaxGoats; ++k) if (plan[k] < 0) quietGoat(k);
            const bool rolled = (style == Strum || style == Down) && allowStrum && strum > 0;
            if (newSyl || !wordsOn) nextSyl();   // every goat sings the same syllable
            for (int j = 0; j < n; ++j) {
                const int pos = style == Down ? n - 1 - j : j;   // Down: the top goat first
                if (rolled && pos > 0) pend[seq[j].first] = {seq[j].second, std::max(1, (int)(pos * strum * 0.3 * 60.0 / bpm * sr))};
                else startGoat(seq[j].first, seq[j].second);
            }
        }
        current = chordIdx;
        chordStartBeat = beatPos; ++chordCount;   // Motion's Retrigger restarts here
        if (record && nTrig < (int)trig.size()) trig[nTrig++] = {chordIdx, plan, beatPos, hostPlaying};
    }
    void stopAll() {
        arpOn = false;
        for (auto& p : pend) p.note = -1;
        for (int k = 0; k < kMaxGoats; ++k) quietGoat(k);
        current = -1;
    }
    void startProg() { progPlaying = true; progOrigin = beatPos; progPos = -1; }
    void stopProg() { if (progPlaying) { progPlaying = false; progPos = -1; stopAll(); } }

    // renders n samples into L/R (adding), at sample offset `offset` of the host block
    void render(float* L, float* R, int n, int offset) {
        const double beatInc = bpm / 60.0 / sr;
        const bool chopOn = chop > 0;
        const double baseT = chopOn ? 1 - 0.46 : 1, amtT = chopOn ? 0.46 : 0, vibT = chopOn ? 22 : 0, mul = chopOn ? chop : 2;
        for (int i = 0; i < n; ++i) {
            curOff = offset + i; ++samples;
            for (int k = 0; k < kMaxGoats; ++k)
                if (pend[k].note >= 0 && --pend[k].delay <= 0) { startGoat(k, pend[k].note); pend[k].note = -1; }
            if (arpOn && pattern.n) {
                const double sp = beatPos * 2 - (double)arpBase;
                const long long step = (long long)std::floor(sp);
                const int si = (int)(std::max(0LL, step) % pattern.n);
                if (step > arpStep) {
                    arpStep = step; gateOff = false;
                    nextSyl();
                    bool inStep[kMaxGoats] = {};
                    for (int j = 0; j < pattern.size[si]; ++j) inStep[pattern.step[si][j].first] = true;
                    for (int k = 0; k < kMaxGoats; ++k) if (!inStep[k]) quietGoat(k);
                    for (int j = 0; j < pattern.size[si]; ++j) startGoat(pattern.step[si][j].first, pattern.step[si][j].second);
                } else if (!gateOff && pattern.gate < 1 && sp - (double)arpStep >= pattern.gate) {
                    gateOff = true;   // a short, clipped release so pulse and bounce stay tight
                    for (int j = 0; j < pattern.size[si]; ++j) quietGoat(pattern.step[si][j].first, 0.045);
                }
            }
            if (progPlaying && progN && (i & 15) == 0) stepProg();
            // chop: a beat-synced square gate, softened, that also wobbles the pitch a little
            double ph = beatPos * mul; ph -= std::floor(ph);
            const double lp = chopLp.process(ph < 0.5 ? 1.0 : -1.0);
            chopBase += (baseT - chopBase) * chopC; chopAmt += (amtT - chopAmt) * chopC; chopVib += (vibT - chopVib) * chopC;
            if ((i & 15) == 0) {
                motionT = motion.gain(beatPos, beatPos - chordStartBeat, chordCount);
                const double t = motion.sync == Motion::Global ? beatPos : beatPos - chordStartBeat, cyc = motion.lenBars * 4;
                motionPhase = motion.on ? t / cyc - std::floor(t / cyc) : -1;
            }
            motionG += (motionT - motionG) * motionC;
            const double gate = (chopBase + chopAmt * lp) * 0.5 * 0.9 * motionG, cents = chopVib * lp;
            double l = 0, r = 0;
            for (auto& v : voices) {
                if (v.idle()) continue;
                const double y = v.tick(cents);
                l += y * v.gL; r += y * v.gR;
            }
            L[i] += (float)(l * gate); R[i] += (float)(r * gate);
            beatPos += beatInc;
        }
    }
    void setOffset(int off) { curOff = off; }
    const Voice& voice(int k) const { return voices[k]; }
    int soundingNote(int k) const { return sounding[k]; }

private:
    double sr = 48000;
    std::array<Voice, kMaxGoats> voices;
    std::array<int, kMaxGoats> sounding{};
    std::array<int, 128> noteRef{};
    struct Pending { int note, delay; };
    std::array<Pending, kMaxGoats> pend{};
    Pattern pattern;
    int wordsI = 0, curSyl = -1; long long sylUse = 0, samples = 0, lastSylAt = -(1LL << 40);
    void nextSyl() {
        ++sylUse;
        if (!wordsOn || !nWords) { curSyl = -1; return; }
        curSyl = wordsI++ % nWords; lastWord = words[(size_t)curSyl].wi; lastSylAt = samples;
    }
    bool arpOn = false, gateOff = false; long long arpBase = 0, arpStep = -1;
    Biquad chopLp; double chopC = 0, chopBase = 1, chopAmt = 0, chopVib = 0;
    int curOff = 0, builtKey = -1;
    double chordStartBeat = 0, motionG = 1, motionT = 1, motionC = 0.01; int chordCount = 0;

    void noteOn(int n) { if (noteRef[n]++ == 0) midiOut.push_back({curOff, n, true}); }
    void noteOff(int n) { if (noteRef[n] > 0 && --noteRef[n] == 0) midiOut.push_back({curOff, n, false}); }
    void startGoat(int k, int n) {
        if (sounding[k] >= 0) noteOff(sounding[k]);
        voices[k].sing(n, vowel, kVoices[singers[k].voice].fs, curSyl >= 0 ? &words[(size_t)curSyl] : nullptr, sylUse, pitch);
        sounding[k] = std::clamp(n + pitch, 0, 127); singers[k].last = n;   // MIDI out sends what is heard
        noteOn(n);
    }
    void quietGoat(int k, double rel = 0.14) {
        pend[k].note = -1;
        voices[k].quiet(rel);
        if (sounding[k] >= 0) { noteOff(sounding[k]); sounding[k] = -1; }
    }
    void stepProg() {
        double total = 0;
        for (int i = 0; i < progN; ++i) total += prog[i].bars * 4;
        if (total <= 0) return;
        double pos = std::fmod(beatPos - (hostPlaying ? 0.0 : progOrigin), total);
        if (pos < 0) pos += total;
        progBeat = pos;
        int idx = 0; double acc = 0;
        for (; idx < progN - 1; ++idx) { acc += prog[idx].bars * 4; if (pos < acc - 1e-9) break; }
        if (idx != progPos) { progPos = idx; playPlan(prog[idx].notes, prog[idx].chord, false, true); }
    }
};

// ---------------- MIDI file: exactly what the choir sang, one track, 96 ticks per beat ----------------
struct ExportItem { Plan notes; double bars; };
// the notes of one recorded chord in time, the way the choir sings it in a style: start and end in beats from the
// chord's start, the goat that sings it, and the step (stepped styles; 0 otherwise). strum = gap between goats in beats.
struct NoteSpan { int note; double a, b; int goat, step; };
inline void itemNotes(const Plan& notes, double bars, int style, double strum, std::vector<NoteSpan>& out) {
    const double len = bars * 4;
    std::pair<int, int> seq[kMaxGoats];
    const int n = sortedPlan(notes, seq);
    if (patterned(style)) {
        Pattern P;
        buildPattern(style, seq, n, P);
        for (int k = 0; P.n && k * 0.5 < len - 1e-6; ++k) {
            const int si = k % P.n;
            for (int j = 0; j < P.size[si]; ++j)
                out.push_back({P.step[si][j].second, k * 0.5, std::min(len, (k + P.gate) * 0.5), P.step[si][j].first, k});
        }
        return;
    }
    if (style != Strum && style != Down) strum = 0;
    for (int j = 0; j < n; ++j) out.push_back({seq[j].second, (style == Down ? n - 1 - j : j) * strum, len, seq[j].first, 0});
}
// strum = the Strum knob; motion (if on) is written as CC11 (expression) every 1/8 beat so the DAW plays the same swell
// syl (Words on): one lyric event (FF 05) per syllable, at its chord or step
inline std::vector<uint8_t> midiFile(const std::vector<ExportItem>& items, double tempo, int style, double strumAmt, const Motion* motion,
                                     const std::string& owner, const Syl* syl = nullptr, int nSyl = 0, int transpose = 0) {
    const int PPQ = 96;
    std::vector<uint8_t> b;
    auto vlq = [&](uint32_t v) { uint8_t o[5]; int n = 0; o[n++] = v & 127; while ((v >>= 7)) o[n++] = (v & 127) | 128; while (n) b.push_back(o[--n]); };
    auto meta = [&](uint8_t type, const std::string& s) { b.push_back(0); b.push_back(0xff); b.push_back(type); vlq((uint32_t)s.size()); for (char c : s) b.push_back((uint8_t)c & 127); };
    meta(0x02, owner);
    meta(0x03, "Choir chords");
    const uint32_t us = (uint32_t)std::lround(60e6 / tempo);
    for (uint8_t x : {0, 0xff, 0x51, 3}) b.push_back(x);
    b.push_back((us >> 16) & 255); b.push_back((us >> 8) & 255); b.push_back(us & 255);
    for (uint8_t x : {0, 0xff, 0x58, 4, 4, 2, 24, 8}) b.push_back(x);
    struct Ev { long t; int on, n; };   // on: 0 off, 1 on, 2 CC11, 3 lyric (n = syllable)
    std::vector<Ev> ev;
    double start = 0;
    int ci = 0;
    auto lyric = [&](double beat) { if (syl && nSyl > 0) ev.push_back({std::lround(beat * PPQ), 3, ci++ % nSyl}); };
    const double strum = style == Strum || style == Down ? strumAmt * 0.3 : 0;   // in beats
    std::vector<NoteSpan> spans;
    for (auto& it : items) {
        spans.clear();
        itemNotes(it.notes, it.bars, style, strum, spans);
        bool seen[128] = {};
        int lastStep = -1;
        for (auto& sp : spans) {
            if (sp.step != lastStep) { lastStep = sp.step; lyric(start + (patterned(style) ? sp.a : 0)); }
            // stepped styles keep doubled notes (pulse and bounce sing every goat); the others sing each pitch once
            if (!patterned(style)) { if (seen[sp.note]) continue; seen[sp.note] = true; }
            ev.push_back({std::lround((start + sp.a) * PPQ), 1, sp.note});
            ev.push_back({std::lround((start + sp.b) * PPQ), 0, sp.note});
        }
        start += it.bars * 4;
    }
    if (motion && motion->on) {
        std::vector<double> anchors; double a = 0;
        for (auto& it : items) { anchors.push_back(a); a += it.bars * 4; }
        int prev = -1;
        for (double bt = 0; bt < start; bt += 1.0 / 8) {
            int k = 0;
            for (int j = 0; j < (int)anchors.size(); ++j) if (anchors[(size_t)j] <= bt + 1e-9) k = j;
            const int v = (int)std::lround(127 * motion->gain(bt, bt - anchors[(size_t)k], k));
            if (v != prev) { ev.push_back({std::lround(bt * PPQ), 2, std::clamp(v, 0, 127)}); prev = v; }
        }
    }
    // at the same tick: note-offs, then the lyric, then note-ons, then CC
    static const int rank[4] = {0, 2, 3, 1};
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& x, const Ev& y) { return x.t != y.t ? x.t < y.t : rank[x.on] < rank[y.on]; });
    long last = 0;
    for (auto& e : ev) {
        vlq((uint32_t)(e.t - last));
        if (e.on == 3) {
            const std::string tx = syl[e.n].lyr;
            b.push_back(0xff); b.push_back(0x05); vlq((uint32_t)tx.size());
            for (char c : tx) b.push_back((uint8_t)c & 127);
        } else if (e.on == 2) { b.push_back(0xb0); b.push_back(11); b.push_back((uint8_t)e.n); }
        else { b.push_back(e.on ? 0x90 : 0x80); b.push_back((uint8_t)std::clamp(e.n + transpose, 0, 127)); b.push_back(e.on ? 96 : 0); }
        last = e.t;
    }
    vlq((uint32_t)std::max(0L, std::lround(start * PPQ) - last)); b.push_back(0xff); b.push_back(0x2f); b.push_back(0);
    std::vector<uint8_t> out{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, (uint8_t)PPQ, 'M', 'T', 'r', 'k'};
    const uint32_t len = (uint32_t)b.size();
    for (int s : {24, 16, 8, 0}) out.push_back((len >> s) & 255);
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

} // namespace goat
