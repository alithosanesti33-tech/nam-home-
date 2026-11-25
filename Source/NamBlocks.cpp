#include "BlockTypes.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#include "json.hpp"

using namespace dsp;

static void logLine (const juce::String& s)
{
    auto f = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/NAM Home/log.txt");
    f.getParentDirectory().createDirectory();
    f.appendText (juce::Time::getCurrentTime().toString (true, true) + "  " + s + "\n");
}

//==============================================================================
NamBlock::NamBlock (bool isAmp) : Block (isAmp ? Type::NamAmp : Type::NamPedal), amp (isAmp)
{
    auto shaping = [] (Param& p) { p.realControl = false; return &p; };
    if (amp)
    {
        shaping (addFloat ("drive", "Input drive", -24, 24, 0, "dB", -1, 0.1f));
        shaping (addFloat ("tight", "Tight (pre)", 20, 400, 20, "Hz", 100, 1));
        shaping (addFloat ("preMid", "Mid (pre)", -12, 12, 0, "dB", -1, 0.1f));
        shaping (addFloat ("low", "Low (post)", -12, 12, 0, "dB", -1, 0.1f));
        shaping (addFloat ("mid", "Mid (post)", -12, 12, 0, "dB", -1, 0.1f));
        shaping (addFloat ("high", "High (post)", -12, 12, 0, "dB", -1, 0.1f));
        shaping (addFloat ("out", "Output", -40, 12, 0, "dB", -1, 0.1f));
    }
    else
    {
        shaping (addFloat ("drive", "Input", -24, 24, 0, "dB", -1, 0.1f));
        shaping (addFloat ("out", "Output", -40, 12, 0, "dB", -1, 0.1f));
    }
    shaping (addToggle ("normalize", "Even out loudness", true));
}

NamBlock::~NamBlock() = default;

juce::String NamBlock::getTitle() const
{
    return loaded.load() ? captureName : (amp ? "Amp" : "NAM pedal");
}

juce::String NamBlock::getSubtitle() const
{
    if (! loaded.load()) return "Drop a capture";
    return includesCab() ? juce::String ("Includes cab") : (amp ? juce::String ("NAM amp") : juce::String ("NAM pedal"));
}

juce::var NamBlock::saveExtra() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("path", path);
    return juce::var (o);
}

void NamBlock::loadExtra (const juce::var& v)
{
    const juce::File f (v.getProperty ("path", {}).toString());
    if (f.existsAsFile()) load (f);
}

juce::String NamBlock::load (const juce::File& f)
{
    std::unique_ptr<nam::DSP> next;
    nam::dspData data;
    juce::String firstError;
    try
    {
        next = nam::get_dsp (std::filesystem::path (f.getFullPathName().toStdString()), data);
    }
    catch (const std::exception& e) { firstError = e.what(); }
    catch (...) { firstError = "unknown error"; }

    if (next == nullptr)
    {
        // second route: read the file ourselves and hand NAM the parsed JSON
        try
        {
            const auto text = f.loadFileAsString();
            auto j = nlohmann::json::parse (text.toStdString());
            next = nam::get_dsp (j, data);
        }
        catch (const std::exception& e) { logLine ("Load failed (both routes): " + f.getFullPathName() + " | " + firstError + " | " + e.what()); }
        catch (...) {}
    }
    if (next == nullptr)
    {
        logLine ("Load failed: " + f.getFullPathName() + " | " + firstError);
        return "Couldn't load " + f.getFileName() + ".\n\nNAM said: " + firstError
             + "\n\nIf this is an adjustable (parametric) model made for a different player, NAM Home can't read that format yet.";
    }

    if (auto* s = dynamic_cast<nam::SlimmableModel*> (next.get()))
        s->SetSlimmableSize (1.0);

    const double exp = next->GetExpectedSampleRate();
    const double rate = exp > 0 ? exp : 48000.0;
    const bool mm = ! next->SupportsArbitrarySampleRate() && std::abs (rate - sampleRate) > 1.0;
    next->Reset (sampleRate, maxBlock);
    const float nDb = next->HasLoudness() ? (float) (-18.0 - next->GetLoudness()) : 0.0f;

    auto meta = [&data] (const char* k) -> juce::String
    {
        if (data.metadata.is_object() && data.metadata.contains (k) && data.metadata[k].is_string())
            return juce::String (data.metadata[k].get<std::string>()).trim();
        return {};
    };

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (dsp, next);
        normGainDb = nDb;
        expectedRate = rate;
        mismatch = mm;
        path = f.getFullPathName();
        captureName = meta ("name").isNotEmpty() ? meta ("name") : f.getFileNameWithoutExtension();
        creator = meta ("modeled_by");
        gearMake = meta ("gear_make");
        gearModel = meta ("gear_model");
        gearType = meta ("gear_type");
        toneType = meta ("tone_type");
        loaded = true;
    }
    return {};
}

void NamBlock::unload()
{
    std::unique_ptr<nam::DSP> old;
    const juce::SpinLock::ScopedLockType sl (lock);
    std::swap (dsp, old);
    loaded = false;
    path = {}; captureName = {}; creator = {}; gearType = {}; toneType = {};
}

void NamBlock::prepare (double sr, int maxBlockSize)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    mono.assign ((size_t) maxBlockSize, 0.0f);
    out.assign ((size_t) maxBlockSize, 0.0f);
    sIn.reset ((float) sr); sOut.reset ((float) sr);
    if (dsp != nullptr)
    {
        dsp->Reset (sr, maxBlockSize);
        mismatch = ! dsp->SupportsArbitrarySampleRate() && std::abs (expectedRate - sr) > 1.0;
    }
}

void NamBlock::process (float* L, float* R, int n)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || dsp == nullptr) return;

    const int outIdx = amp ? 6 : 1;
    const bool normalize = params.back()->on();
    sIn.setTarget (dbToGain (p (0)));
    sOut.setTarget (dbToGain (p (outIdx)) * (normalize ? dbToGain (normGainDb) : 1.0f));

    if (amp)
    {
        tight.set (Biquad::Type::HighPass, sampleRate, p (1), 0.707f);
        preMid.set (Biquad::Type::Peak, sampleRate, 800.0f, 0.8f, p (2));
        postLow.set (Biquad::Type::LowShelf, sampleRate, 120.0f, 0.707f, p (3));
        postMid.set (Biquad::Type::Peak, sampleRate, 700.0f, 0.7f, p (4));
        postHigh.set (Biquad::Type::HighShelf, sampleRate, 4000.0f, 0.707f, p (5));
    }
    const bool useTight = amp && p (1) > 21.0f;

    for (int i = 0; i < n; ++i)
    {
        float x = 0.5f * (L[i] + R[i]);   // a capture is mono
        if (useTight) x = tight.process (x);
        if (amp) x = preMid.process (x);
        mono[(size_t) i] = x * sIn.next();
    }
    float* in = mono.data();
    float* o = out.data();
    dsp->process (&in, &o, n);
    for (int i = 0; i < n; ++i)
    {
        float y = o[(size_t) i];
        if (amp) y = postHigh.process (postMid.process (postLow.process (y)));
        y *= sOut.next();
        L[i] = R[i] = y;
    }
}

//==============================================================================
CabBlock::CabBlock() : Block (Type::Cab)
{
    for (int k = 1; k <= numIrs; ++k)
    {
        const auto s = juce::String (k);
        addFloat ("level" + s, "IR " + s + " level", -40, 6, k == 1 ? 0.0f : -6.0f, "dB", -1, 0.1f);
        addFloat ("pan" + s, "IR " + s + " pan", -1, 1, 0, "pan", -1, 0.01f);
        addToggle ("polarity" + s, "IR " + s + " polarity", false);
        addFloat ("align" + s, "IR " + s + " align", 0, 200, 0, "smp", -1, 1);
        addToggle ("mute" + s, "IR " + s + " mute", false);
    }
    addFloat ("lowCut", "Low cut", 20, 400, 20, "Hz", 100, 1);
    addFloat ("highCut", "High cut", 2000, 20000, 20000, "Hz", 8000, 1);
}

int CabBlock::numLoaded() const
{
    int c = 0;
    for (auto& ir : irs) if (ir.loaded.load()) ++c;
    return c;
}

juce::String CabBlock::getSubtitle() const
{
    const int c = numLoaded();
    return c == 0 ? juce::String ("Drop an IR") : c == 1 ? juce::String ("1 IR") : "IR blend x" + juce::String (c);
}

juce::var CabBlock::saveExtra() const
{
    auto* o = new juce::DynamicObject();
    for (int k = 0; k < numIrs; ++k) o->setProperty ("ir" + juce::String (k), irs[(size_t) k].path);
    return juce::var (o);
}

void CabBlock::loadExtra (const juce::var& v)
{
    for (int k = 0; k < numIrs; ++k)
    {
        const juce::File f (v.getProperty ("ir" + juce::String (k), {}).toString());
        if (f.existsAsFile()) loadIr (k, f);
    }
}

juce::String CabBlock::loadIr (int index, const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return "Couldn't read " + f.getFileName() + " as an IR.";
    if ((double) r->lengthInSamples / r->sampleRate > 2.0) return f.getFileName() + " is too long to be a cabinet IR.";

    auto& ir = irs[(size_t) index];
    juce::AudioBuffer<float> tmp ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&tmp, 0, (int) r->lengthInSamples, 0, true, true);
    ir.original.setSize (1, tmp.getNumSamples());
    ir.original.clear();
    for (int c = 0; c < tmp.getNumChannels(); ++c)
        ir.original.addFrom (0, 0, tmp, c, 0, tmp.getNumSamples(), 1.0f / (float) tmp.getNumChannels());
    ir.rate = r->sampleRate;
    ir.path = f.getFullPathName();
    ir.appliedAlign = -1;
    rebuild (index);
    ir.loaded = true;
    return {};
}

void CabBlock::clearIr (int index)
{
    auto& ir = irs[(size_t) index];
    ir.loaded = false;
    ir.path = {};
}

void CabBlock::rebuild (int index)
{
    auto& ir = irs[(size_t) index];
    if (ir.original.getNumSamples() == 0) return;
    const int align = (int) p (index * 5 + 3);
    juce::AudioBuffer<float> b (1, ir.original.getNumSamples() + align);
    b.clear();
    b.copyFrom (0, align, ir.original, 0, 0, ir.original.getNumSamples());
    ir.conv.loadImpulseResponse (std::move (b), ir.rate, juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
    ir.appliedAlign = align;
}

void CabBlock::updateAlignment()
{
    for (int k = 0; k < numIrs; ++k)
        if (irs[(size_t) k].loaded.load() && (int) p (k * 5 + 3) != irs[(size_t) k].appliedAlign)
            rebuild (k);
}

void CabBlock::prepare (double sr, int maxBlockSize)
{
    mono.setSize (1, maxBlockSize);
    work.setSize (1, maxBlockSize);
    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlockSize, 1 };
    for (auto& ir : irs) { ir.conv.prepare (spec); ir.conv.reset(); ir.gain.reset ((float) sr); }
    for (int c = 0; c < 2; ++c) { lowCut[c].reset(); highCut[c].reset(); }
}

void CabBlock::process (float* L, float* R, int n)
{
    if (numLoaded() == 0) return;   // empty cab passes the signal through
    float* m = mono.getWritePointer (0);
    for (int i = 0; i < n; ++i) m[i] = 0.5f * (L[i] + R[i]);
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);

    for (int k = 0; k < numIrs; ++k)
    {
        auto& ir = irs[(size_t) k];
        const int b = k * 5;
        if (! ir.loaded.load() || p (b + 4) > 0.5f) continue;
        float* o = work.getWritePointer (0);
        std::copy (m, m + n, o);
        juce::dsp::AudioBlock<float> blk (work.getArrayOfWritePointers(), 1, (size_t) n);
        ir.conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
        const float pol = p (b + 2) > 0.5f ? -1.0f : 1.0f;
        const float angle = (p (b + 1) + 1.0f) * pi * 0.25f;
        const float gl = std::cos (angle) * 1.41421356f, gr = std::sin (angle) * 1.41421356f;
        ir.gain.setTarget (dbToGain (p (b)) * pol);
        for (int i = 0; i < n; ++i)
        {
            const float s = o[i] * ir.gain.next();
            L[i] += s * gl;
            R[i] += s * gr;
        }
    }

    const float lc = p (numIrs * 5), hc = p (numIrs * 5 + 1);
    for (int c = 0; c < 2; ++c)
    {
        lowCut[c].set (Biquad::Type::HighPass, sampleRate, lc, 0.707f);
        highCut[c].set (Biquad::Type::LowPass, sampleRate, hc, 0.707f);
        float* d = c == 0 ? L : R;
        for (int i = 0; i < n; ++i)
        {
            float x = d[i];
            if (lc > 21.0f) x = lowCut[c].process (x);
            if (hc < 19900.0f) x = highCut[c].process (x);
            d[i] = x;
        }
    }
}
