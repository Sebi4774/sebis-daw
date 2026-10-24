#include "PluginEditor.h"

using namespace juce;

namespace gc {
namespace {
const int W = 1060, H = 960;
Font bold(float s) { return Font(s, Font::bold); }
Colour rgb(uint32 c) { return Colour(0xff000000 | c); }
float hash(int a, int b) { uint32 h = ((uint32)a * 73856093u) ^ ((uint32)b * 19349663u); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (h & 0xffff) / 65535.0f; }

// clay: a soft radial light from the upper left, a darker rim, a faint outline
void clayEllipse(Graphics& g, Rectangle<float> r, Colour c) {
    ColourGradient grad(c.brighter(0.22f), r.getX() + r.getWidth() * 0.34f, r.getY() + r.getHeight() * 0.28f,
                        c.darker(0.3f), r.getRight(), r.getBottom(), true);
    grad.addColour(0.55, c);
    g.setGradientFill(grad);
    g.fillEllipse(r);
    g.setColour(c.darker(0.5f).withAlpha(0.3f));
    g.drawEllipse(r, jmax(0.7f, r.getWidth() * 0.018f));
}
void clayLimb(Graphics& g, Point<float> a, Point<float> b, float w, Colour c) {
    Path p;
    const float len = a.getDistanceFrom(b);
    p.addRoundedRectangle(-w / 2, 0, w, len, w / 2);
    const float ang = std::atan2(b.y - a.y, b.x - a.x) - MathConstants<float>::halfPi;
    p.applyTransform(AffineTransform::rotation(ang).translated(a));
    ColourGradient grad(c.brighter(0.15f), a.x - w, a.y, c.darker(0.25f), a.x + w, b.y, false);
    g.setGradientFill(grad);
    g.fillPath(p);
    g.setColour(c.darker(0.5f).withAlpha(0.3f));
    g.strokePath(p, PathStrokeType(jmax(0.7f, w * 0.05f)));
}
} // namespace

// ---------------- look and feel ----------------
LnF::LnF() {
    setColour(ResizableWindow::backgroundColourId, col::bg);
    setColour(TextButton::buttonColourId, col::panel2);
    setColour(TextButton::buttonOnColourId, col::gold);
    setColour(TextButton::textColourOffId, col::ink);
    setColour(TextButton::textColourOnId, col::bg);
    setColour(ComboBox::backgroundColourId, col::panel2);
    setColour(ComboBox::textColourId, col::ink);
    setColour(ComboBox::arrowColourId, col::dim);
    setColour(ComboBox::outlineColourId, Colours::transparentBlack);
    setColour(PopupMenu::backgroundColourId, col::panel2);
    setColour(PopupMenu::textColourId, col::ink);
    setColour(PopupMenu::highlightedBackgroundColourId, col::tonic);
    setColour(PopupMenu::highlightedTextColourId, Colours::white);
    setColour(Label::textColourId, col::ink);
    setColour(ToggleButton::textColourId, col::ink);
    setColour(ToggleButton::tickColourId, col::gold);
    setColour(ToggleButton::tickDisabledColourId, col::dim);
    setColour(Slider::thumbColourId, col::gold);
    setColour(Slider::trackColourId, col::tonic);
    setColour(Slider::backgroundColourId, col::panel2);
    setColour(Slider::textBoxTextColourId, col::ink);
    setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour(ScrollBar::thumbColourId, col::dim.withAlpha(0.5f));
    setColour(TooltipWindow::backgroundColourId, col::panel2);
    setColour(TooltipWindow::textColourId, col::ink);
}
void LnF::drawButtonBackground(Graphics& g, Button& b, const Colour& base, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1);
    Colour c = b.getToggleState() ? b.findColour(TextButton::buttonOnColourId) : base;
    if (down) c = c.brighter(0.2f); else if (over) c = c.brighter(0.08f);
    g.setColour(c);
    g.fillRoundedRectangle(r, 7);
}
void LnF::drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box) {
    auto r = Rectangle<float>(0, 0, (float)w, (float)h).reduced(1);
    g.setColour(box.findColour(ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 7);
    Path tri;
    const float cx = w - 13.0f, cy = h * 0.5f;
    tri.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour(col::dim);
    g.fillPath(tri);
}
void LnF::drawLinearSlider(Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider&) {
    const float cy = y + h * 0.5f;
    g.setColour(col::panel2);
    g.fillRoundedRectangle((float)x, cy - 3, (float)w, 6, 3);
    g.setColour(col::tonic);
    g.fillRoundedRectangle((float)x, cy - 3, pos - x, 6, 3);
    g.setColour(col::gold);
    g.fillEllipse(pos - 7, cy - 7, 14, 14);
}

void LnF::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& sl) {
    const auto r = Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4);
    const float rad = jmin(r.getWidth(), r.getHeight()) / 2, cx = r.getCentreX(), cy = r.getCentreY();
    Path track, val;
    track.addCentredArc(cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
    // a bipolar knob (Pitch) grows its arc from the top
    const float from = sl.getProperties().contains("bipolar") ? (a0 + a1) / 2 : a0, to = a0 + (a1 - a0) * pos;
    val.addCentredArc(cx, cy, rad - 3, rad - 3, 0, jmin(from, to), jmax(from, to), true);
    g.setColour(col::panel2); g.strokePath(track, PathStrokeType(5, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour(col::gold); g.strokePath(val, PathStrokeType(5, PathStrokeType::curved, PathStrokeType::rounded));
    const float a = a0 + (a1 - a0) * pos - MathConstants<float>::halfPi;
    g.setColour(col::ink);
    g.drawLine(cx, cy, cx + std::cos(a) * (rad - 10), cy + std::sin(a) * (rad - 10), 2.5f);
}

// ---------------- stage ----------------
Stage::Stage(GoatChoirProcessor& p) : proc(p) { startTimerHz(12); }

// mode: 0 day, 1 sunset, 2 night. The land is tinted toward the light of the hour.
void Stage::drawBackground(Graphics& g, Rectangle<float> r, int mode) {
    const float w = r.getWidth(), h = r.getHeight();
    static const uint32 skyTop[3] = {0x7fbfea, 0xe58a6a, 0x0f1a33}, skyLow[3] = {0xd9eef3, 0xf6c79c, 0x24335a};
    static const uint32 tintC[3] = {0xffffff, 0xd9774a, 0x101a38};
    static const float tintA[3] = {0, 0.22f, 0.62f};
    auto T = [mode](Colour c) { return c.interpolatedWith(rgb(tintC[mode]), tintA[mode]); };
    auto rgbT = [&](uint32 c) { return T(rgb(c)); };
    g.setGradientFill(ColourGradient(rgb(skyTop[mode]), 0, 0, rgb(skyLow[mode]), 0, h * 0.62f, false));
    g.fillRect(r);
    if (mode == 2) {
        // stars and the moon
        for (int i = 0; i < 90; ++i) {
            const float x = w * hash(i, 21), y = h * 0.55f * hash(i, 22), s = 0.8f + 1.6f * hash(i, 23);
            g.setColour(Colours::white.withAlpha(0.35f + 0.6f * hash(i, 24))); g.fillEllipse(x, y, s, s);
        }
        g.setGradientFill(ColourGradient(Colour(0x55dfe6ff), w * 0.82f, h * 0.18f, Colour(0x00dfe6ff), w * 0.82f + h * 0.22f, h * 0.18f, true));
        g.fillEllipse(w * 0.82f - h * 0.22f, h * 0.18f - h * 0.22f, h * 0.44f, h * 0.44f);
        g.setColour(Colour(0xfff3f1e4)); g.fillEllipse(w * 0.82f - h * 0.07f, h * 0.18f - h * 0.07f, h * 0.14f, h * 0.14f);
        g.setColour(Colour(0xffd9d5c4)); g.fillEllipse(w * 0.82f - h * 0.02f, h * 0.18f - h * 0.035f, h * 0.035f, h * 0.03f);
    } else {
        // the sun: high in the day, low and orange at sunset
        const float sx = mode == 0 ? 0.16f : 0.74f, sy = mode == 0 ? 0.2f : 0.4f;
        const Colour glow = mode == 0 ? Colour(0xfffff1c9) : Colour(0xffffc27a), disc = mode == 0 ? Colour(0xfffff3d2) : Colour(0xffffd9a0);
        g.setGradientFill(ColourGradient(glow, w * sx, h * sy, glow.withAlpha(0.0f), w * sx + h * 0.32f, h * sy, true));
        g.fillEllipse(w * sx - h * 0.32f, h * sy - h * 0.32f, h * 0.64f, h * 0.64f);
        g.setColour(disc);
        g.fillEllipse(w * sx - h * 0.06f, h * sy - h * 0.06f, h * 0.12f, h * 0.12f);
    }
    // three rows of rounded clay hills, far to near, with pines on the first two
    struct Row { float base, amp, f1, f2, ph; uint32 c; int trees; };
    const Row rows[3] = {{0.5f, 0.12f, 2.1f, 5.3f, 0.4f, 0x8dbb6e, 9}, {0.6f, 0.1f, 2.9f, 6.1f, 2.2f, 0x6ea64c, 7}, {0.7f, 0.07f, 3.7f, 8.2f, 4.1f, 0x5b973a, 0}};
    for (int ri = 0; ri < 3; ++ri) {
        const auto& row = rows[ri];
        auto yAt = [&](float x) { const float u = x / w; return h * (row.base - row.amp * (0.6f * std::sin(u * row.f1 * 3.1f + row.ph) * 0.5f + 0.5f) - row.amp * 0.4f * std::sin(u * row.f2 * 3.1f + row.ph * 2)); };
        Path p;
        p.startNewSubPath(0, h);
        for (float x = 0; x <= w + 8; x += 8) p.lineTo(x, yAt(x));
        p.lineTo(w, h); p.closeSubPath();
        const Colour c = rgbT(row.c);
        g.setGradientFill(ColourGradient(c.brighter(0.12f), 0, h * (row.base - row.amp), c.darker(0.12f), 0, h, false));
        g.fillPath(p);
        for (int t = 0; t < row.trees; ++t) {
            const float x = w * (0.04f + 0.92f * hash(ri * 31 + t, 7)), y = yAt(x) + 4, s = h * (ri == 0 ? 0.045f : 0.065f) * (0.8f + 0.4f * hash(t, ri));
            const Colour tc = rgbT(ri == 0 ? 0x4f7f3d : 0x3d6e2f);
            g.setColour(rgbT(0x5a4632)); g.fillRect(x - s * 0.06f, y - s * 0.3f, s * 0.12f, s * 0.3f);
            for (int l = 0; l < 3; ++l) {
                Path tri;
                const float ty = y - s * 0.25f - l * s * 0.32f, tw = s * (0.55f - l * 0.12f);
                tri.addTriangle(x - tw, ty, x + tw, ty, x, ty - s * 0.55f);
                g.setColour(tc.brighter(l * 0.06f));
                g.fillPath(tri);
            }
        }
    }
    // the grassy ledge the goats stand on
    Rectangle<float> ledge(-w * 0.1f, h * 0.74f, w * 1.2f, h * 0.6f);
    g.setGradientFill(ColourGradient(rgbT(0x6cab42), 0, h * 0.74f, rgbT(0x4c8a2f), 0, h, false));
    g.fillEllipse(ledge);
    for (int i = 0; i < 70; ++i) {
        const float x = w * hash(i, 3), y = h * (0.79f + 0.2f * hash(i, 5));
        if (hash(i, 9) < 0.55f) {
            g.setColour(rgbT(0x4a8a2e).brighter(0.1f * hash(i, 4)));
            Path tuft; tuft.addTriangle(x - 3, y, x + 3, y, x + (hash(i, 6) - 0.5f) * 4, y - 7 - 6 * hash(i, 2));
            g.fillPath(tuft);
        } else {
            static const uint32 petals[4] = {0xfff1a8, 0xffb3c8, 0xffffff, 0xc9b6ff};
            const Colour pc = rgbT(petals[i % 4]);
            for (int k = 0; k < 5; ++k) {
                const float a = k * MathConstants<float>::twoPi / 5;
                g.setColour(pc); g.fillEllipse(x + std::cos(a) * 3 - 2.2f, y + std::sin(a) * 3 - 2.2f, 4.4f, 4.4f);
            }
            g.setColour(rgbT(0xe8b33a)); g.fillEllipse(x - 1.8f, y - 1.8f, 3.6f, 3.6f);
        }
    }
}

void Stage::drawGoat(Graphics& g, int k, float cx, float gy, float u, int idx) {
    const auto& cm = goat::kCast[(size_t)k];
    const Colour fur = rgb(cm.fur), scarf = rgb(goat::kLooks[(size_t)proc.look[(size_t)k]].c), dark(0xff2a2624);
    const float t = frame / 12.0f;
    const float lvl = jlimit(0.0f, 1.2f, proc.goatLevel[(size_t)k].load());
    const bool singing = proc.goatNote[(size_t)k].load() >= 0, muted = proc.muted[(size_t)k].load();
    const auto& vw = goat::kVowels[(size_t)proc.choice("vowel")];
    const int snd = proc.sound[(size_t)k].load();
    float jaw = jlimit(0.0f, 1.0f, lvl * (0.25f + 0.75f * vw.open) * (0.8f + 0.2f * std::sin(t * 9 + k)));
    if (snd == goat::SndTrumpet || snd == goat::SndFlute) jaw = jmin(jaw, 0.06f);   // lips on the mouthpiece
    const float beat = (float)proc.hostBpm.load() / 60.0f;
    const float bob = singing ? std::abs(std::sin(t * MathConstants<float>::pi * beat)) * u * 0.035f : std::sin(t * 1.3f + k) * u * 0.006f;
    const float tilt = singing ? std::sin(t * 1.7f + k * 1.3f) * 0.08f : std::sin(t * 0.6f + k) * 0.02f;
    const bool blink = ((frame + k * 17) % 46) < 2;
    const float jx = (hash(frame, k) - 0.5f) * 1.2f, jy = (hash(k, frame) - 0.5f) * 1.2f;   // stop-motion boil
    auto P = [&](float x, float y) { return Point<float>(cx + x * u + jx, gy - y * u - bob + jy); };
    auto E = [&](float x, float y, float w, float h) { return Rectangle<float>(w * u, h * u).withCentre(P(x, y)); };

    // shadow and selection ring
    g.setColour(Colours::black.withAlpha(0.18f));
    g.fillEllipse(Rectangle<float>(u * 0.55f, u * 0.08f).withCentre({cx, gy + 2}));
    if (idx == proc.selected.load()) {
        g.setColour(Colours::white.withAlpha(0.75f));
        g.drawEllipse(Rectangle<float>(u * 0.7f, u * 0.12f).withCentre({cx, gy + 2}), 2.0f);
    }
    // hind legs with black hooves
    for (float sx : {-1.0f, 1.0f}) {
        clayLimb(g, P(sx * 0.09f, 0.36f), P(sx * 0.1f, 0.05f), u * 0.12f, fur);
        clayEllipse(g, E(sx * 0.1f, 0.03f, 0.12f, 0.06f), dark);
    }
    // body: a shaggy pear with a woolly chest
    clayEllipse(g, E(0, 0.47f, 0.46f, 0.52f), fur);
    for (int m = 0; m < 6; ++m) {
        const float a = (m / 5.0f - 0.5f) * 2.2f;
        clayEllipse(g, E(std::sin(a) * 0.13f, 0.6f - std::abs(a) * 0.05f, 0.1f, 0.12f), fur.brighter(0.04f));
    }
    // front legs held in front like a singer's hands
    for (float sx : {-1.0f, 1.0f}) {
        const float lift = singing ? 0.04f + 0.03f * std::sin(t * 2.4f + sx + k) : 0.0f;
        clayLimb(g, P(sx * 0.17f, 0.64f), P(sx * 0.11f, 0.44f + lift), u * 0.085f, fur);
        clayEllipse(g, E(sx * 0.11f, 0.43f + lift, 0.08f, 0.05f), dark);
    }
    // scarf
    clayEllipse(g, E(0, 0.7f, 0.36f, 0.11f), scarf);
    {
        Graphics::ScopedSaveState ss(g);
        g.addTransform(AffineTransform::rotation(0.35f, P(0.12f, 0.68f).x, P(0.12f, 0.68f).y));
        clayEllipse(g, E(0.12f, 0.62f, 0.065f, 0.14f), scarf.darker(0.08f));
    }
    // head, tilted around the neck
    {
    Graphics::ScopedSaveState ss(g);
    const auto neck = P(0, 0.74f);
    g.addTransform(AffineTransform::rotation(tilt, neck.x, neck.y));
    for (float sx : {-1.0f, 1.0f}) {   // horns
        Path horn;
        horn.startNewSubPath(P(sx * 0.06f, 1.0f));
        horn.quadraticTo(P(sx * 0.055f, 1.16f), P(sx * 0.15f, 1.14f));
        g.setColour(dark);
        g.strokePath(horn, PathStrokeType(u * 0.04f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    for (float sx : {-1.0f, 1.0f}) {   // floppy ears
        Graphics::ScopedSaveState es(g);
        const auto e = P(sx * 0.19f, 0.92f);
        g.addTransform(AffineTransform::rotation(sx * 0.45f, e.x, e.y));
        clayEllipse(g, E(sx * 0.19f, 0.92f, 0.19f, 0.075f), fur.darker(0.05f));
    }
    clayEllipse(g, E(0, 0.9f, 0.35f, 0.31f), fur);
    for (float sx : {-1.0f, 1.0f}) {   // eyes with sideways goat pupils
        const auto er = E(sx * 0.082f, 0.93f, 0.082f, 0.082f);
        g.setColour(Colour(0xfffbf6e8)); g.fillEllipse(er);
        if (blink) {
            g.setColour(fur.darker(0.06f)); g.fillEllipse(er.expanded(1));
            g.setColour(dark); g.drawLine(er.getX(), er.getCentreY(), er.getRight(), er.getCentreY(), 1.5f);
        } else {
            g.setColour(Colour(0xff1d1b1a));
            g.fillRoundedRectangle(Rectangle<float>(u * 0.045f, u * 0.014f).withCentre(er.getCentre().translated(0, u * 0.004f)), u * 0.007f);
            g.setColour(Colours::white.withAlpha(0.8f)); g.fillEllipse(er.getX() + er.getWidth() * 0.25f, er.getY() + er.getHeight() * 0.18f, 2.2f, 2.2f);
        }
    }
    g.setColour(Colour(0xffff9fae).withAlpha(0.35f));   // rosy cheeks
    for (float sx : {-1.0f, 1.0f}) g.fillEllipse(E(sx * 0.115f, 0.86f, 0.06f, 0.035f));
    clayEllipse(g, E(0, 0.82f, 0.19f, 0.13f), fur.brighter(0.05f));   // snout
    clayEllipse(g, E(0, 0.765f - 0.04f * jaw, 0.11f, 0.05f), fur);       // lower jaw
    clayEllipse(g, E(0, 0.715f - 0.045f * jaw, 0.055f, 0.09f), fur.darker(0.07f));   // beard
    const float mw = 0.075f * (1 - 0.4f * vw.round), mh = 0.012f + 0.06f * jaw;
    g.setColour(Colour(0xff3a1a1f)); g.fillEllipse(E(0, 0.79f - 0.02f * jaw, mw, mh));
    if (jaw > 0.2f) { g.setColour(Colour(0xffd96f80)); g.fillEllipse(E(0, 0.79f - 0.02f * jaw - mh * 0.22f, mw * 0.6f, mh * 0.4f)); }
    g.setColour(Colour(0xff2e2a2a)); g.fillEllipse(E(0, 0.855f, 0.065f, 0.028f));   // nose
    if (goat::kSoundDefs[(size_t)snd].inst) drawInstrument(g, snd, P, u, lvl, t);
    }

    // the note above the head, the name under the feet
    const int note = proc.goatNote[(size_t)k].load();
    if (note >= 0) {
        const auto lp = P(0, 1.3f);
        const String s = goat::noteName(note);
        g.setFont(bold(13));
        const float tw = g.getCurrentFont().getStringWidthFloat(s) + 14;
        g.setColour(col::bg.withAlpha(0.72f));
        g.fillRoundedRectangle(Rectangle<float>(tw, 20).withCentre(lp), 10);
        g.setColour(col::ink); g.drawText(s, Rectangle<float>(tw, 20).withCentre(lp), Justification::centred);
    }
    g.setFont(bold(12));
    g.setColour(proc.stageMode.load() == 2 ? Colour(0xffc9cfe6).withAlpha(muted ? 0.5f : 0.85f) : muted ? Colour(0xff3a3340) : Colour(0xff1f3a17));
    g.drawText(String(cm.name) + (muted ? " (muted)" : ""), Rectangle<float>(u, 16).withCentre({cx, gy + 14}), Justification::centred);
    hit[(size_t)k] = Rectangle<float>(cx - u * 0.3f, gy - u * 1.15f, u * 0.6f, u * 1.2f);
}

// the instrument a goat holds and plays: clay trumpet, flute, violin and bow, tuba
void Stage::drawInstrument(Graphics& g, int snd, std::function<Point<float>(float, float)> P, float u, float lvl, float t) {
    const Colour gold(0xffe2b548), silver(0xffd9dde3), wood(0xff9a4f22), woodDark(0xff3a2116);
    const float pulse = 1 + 0.06f * jmin(1.0f, lvl) * (0.5f + 0.5f * std::sin(t * 6.5f));
    auto tube = [&](Point<float> a, Point<float> b, float wd, Colour c) {
        Path p; p.startNewSubPath(a); p.lineTo(b);
        g.setColour(c.darker(0.25f)); g.strokePath(p, PathStrokeType(wd + 1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(c); g.strokePath(p, PathStrokeType(wd, PathStrokeType::curved, PathStrokeType::rounded));
    };
    if (snd == goat::SndTrumpet) {
        // from the lips out and down to the goat's left, bell toward the audience
        tube(P(0.0f, 0.79f), P(0.3f, 0.63f), u * 0.035f, gold);
        tube(P(0.08f, 0.71f), P(0.24f, 0.62f), u * 0.025f, gold);
        for (int v = 0; v < 3; ++v) tube(P(0.1f + v * 0.045f, 0.75f - v * 0.024f), P(0.1f + v * 0.045f, 0.8f - v * 0.024f), u * 0.018f, silver);
        const auto bell = P(0.33f, 0.61f);
        const float bw = u * 0.15f * pulse, bh = u * 0.17f * pulse;
        clayEllipse(g, Rectangle<float>(bw, bh).withCentre(bell), gold);
        g.setColour(gold.darker(0.5f)); g.fillEllipse(Rectangle<float>(bw * 0.55f, bh * 0.55f).withCentre(bell));
    } else if (snd == goat::SndFlute) {
        // sideways at the mouth, out to the goat's left
        tube(P(-0.06f, 0.785f), P(0.5f, 0.755f), u * 0.03f, silver);
        for (int k = 0; k < 6; ++k) { g.setColour(silver.darker(0.35f)); g.fillEllipse(Rectangle<float>(u * 0.022f, u * 0.022f).withCentre(P(0.16f + k * 0.055f, 0.778f - k * 0.003f))); }
    } else if (snd == goat::SndViolin) {
        // under the chin on the goat's right shoulder, the bow sawing across
        Graphics::ScopedSaveState ss(g);
        const auto c = P(-0.17f, 0.64f);
        g.addTransform(AffineTransform::rotation(0.75f, c.x, c.y));
        clayEllipse(g, Rectangle<float>(u * 0.17f, u * 0.13f).withCentre(c.translated(u * 0.03f, 0)), wood);
        clayEllipse(g, Rectangle<float>(u * 0.12f, u * 0.1f).withCentre(c.translated(-u * 0.09f, 0)), wood);
        g.setColour(woodDark); g.fillRect(Rectangle<float>(u * 0.22f, u * 0.025f).withCentre(c.translated(-u * 0.22f, 0)));
        g.fillEllipse(Rectangle<float>(u * 0.05f, u * 0.04f).withCentre(c.translated(-u * 0.34f, 0)));
        g.setColour(woodDark.withAlpha(0.8f));
        for (float sy : {-1.0f, 1.0f}) g.fillRoundedRectangle(Rectangle<float>(u * 0.01f, u * 0.035f).withCentre(c.translated(u * 0.01f, sy * u * 0.03f)), 2);
        const float saw = std::sin(t * 6.5f * 0.55f) * 0.1f * jmin(1.0f, lvl);
        Path bow; bow.startNewSubPath(c.translated(u * (0.05f + saw) - u * 0.02f, -u * 0.25f)); bow.lineTo(c.translated(u * (0.05f + saw) + u * 0.02f, u * 0.25f));
        g.setColour(woodDark); g.strokePath(bow, PathStrokeType(u * 0.012f));
        g.setColour(Colour(0xfff3ead8)); g.strokePath(bow, PathStrokeType(u * 0.005f), AffineTransform::translation(u * 0.012f, 0));
    } else if (snd == goat::SndTuba) {
        // a big gold coil in front of the belly, the bell up over the goat's left shoulder
        const auto c = P(0.0f, 0.42f);
        Path coil; coil.addEllipse(Rectangle<float>(u * 0.27f, u * 0.25f).withCentre(c));
        g.setColour(gold.darker(0.25f)); g.strokePath(coil, PathStrokeType(u * 0.075f));
        g.setColour(gold); g.strokePath(coil, PathStrokeType(u * 0.06f));
        tube(P(0.12f, 0.48f), P(0.24f, 0.78f), u * 0.06f, gold);
        for (int v = 0; v < 3; ++v) tube(P(-0.04f + v * 0.035f, 0.52f), P(-0.04f + v * 0.035f, 0.58f), u * 0.022f, silver);
        const auto bell = P(0.27f, 0.84f);
        const float bw = u * 0.26f * pulse, bh = u * 0.11f * pulse;
        clayEllipse(g, Rectangle<float>(bw, bh).withCentre(bell), gold);
        g.setColour(gold.darker(0.5f)); g.fillEllipse(Rectangle<float>(bw * 0.7f, bh * 0.5f).withCentre(bell.translated(0, -bh * 0.08f)));
    }
}

void Stage::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat();
    const int mode = jlimit(0, 2, proc.stageMode.load());
    if (bg.isNull() || bg.getWidth() != getWidth() || bg.getHeight() != getHeight() || bgMode != mode) {
        bg = Image(Image::RGB, jmax(1, getWidth()), jmax(1, getHeight()), true);
        Graphics bgG(bg);
        drawBackground(bgG, r, mode);
        bgMode = mode;
    }
    g.drawImageAt(bg, 0, 0);
    const float t = frame / 12.0f, w = r.getWidth(), h = r.getHeight();
    // drifting clouds and pollen
    for (int c = 0; c < 4; ++c) {
        const float x = std::fmod(w * hash(c, 1) + t * (6 + 4 * c), w + 220) - 110, y = h * (0.08f + 0.1f * c), s = 26 + 8 * hash(c, 2);
        g.setColour(mode == 0 ? Colours::white.withAlpha(0.85f) : mode == 1 ? Colour(0xd9ffd2c0) : Colour(0x8056607d));
        for (int b = 0; b < 4; ++b) g.fillEllipse(x + b * s * 0.7f, y - (b % 2) * s * 0.3f, s * 1.2f, s * 0.8f);
    }
    // pollen by day, fireflies at night
    g.setColour(mode == 2 ? Colour(0xfffff07a).withAlpha(0.85f) : Colour(0xfffff6c8).withAlpha(0.7f));
    for (int i = 0; i < 26; ++i) {
        const float x = std::fmod(w * hash(i, 11) + std::sin(t * 0.4f + i) * 20 + t * 3, w), y = h - std::fmod(h * hash(i, 12) + t * (6 + 4 * hash(i, 13)), h * 0.9f);
        g.fillEllipse(x, y, 2.4f, 2.4f);
    }

    // goats in a row, alternate ones a step back
    const int n = proc.choice("goats");
    const float spacing = w / (n + 1.0f);
    hit.fill({});
    std::vector<int> order;
    for (int k = 0; k < n; ++k) order.push_back(k);
    std::stable_sort(order.begin(), order.end(), [](int a, int b) { return (a % 2) > (b % 2); });   // back row first
    for (int k : order) {
        const float u = jmin(h * 0.52f, spacing * 1.25f) * goat::kCast[(size_t)k].size;
        drawGoat(g, k, spacing * (k + 1), h - 34 - (k % 2) * 14, u, k);
    }

    // Night: the land goes dark and a spotlight follows the selected goat
    spotOn += ((mode == 2 ? 1.0f : 0.0f) - spotOn) * 0.3f;
    const int sel = jlimit(0, n - 1, proc.selected.load());
    const float tx = spacing * (sel + 1), gy = h - 34 - (sel % 2) * 14, su = jmin(h * 0.52f, spacing * 1.25f);
    spotX = spotX < 0 ? tx : spotX + (tx - spotX) * 0.35f;
    if (spotOn > 0.01f) {
        const float poolW = su * 0.9f, poolH = su * 0.22f, sing = proc.goatLevel[(size_t)sel].load() > 0.05f ? 1.0f : 0.0f;
        Path dark;
        dark.setUsingNonZeroWinding(false);
        dark.addRectangle(r);
        Path beam;
        beam.startNewSubPath(spotX - 26, 0); beam.lineTo(spotX + 26, 0);
        beam.lineTo(spotX + poolW * 0.5f, gy); beam.lineTo(spotX - poolW * 0.5f, gy); beam.closeSubPath();
        dark.addPath(beam);
        g.setColour(Colour(0xff0b1430).withAlpha(0.42f * spotOn));
        g.fillPath(dark);
        g.setGradientFill(ColourGradient(Colour(0xfffff0cf).withAlpha((0.16f + 0.05f * sing) * spotOn), spotX, 0,
                                         Colour(0xffffe2aa).withAlpha(0.06f * spotOn), spotX, gy, false));
        g.fillPath(beam);
        const auto pool = Rectangle<float>(poolW, poolH).withCentre({spotX, gy + 2});
        g.setGradientFill(ColourGradient(Colour(0xffffe2aa).withAlpha(0.5f * spotOn), pool.getCentreX(), pool.getCentreY(),
                                         Colour(0x00ffd696), pool.getRight(), pool.getCentreY(), true));
        g.fillEllipse(pool);
    }

    // the chord being sung
    const int cur = proc.currentChord.load();
    String name, roman;
    if (cur >= 0) {
        name = String::fromUTF8(lab[(size_t)cur].name.c_str()); roman = String::fromUTF8(lab[(size_t)cur].roman.c_str());
        // with the Pitch knob moved, show the chord that sounds (the strips stay in the key)
        if (const int sh = proc.pitch()) {
            const int len = name.length() > 1 && name[1] == '#' ? 2 : 1;
            int pc = 0;
            while (pc < 12 && name.substring(0, len) != goat::kNames[pc]) ++pc;
            if (pc < 12) name = goat::kNames[(pc + sh + 120) % 12] + name.substring(len);
        }
    }
    else {
        for (int k = 0; k < goat::kMaxGoats; ++k)
            if (proc.goatNote[(size_t)k].load() >= 0) { name = "Singing"; break; }
    }
    if (name.isNotEmpty()) {
        g.setFont(bold(30));
        const float tw = jmax(g.getCurrentFont().getStringWidthFloat(name), 60.0f) + 30;
        g.setColour(col::bg.withAlpha(0.7f));
        g.fillRoundedRectangle(12, 12, tw, 62, 12);
        g.setColour(col::ink); g.drawText(name, 27, 14, (int)tw, 36, Justification::left);
        g.setFont(bold(14)); g.setColour(col::gold); g.drawText(roman, 27, 48, (int)tw, 20, Justification::left);
    }
    // tempo and transport, top right
    const String hostTxt = String((int)std::lround(proc.hostBpm.load())) + " BPM" + (proc.hostPlaying.load() ? "  |  song playing" : "");
    g.setFont(bold(13));
    const float bw = g.getCurrentFont().getStringWidthFloat(hostTxt) + 22;
    g.setColour(col::bg.withAlpha(0.65f)); g.fillRoundedRectangle(w - bw - 12, 12, bw, 26, 13);
    g.setColour(col::ink); g.drawText(hostTxt, Rectangle<float>(w - bw - 12, 12, bw, 26), Justification::centred);
    if (GoatChoirProcessor::kDemo) {
        g.setColour(col::dom); g.fillRoundedRectangle(w - 90, 44, 78, 22, 11);
        g.setColour(Colours::white); g.setFont(bold(12)); g.drawText("DEMO", Rectangle<float>(w - 90, 44, 78, 22), Justification::centred);
    }
}

void Stage::mouseDown(const MouseEvent& e) {
    for (int k = 0; k < goat::kMaxGoats; ++k)
        if (hit[(size_t)k].contains(e.position)) { proc.selected = k; if (onSelect) onSelect(); repaint(); return; }
}

// ---------------- chord strips ----------------
void StripButton::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(2);
    const Colour fc = col::fn(chord.fn);
    const bool locked = GoatChoirProcessor::stripLocked(idx);
    g.setColour(on ? fc.withAlpha(0.42f) : (idx < 8 ? col::panel : col::panel2));
    g.fillRoundedRectangle(r, 10);
    if (on) { g.setColour(fc); g.drawRoundedRectangle(r, 10, 2); }
    g.setColour(fc); g.fillRoundedRectangle(r.getX() + 6, r.getY() + 8, 5, r.getHeight() - 16, 2.5f);
    g.setColour(col::dim); g.setFont(bold(11));
    g.drawText(String(idx + 1), (int)r.getX() + 18, (int)r.getY() + 5, 30, 14, Justification::left);
    g.drawText(goat::noteName(goat::kFirstStripNote + idx), (int)r.getRight() - 44, (int)r.getY() + 5, 38, 14, Justification::right);
    g.setColour(col::ink); g.setFont(bold(18));
    g.drawFittedText(String::fromUTF8(lab.name.c_str()), (int)r.getX() + 18, (int)r.getY() + 17, (int)r.getWidth() - 24, 22, Justification::left, 1);
    g.setColour(fc.brighter(0.35f)); g.setFont(bold(12));
    g.drawText(String::fromUTF8(lab.roman.c_str()), (int)r.getX() + 18, (int)r.getBottom() - 19, (int)r.getWidth() - 50, 15, Justification::left);
    for (int k = 0; k < chord.n; ++k) {   // one dot per note in the chord
        g.setColour(fc.withAlpha(0.85f));
        g.fillEllipse(r.getRight() - 12 - k * 8.0f, r.getBottom() - 14, 5, 5);
    }
    if (locked) {
        g.setColour(col::bg.withAlpha(0.6f)); g.fillRoundedRectangle(r, 10);
        g.setColour(col::gold); g.setFont(bold(11));
        g.drawText("FULL VERSION", r, Justification::centred);
    }
}

// ---------------- segmented controls ----------------
Seg::Seg(GoatChoirProcessor& p, const char* paramId, const String& t) : title(t) {
    auto* param = p.apvts.getParameter(paramId);
    const auto opts = param->getAllValueStrings();
    for (int i = 0; i < opts.size(); ++i) {
        auto* b = buttons.add(new TextButton(opts[i]));
        b->setClickingTogglesState(false);
        b->onClick = [this, i] { if (att) att->setValueAsCompleteGesture((float)i); };
        addAndMakeVisible(b);
    }
    att = std::make_unique<ParameterAttachment>(*param, [this](float v) {
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState(i == (int)std::lround(v), dontSendNotification);
    });
    att->sendInitialUpdate();
}
void Seg::setRows(int r, bool small) {
    rows = jmax(1, r);
    for (auto* b : buttons) {
        if (rows > 1 || small) b->getProperties().set("small", true); else b->getProperties().remove("small");
        b->setButtonText(b->getButtonText().replace(" Dn", String::fromUTF8(" \xe2\x86\x93")));   // Arp Dn -> Arp (down arrow)
    }
    resized();
}
void Seg::resized() {
    auto r = getLocalBounds().withTrimmedTop(16);
    const int perRow = (buttons.size() + rows - 1) / rows, rh = r.getHeight() / rows;
    const int bw = r.getWidth() / jmax(1, perRow);
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds(Rectangle<int>(r.getX() + (i % perRow) * bw, r.getY() + (i / perRow) * rh, bw, rh).reduced(1, rows > 1 ? 1 : 0));
}
void Seg::paint(Graphics& g) {
    g.setColour(col::dim); g.setFont(bold(11));
    g.drawText(title.toUpperCase(), 2, 0, getWidth(), 14, Justification::left);
}

// ---------------- Words ----------------
void WordsLine::paint(Graphics& g) {
    g.setColour(col::panel); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6);
    const auto& list = proc.wordsList;
    if (list.isEmpty()) {
        g.setColour(col::dim); g.setFont(Font(12.5f));
        g.drawText("one syllable per chord, or per step", getLocalBounds().reduced(10, 0), Justification::centredLeft);
        return;
    }
    const int cur = proc.sungWord.load();
    Font f = bold(14);
    // keep the word being sung in view
    float x = 0, curX = 0, curW = 0;
    for (int k = 0; k < list.size(); ++k) {
        const float w = f.getStringWidthFloat(list[k] + " ");
        if (k == cur) { curX = x; curW = w; }
        x += w;
    }
    const float inner = (float)getWidth() - 20, off = jmax(0.0f, curX + curW - inner);
    g.reduceClipRegion(getLocalBounds().reduced(8, 0));
    g.setFont(f);
    x = 10 - off;
    for (int k = 0; k < list.size(); ++k) {
        const float w = f.getStringWidthFloat(list[k] + " ");
        g.setColour(k == cur ? col::gold : proc.wordsOn ? col::ink : col::dim);
        g.drawText(list[k], Rectangle<float>(x, 0, w + 4, (float)getHeight()), Justification::centredLeft);
        x += w;
    }
}

// ---------------- note graph ----------------
void NoteGraph::timerCallback() {
    // smooth while the progression plays; otherwise only when it changes (and now and then for knob changes)
    const bool playing = proc.progBeat.load() >= 0;
    if (playing || proc.progVersion != seenVersion || ++idle > 8) { seenVersion = proc.progVersion; idle = 0; repaint(); }
}
void NoteGraph::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setGradientFill(ColourGradient(Colour(0xff1b1724), 0, 0, Colour(0xff0d0b12), 0, r.getHeight(), false));
    g.fillRoundedRectangle(r, 8);
    const auto& items = proc.prog;
    if (items.empty()) {
        g.setColour(col::dim); g.setFont(Font(13.0f));
        g.drawText("NOTE GRAPH: record a progression and every note the goats sing shows up here", r, Justification::centred);
        return;
    }
    // every note the choir will sing, in beats from the start, the way the current style sings it
    const int style = proc.choice("style"), shift = proc.pitch();
    const double strum = proc.apvts.getRawParameterValue("strum")->load() * 0.3;
    struct N { int n; double a, b; int goat; };
    std::vector<N> notes; std::vector<std::pair<double, double>> secs; std::vector<goat::NoteSpan> sp;
    double B = 0;
    for (auto& it : items) {
        sp.clear();
        goat::itemNotes(it.notes, it.bars, style, strum, sp);
        for (auto& x : sp) notes.push_back({x.note + shift, B + x.a, B + x.b, x.goat});
        secs.push_back({B, B + it.bars * 4});
        B += it.bars * 4;
    }
    if (notes.empty() || B <= 0) return;
    int lo = 127, hi = 0;
    for (auto& x : notes) { lo = jmin(lo, x.n); hi = jmax(hi, x.n); }
    const double mid = (lo + hi) / 2.0, span = jmax(18.0, hi - lo + 3.0), bot = mid - span / 2;
    const float pad = 8, top = 20, W = r.getWidth(), H = r.getHeight();
    auto X = [&](double b) { return pad + (float)(b / B) * (W - 2 * pad); };
    auto Y = [&](double n) { return top + (float)(1 - (n - bot) / span) * (H - top - pad); };
    const float nh = jmax(4.0f, (H - top - pad) / (float)span * 1.5f);
    const double ph = proc.progBeat.load();
    // octave lines
    g.setFont(Font(10.0f));
    for (int n = (int)std::ceil(bot / 12) * 12; n <= bot + span; n += 12) {
        const float y = Y(n);
        g.setColour(Colours::white.withAlpha(0.06f)); g.drawHorizontalLine((int)y, pad, W - pad);
        g.setColour(Colours::white.withAlpha(0.22f)); g.drawText(goat::noteName(n), (int)(W - pad - 26), (int)y - 12, 24, 10, Justification::right);
    }
    // chord sections: soft bands, names along the top
    g.setFont(bold(11));
    for (size_t k = 0; k < secs.size(); ++k) {
        const auto [a, b] = secs[k];
        const bool on = ph >= a && ph < b;
        const float x0 = X(a), x1 = X(b);
        if (k % 2) { g.setColour(Colours::white.withAlpha(0.025f)); g.fillRect(x0, top - 4, x1 - x0, H - top); }
        if (on) { g.setGradientFill(ColourGradient(Colour(0x1affd76a), 0, top, Colour(0x00ffd76a), 0, H, false)); g.fillRect(x0, top - 4, x1 - x0, H - top); }
        g.setColour(Colours::white.withAlpha(0.08f)); g.drawVerticalLine((int)x0, top - 4, H - pad);
        g.setColour(on ? Colour(0xffffd76a) : Colours::white.withAlpha(0.5f));
        g.drawText(items[k].name, (int)x0 + 4, 2, (int)jmax(10.0f, x1 - x0 - 6), 14, Justification::left, true);
    }
    // the notes: one colour per goat (their scarf), glowing while they sound
    for (auto& x : notes) {
        const float x0 = X(x.a), w = jmax(2.0f, X(x.b) - x0 - 1.5f), y = Y(x.n) - nh / 2;
        const Colour c = rgb(goat::kLooks[(size_t)proc.look[(size_t)jlimit(0, goat::kMaxGoats - 1, x.goat)]].c);
        const bool live = ph >= x.a && ph < x.b;
        const Rectangle<float> rr(x0, y, w, nh);
        if (live) { g.setColour(c.withAlpha(0.35f)); g.fillRoundedRectangle(rr.expanded(3), jmin(nh / 2 + 3, 7.0f)); }
        g.setGradientFill(ColourGradient(live ? Colour(0xfffff4d6) : c.brighter(0.15f), 0, y, c, 0, y + nh, false));
        g.setOpacity(live ? 1.0f : ph >= 0 ? 0.55f : 0.85f);
        g.fillRoundedRectangle(rr, jmin(nh / 2, 4.0f));
        g.setOpacity(1.0f);
    }
    // playhead, with a short glowing trail
    if (ph >= 0) {
        const float x = X(ph);
        g.setGradientFill(ColourGradient(Colour(0x00ffd76a), x - 40, 0, Colour(0x2effd76a), x, 0, false));
        g.fillRect(x - 40, top - 4, 40.0f, H - top);
        g.setColour(Colour(0x55ffd76a)); g.fillRect(x - 3, top - 6, 6.0f, H - top + 2);
        g.setColour(Colour(0xffffd76a)); g.fillRect(x - 1, top - 6, 2.0f, H - top + 2);
    }
}

// ---------------- progression chips ----------------
void Chips::refresh() {
    rects.clear();
    Font f = bold(14);
    float x = 4;
    for (auto& it : proc.prog) {
        const float w = jmax(f.getStringWidthFloat(it.name), 40.0f) + 46;
        rects.push_back({x, 4, w, 38});
        x += w + 6;
    }
    setSize(jmax((int)x + 4, getParentWidth()), 46);
    repaint();
}
static String barsLabel(double b) {
    return (b == 0.5 ? String::fromUTF8("\xc2\xbd") : String(b, b == std::floor(b) ? 0 : 2)) + (b > 1 ? " bars" : " bar");
}
void Chips::paint(Graphics& g) {
    if (proc.prog.empty()) {
        g.setColour(col::dim); g.setFont(Font(13.0f));
        g.drawText("Press Rec, then play strips (mouse or MIDI keys). With the song playing, chord lengths follow the beat.",
                   getLocalBounds().reduced(6, 0), Justification::centredLeft);
        return;
    }
    const int pos = proc.progPos.load();
    for (size_t i = 0; i < rects.size() && i < proc.prog.size(); ++i) {
        const auto r = rects[i];
        const auto& it = proc.prog[i];
        const Colour fc = it.chord >= 0 && it.chord < 8 ? col::tonic : col::pre;
        g.setColour((int)i == pos ? col::gold : col::panel2);
        g.fillRoundedRectangle(r, 9);
        g.setColour((int)i == pos ? col::bg : col::ink); g.setFont(bold(14));
        g.drawText(it.name, r.withTrimmedLeft(10).withTrimmedRight(24).withHeight(22), Justification::left);
        g.setFont(Font(11.0f)); g.setColour((int)i == pos ? col::bg : fc.brighter(0.4f));
        g.drawText(barsLabel(it.bars), r.withTrimmedLeft(10).withTrimmedTop(20), Justification::left);
        g.setColour((int)i == pos ? col::bg : col::dim); g.setFont(bold(14));
        g.drawText("x", r.withTrimmedLeft(r.getWidth() - 22), Justification::centred);
    }
}
void Chips::mouseDown(const MouseEvent& e) {
    for (size_t i = 0; i < rects.size() && i < proc.prog.size(); ++i) {
        if (!rects[i].contains(e.position)) continue;
        if (e.position.x > rects[i].getRight() - 22) proc.prog.erase(proc.prog.begin() + (long)i);
        else {
            static const double lens[4] = {1, 2, 4, 0.5};
            auto& b = proc.prog[i].bars;
            int at = -1;
            for (int k = 0; k < 4; ++k) if (lens[k] == b) at = k;
            b = lens[(at + 1) % 4];
            if (!proc.isProgPlaying()) proc.uiAudition((int)i);
        }
        proc.progChanged();
        return;
    }
}

// ---------------- motion ----------------
Point<float> MotionCurve::toUnit(Point<float> p) const {
    const float pad = 9;
    return {jlimit(0.0f, 1.0f, (p.x - pad) / (getWidth() - 2 * pad)), jlimit(0.0f, 1.0f, 1 - (p.y - pad) / (getHeight() - 2 * pad))};
}
Point<float> MotionCurve::toPx(float x, float y) const {
    const float pad = 9;
    return {pad + x * (getWidth() - 2 * pad), pad + (1 - y) * (getHeight() - 2 * pad)};
}
int MotionCurve::nearPoint(Point<float> p) const {
    int best = -1; float bd = 14;
    for (size_t i = 0; i < proc.motionPts.size(); ++i) {
        const float d = toPx(proc.motionPts[i].first, proc.motionPts[i].second).getDistanceFrom(p);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}
void MotionCurve::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat();
    const bool on = proc.apvts.getRawParameterValue("motionOn")->load() > 0.5f;
    g.setColour(col::panel); g.fillRoundedRectangle(r, 9);
    g.setColour(col::panel2);
    for (int k = 1; k < 4; ++k) { const float x = toPx(k / 4.0f, 0).x; g.drawVerticalLine((int)x, 6, r.getBottom() - 6); }
    auto m = proc.motionNow();
    Path line, fill;
    for (int i = 0; i <= 160; ++i) {
        const float x = i / 160.0f; const auto pt = toPx(x, (float)m.at(jmin(x, 0.99999f)));
        if (i == 0) line.startNewSubPath(pt); else line.lineTo(pt);
    }
    fill = line;
    fill.lineTo(toPx(1, 0)); fill.lineTo(toPx(0, 0)); fill.closeSubPath();
    const Colour c = on ? col::gold : col::gold.withAlpha(0.45f);
    g.setColour(c.withAlpha(on ? 0.14f : 0.06f)); g.fillPath(fill);
    g.setColour(c); g.strokePath(line, PathStrokeType(2.2f));
    for (size_t i = 0; i < proc.motionPts.size(); ++i) {
        const auto pt = toPx(proc.motionPts[i].first, proc.motionPts[i].second);
        const float rr = (int)i == drag ? 6.0f : 4.5f;
        g.setColour(col::ink); g.fillEllipse(pt.x - rr, pt.y - rr, rr * 2, rr * 2);
    }
    const float ph = proc.motionPhase.load();
    if (on && ph >= 0) { g.setColour(Colours::white.withAlpha(0.5f)); g.drawVerticalLine((int)toPx(ph, 0).x, 4, r.getBottom() - 4); }
}
void MotionCurve::mouseDown(const MouseEvent& e) {
    auto& P = proc.motionPts;
    int i = nearPoint(e.position);
    if (i < 0 && (int)P.size() < goat::Motion::kMaxPts) {
        const auto u = toUnit(e.position);
        i = (int)P.size() - 1;
        for (size_t k = 1; k < P.size(); ++k) if (P[k].first > u.x) { i = (int)k; break; }
        if (i <= 0) i = (int)P.size() - 1;
        P.insert(P.begin() + i, {u.x, u.y});
        proc.motionChanged();
        if (onEdit) onEdit();
    }
    drag = i;
    repaint();
}
void MotionCurve::mouseDrag(const MouseEvent& e) {
    auto& P = proc.motionPts;
    if (drag < 0 || drag >= (int)P.size()) return;
    const auto u = toUnit(e.position);
    const int last = (int)P.size() - 1;
    if (drag > 0 && drag < last) P[(size_t)drag].first = jlimit(P[(size_t)drag - 1].first + 0.005f, P[(size_t)drag + 1].first - 0.005f, u.x);
    P[(size_t)drag].second = u.y;
    proc.motionChanged();
    if (onEdit) onEdit();
}
void MotionCurve::mouseDoubleClick(const MouseEvent& e) {
    const int i = nearPoint(e.position);
    if (i > 0 && i < (int)proc.motionPts.size() - 1) { proc.motionPts.erase(proc.motionPts.begin() + i); proc.motionChanged(); if (onEdit) onEdit(); }
}

VBar::VBar(GoatChoirProcessor& p, const char* paramId, const String& t) : title(t) {
    att = std::make_unique<ParameterAttachment>(*p.apvts.getParameter(paramId), [this](float v) { value = v; repaint(); });
    att->sendInitialUpdate();
}
void VBar::set(const MouseEvent& e) {
    const auto r = getLocalBounds().toFloat().withTrimmedTop(16);
    att->setValueAsPartOfGesture(jlimit(0.0f, 1.0f, std::round((1 - (e.position.y - r.getY()) / r.getHeight()) * 100) / 100));
}
void VBar::paint(Graphics& g) {
    g.setColour(col::dim); g.setFont(bold(11));
    g.drawText(title.toUpperCase(), 0, 0, getWidth(), 14, Justification::centred);
    auto r = getLocalBounds().toFloat().withTrimmedTop(16);
    g.setColour(col::panel2); g.fillRoundedRectangle(r, 8);
    g.setColour(col::gold.withAlpha(0.8f)); g.fillRoundedRectangle(r.withTop(r.getBottom() - r.getHeight() * value), 8);
    g.setColour(col::ink); g.setFont(bold(13));
    g.drawText(String(std::lround(value * 100)) + "%", r, Justification::centred);
}

// ---------------- drag to FL ----------------
void DragHandle::paint(Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1);
    const bool off = GoatChoirProcessor::kDemo || proc.prog.empty();
    g.setColour(off ? col::panel2 : col::tonic);
    g.fillRoundedRectangle(r, 9);
    g.setColour(off ? col::dim : Colours::white);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) g.fillEllipse(r.getX() + 12 + j * 7.0f, r.getCentreY() - 10 + i * 8.0f, 4, 4);
    g.setFont(bold(14));
    g.drawText(GoatChoirProcessor::kDemo ? "Drag MIDI (full)" : "Drag MIDI to FL", r.withTrimmedLeft(30), Justification::centred);
}
void DragHandle::mouseDrag(const MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 5) return;
    dragging = true;
    if (GoatChoirProcessor::kDemo) { if (say) say("Dragging MIDI out is in the full version of Goat Choir"); return; }
    if (proc.prog.empty()) { if (say) say("Record a progression first: press Rec and play some strips"); return; }
    const auto f = GoatChoirProcessor::exportFolder().getChildFile(proc.fileBase() + ".mid");
    if (!proc.writeMidi(f)) { if (say) say("Could not write " + f.getFullPathName()); return; }
    DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}
void DragHandle::mouseUp(const MouseEvent& e) {
    if (!dragging && say) say(GoatChoirProcessor::kDemo ? "Dragging MIDI out is in the full version of Goat Choir"
                                                         : "Drag this onto FL's playlist or piano roll to drop the progression in");
    dragging = false;
    ignoreUnused(e);
}
} // namespace gc

// ---------------- editor ----------------
GoatChoirEditor::GoatChoirEditor(GoatChoirProcessor& p)
    : AudioProcessorEditor(&p), proc(p), stage(p),
      sing(p, "sing", "Sing"), style(p, "style", "Style"), vowel(p, "vowel", "Vowel"), chop(p, "chop", "Chop"), flavor(p, "flavor", "Chord flavor"),
      motionSync(p, "motionSync", "Sync"), motionLen(p, "motionLen", "Length"),
      curve(p), depthBar(p, "motionDepth", "Depth"), chanceBar(p, "motionChance", "Chance"),
      chips(p), drag(p), roll(p), wordsLine(p) {
    setLookAndFeel(&lnf);
    addAndMakeVisible(stage);
    stage.onSelect = [this] { refreshCard(); };
    for (int i = 0; i < goat::kStrips; ++i) { strips.push_back(std::make_unique<gc::StripButton>(p, i)); addAndMakeVisible(*strips.back()); }

    auto combo = [&](ComboBox& b, const char* id, std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>& att, const String& tip) {
        b.addItemList(p.apvts.getParameter(id)->getAllValueStrings(), 1);
        b.setTooltip(tip);
        addAndMakeVisible(b);
        att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, id, b);
    };
    combo(keyBox, "key", keyAtt, "Key");
    combo(scaleBox, "scale", scaleAtt, "Scale");
    combo(colourBox, "colour", colourAtt, "Triads, 7ths or 9ths");
    combo(keysBox, "keys", keysAtt, "What your MIDI keys do: play the 16 strips (C5 and up), or have the goats sing exactly the notes you hold");
    addAndMakeVisible(midiOut);
    midiOut.setTooltip("Send the notes the goats sing out as MIDI, to drive another synth");
    midiOutAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "midiOut", midiOut);
    for (auto* s : {&vol, &room}) { s->setSliderStyle(Slider::LinearHorizontal); s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0); addAndMakeVisible(*s); }
    volAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "volume", vol);
    roomAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "room", room);
    vol.setTooltip("Volume"); room.setTooltip("Room: how much mountain echo");

    for (auto* c : {&sing, &style, &vowel, &chop, &flavor, &motionSync, &motionLen}) addAndMakeVisible(*c);
    style.setRows(2); flavor.setRows(2); chop.setRows(1, true);

    // Strum knob: the gap between goats, low to high. Turning it up from Block switches to Strum
    strumKnob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    strumKnob.setTextBoxStyle(Slider::TextBoxBelow, true, 60, 16);
    strumKnob.setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    strumKnob.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    strumKnob.setDoubleClickReturnValue(true, 0.45);
    strumKnob.setTooltip("Strum: how far apart the goats come in, low to high");
    addAndMakeVisible(strumKnob);
    strumAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "strum", strumKnob);

    // Pitch knob: the whole choir up or down in semitones; the vowels keep their colour
    pitchKnob.setSliderStyle(Slider::RotaryVerticalDrag);
    pitchKnob.setTextBoxStyle(Slider::TextBoxBelow, true, 60, 16);
    pitchKnob.setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    pitchKnob.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    pitchKnob.getProperties().set("bipolar", true);
    pitchKnob.setMouseDragSensitivity(8 * 24);   // 8 px per semitone
    pitchKnob.setWantsKeyboardFocus(true);
    pitchKnob.setTooltip("Pitch: the whole choir up or down, in semitones. Double-click for 0");
    addAndMakeVisible(pitchKnob);
    pitchAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "pitch", pitchKnob);
    pitchKnob.setDoubleClickReturnValue(true, 0);
    pitchKnob.textFromValueFunction = [](double v) { const int i = (int)std::lround(v); return (i > 0 ? "+" : "") + String(i); };
    pitchKnob.updateText();
    strumKnob.onValueChange = [this] {
        if (strumKnob.isMouseButtonDown() && strumKnob.getValue() > 0 && proc.choice("style") == goat::Block)
            if (auto* st = proc.apvts.getParameter("style")) { st->beginChangeGesture(); st->setValueNotifyingHost(st->convertTo0to1((float)goat::Strum)); st->endChangeGesture(); }
    };

    // Motion: a drawn curve rides the choir's level, locked to FL's beat
    motionOnBtn.setClickingTogglesState(true);
    motionOnBtn.setTooltip("Motion: the choir's level follows the curve, in time with the song");
    motionOnAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "motionOn", motionOnBtn);
    addAndMakeVisible(motionOnBtn);
    for (int i = 0; i < 10; ++i) {
        auto* b = shapeBtns.add(new TextButton(goat::kMotionShapes[i]));
        b->getProperties().set("small", true);
        b->onClick = [this, i] {
            proc.motionPts = goat::motionShape(i); proc.motionShape = i; proc.motionChanged();
            if (!motionOnBtn.getToggleState()) motionOnBtn.setToggleState(true, sendNotification);
            refreshShapes();
        };
        addAndMakeVisible(b);
    }
    curve.onEdit = [this] {
        proc.motionShape = -1;
        if (!motionOnBtn.getToggleState()) motionOnBtn.setToggleState(true, sendNotification);
        refreshShapes();
    };
    for (Component* c : std::initializer_list<Component*>{&curve, &depthBar, &chanceBar}) addAndMakeVisible(*c);
    refreshShapes();

    goatsAtt = std::make_unique<ParameterAttachment>(*p.apvts.getParameter("goats"), [this](float v) { goats = (int)std::lround(v); repaint(); refreshCard(); });
    goatsAtt->sendInitialUpdate();
    fewer.onClick = [this] { goatsAtt->setValueAsCompleteGesture((float)jmax(1, goats - 1)); };
    more.onClick = [this] { goatsAtt->setValueAsCompleteGesture((float)jmin(goat::kMaxGoats, goats + 1)); };
    latchBtn.setClickingTogglesState(true);
    latchBtn.setToggleState(p.latch, dontSendNotification);
    latchBtn.setTooltip("On: a click keeps the chord singing until you click it again. Off: it sings while you hold the mouse");
    latchBtn.onClick = [this] { proc.latch = latchBtn.getToggleState(); };
    stopBtn.onClick = [this] { proc.setProgPlaying(false); proc.uiStop(); };
    for (auto* b : {&fewer, &more, &latchBtn, &stopBtn}) addAndMakeVisible(*b);

    prevG.onClick = [this] { proc.selected = (proc.selected.load() + goats - 1) % goats; refreshCard(); };
    nextG.onClick = [this] { proc.selected = (proc.selected.load() + 1) % goats; refreshCard(); };
    for (auto [title, sex] : {std::pair<const char*, char>{"Male", 'm'}, {"Female", 'f'}, {"Young", 'k'}}) {
        voiceBox.addSectionHeading(title);
        for (int v = 0; v < goat::kVoiceTypes; ++v) if (goat::kVoices[(size_t)v].sex == sex) voiceBox.addItem(goat::kVoices[(size_t)v].label, v + 1);
    }
    // Male / Female: the same part in the other voice (Bass and Alto, Baritone and Mezzo, Tenor and Soprano)
    for (auto* b : {&maleBtn, &femaleBtn}) { b->setClickingTogglesState(false); b->getProperties().set("small", true); }
    maleBtn.setTooltip("Male voice"); femaleBtn.setTooltip("Female voice");
    auto pickSex = [this](char x) {
        auto& vt = proc.voiceType[(size_t)proc.selected.load()];
        vt = goat::swapVoice(vt.load(), x);
        refreshCard();
    };
    maleBtn.onClick = [pickSex] { pickSex('m'); };
    femaleBtn.onClick = [pickSex] { pickSex('f'); };
    for (int k = 0; k < goat::kSounds; ++k) soundBox.addItem(goat::kSoundDefs[(size_t)k].label, k + 1);
    soundBox.setTooltip("What this goat sounds like: a voice, or an instrument it holds and plays");
    soundBox.onChange = [this] { proc.sound[(size_t)proc.selected.load()] = soundBox.getSelectedId() - 1; refreshCard(); };
    bandBtn.setClickingTogglesState(false);
    bandBtn.getProperties().set("small", true);
    bandBtn.setTooltip("Hand every goat the instrument for its voice (press again for voices)");
    bandBtn.onClick = [this] { proc.uiBand(); refreshCard(); };
    for (Component* c : std::initializer_list<Component*>{&maleBtn, &femaleBtn, &soundBox, &bandBtn}) addAndMakeVisible(*c);
    // the stage: Day, Sunset or Night (a spotlight follows the selected goat)
    for (int i = 0; i < 3; ++i) {
        auto* b = stageBtns.add(new TextButton(i == 0 ? "Day" : i == 1 ? "Sunset" : "Night"));
        b->getProperties().set("small", true);
        b->setClickingTogglesState(false);
        b->onClick = [this, i] { proc.stageMode = i; refreshCard(); };
        addAndMakeVisible(b);
    }
    addAndMakeVisible(roll);
    voiceBox.onChange = [this] { proc.voiceType[(size_t)proc.selected.load()] = voiceBox.getSelectedId() - 1; refreshCard(); };
    voiceBox.setTooltip("This goat's voice: sets its range and its formants");
    muteBtn.setClickingTogglesState(true);
    muteBtn.onClick = [this] { proc.muted[(size_t)proc.selected.load()] = muteBtn.getToggleState(); refreshCard(); };
    scarfBtn.onClick = [this] {
        if (GoatChoirProcessor::kDemo) { say("Dressing your goats is in the full version of Goat Choir"); return; }
        auto& l = proc.look[(size_t)proc.selected.load()];
        l = (l + 1) % (int)goat::kLooks.size();
        refreshCard();
    };
    goatName.setFont(gc::bold(13));
    goatName.setJustificationType(Justification::centred);
    for (Component* c : std::initializer_list<Component*>{&prevG, &nextG, &voiceBox, &muteBtn, &scarfBtn, &goatName}) addAndMakeVisible(*c);
    muteBtn.getProperties().set("small", true); scarfBtn.getProperties().set("small", true);

    recBtn.setClickingTogglesState(true);
    recBtn.setColour(TextButton::buttonOnColourId, gc::col::rec);
    recBtn.onClick = [this] {
        if (recBtn.getToggleState() && proc.isProgPlaying()) proc.setProgPlaying(false);
        proc.setRecording(recBtn.getToggleState());
        say(recBtn.getToggleState() ? "Recording: play your chords" : "Recorded");
    };
    playBtn.setClickingTogglesState(true);
    playBtn.onClick = [this] {
        if (playBtn.getToggleState() && proc.prog.empty()) { playBtn.setToggleState(false, dontSendNotification); say("Nothing to play yet: press Rec and play some strips"); return; }
        if (playBtn.getToggleState() && proc.recording) { proc.setRecording(false); recBtn.setToggleState(false, dontSendNotification); }
        proc.setProgPlaying(playBtn.getToggleState());
        say(playBtn.getToggleState() ? (proc.hostPlaying.load() ? "Looping with the song" : "Looping (it locks to the song when FL plays)") : "Stopped");
    };
    undoBtn.onClick = [this] { if (!proc.prog.empty()) { proc.prog.pop_back(); proc.progChanged(); } };
    clearBtn.onClick = [this] { proc.prog.clear(); proc.progChanged(); };
    saveBtn.onClick = [this] { saveMidi(); };
    for (auto* b : {&recBtn, &playBtn, &undoBtn, &clearBtn, &saveBtn}) addAndMakeVisible(*b);
    chipsView.setViewedComponent(&chips, false);
    chipsView.setScrollBarsShown(false, true);
    chipsView.setScrollBarThickness(6);
    addAndMakeVisible(chipsView);
    drag.say = [this](const String& s) { say(s); };
    addAndMakeVisible(drag);
    // Words: typing a line turns them on; the button goes back to plain vowels and keeps the text
    wordsBox.setTextToShowWhenEmpty("Type a line and the goats sing it", gc::col::dim);
    wordsBox.setInputRestrictions(300);
    wordsBox.setFont(Font(15.0f));
    wordsBox.setIndents(8, 6);
    wordsBox.setColour(TextEditor::backgroundColourId, gc::col::panel2);
    wordsBox.setColour(TextEditor::textColourId, gc::col::ink);
    wordsBox.setColour(TextEditor::outlineColourId, Colours::transparentBlack);
    wordsBox.setColour(TextEditor::focusedOutlineColourId, gc::col::gold);
    wordsBox.setColour(CaretComponent::caretColourId, gc::col::gold);
    wordsBox.setText(proc.wordsText, false);
    wordsBox.onTextChange = [this] { proc.setWords(wordsBox.getText(), true); refreshWords(); };
    wordsOnBtn.setClickingTogglesState(false);
    wordsOnBtn.onClick = [this] {
        if (!proc.wordsOn && proc.wordsSyl.empty()) { say("Type a line first"); return; }
        proc.setWords(proc.wordsText, !proc.wordsOn); refreshWords();
    };
    sayBtn.onClick = [this] { proc.uiSay(); };
    sayBtn.setTooltip("The selected goat says the line");
    for (Component* c : std::initializer_list<Component*>{&wordsBox, &wordsLine, &wordsOnBtn, &sayBtn}) addAndMakeVisible(*c);
    refreshWords();

    status.setFont(Font(13.0f));
    status.setColour(Label::textColourId, gc::col::dim);
    addAndMakeVisible(status);

    setSize(gc::W, gc::H);
    refreshLabels();
    refreshCard();
    chips.refresh();
    startTimerHz(30);
}

GoatChoirEditor::~GoatChoirEditor() { setLookAndFeel(nullptr); }

void GoatChoirEditor::refreshLabels() {
    std::array<goat::Chord, goat::kStrips> c;
    goat::buildChords(proc.choice("key"), proc.choice("scale"), proc.choice("colour") + 3, c, &stage.lab, proc.choice("flavor"));
    for (int i = 0; i < goat::kStrips; ++i) { strips[(size_t)i]->lab = stage.lab[(size_t)i]; strips[(size_t)i]->chord = c[(size_t)i]; strips[(size_t)i]->repaint(); }
}

void GoatChoirEditor::refreshShapes() {
    for (int i = 0; i < shapeBtns.size(); ++i) shapeBtns[i]->setToggleState(i == proc.motionShape, dontSendNotification);
    seenMotion = proc.motionVersion;
    curve.repaint();
}

void GoatChoirEditor::refreshCard() {
    int sel = proc.selected.load();
    if (sel >= goats) { sel = goats - 1; proc.selected = sel; }
    const int v = proc.voiceType[(size_t)sel].load();
    const auto& V = goat::kVoices[(size_t)v];
    goatName.setText(String(goat::kCast[(size_t)sel].name) + "  " + goat::noteName(V.lo) + "-" + goat::noteName(V.hi), dontSendNotification);
    voiceBox.setSelectedId(v + 1, dontSendNotification);
    maleBtn.setToggleState(V.sex == 'm', dontSendNotification);
    femaleBtn.setToggleState(V.sex == 'f', dontSendNotification);
    soundBox.setSelectedId(proc.sound[(size_t)sel].load() + 1, dontSendNotification);
    bandBtn.setToggleState(proc.isBand(), dontSendNotification);
    for (int i = 0; i < stageBtns.size(); ++i) stageBtns[i]->setToggleState(i == proc.stageMode.load(), dontSendNotification);
    muteBtn.setToggleState(proc.muted[(size_t)sel].load(), dontSendNotification);
    scarfBtn.setColour(TextButton::buttonColourId, gc::rgb(goat::kLooks[(size_t)proc.look[(size_t)sel]].c));
    seenSel = sel;
    stage.repaint();
}

void GoatChoirEditor::refreshWords() {
    wordsOnBtn.setButtonText(proc.wordsOn ? "Words on" : "Words off");
    wordsOnBtn.setToggleState(proc.wordsOn, dontSendNotification);
    if (!wordsBox.hasKeyboardFocus(true) && wordsBox.getText() != proc.wordsText) wordsBox.setText(proc.wordsText, false);
    wordsLine.version = proc.wordsVersion;
    wordsLine.repaint();
}

void GoatChoirEditor::timerCallback() {
    if (wordsLine.version != proc.wordsVersion) refreshWords();
    if (wordsLine.shown != proc.sungWord.load()) { wordsLine.shown = proc.sungWord.load(); wordsLine.repaint(); }
    const int key = ((proc.choice("key") * 100 + proc.choice("scale") * 10 + proc.choice("colour")) * 10 + proc.choice("flavor")) * 30 + proc.pitch() + 12;
    if (key != seenKey) { seenKey = key; refreshLabels(); }
    const int cur = proc.currentChord.load();
    for (int i = 0; i < goat::kStrips; ++i) {
        auto& s = *strips[(size_t)i];
        if (s.on != (i == cur)) { s.on = i == cur; s.repaint(); }
    }
    if (proc.progVersion != seenVersion) { seenVersion = proc.progVersion; chips.refresh(); drag.repaint(); }
    const int pos = proc.progPos.load();
    if (pos != seenPos) {
        seenPos = pos; chips.repaint();
        if (pos >= 0 && pos < (int)proc.prog.size()) {   // keep the playing chip in view
            const int x = (int)(pos * 90.0);
            if (x < chipsView.getViewPositionX() || x > chipsView.getViewPositionX() + chipsView.getWidth() - 90) chipsView.setViewPosition(jmax(0, x - 40), 0);
        }
    }
    if (proc.selected.load() != seenSel) refreshCard();
    if (proc.motionVersion != seenMotion) refreshShapes();
    if (playBtn.getToggleState() != proc.isProgPlaying()) playBtn.setToggleState(proc.isProgPlaying(), dontSendNotification);
    if (recBtn.getToggleState() != proc.recording) recBtn.setToggleState(proc.recording, dontSendNotification);
    if (sayUntil && Time::getMillisecondCounter() > sayUntil) { sayUntil = 0; status.setText({}, dontSendNotification); }
}

void GoatChoirEditor::saveMidi() {
    if (GoatChoirProcessor::kDemo) { say("Saving MIDI is in the full version of Goat Choir"); return; }
    if (proc.prog.empty()) { say("Record a progression first: press Rec and play some strips"); return; }
    chooser = std::make_unique<FileChooser>("Save the progression as MIDI", GoatChoirProcessor::exportFolder().getChildFile(proc.fileBase() + ".mid"), "*.mid");
    chooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                         [this](const FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f == File()) return;
                             if (!f.hasFileExtension("mid")) f = f.withFileExtension("mid");
                             say(proc.writeMidi(f) ? "Saved " + f.getFileName() + ". In FL: piano roll > File > Import MIDI file, or drag it in"
                                                   : "Could not save " + f.getFullPathName());
                         });
}

void GoatChoirEditor::paint(Graphics& g) {
    g.fillAll(gc::col::bg);
    g.setColour(gc::col::panel);
    g.fillRect(0, 0, getWidth(), 46);
    g.setColour(gc::col::ink); g.setFont(gc::bold(24));
    g.drawText("GOAT CHOIR", 16, 0, 170, 46, Justification::centredLeft);
    g.setColour(gc::col::gold); g.setFont(gc::bold(12));
    g.drawText("by Sebi Inc.", 166, 4, 90, 40, Justification::centredLeft);
    g.setColour(gc::col::dim); g.setFont(gc::bold(11));
    g.drawText("KEY", 258, 0, 30, 46, Justification::centredLeft);
    g.drawText("MIDI KEYS", 584, 0, 70, 46, Justification::centredLeft);
    g.drawText("VOL", 922, 0, 30, 46, Justification::centredLeft);
    g.drawText("GOATS", 14, 552, 60, 14, Justification::left);
    g.drawText("ROOM", 154, 552, 60, 14, Justification::left);
    g.drawText("SELECTED GOAT", 464, 552, 120, 14, Justification::left);
    g.drawText("VOICE", 618, 552, 60, 14, Justification::left);
    g.drawText("SOUND", 786, 552, 60, 14, Justification::left);
    g.drawText("STRUM", 12, 612, 60, 14, Justification::left);
    g.drawText("PITCH", 74, 612, 60, 14, Justification::left);
    g.drawText("MOTION", 142, 612, 70, 14, Justification::left);
    g.drawText("SHAPES", 374, 612, 70, 14, Justification::left);
    g.drawText("WORDS", 14, 718, 60, 30, Justification::centredLeft);
    g.drawText("PROGRESSION", 14, 764, 120, 14, Justification::left);
    g.setColour(gc::col::ink); g.setFont(gc::bold(16));
    g.drawText(String(goats), 42, 568, 68, 28, Justification::centred);
    g.setColour(gc::col::panel);
    g.fillRect(0, 604, getWidth(), 1);
    g.fillRect(0, 712, getWidth(), 1);
    g.fillRect(0, 756, getWidth(), 1);
    g.fillRect(0, 852, getWidth(), 1);
}

void GoatChoirEditor::resized() {
    keyBox.setBounds(286, 9, 70, 28);
    scaleBox.setBounds(362, 9, 140, 28);
    colourBox.setBounds(508, 9, 70, 28);
    keysBox.setBounds(650, 9, 150, 28);
    midiOut.setBounds(808, 9, 100, 28);
    vol.setBounds(950, 9, 98, 28);
    stage.setBounds(0, 46, getWidth(), 300);

    const int sx = 10, sy = 352, sw = (getWidth() - 2 * sx) / 8, sh = 62;
    for (int i = 0; i < goat::kStrips; ++i) strips[(size_t)i]->setBounds(sx + (i % 8) * sw, sy + (i / 8) * sh, sw, sh);

    sing.setBounds(12, 492, 210, 48);
    style.setBounds(236, 492, 210, 72);   // 4 x 2 grid of the eight styles
    vowel.setBounds(460, 492, 232, 48);
    chop.setBounds(700, 492, 168, 48);
    flavor.setBounds(876, 492, 172, 72);   // 4 + 3 grid of the flavors

    fewer.setBounds(12, 568, 30, 28);
    more.setBounds(110, 568, 30, 28);
    room.setBounds(152, 568, 140, 28);
    latchBtn.setBounds(300, 568, 60, 28);
    stopBtn.setBounds(364, 568, 60, 28);
    prevG.setBounds(434, 568, 26, 28);
    goatName.setBounds(462, 568, 120, 28);
    nextG.setBounds(584, 568, 26, 28);
    voiceBox.setBounds(618, 568, 94, 28);
    maleBtn.setBounds(716, 568, 28, 28);
    femaleBtn.setBounds(746, 568, 28, 28);
    soundBox.setBounds(784, 568, 100, 28);
    bandBtn.setBounds(888, 568, 50, 28);
    muteBtn.setBounds(944, 568, 50, 28);
    scarfBtn.setBounds(998, 568, 50, 28);
    for (int i = 0; i < stageBtns.size(); ++i) stageBtns[i]->setBounds(getWidth() - 12 - (3 - i) * 60, 46 + 300 - 32, 56, 24);
    roll.setBounds(10, 860, getWidth() - 20, 92);

    strumKnob.setBounds(6, 626, 62, 80);
    pitchKnob.setBounds(68, 626, 62, 80);
    motionOnBtn.setBounds(140, 630, 74, 26);
    motionSync.setBounds(222, 614, 140, 42);
    motionLen.setBounds(140, 664, 222, 42);
    for (int i = 0; i < shapeBtns.size(); ++i) shapeBtns[i]->setBounds(372 + (i % 5) * 49, 630 + (i / 5) * 32, 47, 28);
    curve.setBounds(620, 612, 296, 94);
    depthBar.setBounds(926, 612, 56, 94);
    chanceBar.setBounds(990, 612, 58, 94);

    recBtn.setBounds(120, 762, 64, 26);
    playBtn.setBounds(190, 762, 64, 26);
    undoBtn.setBounds(260, 762, 64, 26);
    clearBtn.setBounds(330, 762, 64, 26);
    status.setBounds(404, 762, 420, 26);
    chipsView.setBounds(10, 796, 820, 54);
    chips.refresh();
    saveBtn.setBounds(842, 762, 206, 28);
    drag.setBounds(842, 798, 206, 48);

    wordsBox.setBounds(74, 718, 470, 32);
    wordsLine.setBounds(552, 718, 326, 32);
    wordsOnBtn.setBounds(886, 718, 92, 32);
    sayBtn.setBounds(984, 718, 64, 32);
}

