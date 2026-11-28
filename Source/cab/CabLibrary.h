#pragma once
#include <JuceHeader.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// Cab Studio's cabinet library.
//
// You drop IR packs (zips, folders or single WAVs) into  <library>/Cabs/Inbox  on your SSD.
// NAM Home unpacks them into  <library>/Cabs/Library , works out which cab, mic, position,
// distance and angle each IR is from its file and folder names, and keeps an index in
// <library>/Cabs/cabs.json . Nothing goes through GitHub: the packs stay on your drive.
namespace cab
{
// the mic models Cab Studio offers (IRs of other mics are kept and usable as "other")
enum class Mic { SM57, MD421, R121, U87, Other };
const char* micName (Mic m);          // "SM57-style dynamic" ...
const char* micShort (Mic m);         // "SM57" ...
Mic micFromId (const juce::String& id);
juce::String micId (Mic m);           // "sm57" ...

struct Ir
{
    juce::String file;     // path relative to Cabs/Library
    Mic mic = Mic::Other;
    juce::String micLabel; // the mic as named in the pack ("C414", "e906"...) when it isn't one of ours
    float r = -1.0f;       // inches from the speaker centre (-1: not stated)
    float dist = -1.0f;    // inches from the grille (-1: not stated)
    float angle = -1.0f;   // degrees off axis (-1: not stated)
    int speaker = -1;      // which speaker, when the pack says (0-based)
    juce::String speakerType; // the speaker loaded when this IR was made ("Jensen P12R-style", "Weber A12Q"...)
    bool room = false;     // a room / ambient mic, not used for close mics
};

struct Entry
{
    juce::String id;          // folder path relative to Cabs/Library
    juce::String name, pack;
    int speakers = 0;         // 1, 2 or 4 (0: not stated, shown as a 1x12)
    int size = 12;            // speaker size in inches
    juce::String speaker;     // speaker type when the names say ("V30", "Greenback"...)
    int back = 0;             // 0 not stated, 1 closed, 2 open
    bool tweed = false;       // tweed-covered (5E3 Deluxe, Champ, Bassman...): drawn in tweed, open back
    bool favourite = false, user = false;
    std::vector<Ir> irs;
    juce::String description() const;
    juce::String config() const;   // "4x12"
    bool hasMic (Mic m) const;
    juce::StringArray speakerTypes() const;   // the speakers this cab was captured with (a speaker choice when more than one)
};

class Library : private juce::Thread
{
public:
    static Library& get();
    void setRoot (const juce::File& libraryFolder);   // message thread, at start-up
    juce::File root() const;
    juce::File inbox() const { return root().getChildFile ("Inbox"); }
    juce::File store() const { return root().getChildFile ("Library"); }
    juce::File fileFor (const juce::String& relFile) const;   // an IR's file (Cabs/Library, or "@lib/..." in your main library)

    void rescan();                                    // imports the inbox and re-reads the store (background)
    void importFiles (const juce::Array<juce::File>& files);   // copies into the inbox, then rescans
    std::vector<Entry> entries() const;
    bool find (const juce::String& id, Entry& out) const;
    void setFavourite (const juce::String& id, bool on);
    int version() const { return changes.load(); }   // goes up whenever the list changes
    juce::String status() const;

    // IR audio, resampled to 48 kHz mono and cached (any thread but the audio thread)
    std::shared_ptr<const std::vector<float>> audio (const juce::String& relFile);
    static constexpr double rate = 48000.0;

private:
    Library() : juce::Thread ("Cab library") {}
    ~Library() override { stopThread (4000); }
    void run() override;
    void importInbox();
    void index();
    void save() const;

    mutable std::mutex m;
    juce::File rootDir;
    std::vector<Entry> list;
    std::map<juce::String, bool> favourites;
    std::map<juce::String, std::shared_ptr<const std::vector<float>>> cache;
    std::atomic<int> changes { 0 };
    std::atomic<bool> again { false };
    juce::String statusText;
};

// names -> facts (exposed for testing)
void parseName (const juce::String& pathLower, Ir& ir);
void parseCab (const juce::String& pathLower, Entry& e);
}
