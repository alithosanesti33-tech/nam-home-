#include "Measure.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#include "json.hpp"
#include <filesystem>
#include <map>

namespace measure
{
static std::unique_ptr<nam::DSP> loadNam (const juce::File& f)
{
    nam::dspData data;
    try { return nam::get_dsp (std::filesystem::path (f.getFullPathName().toStdString()), data); }
    catch (...) {}
    try { auto j = nlohmann::json::parse (f.loadFileAsString().toStdString()); return nam::get_dsp (j, data); }
    catch (...) {}
    return nullptr;
}

float namGain (const juce::File& f)
{
    auto dsp = loadNam (f);
    if (dsp == nullptr) return -1.0f;
    if (auto* s = dynamic_cast<nam::SlimmableModel*> (dsp.get())) s->SetSlimmableSize (1.0);
    const double sr = dsp->GetExpectedSampleRate() > 0 ? dsp->GetExpectedSampleRate() : 48000.0;
    const int block = 512, total = (int) (sr * 0.5);
    dsp->Reset (sr, block);
    // a 220 Hz tone at a typical guitar level (about -18 dBFS)
    std::vector<float> in ((size_t) block), out ((size_t) block), y;
    y.reserve ((size_t) total);
    double ph = 0;
    for (int done = 0; done < total; done += block)
    {
        for (int i = 0; i < block; ++i) { in[(size_t) i] = (float) (0.125 * std::sin (ph)); ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr; }
        NAM_SAMPLE* ip = in.data(); NAM_SAMPLE* op = out.data();
        dsp->process (&ip, &op, block);
        y.insert (y.end(), out.begin(), out.end());
    }
    // distortion = energy not at the 220 Hz fundamental, over the last 0.25 s
    const int n = (int) (sr * 0.25), a = (int) y.size() - n;
    double mean = 0; for (int i = a; i < a + n; ++i) mean += y[(size_t) i]; mean /= n;
    double tot = 0, re = 0, im = 0;
    for (int i = 0; i < n; ++i)
    {
        const double v = y[(size_t) (a + i)] - mean;
        tot += v * v;
        const double w = 2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr;
        re += v * std::cos (w); im += v * std::sin (w);
    }
    if (tot < 1.0e-12) return 0.0f;
    const double fund = 2.0 * (re * re + im * im) / n;   // energy of the fundamental
    const double thd = juce::jlimit (1.0e-4, 1.0, 1.0 - fund / tot);
    // 1% or less = clean, 60% or more = high gain, log scale in between
    return (float) juce::jlimit (0.0, 1.0, (std::log10 (thd) + 2.0) / (std::log10 (0.6) + 2.0));
}

float irBrightness (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples < 16) return -1.0f;
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 0.2));
    juce::AudioBuffer<float> b ((int) r->numChannels, n);
    r->read (&b, 0, n, 0, true, true);
    // energy above 2 kHz from a one-pole split (cheap and good enough to rank cabs)
    const double c = std::exp (-2.0 * juce::MathConstants<double>::pi * 2000.0 / r->sampleRate);
    double lp = 0, hiE = 0, all = 0;
    for (int i = 0; i < n; ++i)
    {
        const double x = b.getSample (0, i);
        lp = (1 - c) * x + c * lp;
        const double hi = x - lp;
        hiE += hi * hi; all += x * x;
    }
    if (all <= 0) return -1.0f;
    const double ratio = juce::jlimit (1.0e-3, 1.0, hiE / all);
    return (float) juce::jlimit (0.0, 1.0, (std::log10 (ratio) + 2.0) / 1.7);
}

static juce::CriticalSection cacheLock;
static std::map<juce::String, float>& cache() { static std::map<juce::String, float> m; return m; }
void remember (const juce::String& path, float v) { if (v < 0) return; const juce::ScopedLock sl (cacheLock); cache()[path] = v; }
float recall (const juce::String& path) { const juce::ScopedLock sl (cacheLock); auto it = cache().find (path); return it == cache().end() ? -1.0f : it->second; }

int gainStage (float g) { return g < 0 ? -1 : g < 0.2f ? 0 : g < 0.4f ? 1 : g < 0.6f ? 2 : g < 0.8f ? 3 : 4; }

juce::String gainStageName (int s)
{
    static const char* n[5] = { "Clean", "Edge of breakup", "Crunch", "Rock gain", "High gain" };
    return s >= 0 && s < 5 ? n[s] : "Unmeasured";
}

juce::Colour gainColour (float g)
{
    if (g < 0) return juce::Colour (0xff00f0d0);
    static const juce::Colour stops[5] = { juce::Colour (0xff2f7bff), juce::Colour (0xff2bff6a), juce::Colour (0xffffe600), juce::Colour (0xffff8a00), juce::Colour (0xffff2a2a) };
    const float x = juce::jlimit (0.0f, 1.0f, g) * 4.0f;
    const int i = juce::jmin (3, (int) x);
    return stops[i].interpolatedWith (stops[i + 1], x - (float) i);
}

juce::Colour irColour (float b)
{
    if (b < 0) return juce::Colour (0xffa855ff);
    return juce::Colour (0xff6a1bff).interpolatedWith (juce::Colour (0xffd9b8ff), juce::jlimit (0.0f, 1.0f, b));
}
}
