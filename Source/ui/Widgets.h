#pragma once
#include "Look.h"
#include "../Blocks.h"

class NAMHomeProcessor;

// Right-click menu for MIDI learn on any control (knob, switch, choice).
void showMidiLearnMenu (juce::Component& target, NAMHomeProcessor& proc, Block& block, int paramIndex, std::function<void()> resetToDefault);

// A knob bound to a block parameter. Right-click for MIDI learn; double-click resets.
class ParamKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamKnob (NAMHomeProcessor& p, Block& b, int paramIndex, juce::Colour c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void sync();                     // reflect changes from MIDI / presets
    std::function<void()> onChange;
    juce::Slider slider;
private:
    NAMHomeProcessor& proc;
    Block& block;
    int index;
    Param& prm;
    juce::String midiText;
};

// Segmented buttons for a choice parameter (e.g. delay type).
class ParamSegments : public juce::Component
{
public:
    ParamSegments (NAMHomeProcessor& p, Block& b, int paramIndex, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hoverIdx = -1; repaint(); }
    int preferredHeight (int width) const;
    std::function<void()> onChange;
private:
    juce::Array<juce::Rectangle<int>> layout (int width) const;
    NAMHomeProcessor& proc;
    Block& block;
    int index;
    Param& prm;
    juce::Colour colour;
    int hoverIdx = -1;
};

// Combo box for a choice parameter.
class ParamCombo : public juce::Component
{
public:
    ParamCombo (NAMHomeProcessor& p, Block& b, int paramIndex);
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void sync();
    std::function<void()> onChange;
private:
    NAMHomeProcessor& proc;
    Block& block;
    int index;
    Param& prm;
    juce::Label name;
    juce::ComboBox box;
};

// Switch for a toggle parameter.
class ParamSwitch : public juce::ToggleButton
{
public:
    ParamSwitch (NAMHomeProcessor& p, Block& b, int paramIndex);
    void sync() { setToggleState (prm.on(), juce::dontSendNotification); }
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onChange;
private:
    NAMHomeProcessor& proc;
    Block& block;
    int index;
    Param& prm;
};

// A knob bound to any float (used for merges and global levels).
class ValueKnob : public juce::Component
{
public:
    ValueKnob (const juce::String& label, float lo, float hi, float def, const juce::String& unit,
               std::function<float()> get, std::function<void (float)> set, juce::Colour c);
    void resized() override;
    void sync() { slider.setValue (getter(), juce::dontSendNotification); }
    juce::Slider slider;
private:
    juce::String name;
    std::function<float()> getter;
    void paint (juce::Graphics&) override;
};

// Small level meter.
class Meter : public juce::Component
{
public:
    void setLevel (float l) { if (std::abs (l - level) > 0.0005f) { level = l; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    float level = 0;
};

// Brief messages at the bottom of the window.
class Toasts : public juce::Component, private juce::Timer
{
public:
    Toasts() { setInterceptsMouseClicks (false, false); }
    void show (const juce::String& text, bool warning = false);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    struct Item { juce::String text; bool warning; double until; };
    juce::Array<Item> items;
};

// Icon button drawn with a path.
class IconButton : public juce::Button
{
public:
    enum class Icon { Gear, Fullscreen, Library, Metronome, Close, Chevron, Plus, Save, Prev, Next, Midi, Power, Tuner, Spectrum };
    IconButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i) { setTooltip (tip); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    Icon icon;
    bool lit = false;
};
