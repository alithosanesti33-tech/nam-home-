#pragma once
#include <JuceHeader.h>
#include "Look.h"
#include "Widgets.h"
#include "Tuner.h"

class NAMHomeProcessor;

// Gig check results: a green READY TO PLAY, or what needs attention.
class GigCheckPanel : public juce::Component
{
public:
    GigCheckPanel (NAMHomeProcessor& p, juce::AudioDeviceManager* dm);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { if (onClose) onClose(); }
    int preferredHeight (int width) const;
    std::function<void()> onClose;
    bool allGood() const;
private:
    struct Item { bool ok; juce::String text; };
    std::vector<Item> items;
};

// Live mode: the whole window becomes a performance screen. Nothing can be dragged or deleted;
// leaving takes a deliberate press-and-hold.
class LiveView : public juce::Component, private juce::Timer
{
public:
    LiveView (NAMHomeProcessor& p, juce::AudioDeviceManager* dm);
    ~LiveView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onExit;
    std::function<void (const juce::String&, bool)> onMessage;
private:
    void timerCallback() override;
    void stripMenu (int slot);
    NAMHomeProcessor& proc;
    juce::AudioDeviceManager* deviceManager;
    LiveTuner tuner;
    std::unique_ptr<GigCheckPanel> check;
    juce::Rectangle<int> exitR, checkR, prevR, nextR, nameR, tempoR, tapR, panicR, bannerR, sceneR[8], stripR[6];
    double exitHeld = 0;   // when the exit press started (ms), 0 = not held
    int pressed = -1;      // which area is pressed, for the button flash
    int lastScene = -1;
};

// MIDI setup mode: a banner over the window; click any control, press a switch on your controller.
class MidiSetupOverlay : public juce::Component, private juce::Timer
{
public:
    explicit MidiSetupOverlay (NAMHomeProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onDone;
    juce::Rectangle<int> banner;
private:
    void timerCallback() override { repaint(); }
    NAMHomeProcessor& proc;
    juce::TextButton done { "Done" };
};

// Settings > Controller: foot controller layouts, emergency bypass and the MIDI map.
class ControllerPage : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    explicit ControllerPage (NAMHomeProcessor& p);
    ~ControllerPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void()> onMidiSetup;
    void refresh();
private:
    void timerCallback() override;
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    NAMHomeProcessor& proc;
    juce::Label layoutTitle, panicTitle, mapTitle, result;
    juce::ComboBox layout, switches, sends, firstNum, panicBox, safeBox;
    juce::ToggleButton latching { "Latching switches (each press sends on, then off)" };
    juce::TextButton generate { "Set up my controller" }, learnPanic { "MIDI learn the emergency switch" },
                     setupMode { "MIDI setup mode" }, rename { "Rename" }, remove { "Remove" };
    juce::ListBox list;
    struct Row { bool global; int index; juce::String name, message, target; bool duplicate; int kind, number; };
    std::vector<Row> rows;
    int lastHit = -1;
    double hitAt = 0;
    int hitKind = -1, hitNum = 0;
};
