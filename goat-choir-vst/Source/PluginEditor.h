#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace gc {
namespace col {
const juce::Colour bg(0xff0d0b10), panel(0xff17141c), panel2(0xff221d29), ink(0xffece6da), dim(0xff9a92a6),
    tonic(0xff7d69dc), pre(0xff3c9db2), dom(0xffc9505f), gold(0xffe2b548), rec(0xffe0484f);
inline juce::Colour fn(int f) { return f == goat::Tonic ? tonic : f == goat::Pre ? pre : dom; }
}

class LnF : public juce::LookAndFeel_V4 {
public:
    LnF();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton& b, int h) override {
        return juce::Font(b.getProperties().contains("small") ? 11.5f : juce::jmin(15.0f, h * 0.55f), juce::Font::bold);
    }
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(14.0f, juce::Font::bold); }
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
};

// the green-mountain ledge with the clay goats on it; steps at 12 fps like stop-motion
class Stage : public juce::Component, private juce::Timer {
public:
    explicit Stage(GoatChoirProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override { bg = {}; }
    void mouseDown(const juce::MouseEvent&) override;
    std::array<goat::ChordLabel, goat::kStrips> lab;
    std::function<void()> onSelect;
private:
    void timerCallback() override { ++frame; repaint(); }
    void drawBackground(juce::Graphics&, juce::Rectangle<float>);
    void drawGoat(juce::Graphics&, int k, float cx, float gy, float u, int idx);
    GoatChoirProcessor& proc;
    juce::Image bg;
    int frame = 0;
    std::array<juce::Rectangle<float>, goat::kMaxGoats> hit;
};

class StripButton : public juce::Component {
public:
    StripButton(GoatChoirProcessor& p, int i) : proc(p), idx(i) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { proc.uiStrip(idx, true); }
    void mouseUp(const juce::MouseEvent&) override { proc.uiStrip(idx, false); }
    goat::ChordLabel lab; goat::Chord chord; bool on = false;
private:
    GoatChoirProcessor& proc; int idx;
};

// a row of buttons that picks one option of a choice parameter
class Seg : public juce::Component {
public:
    Seg(GoatChoirProcessor& p, const char* paramId, const juce::String& title);
    void resized() override;
    void paint(juce::Graphics&) override;
    void setRows(int r);
private:
    juce::String title;
    int rows = 1;
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// the recorded progression: click a chip to change its length (and hear it), x to remove it
class Chips : public juce::Component {
public:
    explicit Chips(GoatChoirProcessor& p) : proc(p) {}
    void refresh();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    int shown = -1;
private:
    GoatChoirProcessor& proc;
    std::vector<juce::Rectangle<float>> rects;
};

// Motion: draw the curve the choir's level follows. Drag points, click to add one, double-click to remove one
class MotionCurve : public juce::Component, private juce::Timer {
public:
    explicit MotionCurve(GoatChoirProcessor& p) : proc(p) { startTimerHz(30); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { drag = -1; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void()> onEdit;   // the drawing became custom
private:
    void timerCallback() override { repaint(); }
    juce::Point<float> toUnit(juce::Point<float>) const;
    juce::Point<float> toPx(float x, float y) const;
    int nearPoint(juce::Point<float>) const;
    GoatChoirProcessor& proc;
    int drag = -1;
};

// a vertical 0-100% bar for a parameter (Motion depth and chance)
class VBar : public juce::Component {
public:
    VBar(GoatChoirProcessor& p, const char* paramId, const juce::String& title);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { att->beginGesture(); set(e); }
    void mouseDrag(const juce::MouseEvent& e) override { set(e); }
    void mouseUp(const juce::MouseEvent&) override { att->endGesture(); }
private:
    void set(const juce::MouseEvent&);
    juce::String title;
    float value = 1;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// drag this straight into FL's playlist or piano roll
class DragHandle : public juce::Component {
public:
    explicit DragHandle(GoatChoirProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    std::function<void(const juce::String&)> say;
private:
    GoatChoirProcessor& proc; bool dragging = false;
};
} // namespace gc

class GoatChoirEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit GoatChoirEditor(GoatChoirProcessor&);
    ~GoatChoirEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void say(const juce::String& s) { status.setText(s, juce::dontSendNotification); sayUntil = juce::Time::getMillisecondCounter() + 5000; }
private:
    void timerCallback() override;
    void refreshLabels();
    void refreshCard();
    void saveMidi();

    GoatChoirProcessor& proc;
    gc::LnF lnf;
    gc::Stage stage;
    std::vector<std::unique_ptr<gc::StripButton>> strips;
    juce::ComboBox keyBox, scaleBox, colourBox, keysBox, voiceBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt, colourAtt, keysAtt;
    juce::ToggleButton midiOut{"MIDI out"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiOutAtt;
    juce::Slider vol, room;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAtt, roomAtt;
    gc::Seg sing, style, vowel, chop, motionSync, motionLen;
    juce::Slider strumKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> strumAtt;
    juce::TextButton motionOnBtn{"Motion"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> motionOnAtt;
    juce::OwnedArray<juce::TextButton> shapeBtns;
    gc::MotionCurve curve;
    gc::VBar depthBar, chanceBar;
    void refreshShapes();
    juce::TextButton fewer{"-"}, more{"+"}, latchBtn{"Latch"}, stopBtn{"Stop"};
    std::unique_ptr<juce::ParameterAttachment> goatsAtt;
    int goats = 4;
    juce::TextButton prevG{"<"}, nextG{">"}, muteBtn{"Mute"}, scarfBtn{"Scarf"};
    juce::Label goatName;
    juce::TextButton recBtn{"Rec"}, playBtn{"Play"}, undoBtn{"Undo"}, clearBtn{"Clear"}, saveBtn{"Save MIDI"};
    gc::Chips chips;
    juce::Viewport chipsView;
    gc::DragHandle drag;
    juce::Label status;
    juce::uint32 sayUntil = 0;
    int seenVersion = -1, seenKey = -1, seenSel = -1, seenPos = -2, seenMotion = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GoatChoirEditor)
};
