#pragma once
#include "Blocks.h"

namespace nam { class DSP; }

//==============================================================================
// NAM capture (amp or pedal). The capture itself is never altered; the knobs are added shaping.
class NamBlock : public Block
{
public:
    explicit NamBlock (bool isAmp);
    ~NamBlock() override;
    juce::String getTitle() const override;
    juce::String getSubtitle() const override;
    juce::var saveExtra() const override;
    void loadExtra (const juce::var&) override;

    juce::String load (const juce::File& f);   // message thread; returns error text or empty
    void unload();
    juce::String getPath() const { return path; }
    bool isLoaded() const { return loaded.load(); }
    bool includesCab() const { return gearType.containsIgnoreCase ("cab"); }
    juce::String getToneType() const { return toneType; }
    juce::String getCreator() const { return creator; }
    juce::String getCaptureName() const { return captureName; }
    juce::String getGear() const { return (gearMake + " " + gearModel).trim(); }
    bool sampleRateMismatch() const { return mismatch.load(); }
    double getExpectedRate() const { return expectedRate; }
    const bool amp;

protected:
    void prepare (double sr, int maxBlock) override;
    void process (float* L, float* R, int n) override;

private:
    juce::SpinLock lock;
    std::unique_ptr<nam::DSP> dsp;
    juce::String path, captureName, creator, gearMake, gearModel, gearType, toneType;
    std::atomic<bool> loaded { false }, mismatch { false };
    double expectedRate = 48000.0;
    float normGainDb = 0;
    std::vector<float> mono, out;
    dsp::Biquad tight, preMid, postLow, postMid, postHigh;
    dsp::Smoothed sIn, sOut;
};

//==============================================================================
// Cabinet: blend of up to three IRs with level, pan, polarity, alignment and mute.
class CabBlock : public Block
{
public:
    static constexpr int numIrs = 3;
    CabBlock();
    juce::String getSubtitle() const override;
    juce::var saveExtra() const override;
    void loadExtra (const juce::var&) override;

    juce::String loadIr (int index, const juce::File& f);   // message thread
    void clearIr (int index);
    juce::String getIrPath (int index) const { return irs[(size_t) index].path; }
    bool isIrLoaded (int index) const { return irs[(size_t) index].loaded.load(); }
    int numLoaded() const;
    void updateAlignment();                                 // message thread (timer)

protected:
    void prepare (double sr, int maxBlock) override;
    void process (float* L, float* R, int n) override;

private:
    struct Ir
    {
        juce::dsp::Convolution conv;
        juce::AudioBuffer<float> original;
        double rate = 48000.0;
        juce::String path;
        std::atomic<bool> loaded { false };
        int appliedAlign = -1;
        dsp::Smoothed gain;
    };
    void rebuild (int index);
    std::array<Ir, numIrs> irs;
    juce::AudioBuffer<float> mono, work;
    dsp::Biquad lowCut[2], highCut[2];
};

//==============================================================================
class GateBlock : public Block
{
public:
    GateBlock();
protected:
    void prepare (double, int) override { env = {}; gain = 1; open = false; holdLeft = 0; }
    void process (float* L, float* R, int n) override;
private:
    dsp::Envelope env;
    float gain = 1;
    bool open = false;
    int holdLeft = 0;
};

class CompressorBlock : public Block
{
public:
    CompressorBlock();
    std::atomic<float> reductionDb { 0 };
protected:
    void prepare (double, int) override { envDb = -100; grDb = 0; }
    void process (float* L, float* R, int n) override;
private:
    float envDb = -100, grDb = 0;
};

class DriveBlock : public Block
{
public:
    DriveBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void process (float* L, float* R, int n) override;
private:
    void updateFilters();
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    dsp::Biquad clipHp[2], tone[2], postHp[2];
    dsp::OnePole clipLp[2];
    dsp::DelayLine cleanDelay[2];
    std::vector<float> clean[2];
    dsp::Smoothed level, gain;
    int lastType = -1, osLatency = 0;
    float lastTone = -1, lastDrive = -1;
};

class WahBlock : public Block
{
public:
    WahBlock();
protected:
    void prepare (double, int) override { for (auto& f : svf) f.reset(); posSm = -1; }
    void process (float* L, float* R, int n) override;
private:
    dsp::SVF svf[2];
    dsp::Biquad hp[2];
    float posSm = -1;
    dsp::Smoothed level;
};

class VolumeBlock : public Block
{
public:
    VolumeBlock();
protected:
    void prepare (double sr, int) override { g.reset ((float) sr, 0.02f); }
    void process (float* L, float* R, int n) override;
private:
    dsp::Smoothed g;
};

class GraphicEQBlock : public Block
{
public:
    GraphicEQBlock();
    static constexpr int numBands = 10;
    static const float freqs[numBands];
protected:
    void prepare (double, int) override { dirty = true; for (auto& c : f) for (auto& b : c) b.reset(); }
    void process (float* L, float* R, int n) override;
private:
    dsp::Biquad f[2][numBands];
    float last[numBands + 1] {};
    bool dirty = true;
    dsp::Smoothed level;
};

class ParametricEQBlock : public Block
{
public:
    ParametricEQBlock();
    static constexpr int numBands = 5;
    double responseDb (double freq) const;   // for the live graph (message thread)
protected:
    void prepare (double, int) override { dirty = true; for (auto& c : f) for (auto& b : c) b.reset(); }
    void process (float* L, float* R, int n) override;
private:
    void design (dsp::Biquad* out, float sr) const;
    dsp::Biquad f[2][numBands + 2];
    std::vector<float> last;
    bool dirty = true;
    dsp::Smoothed level;
};

class UtilityBlock : public Block
{
public:
    UtilityBlock();
protected:
    void prepare (double sr, int) override { gl.reset ((float) sr); gr.reset ((float) sr); }
    void process (float* L, float* R, int n) override;
private:
    dsp::Smoothed gl, gr;
};

//==============================================================================
class DelayBlock : public Block
{
public:
    DelayBlock();
    bool hasTails() const override { return true; }
    enum Kind { Digital, Tape, Analog, PingPong, Dual, Reverse, MultiTap, Swell };
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    dsp::DelayLine line[2];
    dsp::Biquad fbLow[2], fbHigh[2], headBump[2];
    dsp::OnePole age[2];
    dsp::Wander wow[2];
    dsp::LFO flutter, mod;
    dsp::Smoothed timeSm[2];
    dsp::Envelope swellEnv;
    float swellGain = 0, swellLastEnv = 0;
    float revPhase[2] = { 0, 0 };
    float fbState[2] = { 0, 0 };
    juce::Random rng;
};

class ReverbBlock : public Block
{
public:
    ReverbBlock();
    bool hasTails() const override { return true; }
    enum Kind { Room, Hall, Plate, Spring, Shimmer, Bloom, Ambient };
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    static constexpr int N = 8;
    void configure (int kind, float size);
    dsp::DelayLine pre, fdn[N];
    dsp::Allpass diff[4], springAp[2][12];
    dsp::OnePole damp[N];
    dsp::Biquad lowCut[2], springLp[2], springHp[2], drip[2];
    dsp::Wander wander[N];
    dsp::DelayLine shimmerBuf;
    float shimmerPh = 0;
    float len[N] {};
    float fdnOut[N] {};
    int configuredKind = -1;
    float configuredSize = -1;
    dsp::Envelope bloomEnv;
    float bloomGain = 0;
    dsp::DelayLine springLine[2];
    float springFb[2] = { 0, 0 };

    // Dattorro plate tank
    struct Plate
    {
        dsp::Allpass inDiff[4], decDiff1[2], decDiff2[2];
        dsp::DelayLine d1[2], d2[2];
        dsp::OnePole bandwidth, damping[2];
        dsp::LFO lfo;
        float feedback[2] = { 0, 0 };
        float scale = 1;
        int lenD1[2] {}, lenD2[2] {};
        float lenDD1[2] {};
        void init (float sr, float size);
        void clear();
    } plate;
    float plateSize = -1;
};

class ModulationBlock : public Block
{
public:
    ModulationBlock();
    enum Kind { Chorus, Flanger, Phaser, Tremolo, Vibrato, Rotary, Vibe };
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    dsp::DelayLine line[2];
    dsp::LFO lfo;
    struct AP1 { float z = 0; inline float process (float x, float a) { const float y = a * x + z; z = x - a * y; return y; } };
    AP1 ap[2][12];
    float fb[2] = { 0, 0 };
    float hornPh = 0, drumPh = 0, hornRate = 0.8f, drumRate = 0.7f;
    dsp::Biquad xLow[2], xHigh[2];
    dsp::DelayLine hornLine;
    float vibeLamp = 0;
};

class TriChorusBlock : public Block
{
public:
    TriChorusBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override { for (auto& l : line) l.clear(); for (auto& f : lp) f.reset(); fbState = 0; }
    void process (float* L, float* R, int n) override;
private:
    dsp::DelayLine line[3];
    dsp::Biquad lp[3], inLp;
    dsp::LFO lfo;
    float fbState = 0;
};
