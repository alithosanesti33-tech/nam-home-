#pragma once
#include "Blocks.h"
#include "phat/PhatCore.h"
#include "dsp/GuitarTracker.h"
#include "dsp/SaxVoice.h"

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
    ndsp::Biquad tight, preMid, postLow, postMid, postHigh;
    ndsp::Smoothed sIn, sOut;
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
        ndsp::Smoothed gain;
    };
    void rebuild (int index);
    std::array<Ir, numIrs> irs;
    juce::AudioBuffer<float> mono, work;
    ndsp::Biquad lowCut[2], highCut[2];
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
    ndsp::Envelope env;
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
    ndsp::Biquad clipHp[2], tone[2], postHp[2];
    ndsp::OnePole clipLp[2];
    ndsp::DelayLine cleanDelay[2];
    std::vector<float> clean[2];
    ndsp::Smoothed level, gain;
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
    ndsp::SVF svf[2];
    ndsp::Biquad hp[2];
    float posSm = -1;
    ndsp::Smoothed level;
};

class VolumeBlock : public Block
{
public:
    VolumeBlock();
protected:
    void prepare (double sr, int) override { g.reset ((float) sr, 0.02f); }
    void process (float* L, float* R, int n) override;
private:
    ndsp::Smoothed g;
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
    ndsp::Biquad f[2][numBands];
    float last[numBands + 1] {};
    bool dirty = true;
    ndsp::Smoothed level;
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
    void design (ndsp::Biquad* out, float sr) const;
    ndsp::Biquad f[2][numBands + 2];
    std::vector<float> last;
    bool dirty = true;
    ndsp::Smoothed level;
};

class UtilityBlock : public Block
{
public:
    UtilityBlock();
protected:
    void prepare (double sr, int) override { gl.reset ((float) sr); gr.reset ((float) sr); }
    void process (float* L, float* R, int n) override;
private:
    ndsp::Smoothed gl, gr;
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
    ndsp::DelayLine line[2];
    ndsp::Biquad fbLow[2], fbHigh[2], headBump[2];
    ndsp::OnePole age[2];
    ndsp::Wander wow[2];
    ndsp::LFO flutter, mod;
    ndsp::Smoothed timeSm[2];
    ndsp::Envelope swellEnv;
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
    ndsp::DelayLine pre, fdn[N];
    ndsp::Allpass diff[4], springAp[2][12];
    ndsp::OnePole damp[N];
    ndsp::Biquad lowCut[2], springLp[2], springHp[2], drip[2];
    ndsp::Wander wander[N];
    ndsp::DelayLine shimmerBuf;
    float shimmerPh = 0;
    float len[N] {};
    float fdnOut[N] {};
    int configuredKind = -1;
    float configuredSize = -1;
    ndsp::Envelope bloomEnv;
    float bloomGain = 0;
    ndsp::DelayLine springLine[2];
    float springFb[2] = { 0, 0 };

    // Dattorro plate tank
    struct Plate
    {
        ndsp::Allpass inDiff[4], decDiff1[2], decDiff2[2];
        ndsp::DelayLine d1[2], d2[2];
        ndsp::OnePole bandwidth, damping[2];
        ndsp::LFO lfo;
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
    ndsp::DelayLine line[2];
    ndsp::LFO lfo;
    struct AP1 { float z = 0; inline float process (float x, float a) { const float y = a * x + z; z = x - a * y; return y; } };
    AP1 ap[2][12];
    float fb[2] = { 0, 0 };
    float hornPh = 0, drumPh = 0, hornRate = 0.8f, drumRate = 0.7f;
    ndsp::Biquad xLow[2], xHigh[2];
    ndsp::DelayLine hornLine;
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
    ndsp::DelayLine line[3];
    ndsp::Biquad lp[3], inLp;
    ndsp::LFO lfo;
    float fbState = 0;
};

//==============================================================================
// PhatTrack: guitar-to-synth (pitch tracker + Little-Phatty-style mono voice), ported from the PhatTrack plugin.
class PhatTrackBlock : public Block
{
public:
    PhatTrackBlock();
    std::atomic<float> detectedHz { 0 }, gateOpen { 0 };
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    ndsp::GuitarTracker tracker;   // shared with the Saxophone: no old-note blips on re-picks
    phat::Voice voice;
    double rate = 48000.0;
    float glideMidi = 60, lastHz = 0;
    bool everValid = false;
    float validGain = 0, dryGate = 0;
    float validCoef = 0.05f, dryAtk = 0.01f, dryRel = 0.001f;
};

// Saxophone: soprano, alto, tenor or baritone, played by your guitar.
class SaxBlock : public Block
{
public:
    SaxBlock();
    juce::String getTitle() const override;
    juce::String getSubtitle() const override;
    void setModel (int m);   // also sets that horn's usual octave
    std::atomic<float> detectedHz { 0 };
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    ndsp::GuitarTracker tracker;
    ndsp::SaxVoice voice;
    float rate = 48000.0f;
    bool sounding = false, pending = false, pendingFromSilence = false, releasing = false;
    int pendingAge = 0;
    float fold = 0, glideOff = 0, scoopOff = 0, fallOff = 0, fallTarget = 0;
    float breath = 0, bite = 0, tongue = 0, noteVel = 0.7f, noteTime = 0, vibPhase = 0, vibFromGuitar = 0, dipEnv = 0;
    float lastPitch = 60.0f, outPitch = 60.0f;
};
