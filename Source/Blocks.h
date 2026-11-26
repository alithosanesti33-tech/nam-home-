#pragma once
#include <JuceHeader.h>
#include "dsp/Common.h"

class Block;

// One adjustable control on a block. The audio thread only reads 'value'.
struct Param
{
    enum class Kind { Float, Choice, Toggle };

    juce::String id, name, unit;
    Kind kind = Kind::Float;
    juce::NormalisableRange<float> range;
    float def = 0;
    juce::StringArray choices;
    std::atomic<float> value { 0 };
    std::function<bool (const Block&)> visible;   // hidden for types where it does nothing
    bool realControl = true;                      // false = "added shaping" around a capture (drawn grey)
    juce::String group;                           // editor section (blocks with many controls)
    bool momentary = false;                       // a footswitch "press" (looper, drums): scenes leave it alone

    float get() const { return value.load(); }
    int getInt() const { return (int) std::lround (value.load()); }
    bool on() const { return value.load() > 0.5f; }
    void set (float v) { value = range.snapToLegalValue (juce::jlimit (range.start, range.end, v)); }
    float getNorm() const { return range.convertTo0to1 (get()); }
    void setNorm (float n) { set (range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n))); }
    juce::String text() const;
};

// What actually went to your speakers (mono, after everything), for blocks that need to tell your
// speakers apart from a mic, like the talk box's echo cancellation. Written by the engine at the end
// of each audio block; read on the audio thread only.
struct OutputRing
{
    static constexpr int size = 1 << 15;   // ~0.7 s at 48 kHz
    float ring[size] {};
    int write = 0;
    void push (const float* L, const float* R, int n)
    {
        for (int i = 0; i < n; ++i) { ring[write] = 0.5f * (L[i] + R[i]); write = (write + 1) & (size - 1); }
    }
    // sample written 'ago' samples before the newest one (ago >= 0)
    float at (int ago) const { return ring[(write - 1 - ago) & (size - 1)]; }
    static OutputRing& get() { static OutputRing r; return r; }
};

// Shared musical context (tempo).
struct EngineContext
{
    std::atomic<float> bpm { 120.0f };
    // transport: MIDI Start / Stop / Continue (or Logic's play button inside the plugin)
    std::atomic<bool> running { false };
    std::atomic<int> starts { 0 };          // goes up on every Start, so sequencers restart on beat 1
    std::atomic<double> hostBeats { -1.0 }; // Logic's song position in beats while playing, else -1
    // the looper tells the drums when a loop starts and stops ("Starts with the looper")
    std::atomic<int> looperStarts { 0 }, looperStops { 0 };
};

// Spectrum analyzer taps: the audio thread copies a signal into a ring buffer the screen reads.
// tap 0 = the analyzer strip, tap 1 = the parametric EQ editor's background.
struct SpectrumTap
{
    enum Where { Output, Input, BeforeBlock, AfterBlock };
    std::atomic<bool> on { false };
    std::atomic<int> where { Output }, blockId { -1 };
    static constexpr int size = 8192;
    float ring[size] {};
    std::atomic<int> write { 0 };
    void push (const float* L, const float* R, int n)
    {
        int w = write.load();
        for (int i = 0; i < n; ++i) { ring[w] = 0.5f * (L[i] + R[i]); w = (w + 1) & (size - 1); }
        write = w;
    }
    void read (float* dest, int n) const
    {
        int r = (write.load() - n) & (size - 1);
        for (int i = 0; i < n; ++i) { dest[i] = ring[r]; r = (r + 1) & (size - 1); }
    }
    static SpectrumTap& get (int which) { static SpectrumTap taps[2]; return taps[which & 1]; }
};

class Block
{
public:
    enum class Type
    {
        NamAmp, NamPedal, Cab, Gate, Compressor, Drive, Wah, Volume,
        GraphicEQ, ParametricEQ, Delay, Reverb, TriChorus, Utility, PhatTrack, Sax,
        Chorus, Phaser, Flanger, Whammy, Octave, Harmonizer, EnvFilter, Swell, Doubler, Fuzz, ATDrive, Freeze, Tremolo, Vibrato, Rotary, Vibe, Looper, Drums, TalkBox, BackingTrack, NumTypes
    };

    explicit Block (Type t);
    virtual ~Block() = default;

    static std::unique_ptr<Block> create (Type t);
    static juce::String typeName (Type t);
    static juce::String categoryName (Type t);
    static juce::Colour colour (Type t);
    static juce::Array<Type> allTypes();

    Type getType() const { return type; }
    juce::String getTypeName() const { return typeName (type); }
    virtual juce::Colour getColour() const { return colourFor (type, variantIndex()); }
    // blocks with several models (drive types, delay types...) list each one in the library
    int variantIndex() const;                         // current model, or -1
    Param* variantParam() const;               // the choice that picks the model
    static juce::Colour colourFor (Type t, int variant);   // pedals take the colour of the pedal they are based on
    virtual juce::String getTitle() const { return getTypeName(); }   // big text on the tile
    virtual juce::String getSubtitle() const;                         // small text on the tile

    int id = 0;
    std::atomic<bool> enabled { true };
    const float* cleanInput = nullptr;    // the dry interface input for this row (set by the engine each block)
    const float* interfaceIn[2] { nullptr, nullptr };   // interface inputs 1 and 2 (e.g. a mic), set by the engine
    int interfaceInputs = 0;                            // how many inputs the interface really has
    std::vector<std::unique_ptr<Param>> params;
    Param* param (const juce::String& pid) const;

    void prepareBlock (double sampleRate, int maxBlockSize);
    bool isPrepared() const { return prepared; }
    void run (float* L, float* R, int n);   // with on/off crossfade and analyzer taps
    void runInner (float* L, float* R, int n);           // bypass crossfade + trails

    virtual void reset() {}
    virtual bool hasTails() const { return false; }
    virtual bool canCutTail() const { return true; }   // e.g. a held freeze keeps going
    std::atomic<bool> cutTail { false };              // set on a scene change: drop the echoes / reverb tail now

    // ready-made settings for this block ("Saturn's Ring", "Smooth soprano"...), from the settings panel
    struct StartingPoint { juce::String name, note; std::vector<std::pair<const char*, float>> values; };
    virtual std::vector<StartingPoint> startingPoints() const { return {}; }
    void applyStartingPoint (const StartingPoint& sp)
    {
        for (auto& [pid, v] : sp.values)
            for (auto& prm : params) if (prm->id == pid) { prm->set (v); break; }
    }
    virtual juce::var saveExtra() const { return {}; }
    virtual void loadExtra (const juce::var&) {}

    juce::var toVar() const;
    static std::unique_ptr<Block> fromVar (const juce::var& v);

    static EngineContext& context();

protected:
    virtual void prepare (double sr, int maxBlock) = 0;
    virtual void process (float* L, float* R, int n) = 0;

    Param& addFloat (const juce::String& pid, const juce::String& name, float lo, float hi, float def,
                     const juce::String& unit = {}, float centre = -1.0f, float step = 0.0f);
    Param& addChoice (const juce::String& pid, const juce::String& name, const juce::StringArray& choices, int def);
    Param& addToggle (const juce::String& pid, const juce::String& name, bool def);
    float p (int index) const { return params[(size_t) index]->value.load(); }
    juce::String currentGroup;   // params added while set get this group

    Type type;
    float sampleRate = 48000.0f;
    int maxBlock = 512;
    bool prepared = false;

private:
    std::vector<float> dryL, dryR, zL, zR, cutL, cutR;
    float fade = 1.0f;
    bool wasEnabled = true;
    Param* trailsParam = nullptr;
};
