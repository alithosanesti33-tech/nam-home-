#pragma once
#include <JuceHeader.h>

// Measures what a capture or IR actually does, so colours come from the sound, not a label.
namespace measure
{
    // 0 = clean .. 1 = high gain, from how much a NAM capture distorts a guitar-level tone. -1 if it can't load.
    float namGain (const juce::File& namFile);
    // 0 = dark .. 1 = bright, from how much of an IR's energy sits above 2 kHz. -1 if unreadable.
    float irBrightness (const juce::File& wavFile);
    // the five stages shown in the library and on tiles
    int gainStage (float gain);                    // 0 clean, 1 edge, 2 crunch, 3 rock, 4 high gain
    juce::String gainStageName (int stage);        // "Clean", "Edge of breakup", ...
    juce::Colour gainColour (float gain);          // blue -> green -> yellow -> orange -> red
    juce::Colour irColour (float brightness);      // dark purple -> light purple
    // results by file path, shared by the library and the blocks on the grid
    void remember (const juce::String& path, float value);
    float recall (const juce::String& path);       // -1 if not measured
}
