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

    float get() const { return value.load(); }
    int getInt() const { return (int) std::lround (value.load()); }
    bool on() const { return value.load() > 0.5f; }
    void set (float v) { value = range.snapToLegalValue (juce::jlimit (range.start, range.end, v)); }
    float getNorm() const { return range.convertTo0to1 (get()); }
    void setNorm (float n) { set (range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n))); }
    juce::String text() const;
};

// Shared musical context (tempo).
struct EngineContext
{
    std::atomic<float> bpm { 120.0f };
};

class Block
{
public:
    enum class Type
    {
        NamAmp, NamPedal, Cab, Gate, Compressor, Drive, Wah, Volume,
        GraphicEQ, ParametricEQ, Delay, Reverb, Modulation, TriChorus, Utility, PhatTrack, Sax, NumTypes
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
    juce::Colour getColour() const { return colour (type); }
    virtual juce::String getTitle() const { return getTypeName(); }   // big text on the tile
    virtual juce::String getSubtitle() const;                         // small text on the tile

    int id = 0;
    std::atomic<bool> enabled { true };
    const float* cleanInput = nullptr;    // the dry interface input for this row (set by the engine each block)
    std::vector<std::unique_ptr<Param>> params;
    Param* param (const juce::String& pid) const;

    void prepareBlock (double sampleRate, int maxBlockSize);
    void run (float* L, float* R, int n);           // bypass crossfade + trails

    virtual void reset() {}
    virtual bool hasTails() const { return false; }
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

private:
    std::vector<float> dryL, dryR, zL, zR;
    float fade = 1.0f;
    bool wasEnabled = true;
    Param* trailsParam = nullptr;
};
