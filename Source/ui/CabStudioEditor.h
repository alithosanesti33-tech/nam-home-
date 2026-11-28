#pragma once
#include "BlockEditors.h"
#include "cab/CabStudio.h"
#include <set>

// Cab Studio's panel: your cab library on the left, the 3D cab and mics in the middle, what each
// mic is really playing (measured or modelled) on the right, and the knobs along the bottom.
class CabStudioEditor : public BlockEditor, private juce::ListBoxModel
{
public:
    CabStudioEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b);
    ~CabStudioEditor() override;
    void paint (juce::Graphics&) override;
    void sync() override;

protected:
    juce::Rectangle<int> layoutSpecial (juce::Rectangle<int> r) override;

private:
    CabStudioBlock& cs;

    // library
    juce::TextEditor search;
    juce::ListBox list { "Cabs", this };
    juce::TextButton importBtn { "Import..." }, inboxBtn { "Open Inbox" };
    juce::ComboBox showBox;   // all / favourites / sizes / tweed
    // the list is grouped by pack; a pack folds open when you click it
    struct Row { bool header = false; juce::String pack; int count = 0; bool open = false; cab::Entry e; };
    std::vector<Row> shown;
    std::set<juce::String> openPacks;
    juce::String autoOpened;
    int libVersion = -1;
    void refreshList();
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    std::unique_ptr<juce::FileChooser> chooser;

    // right side
    std::unique_ptr<ParamSegments> modeBar;
    juce::ComboBox speakerBox;
    juce::StringArray speakerList;
    juce::Rectangle<int> libArea, stageArea, sideArea, infoArea;

    // 3D stage
    std::unique_ptr<juce::Component> stage;
    juce::String lastState;
    int selectedMic = 0;
    void sendState (bool force);
    void onStageEvent (const juce::var& v);
    juce::var stateVar() const;
};

// Cab Studio as its own full-screen page (top bar button). It edits the rig's cab, which isn't a
// block on the grid: the engine plays it right after the amp.
class CabStudioPage : public juce::Component
{
public:
    CabStudioPage (NAMHomeProcessor& p, std::shared_ptr<Block> rigCab);
    void resized() override;
    void paint (juce::Graphics&) override;
    void sync();
    std::shared_ptr<Block> block() const { return cab; }
    std::function<void()> onClose;
    std::function<void (const juce::String&)> onError;
    std::function<void()> onUseIr;   // back to a plain IR loader with the IR(s) you're on
private:
    NAMHomeProcessor& proc;
    std::shared_ptr<Block> cab;
    std::unique_ptr<CabStudioEditor> editor;
    juce::TextButton power { "On" }, done { "Done" }, offOld { "Turn the other Cab block off" }, useIr { "Just the IR" };
    IconButton info { IconButton::Icon::Info, "Show what each control does" };
    bool oldCabOn = false;
    int oldCount = 0;
};
