#pragma once
#include "Blocks.h"

// Short explanations of controls, for the settings panel's info button.
juce::String paramHelp (const Block& b, const Param& prm);   // empty if there's nothing to say
juce::String blockHelp (const Block& b);
