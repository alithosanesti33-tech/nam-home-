#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

//==============================================================================
void Biquad::set (Type t, double sr, double freq, double q, double gainDb)
{
    freq = juce::jlimit (10.0, sr * 0.45, freq);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    const double cw = std::cos (w), sw = std::sin (w);
    const double alpha = sw / (2.0 * q);
    double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;

    switch (t)
    {
        case Type::HighPass:
            B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2;
            A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case Type::LowPass:
            B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2;
            A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case Type::Peak:
            B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
            A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
        case Type::LowShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
            A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s; break;
        }
        case Type::HighShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
            A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s; break;
        }
    }
    b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
    a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
}

//==============================================================================
NamSlot::NamSlot() = default;
NamSlot::~NamSlot() = default;

juce::String NamSlot::load (const juce::File& f, double sampleRate, int maxBlock)
{
    std::unique_ptr<nam::DSP> next;
    try
    {
        next = nam::get_dsp (std::filesystem::path (f.getFullPathName().toStdString()));
    }
    catch (const std::exception& e) { return "Couldn't load this capture: " + juce::String (e.what()); }
    catch (...) { return "Couldn't load this capture."; }
    if (next == nullptr) return "Couldn't load this capture.";

    if (auto* s = dynamic_cast<nam::SlimmableModel*> (next.get()))
        s->SetSlimmableSize (1.0); // always the full-quality model

    const double exp = next->GetExpectedSampleRate();
    expectedRate = exp > 0 ? exp : 48000.0;
    const bool mm = ! next->SupportsArbitrarySampleRate() && std::abs (expectedRate - sampleRate) > 1.0;
    next->Reset (sampleRate, maxBlock);

    const float nDb = next->HasLoudness() ? (float) (-18.0 - next->GetLoudness()) : 0.0f;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (dsp, next);
        normGainDb = nDb;
        path = f.getFullPathName();
        mismatch = mm;
        loaded = true;
    }
    return {}; // old model is destroyed here, outside the lock
}

void NamSlot::unload()
{
    std::unique_ptr<nam::DSP> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (dsp, old);
        loaded = false;
        mismatch = false;
        path = {};
    }
}

void NamSlot::prepare (double sampleRate, int maxBlock)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    maxBlockSize = juce::jmax (32, maxBlock);
    scratch.assign ((size_t) maxBlockSize, 0.0f);
    if (dsp != nullptr)
    {
        dsp->Reset (sampleRate, maxBlockSize);
        mismatch = ! dsp->SupportsArbitrarySampleRate() && std::abs (expectedRate - sampleRate) > 1.0;
    }
}

bool NamSlot::process (float* data, int numSamples) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || dsp == nullptr) return false;

    const float g = normalize.load() ? juce::Decibels::decibelsToGain (normGainDb) : 1.0f;
    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        const int n = juce::jmin (maxBlockSize, numSamples - start);
        float* in = data + start;
        float* out = scratch.data();
        dsp->process (&in, &out, n);
        for (int i = 0; i < n; ++i) in[i] = out[i] * g;
    }
    return true;
}

//==============================================================================
NAMHomeProcessor::NAMHomeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "params", createLayout())
{
    auto g = [this] (const juce::String& id) { return params.getRawParameterValue (id); };
    pr.inGain = g ("inGain"); pr.outLevel = g ("outLevel"); pr.monoOut = g ("monoOut");
    pr.pedalIn = g ("pedalIn"); pr.pedalOut = g ("pedalOut"); pr.pedalBypass = g ("pedalBypass");
    pr.ampDrive = g ("ampDrive"); pr.ampTight = g ("ampTight"); pr.ampPreMid = g ("ampPreMid");
    pr.ampLow = g ("ampLow"); pr.ampMid = g ("ampMid"); pr.ampHigh = g ("ampHigh"); pr.ampOut = g ("ampOut");
    pr.ampBypass = g ("ampBypass"); pr.ampNormalize = g ("ampNormalize");
    pr.cabLowCut = g ("cabLowCut"); pr.cabHighCut = g ("cabHighCut"); pr.cabBypass = g ("cabBypass");
    for (int k = 0; k < numIrs; ++k)
    {
        const auto n = "ir" + juce::String (k + 1);
        pr.irLevel[k] = g (n + "Level"); pr.irPan[k] = g (n + "Pan"); pr.irPolarity[k] = g (n + "Polarity");
        pr.irAlign[k] = g (n + "Align"); pr.irMute[k] = g (n + "Mute");
    }
    startTimerHz (10);
}

NAMHomeProcessor::~NAMHomeProcessor() { stopTimer(); }

juce::AudioProcessorValueTreeState::ParameterLayout NAMHomeProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto db = [&] (const char* id, const char* name, float lo, float hi, float def)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.1f), def, AudioParameterFloatAttributes().withLabel ("dB"))); };
    auto hz = [&] (const char* id, const char* name, float lo, float hi, float def)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 1.0f, 0.3f), def, AudioParameterFloatAttributes().withLabel ("Hz"))); };
    auto onOff = [&] (const char* id, const char* name, bool def)
    { v.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    db ("inGain", "Input", -24, 24, 0);
    db ("outLevel", "Output", -40, 12, 0);
    onOff ("monoOut", "Mono output", false);

    db ("pedalIn", "Pedal input", -24, 24, 0);
    db ("pedalOut", "Pedal output", -24, 24, 0);
    onOff ("pedalBypass", "Pedal bypass", false);

    db ("ampDrive", "Input drive", -24, 24, 0);
    hz ("ampTight", "Tight (pre low cut)", 20, 400, 20);
    db ("ampPreMid", "Pre mid", -12, 12, 0);
    db ("ampLow", "Post low", -12, 12, 0);
    db ("ampMid", "Post mid", -12, 12, 0);
    db ("ampHigh", "Post high", -12, 12, 0);
    db ("ampOut", "Amp output", -40, 12, 0);
    onOff ("ampBypass", "Amp bypass", false);
    onOff ("ampNormalize", "Normalize loudness", true);

    for (int i = 1; i <= numIrs; ++i)
    {
        const auto n = String (i);
        v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ir" + n + "Level", 1 }, "IR " + n + " level", NormalisableRange<float> (-40, 6, 0.1f), i == 1 ? 0.0f : -6.0f, AudioParameterFloatAttributes().withLabel ("dB")));
        v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ir" + n + "Pan", 1 }, "IR " + n + " pan", NormalisableRange<float> (-1, 1, 0.01f), 0.0f));
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "ir" + n + "Polarity", 1 }, "IR " + n + " polarity invert", false));
        v.push_back (std::make_unique<AudioParameterInt> (ParameterID { "ir" + n + "Align", 1 }, "IR " + n + " align (samples)", 0, 200, 0));
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "ir" + n + "Mute", 1 }, "IR " + n + " mute", false));
    }
    hz ("cabLowCut", "Cab low cut", 20, 400, 20);
    hz ("cabHighCut", "Cab high cut", 2000, 20000, 20000);
    onOff ("cabBypass", "Cab bypass", false);

    return { v.begin(), v.end() };
}

bool NAMHomeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto in = l.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono() || in.isDisabled();
}

void NAMHomeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    currentBlock = juce::jmax (32, samplesPerBlock);
    mono.setSize (1, currentBlock);
    irOut.setSize (1, currentBlock);
    stereo.setSize (2, currentBlock);

    amp.prepare (sampleRate, currentBlock);
    pedal.prepare (sampleRate, currentBlock);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) currentBlock, 1 };
    for (auto& ir : irs) { ir.conv.prepare (spec); ir.conv.reset(); }

    for (auto* b : { &tight, &preMid, &postLow, &postMid, &postHigh, &cabLowCut[0], &cabLowCut[1], &cabHighCut[0], &cabHighCut[1] })
        b->reset();
    for (auto* s : { &sIn, &sPedalIn, &sPedalOut, &sDrive, &sAmpOut, &sOut, &sIr[0], &sIr[1], &sIr[2] })
        s->reset (sampleRate, 0.03);

    loadMeasurer.reset (sampleRate, currentBlock);
}

void NAMHomeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());

    const int n = buffer.getNumSamples();
    if (n > mono.getNumSamples()) { buffer.clear(); return; } // never allocate here
    const int numIn = getTotalNumInputChannels();
    const double sr = currentRate;
    auto dbToGain = [] (float d) { return juce::Decibels::decibelsToGain (d, -100.0f); };

    // ---- Input (mono: a guitar is mono)
    float* m = mono.getWritePointer (0);
    if (numIn > 0) juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
    else juce::FloatVectorOperations::clear (m, n);

    sIn.setTargetValue (dbToGain (pr.inGain->load()));
    float inPk = 0;
    for (int i = 0; i < n; ++i) { m[i] *= sIn.getNextValue(); inPk = juce::jmax (inPk, std::abs (m[i])); }
    inputPeak = juce::jmax (inPk, inputPeak.load() * 0.9f);

    // ---- NAM pedal
    if (pr.pedalBypass->load() < 0.5f && pedal.isLoaded())
    {
        sPedalIn.setTargetValue (dbToGain (pr.pedalIn->load()));
        sPedalOut.setTargetValue (dbToGain (pr.pedalOut->load()));
        for (int i = 0; i < n; ++i) m[i] *= sPedalIn.getNextValue();
        pedal.process (m, n);
        for (int i = 0; i < n; ++i) m[i] *= sPedalOut.getNextValue();
    }

    // ---- NAM amp: added shaping around the capture (the capture itself is untouched)
    if (pr.ampBypass->load() < 0.5f && amp.isLoaded())
    {
        amp.normalize = pr.ampNormalize->load() > 0.5f;
        tight.set (Biquad::Type::HighPass, sr, pr.ampTight->load(), 0.707, 0);
        preMid.set (Biquad::Type::Peak, sr, 800.0, 0.8, pr.ampPreMid->load());
        postLow.set (Biquad::Type::LowShelf, sr, 120.0, 0.707, pr.ampLow->load());
        postMid.set (Biquad::Type::Peak, sr, 700.0, 0.7, pr.ampMid->load());
        postHigh.set (Biquad::Type::HighShelf, sr, 4000.0, 0.707, pr.ampHigh->load());
        sDrive.setTargetValue (dbToGain (pr.ampDrive->load()));
        sAmpOut.setTargetValue (dbToGain (pr.ampOut->load()));

        const bool useTight = pr.ampTight->load() > 21.0f;
        for (int i = 0; i < n; ++i)
        {
            float x = m[i];
            if (useTight) x = tight.process (x);
            m[i] = preMid.process (x) * sDrive.getNextValue();
        }
        amp.process (m, n);
        for (int i = 0; i < n; ++i)
            m[i] = postHigh.process (postMid.process (postLow.process (m[i]))) * sAmpOut.getNextValue();
    }

    // ---- Cab: blend up to three IRs (stereo from here on)
    float* L = stereo.getWritePointer (0);
    float* R = stereo.getWritePointer (1);
    bool anyIr = false;
    if (pr.cabBypass->load() < 0.5f)
    {
        juce::FloatVectorOperations::clear (L, n);
        juce::FloatVectorOperations::clear (R, n);
        for (int k = 0; k < numIrs; ++k)
        {
            auto& ir = irs[(size_t) k];
            if (! ir.loaded.load() || pr.irMute[k]->load() > 0.5f) continue;
            anyIr = true;

            float* o = irOut.getWritePointer (0);
            juce::FloatVectorOperations::copy (o, m, n);
            juce::dsp::AudioBlock<float> block (irOut.getArrayOfWritePointers(), 1, (size_t) n);
            ir.conv.process (juce::dsp::ProcessContextReplacing<float> (block));

            const float pol = pr.irPolarity[k]->load() > 0.5f ? -1.0f : 1.0f;
            const float pan = pr.irPan[k]->load();
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f; // constant-power pan
            const float gl = std::cos (angle) * juce::MathConstants<float>::sqrt2;
            const float gr = std::sin (angle) * juce::MathConstants<float>::sqrt2;
            sIr[k].setTargetValue (dbToGain (pr.irLevel[k]->load()) * pol);
            for (int i = 0; i < n; ++i)
            {
                const float s = o[i] * sIr[k].getNextValue();
                L[i] += s * gl;
                R[i] += s * gr;
            }
        }
        if (anyIr)
        {
            const float lc = pr.cabLowCut->load(), hc = pr.cabHighCut->load();
            for (int c = 0; c < 2; ++c)
            {
                cabLowCut[c].set (Biquad::Type::HighPass, sr, lc, 0.707, 0);
                cabHighCut[c].set (Biquad::Type::LowPass, sr, hc, 0.707, 0);
                float* d = stereo.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                {
                    float x = d[i];
                    if (lc > 21.0f) x = cabLowCut[c].process (x);
                    if (hc < 19900.0f) x = cabHighCut[c].process (x);
                    d[i] = x;
                }
            }
        }
    }
    if (! anyIr)
    {
        juce::FloatVectorOperations::copy (L, m, n);
        juce::FloatVectorOperations::copy (R, m, n);
    }

    // ---- Output
    sOut.setTargetValue (dbToGain (pr.outLevel->load()));
    const bool monoOut = pr.monoOut->load() > 0.5f;
    float pkL = 0, pkR = 0;
    for (int i = 0; i < n; ++i)
    {
        const float g = sOut.getNextValue();
        float l = L[i] * g, r = R[i] * g;
        if (monoOut) l = r = 0.5f * (l + r);
        L[i] = l; R[i] = r;
        pkL = juce::jmax (pkL, std::abs (l));
        pkR = juce::jmax (pkR, std::abs (r));
    }
    outputPeakL = juce::jmax (pkL, outputPeakL.load() * 0.9f);
    outputPeakR = juce::jmax (pkR, outputPeakR.load() * 0.9f);

    const int numOut = getTotalNumOutputChannels();
    if (numOut == 1)
    {
        auto* d = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) d[i] = 0.5f * (L[i] + R[i]);
    }
    else
    {
        buffer.copyFrom (0, 0, stereo, 0, 0, n);
        buffer.copyFrom (1, 0, stereo, 1, 0, n);
        for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    }
}

//==============================================================================
juce::String NAMHomeProcessor::loadAmp (const juce::File& f)   { return amp.load (f, currentRate, currentBlock); }
juce::String NAMHomeProcessor::loadPedal (const juce::File& f) { return pedal.load (f, currentRate, currentBlock); }

juce::String NAMHomeProcessor::loadIr (int index, const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return "Couldn't read this IR file.";
    if ((double) r->lengthInSamples / r->sampleRate > Library::maxIrSeconds) return "This .wav is too long to be a cabinet IR.";

    auto& ir = irs[(size_t) index];
    ir.original.setSize (1, (int) r->lengthInSamples);
    ir.original.clear();
    // stereo IR files are folded to mono; each IR gets its own pan in the blend
    juce::AudioBuffer<float> tmp ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&tmp, 0, (int) r->lengthInSamples, 0, true, true);
    for (int c = 0; c < tmp.getNumChannels(); ++c)
        ir.original.addFrom (0, 0, tmp, c, 0, tmp.getNumSamples(), 1.0f / (float) tmp.getNumChannels());
    ir.originalRate = r->sampleRate;
    ir.path = f.getFullPathName();
    ir.appliedAlign = -1;
    rebuildIr (index);
    ir.loaded = true;
    return {};
}

void NAMHomeProcessor::clearIr (int index)
{
    auto& ir = irs[(size_t) index];
    ir.loaded = false;
    ir.path = {};
    ir.original.setSize (0, 0);
}

void NAMHomeProcessor::rebuildIr (int index)
{
    auto& ir = irs[(size_t) index];
    if (ir.original.getNumSamples() == 0) return;
    const int align = (int) pr.irAlign[index]->load();
    // alignment = delay this IR by N samples (at the IR's own sample rate)
    juce::AudioBuffer<float> b (1, ir.original.getNumSamples() + align);
    b.clear();
    b.copyFrom (0, align, ir.original, 0, 0, ir.original.getNumSamples());
    ir.conv.loadImpulseResponse (std::move (b), ir.originalRate,
                                 juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::no,
                                 juce::dsp::Convolution::Normalise::yes);
    ir.appliedAlign = align;
}

void NAMHomeProcessor::timerCallback()
{
    for (int k = 0; k < numIrs; ++k)
    {
        auto& ir = irs[(size_t) k];
        const int align = (int) pr.irAlign[k]->load();
        if (ir.loaded.load() && align != ir.appliedAlign)
            rebuildIr (k);
    }
}

//==============================================================================
void NAMHomeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("ampPath", amp.getPath(), nullptr);
    state.setProperty ("pedalPath", pedal.getPath(), nullptr);
    for (int k = 0; k < numIrs; ++k)
        state.setProperty ("irPath" + juce::String (k + 1), irs[(size_t) k].path, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void NAMHomeProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (params.state.getType())) return;
    params.replaceState (state);

    auto tryLoad = [] (const juce::String& path, auto&& loader)
    {
        juce::File f (path);
        if (path.isNotEmpty() && f.existsAsFile()) loader (f);
    };
    tryLoad (state.getProperty ("ampPath").toString(), [this] (const juce::File& f) { loadAmp (f); });
    tryLoad (state.getProperty ("pedalPath").toString(), [this] (const juce::File& f) { loadPedal (f); });
    for (int k = 0; k < numIrs; ++k)
        tryLoad (state.getProperty ("irPath" + juce::String (k + 1)).toString(), [this, k] (const juce::File& f) { loadIr (k, f); });
}

juce::AudioProcessorEditor* NAMHomeProcessor::createEditor() { return new NAMHomeEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NAMHomeProcessor(); }
