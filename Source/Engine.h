#pragma once
#include "BlockTypes.h"
#include <map>

// The signal chain grid: 4 rows x 10 columns.
// Row 0 is the main path from IN to OUT. Any other row that holds blocks splits off the
// main path just before its first block and merges back just after its last block.
class Engine
{
public:
    static constexpr int rows = 4, cols = 10;

    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlock);
    void process (juce::AudioBuffer<float>& buffer, int numInputChannels, juce::MidiBuffer& midi);

    // ---- editing (message thread)
    std::shared_ptr<Block> get (int r, int c) const { return cells[(size_t) r][(size_t) c]; }
    void set (int r, int c, std::shared_ptr<Block> b);        // prepares the block and publishes the change
    void move (int r1, int c1, int r2, int c2);                // swaps the two cells
    void clearAll();
    std::shared_ptr<Block> findById (int id) const;
    bool findCell (int id, int& r, int& c) const;
    bool rowSpan (int r, int& first, int& last) const;        // columns used by a parallel row
    std::shared_ptr<Block> addBlock (Block::Type t, int r, int c);
    double getSampleRate() const { return sampleRate; }

    // ---- merges for parallel rows (index = row 1..3)
    struct Merge
    {
        std::atomic<float> mainLevel { 0 }, mainPan { 0 }, branchLevel { 0 }, branchPan { 0 };
    };
    Merge merges[rows];

    // ---- inputs: like the Quad Cortex, each row can take its own interface input.
    // rowInput[r] = 0 means "from main path" (split/merge); 1..2 = interface input 1..2;
    // 3 = continues the chain from the row above (one long chain wrapped onto the next line).
    // Row 0 always has its own input (1 by default). A row with its own input is a separate rig,
    // mixed into the output with its merge level and pan.
    std::atomic<int> rowInput[rows] { { 1 }, { 0 }, { 0 }, { 0 } };
    bool rowIsIndependent (int r) const { const int in = rowInput[r].load(); return r == 0 || in == 1 || in == 2; }
    bool rowContinues (int r) const { return r > 0 && rowInput[r].load() == 3 && (r == 1 || rowInput[r - 1].load() == 3); }
    // insert-style editing: put a block at a position in a row; the blocks after it slide over
    bool insertInRow (int r, int index, std::shared_ptr<Block> b);
    void removeFromRow (int r, int c, bool closeGap);
    // replace a row's blocks with this list, packed from 'startCol' (already-prepared blocks keep their state)
    bool setRow (int r, int startCol, const std::vector<std::shared_ptr<Block>>& blocks);
    std::vector<std::shared_ptr<Block>> rowBlocks (int r) const;
    std::atomic<float> inputPeaks[2] { { 0 }, { 0 } };

    // ---- global controls
    std::atomic<float> inputGainDb { 0 }, outputGainDb { 0 };
    // global output EQ (one EQ over everything, for headphones / monitors / PA). Not saved in presets.
    // 0 low cut Hz, 1 low dB (100 Hz), 2 low-mid dB (400 Hz), 3 mid dB (1.5 kHz), 4 presence dB (4 kHz), 5 high dB (9 kHz shelf), 6 high cut Hz
    std::atomic<float> outEq[7] { { 20 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 20000 } };
    std::atomic<int> outEqVersion { 0 };
    // level matching: each scene has a trim; the preset has one too. Loudness is measured while you play.
    std::atomic<float> presetTrimDb { 0 };
    // Loudness is measured like Logic's LUFS meter (ITU BS.1770: K-weighted, 400 ms blocks, gated so
    // pauses, decays and hum don't count). Scenes are measured before their trim, so matching twice
    // gives the same answer. Meter 0-7 = scenes A-H, meter 8 = the whole preset (before its own trim).
    static constexpr int presetMeter = 8, loudBins = 160;   // 0.5 dB bins from -70 to +10
    std::atomic<int> loudHist[9][loudBins] {};
    float measuredLoudness (int meter, int* seconds = nullptr) const;   // < -90 when there isn't enough playing yet
    void resetLoudness (int meter);
    bool sceneIsUsed (int s) const;   // has been visited / set up
    void sceneToNewRig (int s);       // this rig becomes just scene s, as scene A (the other scenes are cleared)
    float sceneTrim (int s) const;
    void setSceneTrim (int s, float db);
    // spillover: delay / reverb / freeze tails keep ringing for a few seconds after a preset change
    std::atomic<bool> spilloverOn { true };
    std::atomic<bool> cutTailsOnScene { false };   // scene changes drop delay / reverb / freeze tails
    std::atomic<bool> monoOut { false };
    std::atomic<bool> metronomeOn { false };
    std::atomic<float> metronomeLevelDb { -14 };
    std::atomic<int> beatsPerBar { 4 };
    void tapTempo();                                           // message thread
    void setBpm (float bpm) { Block::context().bpm = juce::jlimit (30.0f, 300.0f, bpm); }
    float getBpm() const { return Block::context().bpm.load(); }

    // ---- tuner: the audio thread copies the chosen input into a ring buffer the UI reads
    std::atomic<bool> tunerOn { false }, tunerMute { true };
    std::atomic<int> tunerInput { 1 };
    static constexpr int tunerSize = 8192;
    float tunerBuf[tunerSize] {};
    std::atomic<int> tunerWrite { 0 };
    void readTuner (float* dest, int n) const;   // newest n samples (message thread)

    // ---- transpose (virtual drop tuning) applied to every interface input before the chains
    std::atomic<int> transposeSemis { 0 };
    std::atomic<float> transposeCents { 0 };

    // ---- meters
    std::atomic<float> inputPeak { 0 }, outputPeakL { 0 }, outputPeakR { 0 };
    std::atomic<float> inputPeakHold { 0 }, limiterHoldDb { 0 };   // the loudest since the last gig check
    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }

    // ---- MIDI learn
    // Any CC, Note or Program Change can be learned (footswitches like the M-Vave Chocolate often send
    // Program Change or Notes). Switches are detected as momentary (press + release) or latching.
    enum MidiKind { kindCC = 0, kindNote = 1, kindProgram = 2 };
    struct Mapping
    {
        int kind = -1, number = 0;   // kind -1 = unused
        int blockId = 0, param = 0;  // param -1 = on/off; blockId -1 = tap tempo
        int toggle = 0;              // 1 = each press toggles / steps; 0 = follow the value
        float lo = 0.0f, hi = 1.0f;  // expression pedal range: heel and toe positions of the control
        int curve = 0;               // 0 linear, 1 slow start, 2 fast start
        juce::String name;           // your name for the switch ("Lead boost"), shown in the MIDI map
    };
    // expression pedal ranges, set from the control's MIDI menu
    void setMappingEnd (int blockId, int param, bool toe, float norm);
    void setMappingCurve (int blockId, int param, int curve);
    void resetMappingRange (int blockId, int param);
    const Mapping* findMapping (int blockId, int param) const;
    static constexpr int maxMappings = 128;
    void armLearn (int blockId, int param, bool global = false);   // next MIDI message that arrives gets mapped here
    // ---- foot controller (global: the same in every preset, saved in the app's settings)
    static constexpr int panicMidiId = -4;      // emergency bypass
    static constexpr int presetSlotMidiId = -5; // param = switch in the bank
    static constexpr int bankMidiId = -6;       // param 0 = bank down, 1 = bank up
    static constexpr int stompMidiId = -7;      // param = k-th effect in the rig (stomp mode)
    static constexpr int stripMidiId = -8;      // param = slot on the live performance strip
    static constexpr int tunerMidiId = -9;
    std::array<Mapping, 64> globalMappings;
    std::atomic<bool> globalsChanged { false };
    juce::var globalsToVar() const;
    void globalsFromVar (const juce::var& v);
    void setGlobals (const std::vector<Mapping>& list);
    std::atomic<bool> panicFromMidi { false }, tunerFromMidi { false };
    std::atomic<int> presetSlotFromMidi { -1 }, bankStepFromMidi { 0 };
    std::atomic<juce::uint32> stripFromMidi { 0 };
    // emergency bypass on the output: 0 off, 1 dry guitar straight through, 2 silence
    std::atomic<int> panic { 0 };
    // the MIDI map (setup page): every assignment, preset and global
    struct MapInfo { bool global; int index; Mapping m; juce::String message, target; bool duplicate; };
    std::vector<MapInfo> listMappings() const;
    juce::String describeTarget (const Mapping& m) const;
    void renameMapping (bool global, int index, const juce::String& name);
    void removeMapping (bool global, int index);
    static juce::String midiMessageName (int kind, int num);
    juce::String learnNote;                    // message thread: e.g. a duplicate warning after learning
    std::atomic<int> hitKind { -1 }, hitNum { 0 }, hitCount { 0 };   // the last MIDI that arrived (for the map's test light)
    void cancelLearn() { learnArmed = false; learnStage = 0; }
    bool isLearning() const { return learnArmed.load(); }
    void clearMappingsFor (int blockId, int param);
    juce::String describeMapping (int blockId, int param) const;
    juce::String lastMidiText() const;
    std::atomic<int> lastKind { -1 }, lastNum { 0 }, lastVal { 0 }, lastChannel { 0 }, lastCc { -1 }, pendingProgram { -1 };
    std::atomic<bool> tapFromMidi { false };
    void pollMessageThread();                // timer: finishes learn, MIDI taps

    // ---- scenes (like the Quad Cortex): each preset has 8 scenes. A scene remembers every block's
    // on/off state and every knob, but keeps the same blocks loaded, so switching has no gap.
    // Edits are remembered in the scene you are on.
    static constexpr int numScenes = 8;
    struct SceneState
    {
        bool valid = false;
        juce::String name;
        float trimDb = 0;
        std::map<int, std::pair<bool, std::vector<float>>> blocks;   // block id -> (on, knob values)
    };
    std::array<SceneState, numScenes> scenes;
    std::atomic<int> currentScene { 0 }, pendingScene { -1 }, sceneVersion { 0 };
    void selectScene (int s);                  // message thread (MIDI goes through pendingScene)
    void copyScene (int from, int to);
    void resetScenes();
    juce::String sceneLabel (int s) const;     // "A" or "A  Lead"
    static constexpr int sceneMidiId = -2;     // MIDI mapping target: blockId -2, param = scene
    static constexpr int setlistMidiId = -3;   // blockId -3: param 0 = previous song, 1 = next song
    std::atomic<int> setlistStepFromMidi { 0 };

    // ---- save / load
    juce::var toVar() const;
    void fromVar (const juce::var& v);

private:
    struct Plan
    {
        Block* cell[rows][cols] {};
        Block* spill[8] {};
        int numSpill = 0;
        int first[rows] {}, last[rows] {};
        bool active[rows] {};
    };
    void publish();
    void captureScene (SceneState& into) const;
    void applyScene (const SceneState& sc);
    void handleMidi (juce::MidiBuffer& midi, const Plan& plan);

    std::array<std::array<std::shared_ptr<Block>, cols>, rows> cells;
    std::vector<std::shared_ptr<Block>> keepAlive;   // blocks the live plan may still point at
    Plan live;
    juce::SpinLock lock;

    std::array<Mapping, maxMappings> mappings;
    std::atomic<bool> learnArmed { false };
    std::atomic<int> learnBlock { 0 }, learnParam { 0 };
    std::atomic<int> learnStage { 0 }, learnedKind { -1 }, learnedNum { 0 };
    std::atomic<bool> learnSawRelease { false }, learnGlobal { false };
    double learnFirstSeen = 0;
    void applyMapping (const Mapping& m, int value, bool press, const Plan& plan);

    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::vector<float> buf[rows][2];
    std::vector<float> cleanBuf[2];
    std::vector<float> spillBuf[2];
    std::vector<std::shared_ptr<Block>> spillBlocks;
    double spillStarted = 0;
    float panicSm = 0, panicMuteSm = 0;
    std::atomic<float> spillGain { 0 };
    float trimSm = 1.0f;
    ndsp::Biquad eqF[2][7];
    ndsp::Biquad kw[2][2];          // K-weighting for the loudness meter
    // output limiter: 1 ms look-ahead, ceiling -1.5 dBFS, so the output never clips or goes into the red
    static constexpr int limMax = 256;
    float limL[limMax] {}, limR[limMax] {}, limReq[limMax + 1] {}, limMin[limMax] {};
    int limPos = 0, limReqPos = 0, limLen = 48;
    double limBox = 48.0;
    float limRel = 1.0f, limRelCoef = 0.0003f;
    void resetLimiter (double sr);
public:
    std::atomic<float> limiterReductionDb { 0.0f };   // for the meter
private:
    float kwRate = 0;
    double subE[4] {};              // last four 100 ms energies (one 400 ms block)
    double subAcc = 0; int subN = 0, subCount = 0, loudScene = -1, loudSkip = 0;
    int eqVersionSeen = -1;
    bool eqActive = false;
    juce::AudioProcessLoadMeasurer loadMeasurer;

    ndsp::PitchShifter shifter[2];

    // metronome
    double beatPos = 0;
    int clickLeft = 0, clickSamples = 0;
    float clickPhase = 0, clickFreq = 1000;

    juce::Array<double> tapTimes;
};
