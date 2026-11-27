#pragma once
#include "ui/Spectrum.h"
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Look.h"
#include "ui/Widgets.h"
#include "ui/GridView.h"
#include "ui/BlockEditors.h"
#include "ui/Panels.h"
#include "ui/Tuner.h"
#include "ui/SceneBar.h"

// Tempo display: drag up/down to change, double-click to type a value.
class TempoDisplay : public juce::Component
{
public:
    explicit TempoDisplay (Engine& e) : engine (e) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { startBpm = engine.getBpm(); }
    void mouseDrag (const juce::MouseEvent& e) override { engine.setBpm (std::round (startBpm - e.getDistanceFromDragStartY() * 0.25f)); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    Engine& engine;
    float startBpm = 120;
};

// The bottom drop-down editor for the selected block.
class Drawer : public juce::Component
{
public:
    Drawer (NAMHomeProcessor& p);
    void show (std::shared_ptr<Block> b);
    void clear() { editor = nullptr; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void sync();
    std::shared_ptr<Block> current() const { return editor != nullptr ? editor->block : nullptr; }
    std::function<void()> onClose, onChanged;
    std::function<void (const juce::String&, bool)> onMessage;
    // Logic-style: drag the top edge to resize; drag it far enough down and it closes
    std::function<void (int deltaY, bool finished)> onResizeDrag;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    bool resizing = false;
    NAMHomeProcessor& proc;
    std::unique_ptr<BlockEditor> editor;
    IconButton power { IconButton::Icon::Power, "Switch this block on or off (double-click the tile also works)" };
    IconButton midi { IconButton::Icon::Midi, "MIDI learn: on/off switch for this block" };
    IconButton close { IconButton::Icon::Chevron, "Close the editor (Esc)" };
    juce::TextButton starts { "Starting points" };
    IconButton info { IconButton::Icon::Info, "Info: point at a control to see what it does" };
    juce::String lastTitle;
};

class NAMHomeEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    explicit NAMHomeEditor (NAMHomeProcessor&);
    ~NAMHomeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void parentHierarchyChanged() override;
    // shortcuts (Cmd+Z...) work wherever the keyboard focus is, even after a menu closes
    struct KeyRelay : juce::KeyListener
    {
        NAMHomeEditor& e; explicit KeyRelay (NAMHomeEditor& ed) : e (ed) {}
        bool keyPressed (const juce::KeyPress& k, juce::Component*) override { return e.keyPressed (k); }
    } keyRelay { *this };
    juce::Component::SafePointer<juce::Component> keyTop;

    void message (const juce::String& s, bool warning = false) { toasts.show (s, warning); }
    void toggleFullscreen();
    void applyDisplaySettings();
    void handleUrl (const juce::String& url) { if (proc.tone3000 != nullptr) proc.tone3000->handleCallback (url); }

private:
    void timerCallback() override;
    void selectCell (juce::Point<int> cell);
    void openDrawer (std::shared_ptr<Block> b);
    void closeDrawer();
    void showPresetMenu();
    void saveAs (bool rename);
    void saveAsWithName (const juce::String& suggested);
    void saveAsImpl (bool rename, const juce::String& suggested);
    void stepPreset (int delta);
    void refreshAll();
    void loadFromLibrary (const juce::File& f);
    void addEffectFromLibrary (Block::Type t, int variant = -1);
    void openSettings();
    juce::String presetTitle() const;

    NAMHomeProcessor& proc;
    NamLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // top bar
    IconButton libraryBtn { IconButton::Icon::Library, "Show or hide the library" };
    IconButton prevBtn { IconButton::Icon::Prev, "Previous preset" }, nextBtn { IconButton::Icon::Next, "Next preset" };
    juce::TextButton presetBtn;
    IconButton saveBtn { IconButton::Icon::Save, "Save preset (Cmd+S)" };
    TempoDisplay tempo;
    juce::TextButton tapBtn { "Tap" };
    IconButton metroBtn { IconButton::Icon::Metronome, "Metronome" };
    juce::TextButton browseT3k { "Browse TONE3000" };
    juce::TextButton transposeBtn;
    void updateTransposeText();
    Meter inMeter, outMeter;
    IconButton tunerBtn { IconButton::Icon::Tuner, "Tuner (live tuner and mute options inside)" };
    IconButton analyzerBtn { IconButton::Icon::Spectrum, "Spectrum analyzer" };
    AnalyzerStrip analyzer;
    bool analyzerOpen = false;
    float analyzerShown = 0;   // 0..1, slides up
    std::unique_ptr<LiveTuner> liveTuner;
    std::unique_ptr<TunerPanel> tunerPanel;
    void openTuner();
    IconButton fullBtn { IconButton::Icon::Fullscreen, "Full screen" };
    IconButton liveBtn { IconButton::Icon::Live, "Live mode: a big, safe performance screen (hold to exit)" };
    std::unique_ptr<LiveView> liveView;
    void enterLive();
    void exitLive();
    void showGigCheck();
    juce::AudioDeviceManager* deviceManager() const;
    juce::Rectangle<int> panicLabel;
    IconButton settingsBtn { IconButton::Icon::Gear, "Settings" };

    LibraryPanel library;
    GridView grid;
    SceneBar sceneBar;
    Drawer drawer;
    Toasts toasts;
    std::unique_ptr<SettingsSheet> settingsSheet;

    bool libraryOpen = true, drawerOpen = false;
    int drawerHeight = 340, drawerDragStart = 340, drawerLive = -1;
    double cpu = 0;
    juce::Rectangle<int> topBar;
};
