#pragma once
#include "Spectrum.h"
#include "Widgets.h"
#include "../BlockTypes.h"

class NAMHomeProcessor;

// Content of the drop-down editor for one block. Built from the block's parameters.
class BlockEditor : public juce::Component
{
public:
    BlockEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b);
    ~BlockEditor() override;
    // the info button: hover a control to read what it does (Helix Stadium style)
    static bool& infoMode();
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent& e) override { mouseMove (e); }
    void mouseExit (const juce::MouseEvent&) override;
    void resized() override;
    void paint (juce::Graphics&) override;
    virtual void sync();                       // ~20 Hz: follow MIDI and meter changes
    std::shared_ptr<Block> block;
    std::function<void()> onStructureChanged;  // e.g. a type change shows different controls
    std::function<void (const juce::String&)> onError;

protected:
    void rebuild();
    virtual juce::Rectangle<int> layoutSpecial (juce::Rectangle<int> r) { return r; }
    NAMHomeProcessor& proc;
    std::unique_ptr<ParamSegments> typeBar;
    juce::OwnedArray<ParamKnob> knobs;
    juce::OwnedArray<ParamCombo> combos;
    juce::OwnedArray<ParamSwitch> switches;
    bool skipParam (int i) const;
    int firstParam = 0, lastParam = -1;       // range handled by the generic layout

    // blocks with many controls (synths) are laid out in titled sections that scroll sideways
    struct GroupContent : public juce::Component
    {
        juce::Array<std::pair<juce::String, juce::Rectangle<int>>> sections;
        juce::Colour colour;
        void paint (juce::Graphics&) override;
    };
    bool grouped = false;
    juce::String visibleSig;
    struct InfoStrip : public juce::Component
    {
        juce::String title, text;
        juce::Colour colour;
        void paint (juce::Graphics&) override;
    } info;
    void showInfoFor (int paramIndex);
    juce::Rectangle<int> takeInfoArea (juce::Rectangle<int>& r);
    juce::Viewport viewport;
    GroupContent content;
    void layoutGrouped (juce::Rectangle<int> r);
};

class NamEditor : public BlockEditor
{
public:
    NamEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b);
    void paint (juce::Graphics&) override;
    void sync() override;
protected:
    juce::Rectangle<int> layoutSpecial (juce::Rectangle<int> r) override;
private:
    NamBlock& nam;
    juce::TextButton load { "Load capture" }, clear { "Remove" };
    juce::Rectangle<int> info;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastPath;
};

class CabEditor : public BlockEditor
{
public:
    CabEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b);
    void paint (juce::Graphics&) override;
    void sync() override;
protected:
    juce::Rectangle<int> layoutSpecial (juce::Rectangle<int> r) override;
private:
    CabBlock& cab;
    struct Row
    {
        std::unique_ptr<juce::Slider> level, align;
        std::unique_ptr<ParamKnob> pan;
        std::unique_ptr<juce::TextButton> polarity, mute, load, clear;
        std::unique_ptr<juce::TextEditor> mic;
        juce::Rectangle<int> area;
    };
    std::array<Row, CabBlock::numIrs> rows;
    std::unique_ptr<ParamKnob> lowCut, highCut;
    std::unique_ptr<juce::FileChooser> chooser;
};

class PeqEditor : public BlockEditor
{
public:
    PeqEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b);
    ~PeqEditor() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragBand = -1; }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void sync() override;
protected:
    juce::Rectangle<int> layoutSpecial (juce::Rectangle<int> r) override;
private:
    juce::Point<float> bandPoint (int band) const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;
    ParametricEQBlock& peq;
    std::unique_ptr<SpectrumMeter> meter;   // live spectrum behind the curve
    juce::Rectangle<int> graph;
    int dragBand = -1;
    juce::String lastSnapshot;
};

class CompEditor : public BlockEditor
{
public:
    using BlockEditor::BlockEditor;
    void paint (juce::Graphics&) override;
    void sync() override { BlockEditor::sync(); repaint (meter); }
protected:
    juce::Rectangle<int> layoutSpecial (juce::Rectangle<int> r) override { meter = r.removeFromRight (36).reduced (8, 10); return r; }
private:
    juce::Rectangle<int> meter;
};

std::unique_ptr<BlockEditor> createEditorFor (NAMHomeProcessor& p, std::shared_ptr<Block> b);
