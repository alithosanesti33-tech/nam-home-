#pragma once
#include <JuceHeader.h>
#include "Engine.h"
#include "Library.h"
#include "Tone3000.h"

// Saved rigs live as .namrig files (JSON) in Music/NAM Home/Presets.
class PresetManager
{
public:
    explicit PresetManager (const juce::File& libraryFolder);
    juce::File getFolder() const { return folder; }
    juce::StringArray list() const;
    bool save (const juce::String& name, const juce::var& rig);
    juce::var load (const juce::String& name) const;
    bool remove (const juce::String& name);
    bool rename (const juce::String& from, const juce::String& to);
    bool exists (const juce::String& name) const { return fileFor (name).existsAsFile(); }
    static juce::String cleanName (const juce::String& s);
private:
    juce::File fileFor (const juce::String& name) const { return folder.getChildFile (cleanName (name) + ".namrig"); }
    juce::File folder;
};

class NAMHomeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    NAMHomeProcessor();
    ~NAMHomeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "NAM Home"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    Engine engine;
    Library library;
    PresetManager presets;
    juce::PropertiesFile& settings() { return *appSettings; }
    bool isPlugin() const { return wrapperType != wrapperType_Standalone; }
    std::unique_ptr<Tone3000> tone3000;

    // presets
    juce::String currentPreset;
    std::vector<juce::String> undoStack, redoStack;
    juce::String lastSnapshot;
    double lastAutosave = 0;
    juce::String lastAutosaved;
    bool dirty = false;
    juce::var rigToVar() const;
    void rigFromVar (const juce::var& v);
    bool loadPreset (const juce::String& name);

    // ---- undo / redo (Cmd+Z, Cmd+Shift+Z): snapshots of the rig, taken when you pause editing
    void snapshotIfChanged();
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // ---- setlists: presets in song order, saved in your library folder (on your SSD)
    juce::File setlistFolder() const { return library.getFolder().getChildFile ("Setlists"); }
    juce::StringArray setlistNames() const;
    juce::StringArray setlist;          // the active setlist's presets, in order
    juce::String setlistName;
    int setlistPos = -1;
    void loadSetlist (const juce::String& name);
    void saveSetlist() const;
    bool stepSetlist (int delta);       // next / previous song (also from a footswitch)

    // ---- level matching
    float referenceLevelDb() const { return (float) appSettings->getDoubleValue ("levelRef", -20.0); }
    void setReferenceLevel (float db) { appSettings->setValue ("levelRef", db); }
    juce::String matchPresetLevel();    // sets this preset's trim so it plays at the reference level
    juce::String matchScenesToA();      // every scene plays as loud as scene A

    // ---- output EQ (the whole app) and profiles
    static juce::StringArray outputProfileNames();
    void applyOutputProfile (int index);
    void setOutputEq (int band, float v);
    void loadOutputSettings();

    // ---- autosave and crash recovery
    void autosaveTick();
    bool crashedLastTime = false;
    bool recoveredRig = false;     // the editor shows a note once
    juce::File autosaveFile() const;
    bool savePreset (const juce::String& name);
    void newRig();
    void buildDefaultRig();
    std::function<void()> onRigChanged;      // UI refresh after a preset/program change
    std::function<void (const juce::String&)> onNotice;

    // loading files into blocks, with the TONE3000-style helpers
    juce::String loadFileIntoBlock (const juce::File& f, int row, int col);
    void afterAmpLoaded (int row, int col);

    double getSampleRateNow() const { return currentRate; }

private:
    void timerCallback() override;
    std::unique_ptr<juce::PropertiesFile> appSettings;
    double currentRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMHomeProcessor)
};
