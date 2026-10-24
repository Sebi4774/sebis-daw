#pragma once
// Words: type a line and the goats sing it, one syllable per chord (or per step in the stepped styles).
// Classic rule-based speech, ported from the app's main.js: hand-written letter rules and Klatt / Peterson-Barney
// formant tables (the same ones as the DAW's VOICE engine). No recordings, no trained models, no AI.
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace goat {

// one sound: type V vowel, F fricative, H aspirate, S stop, A affricate, N nasal, L liquid, G glide.
// f = formants (Hz), e = where a diphthong glides to, b = bandwidths, av = voicing, af = hiss, fh/fb = hiss band, d = seconds
struct Ph {
    std::string name; char t = 'V';
    double f[3]{}, e[3]{}, b[3]{}; bool hasE = false;
    double av = 0, af = 0, fh = 0, fb = 0, d = 0.1;
};

inline const std::vector<Ph>& phonemes() {
    static const std::vector<Ph> table = [] {
        const char* src[][2] = {
            {"iy", "V 310 2020 2960 0 0 0 45 200 400 1 0 0 0 120"}, {"ih", "V 400 1800 2570 0 0 0 50 100 140 1 0 0 0 90"},
            {"eh", "V 530 1680 2500 0 0 0 60 90 200 1 0 0 0 100"}, {"ae", "V 620 1660 2430 0 0 0 70 150 320 1 0 0 0 130"},
            {"aa", "V 700 1220 2600 0 0 0 130 70 160 1 0 0 0 130"}, {"ah", "V 620 1220 2550 0 0 0 80 50 140 1 0 0 0 100"},
            {"ao", "V 600 990 2570 0 0 0 90 100 80 1 0 0 0 130"}, {"uh", "V 450 1100 2350 0 0 0 80 100 80 1 0 0 0 90"},
            {"uw", "V 320 900 2200 0 0 0 65 110 140 1 0 0 0 120"}, {"er", "V 470 1270 1540 0 0 0 100 60 110 1 0 0 0 120"},
            {"ax", "V 500 1400 2450 0 0 0 100 50 140 1 0 0 0 60"}, {"ey", "V 480 1720 2520 330 2200 2600 70 100 200 1 0 0 0 140"},
            {"ay", "V 660 1200 2550 400 1880 2500 100 70 200 1 0 0 0 160"}, {"oy", "V 550 960 2400 360 1820 2450 60 50 130 1 0 0 0 160"},
            {"aw", "V 640 1230 2550 420 940 2350 80 70 140 1 0 0 0 160"}, {"ow", "V 540 1100 2300 450 900 2300 80 70 70 1 0 0 0 140"},
            {"s", "F 320 1390 2530 0 0 0 200 80 200 0 1.0 5500 1800 100"}, {"z", "F 240 1390 2530 0 0 0 70 60 180 0.45 0.6 5500 1800 80"},
            {"sh", "F 300 1840 2750 0 0 0 200 100 300 0 1.0 2800 1400 110"}, {"zh", "F 300 1840 2750 0 0 0 70 100 300 0.45 0.6 2800 1400 80"},
            {"f", "F 340 1100 2080 0 0 0 200 120 150 0 0.35 4500 4000 90"}, {"v", "F 220 1100 2080 0 0 0 60 90 120 0.5 0.25 4500 4000 60"},
            {"th", "F 320 1290 2540 0 0 0 200 90 200 0 0.3 5000 4000 90"}, {"dh", "F 270 1290 2540 0 0 0 60 80 170 0.5 0.2 5000 4000 50"},
            {"h", "H 0 0 0 0 0 0 300 160 300 0 0 0 0 70"}, {"p", "S 400 1100 2150 0 0 0 300 150 220 0 0.6 900 1600 90"},
            {"b", "S 200 1100 2150 0 0 0 60 110 130 0.25 0.4 900 1600 70"}, {"t", "S 400 1600 2600 0 0 0 300 120 250 0 0.8 4200 2000 90"},
            {"d", "S 200 1600 2600 0 0 0 60 100 170 0.25 0.5 4200 2000 70"}, {"k", "S 300 1990 2850 0 0 0 250 160 330 0 0.7 2200 1200 90"},
            {"g", "S 200 1990 2850 0 0 0 60 150 280 0.25 0.5 2200 1200 70"}, {"ch", "A 350 1800 2820 0 0 0 200 90 300 0 1.0 2800 1400 120"},
            {"jh", "A 260 1800 2820 0 0 0 60 80 270 0.4 0.6 2800 1400 100"}, {"m", "N 250 1100 2150 0 0 0 120 300 300 0.75 0 0 0 80"},
            {"n", "N 250 1600 2600 0 0 0 120 300 300 0.75 0 0 0 70"}, {"ng", "N 250 2000 2700 0 0 0 120 300 300 0.75 0 0 0 80"},
            {"l", "L 380 1000 2700 0 0 0 80 80 100 0.85 0 0 0 65"}, {"r", "L 350 1060 1380 0 0 0 80 80 120 0.85 0 0 0 65"},
            {"w", "G 290 610 2150 0 0 0 50 80 60 0.85 0 0 0 55"}, {"y", "G 260 2070 3020 0 0 0 40 250 500 0.85 0 0 0 55"}};
        std::vector<Ph> out;
        for (auto& s : src) {
            std::istringstream in(s[1]);
            Ph p; p.name = s[0];
            double x[14]{};
            in >> p.t;
            for (double& v : x) in >> v;
            for (int i = 0; i < 3; ++i) { p.f[i] = x[i]; p.e[i] = x[3 + i]; p.b[i] = x[6 + i]; }
            p.hasE = x[3] != 0;
            p.av = x[9]; p.af = x[10]; p.fh = x[11]; p.fb = x[12]; p.d = x[13] / 1000;
            out.push_back(p);
        }
        return out;
    }();
    return table;
}
inline const Ph* phoneme(const std::string& name) {
    for (auto& p : phonemes()) if (p.name == name) return &p;
    return nullptr;
}

// a syllable: consonants before, the vowel, consonants after; end = pause after it (Say), first = starts a word
struct Syl {
    static constexpr int kMaxCons = 8;
    const Ph* on[kMaxCons]{}; int nOn = 0;
    const Ph* nu = nullptr;
    const Ph* co[kMaxCons]{}; int nCo = 0;
    char lyr[28]{};   // the MIDI lyric: "word", "word-" or "-"
    double end = 0; bool ask = false, first = true; int wi = 0;
};
constexpr int kMaxSyl = 160;

inline const std::unordered_map<std::string, std::string>& wordDict() {
    static const std::unordered_map<std::string, std::string> d = {
        {"a", "ax"}, {"the", "dh ax"}, {"i", "ay"}, {"you", "y uw"}, {"me", "m iy"}, {"my", "m ay"}, {"we", "w iy"}, {"to", "t uw"},
        {"do", "d uw"}, {"of", "ah v"}, {"one", "w ah n"}, {"two", "t uw"}, {"three", "th r iy"}, {"four", "f ao r"}, {"five", "f ay v"},
        {"is", "ih z"}, {"was", "w ah z"}, {"are", "aa r"}, {"have", "h ae v"}, {"love", "l ah v"}, {"come", "k ah m"}, {"some", "s ah m"},
        {"what", "w ah t"}, {"who", "h uw"}, {"they", "dh ey"}, {"there", "dh eh r"}, {"their", "dh eh r"}, {"said", "s eh d"},
        {"says", "s eh z"}, {"been", "b ih n"}, {"done", "d ah n"}, {"gone", "g ao n"}, {"eye", "ay"}, {"eyes", "ay z"},
        {"heart", "h aa r t"}, {"night", "n ay t"}, {"light", "l ay t"}, {"world", "w er l d"}, {"hello", "h ax l ow"},
        {"voice", "v oy s"}, {"phantom", "f ae n t ax m"}, {"test", "t eh s t"}, {"through", "th r uw"}, {"cold", "k ow l d"},
        {"ice", "ay s"}, {"oh", "ow"}, {"yeah", "y eh"}, {"no", "n ow"}, {"go", "g ow"}, {"so", "s ow"}, {"know", "n ow"},
        {"don't", "d ow n t"}, {"can't", "k ae n t"}, {"i'm", "ay m"}, {"it's", "ih t s"}, {"your", "y ao r"}, {"want", "w aa n t"},
        {"again", "ax g eh n"}, {"only", "ow n l iy"}, {"every", "eh v r iy"}, {"people", "p iy p ax l"}, {"music", "m y uw z ih k"},
        {"alone", "ax l ow n"}, {"away", "ax w ey"}, {"baby", "b ey b iy"}, {"money", "m ah n iy"}, {"forever", "f ao r eh v er"},
        {"never", "n eh v er"}, {"ever", "eh v er"}, {"over", "ow v er"}, {"under", "ah n d er"}, {"feel", "f iy l"}, {"real", "r iy l"},
        {"army", "aa r m iy"}, {"man", "m ae n"}, {"red", "r eh d"}, {"dot", "d aa t"}, {"underworld", "ah n d er w er l d"},
        {"sing", "s ih ng"}, {"singing", "s ih ng ih ng"}, {"tokyo", "t ow k y ow"},
        {"goat", "g ow t"}, {"goats", "g ow t s"}, {"choir", "k w ay er"}, {"sebi", "s eh b iy"}, {"inc", "ih ng k"},
        {"mountain", "m aw n t ax n"}, {"mountains", "m aw n t ax n z"}, {"hallelujah", "h ae l ax l uw y ax"}, {"amen", "aa m eh n"},
        {"la", "l aa"}, {"ooh", "uw"}, {"aah", "aa"}, {"baa", "b aa"}, {"mah", "m aa"}, {"hey", "h ey"}, {"yo", "y ow"}, {"ah", "aa"},
        {"woah", "w ow"}, {"whoa", "w ow"}, {"life", "l ay f"}, {"time", "t ay m"}, {"mind", "m ay n d"}, {"blue", "b l uw"},
        {"true", "t r uw"}, {"gold", "g ow l d"}, {"soul", "s ow l"}, {"ghost", "g ow s t"}, {"mother", "m ah dh er"},
        {"father", "f aa dh er"}, {"together", "t ax g eh dh er"}};
    return d;
}

// English letters -> sounds, for words that are not in the dictionary
inline std::vector<std::string> letterRules(const std::string& w) {
    std::vector<std::string> out;
    auto add = [&](const std::string& s) { std::istringstream in(s); std::string p; while (in >> p) out.push_back(p); };
    const int n = (int)w.size();
    auto at = [&](int i) { return i >= 0 && i < n ? w[(size_t)i] : '\0'; };
    // like the app: an empty "next letter" (end of word) counts as a vowel here
    auto isV = [](char c) { return c == '\0' || std::strchr("aeiou", c) != nullptr; };
    auto st = [&](int i, const char* s) { return w.compare((size_t)i, std::strlen(s), s) == 0; };
    const bool finalE = n > 2 && w[(size_t)n - 1] == 'e' && !isV(w[(size_t)n - 2]) && w[(size_t)n - 2] != 'l';
    static const char* rules[][2] = {{"tion", "sh ax n"}, {"igh", "ay"}, {"ch", "ch"}, {"sh", "sh"}, {"th", "th"}, {"ph", "f"}, {"ng", "ng"},
                                     {"ck", "k"}, {"qu", "k w"}, {"wh", "w"}, {"ee", "iy"}, {"ea", "iy"}, {"oo", "uw"}, {"ou", "aw"},
                                     {"ai", "ey"}, {"ay", "ey"}, {"oi", "oy"}, {"oy", "oy"}, {"au", "ao"}, {"aw", "ao"}, {"oa", "ow"}};
    for (int i = 0; i < n;) {
        const char c = w[(size_t)i];
        if (finalE && i == n - 1) break;
        if (st(i, "ing") && i + 3 == n) { add("ih ng"); i += 3; continue; }
        if (st(i, "ow")) { add(i + 2 == n ? "ow" : "aw"); i += 2; continue; }
        bool hit = false;
        for (auto& r : rules) if (st(i, r[0])) { add(r[1]); i += (int)std::strlen(r[0]); hit = true; break; }
        if (hit) continue;
        if (st(i, "ie") && i + 2 == n) { add(n <= 3 ? "ay" : "iy"); i += 2; continue; }
        if (st(i, "ey") && i + 2 == n) { add("iy"); i += 2; continue; }
        if ((st(i, "er") || st(i, "ir") || st(i, "ur")) && !isV(at(i + 2))) { add("er"); i += 2; continue; }
        if (st(i, "ar") && !isV(at(i + 2))) { add("aa r"); i += 2; continue; }
        if (st(i, "or") && !isV(at(i + 2))) { add("ao r"); i += 2; continue; }
        if (i + 1 < n && c == w[(size_t)i + 1] && !isV(c)) { ++i; continue; }
        if (isV(c)) {
            const bool magic = finalE && i + 2 == n - 1 && !isV(at(i + 1)), open = i + 1 == n;
            switch (c) {
                case 'a': add(magic ? "ey" : open ? "aa" : "ae"); break;
                case 'e': add(magic || open ? "iy" : "eh"); break;
                case 'i': add(magic || (open && n <= 2) ? "ay" : open ? "iy" : "ih"); break;
                case 'o': add(magic || open ? "ow" : "aa"); break;
                default: add(magic || open ? "uw" : "ah"); break;
            }
            ++i; continue;
        }
        const char nx = at(i + 1);
        if (c == 'y') add(i == 0 ? "y" : n <= 3 ? "ay" : "iy");
        else if (c == 'c') add(nx && std::strchr("eiy", nx) ? "s" : "k");
        else if (c == 'g') add(nx && std::strchr("ei", nx) ? "jh" : "g");
        else if (c == 'x') add("k s");
        else if (c == 'j') add("jh");
        else if (c == 'q') add("k");
        else if (c == 's') add(i > 0 && i + 1 == n && !isV(w[(size_t)i - 1]) && !std::strchr("tkp", w[(size_t)i - 1]) ? "z" : "s");
        else if (phoneme(std::string(1, c))) add(std::string(1, c));
        ++i;
    }
    return out;
}

// a line of text -> syllables, split the way the app does (one vowel each)
inline std::vector<Syl> textToSyllables(const std::string& text) {
    std::vector<Syl> out;
    std::vector<std::string> toks;
    std::string cur;
    for (char ch : text) {
        const char c = (char)std::tolower((unsigned char)ch);
        if ((c >= 'a' && c <= 'z') || c == '\'') { cur += c; continue; }
        if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
        if (std::strchr(".,!?;:", c) && c) toks.push_back(std::string(1, c));
    }
    if (!cur.empty()) toks.push_back(cur);
    int wi = -1;
    for (auto& tok : toks) {
        bool letters = false;
        for (char c : tok) letters |= c >= 'a' && c <= 'z';
        if (!letters) {
            if (!out.empty()) { out.back().end = tok == "," ? 0.2 : 0.38; out.back().ask = tok == "?"; }
            continue;
        }
        std::vector<std::string> names;
        const auto& dict = wordDict();
        if (auto it = dict.find(tok); it != dict.end()) { std::istringstream in(it->second); std::string p; while (in >> p) names.push_back(p); }
        else { std::string bare; for (char c : tok) if (c != '\'') bare += c; names = letterRules(bare); }
        std::vector<const Ph*> p;
        for (auto& nm : names) if (auto* ph = phoneme(nm)) p.push_back(ph);
        if (p.empty()) continue;
        ++wi;
        std::vector<int> nuc;
        for (int i = 0; i < (int)p.size(); ++i) if (p[(size_t)i]->t == 'V') nuc.push_back(i);
        std::vector<Syl> syl;
        auto make = [&](int a, int ni, int e) {
            Syl s;
            for (int i = a; i < ni && s.nOn < Syl::kMaxCons; ++i) s.on[s.nOn++] = p[(size_t)i];
            s.nu = p[(size_t)ni];
            for (int i = ni + 1; i < e && s.nCo < Syl::kMaxCons; ++i) s.co[s.nCo++] = p[(size_t)i];
            syl.push_back(s);
        };
        if (nuc.empty()) make(0, (int)p.size() - 1, (int)p.size());
        else {
            int start = 0;
            for (size_t k = 0; k < nuc.size(); ++k) {
                const int ni = nuc[k];
                const int end = k + 1 < nuc.size() ? ni + 1 + (nuc[k + 1] - ni - 1 >= 2 ? 1 : 0) : (int)p.size();
                make(start, ni, end); start = end;
            }
        }
        for (size_t k = 0; k < syl.size(); ++k) {
            auto& s = syl[k];
            const std::string lyr = k ? "-" : syl.size() > 1 ? tok + "-" : tok;
            std::strncpy(s.lyr, lyr.c_str(), sizeof(s.lyr) - 1);
            s.first = k == 0; s.end = k + 1 == syl.size() ? 0.045 : 0; s.wi = wi;
        }
        for (auto& s : syl) if ((int)out.size() < kMaxSyl) out.push_back(s);
    }
    return out;
}

} // namespace goat
