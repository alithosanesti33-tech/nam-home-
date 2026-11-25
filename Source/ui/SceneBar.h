#pragma once
#include <JuceHeader.h>

class NAMHomeProcessor;

// A slim row of 8 scene pads (A-H) under the grid, like the Quad Cortex scene footswitches.
// Click to switch, right-click to rename, copy, or MIDI-learn a footswitch.
class SceneBar : public juce::Component, private juce::Timer
{
public:
    explicit SceneBar (NAMHomeProcessor&);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    std::function<void()> onChanged;
    std::function<void (int)> onSceneToNewRig;
    std::function<void (const juce::String&, bool)> onMessage;

private:
    void timerCallback() override;
    juce::Rectangle<float> padBounds (int i) const;
    int padAt (juce::Point<int>) const;
    void showMenu (int i);
    NAMHomeProcessor& proc;
    int shownScene = -1, shownVersion = -1, hover = -1;
};
