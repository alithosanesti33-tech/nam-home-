#pragma once
#include "Look.h"
#include "../Engine.h"

class NAMHomeProcessor;

// YIN pitch detection on the tuner ring buffer (runs on the message thread).
struct PitchReading
{
    bool valid = false;
    float freq = 0, cents = 0, targetFreq = 0;
    juce::String note;
    int octave = 0;
};

class PitchDetector
{
public:
    PitchReading analyse (const Engine& e, double sampleRate, float a4);
private:
    std::vector<float> buf, diff;
    float smoothedFreq = 0;
    int heldFrames = 0;
    PitchReading last;
};

// Full tuner panel (opens over the grid).
class TunerPanel : public juce::Component, private juce::Timer
{
public:
    explicit TunerPanel (NAMHomeProcessor& p);
    ~TunerPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onClose;
private:
    void timerCallback() override;
    NAMHomeProcessor& proc;
    PitchDetector detector;
    PitchReading reading;
    float needle = 0;
    juce::TextButton muteBtn, unitBtn, inputBtn, refDown { "-" }, refUp { "+" }, liveBtn { "Live tuner in top bar" }, closeBtn { "Close" };
    void updateButtons();
    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (640, getWidth() - 40), juce::jmin (440, getHeight() - 40)); }
};

// Compact always-on tuner for the top bar ("live tuner").
class LiveTuner : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit LiveTuner (NAMHomeProcessor& p);
    ~LiveTuner() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void setActive (bool on);
    std::function<void()> onClick;
private:
    void timerCallback() override;
    NAMHomeProcessor& proc;
    PitchDetector detector;
    PitchReading reading;
    float needle = 0;
};
