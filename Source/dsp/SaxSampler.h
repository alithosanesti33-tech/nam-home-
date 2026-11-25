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

    juce::String status() const { const juce::ScopedLock sl (statusLock); return statusText; }

    // if loading failed (no drive, no internet), try again now and then while a sax is in use
    void retryIfMissing()
    {
        if (ready.load() || isThreadRunning()) return;
        const juce::ScopedLock sl (lock);
        if (! wanted || root == juce::File()) return;
        if (juce::Time::getMillisecondCounter() - lastAttempt.load() < 15000u) return;
        startThread (juce::Thread::Priority::low);
    }

private:
    SaxSampleBank() : juce::Thread ("Sax recordings") {}
    ~SaxSampleBank() override { stopThread (3000); }

    // Finds the recordings wherever they ended up (library folder, ~/Music/NAM Home, the folder the
    // installer was told about, a NAM Home folder on any drive). If none are found, downloads them
    // into the library folder itself, so the recorded sax never depends on the install script.
    static bool hasRecordings (const juce::File& sax)
    {
        auto mf = sax.getChildFile ("alto").getChildFile ("mf");
        return mf.isDirectory() && mf.getNumberOfChildFiles (juce::File::findFiles, "*.wav") > 4;
    }

    juce::File findRecordings (const juce::File& libRoot) const
    {
        juce::Array<juce::File> c;
        c.add (libRoot.getChildFile ("Instruments").getChildFile ("Sax"));
        c.add (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("NAM Home").getChildFile ("Instruments").getChildFile ("Sax"));
        auto pathFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("Application Support").getChildFile ("NAM Home").getChildFile ("library-path.txt");
        const auto p = pathFile.loadFileAsString().trim();
        if (p.startsWithChar ('/')) c.add (juce::File (p).getChildFile ("Instruments").getChildFile ("Sax"));
        for (const auto& v : juce::File ("/Volumes").findChildFiles (juce::File::findDirectories, false))
            c.add (v.getChildFile ("NAM Home").getChildFile ("Instruments").getChildFile ("Sax"));
        for (const auto& f : c) if (hasRecordings (f)) return f;
        return {};
    }

    bool download (const juce::File& sax)
    {
        setStatus ("Downloading sax recordings...");
        const juce::URL url ("https://github.com/alithosanesti33-tech/nam-home-/releases/download/sax-recordings/NAM-Home-Sax.zip");
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (15000).withNumRedirectsToFollow (8);
        std::unique_ptr<juce::InputStream> in (url.createInputStream (opts));
        if (in == nullptr) return false;
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NAM-Home-Sax.zip");
        tmp.deleteFile();
        {
            juce::FileOutputStream out (tmp);
            if (! out.openedOk()) return false;
            const auto total = in->getTotalLength();
            juce::HeapBlock<char> buf (1 << 16);
            juce::int64 got = 0; int lastPct = -1;
            for (;;)
            {
                if (threadShouldExit()) return false;
                const int n = in->read (buf.get(), 1 << 16);
                if (n <= 0) break;
                out.write (buf.get(), (size_t) n);
                got += n;
                const int pct = total > 0 ? (int) (100 * got / total) : -1;
                if (pct != lastPct && pct >= 0) { lastPct = pct; setStatus ("Downloading sax recordings " + juce::String (pct) + "%"); }
            }
            if (got < 1000000) return false;
        }
        setStatus ("Unpacking sax recordings...");
        sax.createDirectory();
        juce::ZipFile zip (tmp);
        const bool ok = zip.getNumEntries() > 0 && zip.uncompressTo (sax, true).wasOk();
        tmp.deleteFile();
        return ok && hasRecordings (sax);
    }

    void run() override
    {
        juce::File libRoot;
        { const juce::ScopedLock sl (lock); libRoot = root; }
        lastAttempt = juce::Time::getMillisecondCounter();
        setStatus ("Loading recordings...");
        juce::File base = findRecordings (libRoot);
        if (base == juce::File())
        {
            base = libRoot.getChildFile ("Instruments").getChildFile ("Sax");
            if (! download (base)) { setStatus ("Sax recordings missing (couldn't download them)"); return; }
        }
        pool.clear();
        for (auto& h : slots) for (auto& l : h) for (auto& m : l) m = nullptr;
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
        ready = count[0] > 0 || count[1] > 0;
        setStatus (ready.load() ? juce::String (count[0]) + " alto and " + juce::String (count[1]) + " soprano recordings"
                                : "Couldn't read the sax recordings in " + base.getFullPathName());
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
    juce::String statusText { "Loading recordings..." };
    juce::CriticalSection statusLock;
    std::atomic<juce::uint32> lastAttempt { 0 };
    void setStatus (const juce::String& t) { const juce::ScopedLock sl (statusLock); statusText = t; }
};

// One playing recording (with its own position, loop, fade and pitch)
struct SaxPlayer
{
    const SaxSample* s = nullptr;
    double pos = 0;
    float gain = 0, target = 0, step = 0;
    float note = 60.0f;   // which recording note it was picked for (it keeps it while fading out)
    float bend = 0.0f;    // its last pitch offset, frozen once it is no longer the current note
    bool active() const { return s != nullptr && (gain > 0.0f || target > 0.0f); }

    void start (const SaxSample* smp, float midi, bool fromAttack, float fadeSamples)
    {
        s = smp; note = midi; bend = 0;
        pos = fromAttack ? smp->onset : smp->sustain;
        gain = 0; target = 1; step = 1.0f / std::max (1.0f, fadeSamples);
    }
    void stop (float fadeSamples) { target = 0; step = 1.0f / std::max (1.0f, fadeSamples); }

    // 4-point Hermite: linear interpolation adds a gritty, "digital" edge when re-pitching
    static inline float hermite (const std::vector<float>& d, double p)
    {
        const int n = (int) d.size();
        const int i = (int) p; const float t = (float) (p - i);
        auto at = [&] (int k) { return d[(size_t) juce::jlimit (0, n - 1, k)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    inline float tick (double rate)
    {
        if (s == nullptr) return 0.0f;
        if (gain < target) gain = std::min (target, gain + step); else if (gain > target) gain = std::max (target, gain - step);
        if (gain <= 0.0f && target <= 0.0f) { s = nullptr; return 0.0f; }
        const auto& d = s->data;
        float y = hermite (d, pos);
        // seamless loop: in the last 80 ms before the loop end, equal-power cross-fade into the loop start
        const double xf = 0.08 * s->rate;
        const double intoXf = pos - (s->loopEnd - xf);
        if (intoXf > 0)
        {
            const float t = (float) juce::jlimit (0.0, 1.0, intoXf / xf);
            y = y * std::cos (1.5707963f * t) + hermite (d, s->loopStart - xf + intoXf) * std::sin (1.5707963f * t);
        }
        pos += rate;
        if (pos >= s->loopEnd) pos = s->loopStart + (pos - s->loopEnd);
        // smooth fade curve (no corner at the start or end of a fade)
        const float g = gain * gain * (3.0f - 2.0f * gain);
        return y * s->gain * g;
    }
};

class SaxSampler
{
public:
    void prepare (float sampleRate) { sr = sampleRate; reset(); }
    void reset() { for (auto& p : players) p = {}; cur = -1; layer = 1; tone.reset(); lastFc = -1; fcCount = 0; drift = 0; driftTarget = 0; driftCount = 0; }

    // horn: 0 alto, 1 soprano; shiftSemis lets tenor/bari borrow the alto recordings
    void setHorn (int h, float shiftSemis) { horn = h; shift = shiftSemis; }

    // kind: 0 attack (play the real attack), 1 legato (sustain to sustain), 2 tongued re-pick.
    // breath: how hard this note is played (0..1). It picks ONE recording (pp, mf or ff) for the note,
    // so two different takes never play on top of each other (that phasing is what sounds synthetic).
    void noteStart (float midi, int kind, float breath)
    {
        auto& bank = SaxSampleBank::get();
        const float src = midi + shift;
        const float fade = kind == 0 ? 0.003f : (kind == 1 ? 0.025f : 0.008f);
        if (kind != 1) layer = breath < 0.42f ? 0 : (breath < 0.82f ? 1 : 2);   // legato keeps the same take
        auto* smp = bank.nearest (horn, layer, src);
        if (smp == nullptr) return;
        if (cur >= 0) players[cur].stop (fade * sr);
        // use a free slot, or the quietest one, so a slot that is still fading never gets cut off
        int best = 0; float bestG = 2.0f;
        for (int i = 0; i < numPlayers; ++i)
        {
            if (i == cur) continue;
            const float g = players[i].active() ? players[i].gain + 1.0f : 0.0f;
            if (g < bestG) { bestG = g; best = i; }
        }
        cur = best;
        players[cur].start (smp, src, kind != 1, fade * sr);
    }
    void noteStop (float releaseSec) { for (auto& p : players) p.stop (releaseSec * sr); cur = -1; }
    bool sounding() const { for (auto& p : players) if (p.active()) return true; return false; }

    // bend: semitones away from the current note (your bends and vibrato, scoops, falls, glides).
    // breath 0..1 sets the level and, below the take's own dynamic, closes the tone like a real horn.
    inline float tick (float bend, float breath)
    {
        const float b = clamp (breath, 0.0f, 1.2f);
        // a player's pitch is never perfectly still: a slow, tiny wander (about +-3 cents)
        if (--driftCount <= 0) { driftTarget = (rnd() * 2.0f - 1.0f) * 0.03f; driftCount = (int) (sr * (0.15f + 0.3f * rnd())); }
        drift += (driftTarget - drift) * (1.0f / (0.2f * sr));

        float y = 0.0f;
        for (int i = 0; i < numPlayers; ++i)
        {
            auto& p = players[i];
            if (p.s == nullptr) continue;
            if (i == cur) p.bend = bend;
            const double rate = (p.s->rate / sr) * std::exp2 ((p.note - shift + p.bend + drift - p.s->root) / 12.0);
            y += p.tick (rate);
        }
        // softer than the take = darker, the way blowing softer closes a sax's tone
        static const float ref[3] = { 0.3f, 0.65f, 0.95f };
        const float r = clamp (b / ref[layer], 0.05f, 1.0f);
        const float fc = clamp (18000.0f * std::pow (r, 2.2f), 900.0f, 18000.0f);
        if (--fcCount <= 0 && std::abs (fc - lastFc) > 20.0f) { tone.set (Biquad::Type::LowPass, sr, fc, 0.6f); lastFc = fc; fcCount = 32; }
        y = tone.process (y);
        const float level = std::pow (clamp (b, 0.0f, 1.2f), 0.7f) * (0.55f + 0.2f * layer);
        return y * level;
    }

private:
    inline float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; }
    static constexpr int numPlayers = 4;
    float sr = 48000.0f, shift = 0;
    int horn = 0, cur = -1, layer = 1;
    SaxPlayer players[numPlayers];
    Biquad tone;
    float lastFc = -1, drift = 0, driftTarget = 0;
    int fcCount = 0, driftCount = 0;
    uint32_t seed = 22222u;
};
} // namespace ndsp
