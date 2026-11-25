#pragma once
#include "Blocks.h"
#include "phat/PhatCore.h"
#include "Measure.h"
#include "dsp/GuitarTracker.h"
#include "dsp/SaxVoice.h"
#include "dsp/SaxSampler.h"

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
    juce::Colour getColour() const override { const float g = loaded.load() ? measure::recall (path) : -1.0f; return g >= 0 ? measure::gainColour (g) : Block::getColour(); }
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
    juce::Colour getColour() const override
    {
        for (int i = 0; i < numIrs; ++i) if (isIrLoaded (i)) { const float b = measure::recall (getIrPath (i)); if (b >= 0) return measure::irColour (b); break; }
        return Block::getColour();
    }
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
    void prepare (double, int) override { envDb = -100; grDb = 0; sustained = 0; }
    void process (float* L, float* R, int n) override;
private:
    void processFet (float* L, float* R, int n);
    float envDb = -100, grDb = 0, sustained = 0;
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
    enum Kind { Digital, Tape, Analog, PingPong, Dual, Reverse, MultiTap, Swell, Crystal };
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
    ndsp::PitchShifter pitchSh[2];
    float pitchRatio[2] = { 1, 1 }, crystalRaw[2] = { 0, 0 };
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
    float glideMidi = 60, lastHz = 0, glideOff = 0, lastNote = 60;
    bool everValid = false;
    float validGain = 0, dryGate = 0;
    float validCoef = 0.05f, dryAtk = 0.01f, dryRel = 0.001f;
};

// Saxophone: soprano, alto, tenor or baritone, played by your guitar.
class SaxBlock : public Block
{
public:
    SaxBlock();
    std::vector<StartingPoint> startingPoints() const override;
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
    ndsp::SaxSampler sampler;
    ndsp::Biquad recBright, recSub;
    float lastBright = -1, lastSub = -1, growlPh = 0;
    float rate = 48000.0f;
    bool sounding = false, pending = false, pendingFromSilence = false, releasing = false;
    int pendingAge = 0;
    float fold = 0, glideOff = 0, scoopOff = 0, fallOff = 0, fallTarget = 0;
    float breath = 0, bite = 0, tongue = 0, noteVel = 0.7f, noteTime = 0, vibPhase = 0, vibFromGuitar = 0, dipEnv = 0;
    float lastPitch = 60.0f, outPitch = 60.0f;
    float samplerNote = 60.0f, bendSm = 0, growlRnd = 0;
    uint32_t growlSeed = 777u;
    float growlNoise() { growlSeed = growlSeed * 1664525u + 1013904223u; return (float) (growlSeed >> 8) / 8388608.0f - 1.0f; }
};

//==============================================================================
// More effects (Effects2.cpp)

// Chorus models: warm analog, Dimension-style, 80s stereo rack, ensemble, detune.
class ChorusBlock : public Block
{
public:
    ChorusBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    ndsp::DelayLine line[2];
    ndsp::Biquad bbdLp[2], preHp[2];
    float ph[6] {};
    ndsp::Wander wander[4];
};

// Phasers: small orange pedal, script, big stereo studio phaser.
class PhaserBlock : public Block
{
public:
    PhaserBlock();
protected:
    void prepare (double, int) override { reset(); }
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    struct AP1 { float z = 0; inline float process (float x, float a) { const float y = a * x + z; z = x - a * y; return y; } };
    AP1 ap[2][12];
    float fb[2] {}, ph = 0;
};

// Flangers: jet, filter-matrix, through-zero tape.
class FlangerBlock : public Block
{
public:
    FlangerBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    ndsp::DelayLine line[2], ref[2];
    float fb[2] {}, ph = 0;
    ndsp::Biquad hp[2];
};

// Whammy: two octaves up and down from an expression pedal or MIDI.
class WhammyBlock : public Block
{
public:
    WhammyBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override { for (auto& s : shifter) s.clear(); posSm = -1; }
    void process (float* L, float* R, int n) override;
private:
    ndsp::PitchShifter shifter[2];
    float posSm = -1;
};

// Polyphonic octave (Micro POG / POG2 style): sub octaves, octaves up, dry, detune, attack, filter.
class OctaveBlock : public Block
{
public:
    OctaveBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    static constexpr int order = 10, N = 1 << order, hop = N / 8, voices = 4;
    void spectralFrame (const float ratios[voices], const float gains[voices]);
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> inRing, window, outAcc, buf, lastPhase, mag, freq, synPhase[voices], outRe, outIm;
    int inPos = 0, hopCount = 0, outPos = 0;
    ndsp::PitchShifter fast[voices];
    ndsp::Biquad lp, hp;
    float fastEnv = 0, slowEnv = 0, attackGain = 1;
    float lastFilter = -1;
};

// Harmonizer: one or two voices that stay in key.
class HarmonizerBlock : public Block
{
public:
    HarmonizerBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    ndsp::GuitarTracker tracker;
    ndsp::PitchShifter voice[2];
    float ratioSm[2] { 1, 1 };
    int heldNote = -1;
};

// Envelope filter (auto-wah).
class EnvFilterBlock : public Block
{
public:
    EnvFilterBlock();
protected:
    void prepare (double, int) override { reset(); }
    void reset() override { for (auto& f : svf) f.reset(); env = 0; }
    void process (float* L, float* R, int n) override;
private:
    ndsp::SVF svf[2];
    float env = 0;
};

// Auto-swell: fades each note in.
class SwellBlock : public Block
{
public:
    SwellBlock();
protected:
    void prepare (double, int) override { reset(); }
    void reset() override { fast = slow = gain = 0; cooldown = 0; }
    void process (float* L, float* R, int n) override;
private:
    float fast = 0, slow = 0, gain = 0; int cooldown = 0;
};

// Doubler: a fake second take.
class DoublerBlock : public Block
{
public:
    DoublerBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override { line.clear(); }
    void process (float* L, float* R, int n) override;
private:
    ndsp::DelayLine line;
    ndsp::Wander drift[2];
    ndsp::Biquad tone[2];
};

// Fuzz: Big Muff style and Fuzz Face style.
class FuzzBlock : public Block
{
public:
    FuzzBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void process (float* L, float* R, int n) override;
private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    ndsp::Biquad inHp[2], muffLp[2], muffHp[2], faceLp[2], bigLow[2], post[2];
    ndsp::OnePole stageLp[2][2];
    ndsp::Smoothed level;
    float biasEnv[2] {};
};

// "@ Drive": Andy Timmons style British crunch with 25/50/100 W headroom and a TS boost.
class ATDriveBlock : public Block
{
public:
    ATDriveBlock();
protected:
    void prepare (double sr, int maxBlock) override;
    void process (float* L, float* R, int n) override;
private:
    void updateFilters();
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    ndsp::Biquad tight[2], mid[2], eq[2], air[2], boostHp[2], boostMid[2], post[2];
    ndsp::OnePole interLp[2][2];
    ndsp::Smoothed level, drive;
    float sag[2] {};
    float lastEq = -1, lastAir = -1, lastHead = -1;
};

// Freeze (NightSky inspired): hold what you played, keep only the notes you choose, or drone on them.
class FreezeBlock : public Block
{
public:
    FreezeBlock();
    bool hasTails() const override { return true; }
    bool canCutTail() const override;
    std::vector<StartingPoint> startingPoints() const override;
protected:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    static constexpr int order = 13, fftSize = 1 << order, hop = fftSize / 4;
    void capture (bool add);
    void synthFrame();
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> inRing, window, mag, frozen, work, outAcc[2], phase[2], noteWeight, envSmooth;
    int inPos = 0, hopCount = 0, outRead = 0;
    bool holding = false, haveFrozen = false;
    float level = 0, lastNoteSet = -1;
    juce::Random rng;
    // sound shaping
    float modPh = 0, shimmerSm = 0;
    // step sequencer: steps change the pitch (with glide) and volume of the frozen sound
    ndsp::PitchShifter seqShift[2];
    double seqPos = 0;        // in steps
    int seqStarts = -1, seqStep = -1;
    bool seqWasRunning = false;
    float seqSemi = 0, seqGain = 1, seqGainTarget = 1, seqSemiTarget = 0;
    void advanceSequencer (int n);
};

// Tremolo: optical-style sine, triangle, choppy square, or harmonic (Brownface-style band split).
class TremoloBlock : public Block
{
public:
    TremoloBlock();
protected:
    void prepare (double sr, int) override;
    void reset() override { ph = 0; for (auto& f : split) f.reset(); }
    void process (float* L, float* R, int n) override;
private:
    float ph = 0;
    ndsp::Biquad split[4];   // low/high for each channel (harmonic mode)
};

// Vibrato: pure pitch wobble.
class VibratoBlock : public Block
{
public:
    VibratoBlock();
protected:
    void prepare (double sr, int) override;
    void reset() override { for (auto& l : line) l.clear(); ph = 0; }
    void process (float* L, float* R, int n) override;
private:
    ndsp::DelayLine line[2];
    float ph = 0;
};

// Rotary speaker, after the BOSS RT-20 (modelled on a Leslie 122): horn and drum with their own
// slow/fast speeds and rise time, balance, tube drive, brake, and three modes.
class RotaryBlock : public Block
{
public:
    RotaryBlock();
protected:
    void prepare (double sr, int) override;
    void reset() override;
    void process (float* L, float* R, int n) override;
private:
    ndsp::Biquad xLow, xHigh, hornTone[2], cabinet[2];
    ndsp::DelayLine hornLine, drumLine;
    float hornPh = 0, drumPh = 0.3f, hornRate = 0.8f, drumRate = 0.7f;
};

// Uni-Vibe style: four staggered phase stages swept by a lamp, chorus or vibrato.
class VibeBlock : public Block
{
public:
    VibeBlock();
protected:
    void prepare (double, int) override { reset(); }
    void reset() override { for (auto& c : ap) for (auto& a : c) a.z = 0; lamp = 0; ph = 0; }
    void process (float* L, float* R, int n) override;
private:
    struct AP1 { float z = 0; inline float process (float x, float a) { const float y = a * x + z; z = x - a * y; return y; } };
    AP1 ap[2][4];
    float lamp = 0, ph = 0;
};
