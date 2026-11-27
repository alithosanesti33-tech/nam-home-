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

// Settings > Live: what the emergency button does.
class ControllerPage : public juce::Component, private juce::Timer
{
public:
    explicit ControllerPage (NAMHomeProcessor& p);
    void resized() override;
private:
    void timerCallback() override;
    NAMHomeProcessor& proc;
    juce::Label panicTitle, help, midiLabel;
    juce::ComboBox panicBox, safeBox;
    juce::TextButton learn { "MIDI learn the emergency switch" }, clear { "Clear" };
};
