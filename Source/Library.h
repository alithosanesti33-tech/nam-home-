#pragma once
#include <JuceHeader.h>

// One file in the user's library: a NAM capture or a cabinet IR.
struct LibraryItem
{
    enum class Kind { Amp, Pedal, IR };

    juce::File file;
    Kind kind = Kind::Amp;
    juce::String name;        // display name
    juce::String creator;     // "Captured by" / "IR by" (only what the file or user says)
    juce::String gearMake, gearModel, gearType, toneType;
    juce::String pack;        // folder it came from (e.g. a zip name)
    juce::String micTag;      // user-editable label for IRs; never changes the sound
    bool adjustable = false;  // parametric (adjustable-knob) model
    bool isNew = false;

    juce::String creditLine() const;
    juce::String id() const { return file.getFullPathName(); }
};

// Gain level shown on amp tiles, from the file's tone_type when the creator set one.
enum class GainLevel { Unknown, Clean, Crunch, Overdrive, HighGain, Fuzz };
GainLevel gainLevelFromToneType (const juce::String& toneType);
juce::Colour gainLevelColour (GainLevel g);
juce::String gainLevelName (GainLevel g);

// The library folder (Music/NAM Home), the user's tags, and the Downloads watcher.
class Library : private juce::Timer
{
public:
    Library();
    ~Library() override;

    juce::File getFolder() const { return folder; }
    void rescan();
    const std::vector<LibraryItem>& getItems() const { return items; }
    const LibraryItem* findByPath (const juce::String& path) const;

    // Import files the user picked (copied into the library folder).
    int importFiles (const juce::Array<juce::File>& files);

    // Downloads watcher
    void setWatchDownloads (bool shouldWatch);
    bool isWatchingDownloads() const { return watchDownloads; }

    // Inside Logic (plugin): the app does the Downloads watching and TONE3000 downloads; the plugin
    // just notices new files in the shared library folder.
    void setPluginMode (bool isPlugin) { pluginMode = isPlugin; }

    // User tags (saved in library.json inside the library folder)
    void setMicTag (const juce::String& path, const juce::String& tag);
    void setCreator (const juce::String& path, const juce::String& creator);
    void clearNewFlags();

    std::function<void()> onChanged;
    std::function<void (const juce::String& message)> onNotice;

    static constexpr double maxIrSeconds = 2.0;

private:
    void timerCallback() override;
    void checkDownloads();
    int importOne (const juce::File& f, const juce::File& destFolder);
    int importZip (const juce::File& zip);
    static bool isShortWav (const juce::File& f);
    LibraryItem readItem (const juce::File& f) const;
    void loadTags();
    void saveTags();
    void loadSettings();
    void saveSettings();

    juce::File folder;
    std::vector<LibraryItem> items;
    juce::var tags;             // { path: { mic: "", creator: "" } }
    juce::StringArray newPaths;

    bool watchDownloads = true;
    bool pluginMode = false;
    juce::String folderSignature;
    int signatureTick = 0;
    juce::String computeSignature() const;
    juce::int64 watchStartMs = 0;            // only files that arrive after watching began
    juce::StringArray handledDownloads;      // "path|size|mtime"
    std::map<juce::String, juce::int64> pendingSizes; // wait for downloads to finish
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::PropertiesFile> settings;
};
