#pragma once
#include "BlockTypes.h"

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
    // rowInput[r] = 0 means "from main path" (split/merge); 1..2 = interface input 1..2.
    // Row 0 always has its own input (1 by default). A row with its own input is a separate rig,
    // mixed into the output with its merge level and pan.
    std::atomic<int> rowInput[rows] { { 1 }, { 0 }, { 0 }, { 0 } };
    bool rowIsIndependent (int r) const { return r == 0 || rowInput[r].load() > 0; }
    std::atomic<float> inputPeaks[2] { { 0 }, { 0 } };

    // ---- global controls
    std::atomic<float> inputGainDb { 0 }, outputGainDb { 0 };
    std::atomic<bool> monoOut { false };
    std::atomic<bool> metronomeOn { false };
    std::atomic<float> metronomeLevelDb { -14 };
    std::atomic<int> beatsPerBar { 4 };
    void tapTempo();                                           // message thread
    void setBpm (float bpm) { Block::context().bpm = juce::jlimit (30.0f, 300.0f, bpm); }
    float getBpm() const { return Block::context().bpm.load(); }

    // ---- meters
    std::atomic<float> inputPeak { 0 }, outputPeakL { 0 }, outputPeakR { 0 };
    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }

    // ---- MIDI learn
    struct Mapping { int cc = -1; int blockId = 0; int param = 0; };  // param -1 = on/off; blockId -1 = tap tempo
    static constexpr int maxMappings = 64;
    void armLearn (int blockId, int param);   // next CC that arrives gets mapped here
    void cancelLearn() { learnArmed = false; }
    bool isLearning() const { return learnArmed.load(); }
    void clearMappingsFor (int blockId, int param);
    juce::String describeMapping (int blockId, int param) const;
    std::atomic<int> lastCc { -1 }, pendingProgram { -1 };
    std::atomic<bool> tapFromMidi { false };
    void pollMessageThread();                // timer: finishes learn, MIDI taps

    // ---- save / load
    juce::var toVar() const;
    void fromVar (const juce::var& v);

private:
    struct Plan
    {
        Block* cell[rows][cols] {};
        int first[rows] {}, last[rows] {};
        bool active[rows] {};
    };
    void publish();
    void handleMidi (juce::MidiBuffer& midi, const Plan& plan);

    std::array<std::array<std::shared_ptr<Block>, cols>, rows> cells;
    std::vector<std::shared_ptr<Block>> keepAlive;   // blocks the live plan may still point at
    Plan live;
    juce::SpinLock lock;

    std::array<Mapping, maxMappings> mappings;
    std::atomic<bool> learnArmed { false };
    std::atomic<int> learnBlock { 0 }, learnParam { 0 }, learnedCc { -1 };

    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::vector<float> buf[rows][2];
    juce::AudioProcessLoadMeasurer loadMeasurer;

    // metronome
    double beatPos = 0;
    int clickLeft = 0, clickSamples = 0;
    float clickPhase = 0, clickFreq = 1000;

    juce::Array<double> tapTimes;
};
