#pragma once
#include <set>
#include "Widgets.h"
#include "../Library.h"
#include "../Blocks.h"

class NAMHomeProcessor;

// Left pop-out library: captures, IRs and built-in effects. Drag onto the grid.
class LibraryPanel : public juce::Component, private juce::ListBoxModel
{
public:
    LibraryPanel (NAMHomeProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (const juce::File&)> onLoadFile;          // double-click a capture / IR
    std::function<void (Block::Type, int)> onAddEffect;                 // double-click an effect
    std::function<void (const juce::String&, bool)> onMessage;

private:
    struct Entry
    {
        const LibraryItem* item = nullptr; Block::Type type = Block::Type::Gate; bool header = false; juce::String label; int variant = -1;
        bool folder = false; juce::String key; int depth = 0; int count = 0; juce::Colour swatch;
    };
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    std::set<juce::String> openFolders;
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    NAMHomeProcessor& proc;
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> chips;
    int filter = 0;
    juce::ListBox list;
    juce::TextButton importBtn { "Import files" }, folderBtn { "Show folder" };
    std::vector<Entry> shown;
    std::unique_ptr<juce::FileChooser> chooser;
};

// Settings sheet shown over the app.
class SettingsSheet : public juce::Component, private juce::Timer
{
public:
    SettingsSheet (NAMHomeProcessor& p, juce::AudioDeviceManager* dm);
    ~SettingsSheet() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onClose;
    std::function<void()> onDisplayChanged;   // zoom / font size
    std::function<void()> onToggleFullscreen;
    void showPage (int page);

private:
    void timerCallback() override;
    void buildMidiList();
    juce::Rectangle<int> card() const;

    NAMHomeProcessor& proc;
    juce::AudioDeviceManager* deviceManager;
    int page = 0;
    juce::OwnedArray<juce::TextButton> nav;
    IconButton closeBtn { IconButton::Icon::Close, "Close settings" };

    // pages
    juce::Component audioPage, midiPage, libraryPage, t3kPage, displayPage, outputPage, setlistPage;
    // output EQ (whole app, after everything) and spillover
    juce::OwnedArray<ValueKnob> eqKnobs;
    juce::ComboBox outProfile;
    juce::Label outHelp;
    juce::ToggleButton spill { "Let delay, reverb and freeze tails ring over when you change preset" };
    juce::ToggleButton cutScene { "Cut delay, reverb and freeze tails when you change scenes" };
    // setlists
    juce::ComboBox setlistBox;
    juce::TextButton newSetlist { "New setlist" }, addSong { "Add current preset" }, removeSong { "Remove" },
                     learnPrev { "MIDI learn: previous song" }, learnNext { "MIDI learn: next song" }, songUp { "Move up" }, songDown { "Move down" }, loadSong { "Load" }, showSetlists { "Show setlists folder" };
    juce::ListBox songList;
    std::unique_ptr<juce::ListBoxModel> songModel;
    juce::Label setlistHelp;
    void refreshSetlists();
    juce::TextEditor t3kKey;
    juce::TextButton t3kSave { "Save key" }, t3kSignOut { "Sign out" }, t3kOpen { "Open TONE3000 settings" };
    juce::Label t3kHelp, t3kStatus;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    std::unique_ptr<ValueKnob> inGain, outGain, metroLevel;
    juce::ToggleButton monoOut { "Mono output" };
    juce::ComboBox beatsBox;
    juce::Label beatsLabel;

    juce::OwnedArray<juce::ToggleButton> midiToggles;
    juce::TextButton bluetoothBtn { "Pair a Bluetooth MIDI device" };
    juce::TextButton tapLearn { "MIDI learn tap tempo" };
    juce::Label midiInfo, lastCcLabel;

    juce::ToggleButton watchDl { "Watch Downloads for new captures and IRs" },
                       pairIr { "Load the IR that came with an amp capture into the cab" },
                       bypassCab { "Switch off the cab after \"amp + cab\" captures" };
    juce::TextButton showLib { "Show library folder" }, rescan { "Rescan library" }, showPresets { "Show presets folder" };

    juce::Slider zoom, fontSize;
    juce::Label zoomLabel, fontLabel;
    juce::TextButton fullscreen { "Full screen" };
    juce::StringArray lastMidiDevices;
};

// Small panel for a merge point (parallel paths): levels and pans.
class MergePanel : public juce::Component
{
public:
    MergePanel (NAMHomeProcessor& p, int row);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    std::unique_ptr<ValueKnob> mainLevel, mainPan, branchLevel, branchPan;
    int row;
    bool independent = false;
};

// Transpose: semitones and cents, plus common drop tunings.
class TransposePanel : public juce::Component
{
public:
    TransposePanel (NAMHomeProcessor& p, std::function<void()> changed);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    NAMHomeProcessor& proc;
    std::unique_ptr<ValueKnob> semis, cents;
    juce::OwnedArray<juce::TextButton> presets;
    std::function<void()> onChange;
};
