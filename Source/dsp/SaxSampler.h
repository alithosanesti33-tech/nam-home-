#pragma once
// Recorded saxophone: plays real alto and soprano recordings (University of Iowa Musical
// Instrument Samples, free for any use), one recording per note at pp, mf and ff.
// Your guitar picks the note and how hard; the recording is re-pitched by the small amount
// needed to follow your exact pitch, bends and vibrato. Held notes loop smoothly; legato note
// changes cross-fade from sustain to sustain; picked notes play the real attack.
#include <JuceHeader.h>
#include "Common.h"

namespace ndsp
{
struct SaxSample
{
    std::vector<float> data;
    double rate = 44100.0;
    float root = 60.0f;        // measured pitch (fractional MIDI)
    int onset = 0;             // first audible sample
    int sustain = 0;           // where the attack has settled
    int loopStart = 0, loopEnd = 0;
    float gain = 1.0f;         // evens out the steady level
};

class SaxSampleBank : private juce::Thread
{
public:
    static SaxSampleBank& get() { static SaxSampleBank b; return b; }

    // Where the recordings live: <NAM Home folder>/Instruments/Sax/<alto|soprano>/<pp|mf|ff>/<midi>.wav
    void setRoot (const juce::File& namHomeFolder)
    {
        const juce::ScopedLock sl (lock);
        if (root == namHomeFolder) return;
        root = namHomeFolder;
        if (wanted && ! isThreadRunning() && ! ready.load()) startThread (juce::Thread::Priority::low);
    }
    // loaded only once a sax is actually used (they take ~150 MB of memory)
    void requestLoad() { const juce::ScopedLock sl (lock); wanted = true; if (root != juce::File() && ! ready.load() && ! isThreadRunning()) startThread (juce::Thread::Priority::low); }

    bool isReady() const { return ready.load(); }
    bool has (int horn) const { return ready.load() && count[horn] > 0; }

    // horn: 0 alto, 1 soprano. layer 0 pp, 1 mf, 2 ff. Nearest recording to 'midi' in that layer.
    const SaxSample* nearest (int horn, int layer, float midi) const
    {
        const int m = juce::jlimit (0, 127, (int) std::lround (midi));
        return map[horn][layer][m];
    }

    juce::String status() const { return statusText; }

private:
    SaxSampleBank() : juce::Thread ("Sax recordings") {}
    ~SaxSampleBank() override { stopThread (3000); }

    void run() override
    {
        juce::File base;
        { const juce::ScopedLock sl (lock); base = root.getChildFile ("Instruments").getChildFile ("Sax"); }
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        static const char* horns[2] = { "alto", "soprano" };
        static const char* layers[3] = { "pp", "mf", "ff" };
        for (int h = 0; h < 2; ++h)
            for (int l = 0; l < 3; ++l)
            {
                auto dir = base.getChildFile (horns[h]).getChildFile (layers[l]);
                for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*.wav"))
                {
                    if (threadShouldExit()) return;
                    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (e.getFile()));
                    if (r == nullptr || r->lengthInSamples < 4096) continue;
                    auto s = std::make_unique<SaxSample>();
                    s->rate = r->sampleRate;
                    juce::AudioBuffer<float> buf ((int) r->numChannels, (int) r->lengthInSamples);
                    r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true);
                    s->data.resize ((size_t) buf.getNumSamples());
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                    {
                        float v = 0; for (int c = 0; c < buf.getNumChannels(); ++c) v += buf.getSample (c, i);
                        s->data[(size_t) i] = v / (float) buf.getNumChannels();
                    }
                    const int fileMidi = e.getFile().getFileNameWithoutExtension().getIntValue();
                    if (! analyse (*s, (float) fileMidi)) continue;
                    // the slicer can mislabel a note; trust only recordings within the horn's range
                    if (std::abs (s->root - (float) fileMidi) > 0.6f) continue;
                    pool.push_back (std::move (s));
                    slots[h][l][juce::jlimit (0, 127, fileMidi)] = pool.back().get();
                }
            }
        // every MIDI note points at the nearest recording in its layer (falling back to another layer)
        for (int h = 0; h < 2; ++h)
        {
            count[h] = 0;
            for (int l = 0; l < 3; ++l)
                for (int m = 0; m < 128; ++m)
                {
                    const SaxSample* best = nullptr; int bestD = 1000;
                    for (int ll : { l, 1, 2, 0 })
                    {
                        for (int k = 0; k < 128; ++k)
                            if (slots[h][ll][k] != nullptr && std::abs (k - m) < bestD) { bestD = std::abs (k - m); best = slots[h][ll][k]; }
                        if (best != nullptr && bestD <= 2) break;
                    }
                    map[h][l][m] = best;
                    if (best != nullptr && l == 1 && slots[h][l][m]) ++count[h];
                }
        }
        statusText = juce::String (count[0]) + " alto and " + juce::String (count[1]) + " soprano recordings";
        ready = count[0] > 0 || count[1] > 0;
    }

    // measure pitch, onset, a settled sustain point, and a seamless loop in the steady part
    static bool analyse (SaxSample& s, float guessMidi)
    {
        const auto& x = s.data;
        const int n = (int) x.size();
        float peak = 0; for (float v : x) peak = std::max (peak, std::abs (v));
        if (peak < 1.0e-4f) return false;
        const int hop = (int) (s.rate * 0.005);
        std::vector<float> env;
        for (int i = 0; i + hop <= n; i += hop)
        {
            double e = 0; for (int k = 0; k < hop; ++k) e += x[(size_t) (i + k)] * x[(size_t) (i + k)];
            env.push_back ((float) std::sqrt (e / hop));
        }
        float ePeak = 0; for (float v : env) ePeak = std::max (ePeak, v);
        int on = 0; while (on < (int) env.size() && env[(size_t) on] < ePeak * 0.03f) ++on;
        int off = (int) env.size() - 1; while (off > on && env[(size_t) off] < ePeak * 0.1f) --off;
        s.onset = std::max (0, on * hop - (int) (0.002 * s.rate));
        const int start = on * hop, end = off * hop;
        if (end - start < (int) (0.5 * s.rate)) return false;
        s.sustain = start + (int) (0.12 * s.rate);

        // pitch by normalised autocorrelation around the expected note, in the middle of the note
        const double f0 = 440.0 * std::pow (2.0, (guessMidi - 69.0) / 12.0);
        const int mid = (start + end) / 2, W = (int) std::min (4096.0, (end - start) * 0.4);
        auto corr = [&] (int L)
        {
            double a = 0, e1 = 0, e2 = 0;
            for (int i = mid - W / 2; i < mid + W / 2; ++i) { const double u = x[(size_t) i], v = x[(size_t) (i + L)]; a += u * v; e1 += u * u; e2 += v * v; }
            return a / std::sqrt (e1 * e2 + 1.0e-12);
        };
        const int lo = (int) (s.rate / (f0 * 1.06)), hi = (int) (s.rate / (f0 * 0.94)) + 1;
        int best = lo; double bc = -2;
        for (int L = lo; L <= hi; ++L) { const double c = corr (L); if (c > bc) { bc = c; best = L; } }
        const double ym = corr (best - 1), y0 = bc, yp = corr (best + 1);
        const double d = 0.5 * (ym - yp) / (ym - 2 * y0 + yp + 1.0e-12);
        const double period = best + juce::jlimit (-0.5, 0.5, d);
        s.root = (float) (69.0 + 12.0 * std::log2 (s.rate / period / 440.0));

        // loop: from 35% to 85% of the steady part, end moved to the best period match
        int ls = start + (int) ((end - start) * 0.35), le = start + (int) ((end - start) * 0.85);
        const int P = (int) std::round (period);
        int bestEnd = le; double bestC = -2;
        for (int k = -3 * P; k <= 3 * P; ++k)
        {
            const int e = le + k;
            if (e + 64 >= n) break;
            double a = 0, e1 = 0, e2 = 0;
            for (int i = 0; i < 2 * P; ++i) { const double u = x[(size_t) (ls + i)], v = x[(size_t) (e + i)]; a += u * v; e1 += u * u; e2 += v * v; }
            const double c = a / std::sqrt (e1 * e2 + 1.0e-12);
            if (c > bestC) { bestC = c; bestEnd = e; }
        }
        s.loopStart = ls; s.loopEnd = bestEnd;
        if (s.loopEnd - s.loopStart < (int) (0.25 * s.rate)) return false;

        double r = 0; for (int i = ls; i < s.loopEnd; ++i) r += x[(size_t) i] * x[(size_t) i];
        r = std::sqrt (r / (s.loopEnd - ls));
        s.gain = (float) (0.25 / std::max (1.0e-4, r));
        return true;
    }

    juce::CriticalSection lock;
    juce::File root;
    bool wanted = false;
    std::atomic<bool> ready { false };
    std::vector<std::unique_ptr<SaxSample>> pool;
    const SaxSample* slots[2][3][128] {};
    const SaxSample* map[2][3][128] {};
    int count[2] { 0, 0 };
    juce::String statusText { "loading" };
};

// One playing recording (with its own position, loop and fade)
struct SaxPlayer
{
    const SaxSample* s = nullptr;
    double pos = 0;
    float gain = 0, target = 0, step = 0;
    bool active() const { return s != nullptr && (gain > 0.0f || target > 0.0f); }

    void start (const SaxSample* smp, bool fromAttack, float fadeSamples)
    {
        s = smp;
        pos = fromAttack ? smp->onset : smp->sustain;
        gain = 0; target = 1; step = 1.0f / std::max (1.0f, fadeSamples);
    }
    void stop (float fadeSamples) { target = 0; step = 1.0f / std::max (1.0f, fadeSamples); }

    inline float tick (double rate)
    {
        if (s == nullptr) return 0.0f;
        if (gain < target) gain = std::min (target, gain + step); else if (gain > target) gain = std::max (target, gain - step);
        if (gain <= 0.0f && target <= 0.0f) { s = nullptr; return 0.0f; }
        const auto& d = s->data;
        auto at = [&d] (double p)
        {
            const int i = (int) p; const float f = (float) (p - i);
            const float a = d[(size_t) i], b = d[(size_t) std::min ((int) d.size() - 1, i + 1)];
            return a + (b - a) * f;
        };
        float y = at (pos);
        // seamless loop: in the last 60 ms before the loop end, cross-fade into the loop start
        const double xf = 0.06 * s->rate;
        const double intoXf = pos - (s->loopEnd - xf);
        if (intoXf > 0)
        {
            const float t = (float) (intoXf / xf);
            y = y * std::sqrt (1.0f - t) + at (s->loopStart - xf + intoXf) * std::sqrt (t);
        }
        pos += rate;
        if (pos >= s->loopEnd) pos = s->loopStart + (pos - s->loopEnd);
        return y * s->gain * gain;
    }
};

class SaxSampler
{
public:
    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() { for (auto& n : notes) for (auto& p : n) p = {}; cur = 0; tone.reset(); sub.reset(); }

    // horn: 0 alto, 1 soprano; shiftSemis lets tenor/bari borrow the alto recordings
    void setHorn (int h, float shiftSemis) { horn = h; shift = shiftSemis; }

    // kind: 0 attack (play the real attack), 1 legato (sustain to sustain), 2 tongued re-pick
    void noteStart (float midi, int kind)
    {
        auto& bank = SaxSampleBank::get();
        const float src = midi + shift;
        const float fade = kind == 0 ? 0.004f : (kind == 1 ? 0.030f : 0.010f);
        // old note fades out, a new pair of layers starts
        for (auto& p : notes[cur]) p.stop (fade * sr);
        cur ^= 1;
        for (int l = 0; l < 3; ++l)
            if (auto* s = bank.nearest (horn, l, src)) notes[cur][l].start (s, kind != 1, fade * sr);
    }
    void noteStop (float releaseSec) { for (auto& n : notes) for (auto& p : n) p.stop (releaseSec * sr); }
    bool sounding() const { for (auto& n : notes) for (auto& p : n) if (p.active()) return true; return false; }

    // breath 0..1 blends pp -> mf -> ff and sets the level
    inline float tick (float midi, float breath)
    {
        const float b = clamp (breath, 0.0f, 1.0f);
        float w[3];
        if (b < 0.5f) { const float t = b * 2.0f; w[0] = 1.0f - t; w[1] = t; w[2] = 0.0f; }
        else          { const float t = (b - 0.5f) * 2.0f; w[0] = 0.0f; w[1] = 1.0f - t; w[2] = t; }
        const float level = 0.18f + 0.82f * std::pow (b, 0.7f);
        float y = 0.0f;
        for (int n = 0; n < 2; ++n)
            for (int l = 0; l < 3; ++l)
            {
                auto& p = notes[n][l];
                if (p.s == nullptr) continue;
                const double rate = (p.s->rate / sr) * std::exp2 ((midi - p.s->root) / 12.0f);
                const float v = p.tick (rate);
                y += v * w[l];
            }
        return y * level;
    }

private:
    float sr = 48000.0f, shift = 0;
    int horn = 0, cur = 0;
    SaxPlayer notes[2][3];
    Biquad tone, sub;
};
} // namespace ndsp
