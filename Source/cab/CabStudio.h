#pragma once
#include "Blocks.h"
#include "cab/CabLibrary.h"
#include <array>
#include <mutex>

// Cab Studio: a cabinet from your IR library, recorded with two mics (A and B) that you place
// on the speaker. Each mic plays the measured IR nearest to where you put it; between measured
// spots it blends the nearest ones, and what the pack never measured (a distance, an angle, a
// mic it wasn't recorded with) is modelled with small EQ and delay changes on top. The editor
// says which is which.
//
// The IRs are built on a background thread; the audio thread only runs two convolutions and a
// short chain of filters.
class CabStudioBlock : public Block
{
public:
    CabStudioBlock();
    ~CabStudioBlock() override;

    juce::String getSubtitle() const override;
    juce::var saveExtra() const override;
    void loadExtra (const juce::var&) override;
    std::vector<StartingPoint> startingPoints() const override;

    // which cab and which of its speakers (message thread)
    void setCab (const juce::String& id);
    juce::String getCab() const;
    void setSpeakerType (const juce::String& s);
    juce::String getSpeakerType() const;
    // where on the speaker's face mic k sits, as a clock angle (degrees; looks only: the sound
    // depends on the distance from the centre, which is the Position control)
    float clockAngle (int k) const { return clock[(size_t) (k & 1)].load(); }
    void setClockAngle (int k, float deg) { clock[(size_t) (k & 1)] = deg; }

    // what the builder did, for the editor
    struct Info
    {
        juce::String source[2];    // "Measured: SM57 cap 1 in" / "Blended from 3 measured IRs" / "Modelled..."
        bool measured[2] { false, false };
        float phaseMatch = 0;      // 0..1: how well A and B line up (1 = in phase)
        int alignSamples = 0;      // how far B was moved to line up with A (+ later, - earlier)
        juce::String cabName;
        bool cabMissing = false;
        int builds = 0;
    };
    Info info() const;

    static constexpr int numMics = 2;
    enum Listen { Both, OnlyA, OnlyB };

protected:
    void prepare (double sr, int maxBlock) override;
    void process (float* L, float* R, int n) override;

private:
    struct MicParams { Param *model, *spk, *pos, *dist, *angle, *level, *pol, *on; };
    std::array<MicParams, numMics> mp {};
    Param *mode, *listen, *blend, *alignB, *autoAlign;
    Param *lowCut, *highCut, *reso, *character, *room, *roomSize, *low, *mid, *high, *out, *mono;

    // builder
    class Builder;
    std::unique_ptr<Builder> builder;
    void build();                       // background thread
    juce::String signature() const;
    mutable std::mutex lock;
    juce::String cabId, speakerType;
    Info last;
    std::array<std::atomic<float>, numMics> clock {};

    // audio
    juce::dsp::Convolution conv[numMics];
    std::atomic<bool> hasIr[numMics] { { false }, { false } };
    std::atomic<bool> openBack { false };
    juce::AudioBuffer<float> monoIn, micOut;
    ndsp::Smoothed gainA, gainB, outGain;
    ndsp::Biquad lc[2], hc[2], res[2], tiltLo[2], tiltHi[2], eqLo[2], eqMid[2], eqHi[2];
    // a small stereo room (4-line feedback delay network)
    struct Room
    {
        std::vector<float> line[4];
        int pos[4] {}, len[4] {};
        float damp[4] {};
        void prepare (double sr);
        void process (float inL, float inR, float size, float& outL, float& outR);
    } roomFx;
    double sr = 48000;
};
