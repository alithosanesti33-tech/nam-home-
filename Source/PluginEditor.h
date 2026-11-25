#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace theme
{
    // Black background like a hardware modeler's editor; block colours by type.
    const juce::Colour bg       { 0xff000000 };
    const juce::Colour panel    { 0xff121212 };
    const juce::Colour raised   { 0xff1c1c1c };
    const juce::Colour line     { 0xff2c2c2c };
    const juce::Colour text     { 0xffe8e9eb };
    const juce::Colour muted    { 0xffa0a4ab };
    const juce::Colour gate     { 0xff8a8f98 };
    const juce::Colour pedal    { 0xffe0a030 };
    const juce::Colour amp      { 0xff2fb5a8 };
    const juce::Colour cab      { 0xff9b7be0 };
    const juce::Colour eq       { 0xff5dbb63 };
    const juce::Colour fx       { 0xffe26ba8 };
    const juce::Colour shaping  { 0xff8c9098 }; // "added shaping" knobs (not the amp's own controls)
    const juce::Colour warn     { 0xffef4444 };
}

class NamLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NamLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
};

// A labelled knob attached to a parameter.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label, juce::Colour c);
    void resized() override;
    juce::Slider slider;
private:
    juce::Label name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// A small on/off button attached to a parameter.
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label, juce::Colour on);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

class NAMHomeEditor;

//==============================================================================
class LibraryPanel : public juce::Component, private juce::ListBoxModel
{
public:
    LibraryPanel (NAMHomeEditor& e, Library& lib);
    void refresh();
    void showNotice (const juce::String& s);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void selectedRowsChanged (int) override {}

    NAMHomeEditor& editor;
    Library& library;
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> chips;
    int filter = 0;
    juce::ListBox list;
    juce::TextButton importBtn { "Import files" }, folderBtn { "Show folder" };
    juce::String notice;
    std::vector<const LibraryItem*> shown;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// The chain row: IN -> pedal -> amp -> cab -> OUT. (Stage 2 turns this into the full grid.)
class ChainView : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget
{
public:
    enum Slot { PedalSlot = 0, AmpSlot = 1, CabSlot = 2 };
    explicit ChainView (NAMHomeEditor& e);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { hover = slotAt ({ x, y }); repaint(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override { hover = slotAt ({ x, y }); repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = -1; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void loadIntoSlot (int slot, const juce::File& f);
    int selected = AmpSlot;
private:
    juce::Rectangle<float> tileBounds (int slot) const;
    int slotAt (juce::Point<int> p) const;
    NAMHomeEditor& editor;
    int hover = -1;
};

//==============================================================================
class PedalEditor : public juce::Component
{
public:
    explicit PedalEditor (NAMHomeEditor& e);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    NAMHomeEditor& editor;
    Knob in, out;
    ParamToggle bypass;
    juce::TextButton load { "Load capture" }, clear { "Remove" };
};

class AmpEditor : public juce::Component
{
public:
    explicit AmpEditor (NAMHomeEditor& e);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    NAMHomeEditor& editor;
    Knob drive, tight, preMid, low, mid, high, out;
    ParamToggle bypass, normalize;
    juce::TextButton load { "Load capture" }, clear { "Remove" };
};

class CabEditor : public juce::Component
{
public:
    explicit CabEditor (NAMHomeEditor& e);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshTags();
private:
    struct Row
    {
        std::unique_ptr<juce::Slider> level, align;
        std::unique_ptr<Knob> pan;
        std::unique_ptr<ParamToggle> polarity, mute;
        std::unique_ptr<juce::TextEditor> mic;
        std::unique_ptr<juce::TextButton> load, clear;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAtt, alignAtt;
        juce::Rectangle<int> area;
    };
    NAMHomeEditor& editor;
    std::array<Row, NAMHomeProcessor::numIrs> rows;
    Knob lowCut, highCut;
    ParamToggle bypass;
};

//==============================================================================
class NAMHomeEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit NAMHomeEditor (NAMHomeProcessor&);
    ~NAMHomeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

    // Loads a library file into the right slot (IRs go to the given cab row, or the first empty one).
    void loadItem (const juce::File& f, int irRow = -1);
    void chooseFileFor (int slot, int irRow = -1);
    void select (int slot);
    void showError (const juce::String& s);
    void refreshAll();

    NAMHomeProcessor& proc;

private:
    void timerCallback() override;
    void showSettingsMenu();

    NamLookAndFeel lnf;
    juce::TextButton libraryBtn { "Library" }, settingsBtn { "Settings" };
    LibraryPanel libraryPanel;
    ChainView chain;
    PedalEditor pedalEditor;
    AmpEditor ampEditor;
    CabEditor cabEditor;
    bool libraryOpen = true;
    float inLevel = 0, outLevel = 0;
    double cpu = 0;
    juce::String errorText;
    int errorTicks = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
