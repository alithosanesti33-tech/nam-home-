#pragma once
#include "Blocks.h"
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// Looper (Guitargate Teleport style: 4 tracks, uncompressed WAV loops) and a drum machine.
// Everything they save or read lives in your library folder (on your SSD):
//   <library>/Loops/<date time>/Track 1.wav ...   your loops, 24-bit WAV
//   <library>/Drums/Kits/<kit>/<drum>_<1-3>.wav   recorded drum kits (downloaded by namupdate.sh)
//   <library>/Drums/Loops/*.wav                   your own drum loops
namespace rhythm
{
void setLibraryFolder (const juce::File&);
juce::File libraryFolder();

// drums in a kit
enum Drum { Kick, Snare, Rim, Hat, HatOpen, HatPedal, Ride, Bell, Crash, Tom1, Tom2, Tom3, NumDrums };
const char* drumFileName (int d);

struct DrumSample
{
    juce::AudioBuffer<float> data;   // stereo
    double rate = 48000;
};

struct DrumKit
{
    juce::String name;
    std::vector<DrumSample> layers[NumDrums];   // soft -> hard
};

struct LoopFile
{
    juce::String path;
    juce::AudioBuffer<float> data;   // stereo
    double rate = 48000;
};

// Loads recorded kits and drum loops away from the audio thread. Loaded sounds stay in memory
// until NAM Home quits, so the audio thread can keep plain pointers to them.
class DrumBank : private juce::Thread
{
public:
    static DrumBank& get();
    juce::StringArray kitNames() const;          // folders in Drums/Kits
    juce::Array<juce::File> loopFiles() const;   // wavs in Drums/Loops (and subfolders)
    void requestKit (const juce::String& name);
    void requestLoop (const juce::String& path);
    const DrumKit* kit (const juce::String& name) const;     // nullptr until loaded
    const LoopFile* loop (const juce::String& path) const;
private:
    DrumBank() : juce::Thread ("Drum sounds") {}
    ~DrumBank() override { stopThread (3000); }
    void run() override;
    mutable std::mutex m;
    std::map<juce::String, std::unique_ptr<DrumKit>> kits;
    std::map<juce::String, std::unique_ptr<LoopFile>> loops;
    juce::StringArray wantKits, wantLoops;
};
} // namespace rhythm

//==============================================================================
class LooperBlock : public Block
{
public:
    LooperBlock();
    ~LooperBlock() override;
    juce::String getTitle() const override;
    std::vector<StartingPoint> startingPoints() const override;
    // called by the writer thread
    void writeIfNeeded();
    enum class State { Empty, Recording, Playing, Overdub, Stopped };
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override {}
    void process (float* L, float* R, int n) override;
private:
    static constexpr int tracks = 4;
    static constexpr double maxSeconds = 90.0;
    void press (int which);
    void finishFirstTake();
    juce::AudioBuffer<float> buf[tracks];
    int maxLen = 0, loopLen = 0, recLen = 0, targetLen = 0;
    double pos = 0;
    std::atomic<State> state { State::Empty };
    int recTrack = 0, lastRecorded = -1;
    bool used[tracks] { false, false, false, false };
    float lastPress[8] {};
    std::atomic<bool> wantSave { false }, wantSaveNow { false };
    std::atomic<int> savedLen { 0 };
    juce::String session;   // folder name for this set of loops
    float fadeIn = 0;
    bool primed = false;
    double prepRate = 0;
};

class DrumsBlock : public Block
{
public:
    DrumsBlock();
    juce::String getTitle() const override;
    juce::var saveExtra() const override;
    void loadExtra (const juce::var&) override;
    static juce::StringArray grooveNames();
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    struct Voice { const rhythm::DrumSample* s = nullptr; double pos = 0, inc = 1; float gain = 0, pan = 0; int drum = -1; int delay = 0; };
    void trigger (int drum, float vel, int offset);
    void buildSynthKit();
    juce::StringArray kitChoices, loopChoices, loopPaths;
    rhythm::DrumKit synth;
    const rhythm::DrumKit* recorded = nullptr;
    const rhythm::LoopFile* userLoop = nullptr;
    juce::String wantKit, wantLoop;
    Voice voices[32];
    bool running = false;
    double beat = 0, loopPos = 0;
    int lastStep = -1, lastStarts = -1, lastLooperStarts = -1, lastLooperStops = -1;
    float lastPlay = 0, lastFill = 0;
    bool primed = false;
    int fillBar = -1;
    juce::Random rng;
    double prepRate = 0;
};
