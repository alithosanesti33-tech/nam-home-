#pragma once
#include <JuceHeader.h>
#include "Library.h"

namespace nam { class DSP; }

// Plain RBJ biquad, coefficients computed without allocating (safe on the audio thread).
struct Biquad
{
    enum class Type { HighPass, LowPass, Peak, LowShelf, HighShelf };
    void set (Type t, double sr, double freq, double q, double gainDb);
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// One NAM capture (amp or pedal). Loaded on the message thread, swapped in safely.
class NamSlot
{
public:
    NamSlot();
    ~NamSlot();
    juce::String load (const juce::File& f, double sampleRate, int maxBlock); // returns error text, empty if OK
    void unload();
    void prepare (double sampleRate, int maxBlock);
    // Processes in place. Returns false if nothing is loaded (signal passes through unchanged).
    bool process (float* data, int numSamples) noexcept;

    juce::String getPath() const { return path; }
    bool isLoaded() const { return loaded.load(); }
    bool sampleRateMismatch() const { return mismatch.load(); }
    double getExpectedRate() const { return expectedRate; }
    float getNormGainDb() const { return normGainDb; }
    std::atomic<bool> normalize { true };

private:
    juce::SpinLock lock;
    std::unique_ptr<nam::DSP> dsp;
    juce::String path;
    std::atomic<bool> loaded { false }, mismatch { false };
    double expectedRate = 48000.0;
    float normGainDb = 0.0f;
    std::vector<float> scratch;
    int maxBlockSize = 512;
};

// One cabinet IR inside the blend.
struct IrSlot
{
    juce::dsp::Convolution conv;
    juce::AudioBuffer<float> original; // message thread only, kept to rebuild alignment / polarity
    double originalRate = 48000.0;
    juce::String path;
    std::atomic<bool> loaded { false };
    int appliedAlign = 0;
};

class NAMHomeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    NAMHomeProcessor();
    ~NAMHomeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "NAM Home"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Loading (message thread)
    juce::String loadAmp (const juce::File& f);
    juce::String loadPedal (const juce::File& f);
    juce::String loadIr (int index, const juce::File& f);
    void clearAmp()   { amp.unload(); }
    void clearPedal() { pedal.unload(); }
    void clearIr (int index);

    NamSlot& getAmp()   { return amp; }
    NamSlot& getPedal() { return pedal; }
    IrSlot& getIr (int i) { return irs[(size_t) i]; }
    static constexpr int numIrs = 3;

    juce::AudioProcessorValueTreeState params;
    Library library;

    std::atomic<float> inputPeak { 0 }, outputPeakL { 0 }, outputPeakR { 0 };
    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }
    double getSampleRateNow() const { return currentRate; }
    bool anyRateMismatch() const { return amp.sampleRateMismatch() || pedal.sampleRateMismatch(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void rebuildIr (int index);
    // Parameter pointers cached once, so the audio thread never looks anything up by name.
    struct P
    {
        std::atomic<float> *inGain, *outLevel, *monoOut, *pedalIn, *pedalOut, *pedalBypass,
                           *ampDrive, *ampTight, *ampPreMid, *ampLow, *ampMid, *ampHigh, *ampOut, *ampBypass, *ampNormalize,
                           *cabLowCut, *cabHighCut, *cabBypass;
        std::atomic<float> *irLevel[3], *irPan[3], *irPolarity[3], *irAlign[3], *irMute[3];
    } pr;

    NamSlot amp, pedal;
    std::array<IrSlot, numIrs> irs;

    double currentRate = 48000.0;
    int currentBlock = 512;
    juce::AudioBuffer<float> mono, irOut, stereo;
    Biquad tight, preMid, postLow, postMid, postHigh;
    Biquad cabLowCut[2], cabHighCut[2];
    juce::SmoothedValue<float> sIn, sPedalIn, sPedalOut, sDrive, sAmpOut, sOut, sIr[numIrs];
    juce::AudioProcessLoadMeasurer loadMeasurer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMHomeProcessor)
};
