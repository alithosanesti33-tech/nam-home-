#pragma once
#include <JuceHeader.h>

namespace theme
{
    // Black background; block colours by type; one teal accent.
    const juce::Colour bg       { 0xff000000 };
    const juce::Colour panel    { 0xff0f0f10 };
    const juce::Colour card     { 0xff161618 };
    const juce::Colour raised   { 0xff1e1e21 };
    const juce::Colour hover    { 0xff26262a };
    const juce::Colour line     { 0xff2a2a2e };
    const juce::Colour path     { 0xff3c3c42 };
    const juce::Colour text     { 0xffeceef1 };
    const juce::Colour muted    { 0xff9ea3ab };
    const juce::Colour faint    { 0xff6c7078 };
    const juce::Colour accent   { 0xff2fb5a8 };
    const juce::Colour shaping  { 0xff8c9098 };
    const juce::Colour warn     { 0xffef4444 };
    const juce::Colour good     { 0xff5dbb63 };
    const juce::Colour amber    { 0xffeab308 };

    // fonts (Inter, bundled) scaled by the user's font-size setting
    juce::Font font (float size, int weight = 0);   // weight: 0 regular, 1 medium, 2 semibold
    float& fontScale();
}

class NamLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NamLookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::font (13.0f, 1); }
    juce::Font getPopupMenuFont() override { return theme::font (14.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    int getPopupMenuBorderSize() override { return 6; }
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
};

// The NH speaker-ring logo, drawn as vectors so it stays sharp at any zoom.
void drawLogo (juce::Graphics&, juce::Rectangle<float> area);
