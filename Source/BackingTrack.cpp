// Backing track player. Tracks live in <NAM Home library>/Backing Tracks (MP3, WAV, M4A, AIFF).
// It plays from inside NAM Home on purpose: what it plays goes into the same output the talk box's
// echo canceller hears, so the singer on the track is taken back out of your mic.
#include "BlockTypes.h"

using namespace ndsp;

namespace
{
    enum BtP { bTrack, bPlay, bRestart, bLoop, bVolume, bSpeed, bKey, bLoopSec, bA, bB, bSetA, bSetB, bBack, bFwd };
    juce::String mmss (float sec)
    {
        const int t = juce::jmax (0, (int) sec);
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }
    inline double princarg (double x) { return x - 6.283185307179586 * std::floor ((x + 3.141592653589793) / 6.283185307179586); }
}

juce::File BackingTrackBlock::folder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("NAM Home").getChildFile ("Backing Tracks");
}

BackingTrackBlock::BackingTrackBlock() : Block (Type::BackingTrack), juce::Thread ("NAM Home backing track")
{
    auto dir = folder();
    dir.createDirectory();
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.mp3;*.wav;*.m4a;*.aif;*.aiff;*.flac"))
        files.add (f.getFileName());
    files.sortNatural();

    juce::StringArray names;
    for (const auto& f : files) names.add (folder().getChildFile (f).getFileNameWithoutExtension());
    if (names.isEmpty()) names.add ("(no tracks yet)");

    addChoice ("track", "Track", names, 0);
    addToggle ("play", "Play", false);
    addToggle ("restart", "Restart", false);
    addToggle ("loop", "Loop", true);
    addFloat ("volume", "Volume", -40, 6, -6, "dB", -12, 0.5f);
    // practice: slow it down without the pitch dropping, change its key, loop one part
    currentGroup = "Practice";
    addFloat ("speed", "Speed", 50, 125, 100, "%", 100, 1);
    addFloat ("key", "Key", -12, 12, 0, "st", -1, 1);
    addToggle ("loopSection", "Loop section", false);
    addFloat ("loopStart", "Loop start", 0, 480, 0, "s", 60, 0.1f);
    addFloat ("loopEnd", "Loop end", 0, 480, 30, "s", 60, 0.1f);
    addToggle ("setStart", "Set start here", false);
    addToggle ("setEnd", "Set end here", false);
    addToggle ("back", "Back 5 s", false);
    addToggle ("forward", "Forward 5 s", false);
    currentGroup = {};
    for (int k : { (int) bRestart, (int) bSetA, (int) bSetB, (int) bBack, (int) bFwd }) params[(size_t) k]->momentary = true;
}

BackingTrackBlock::~BackingTrackBlock() { stopThread (4000); }

juce::String BackingTrackBlock::getSubtitle() const
{
    if (files.isEmpty()) return "Add tracks to Music/NAM Home/Backing Tracks";
    auto t = std::atomic_load (&track);
    if (t == nullptr) return "Loading...";
    juce::String sub = (params[(size_t) bPlay]->on() ? "Playing: " : "") + t->name + "   " + mmss (posSec.load()) + " / " + mmss (lenSec.load());
    const bool ident = std::abs (p (bSpeed) - 100.0f) < 0.5f && std::abs (p (bKey)) < 0.5f;
    auto s = std::atomic_load (&stretched);
    if (! ident && (s == nullptr || std::abs (s->speed * 100.0f - p (bSpeed)) > 0.5f || std::abs (s->key - p (bKey)) > 0.5f))
        sub += "   (preparing new speed / key...)";
    if (params[(size_t) bLoopSec]->on()) sub += "   loop " + mmss (p (bA)) + "-" + mmss (p (bB));
    return sub;
}

juce::var BackingTrackBlock::saveExtra() const
{
    const int k = juce::jlimit (0, juce::jmax (0, files.size() - 1), (int) p (bTrack));
    return files.isEmpty() ? juce::var() : juce::var (files[k]);
}

void BackingTrackBlock::loadExtra (const juce::var& v)
{
    // the folder may have changed since this was saved: pick the track by name
    savedName = v.toString();
    const int k = files.indexOf (savedName);
    if (k >= 0) params[(size_t) bTrack]->set ((float) k);
}

void BackingTrackBlock::prepare (double sr, int)
{
    if (std::abs (sr - loadRate.load()) > 1.0) loaded = -1;   // decoded at another rate: load again
    loadRate = sr;
    pos = 0;
}

void BackingTrackBlock::run()
{
    // 1. load the chosen file
    while (! threadShouldExit() && loaded.load() != wanted.load())
    {
        const int k = wanted.load();
        if (k < 0 || k >= files.size()) return;
        const double rate = loadRate.load();

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (folder().getChildFile (files[k])));
        if (r == nullptr) { loaded = k; return; }

        const int len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60 * 8));   // up to 8 minutes
        juce::AudioBuffer<float> src (2, len);
        r->read (&src, 0, len, 0, true, true);
        if ((int) r->numChannels < 2) src.copyFrom (1, 0, src, 0, 0, len);

        auto t = std::make_shared<Track>();
        t->name = folder().getChildFile (files[k]).getFileNameWithoutExtension();
        const double ratio = r->sampleRate / rate;   // input samples per output sample
        const int outLen = (int) std::floor (len / ratio);
        t->l.resize ((size_t) outLen); t->r.resize ((size_t) outLen);
        juce::LagrangeInterpolator li[2];
        li[0].process (ratio, src.getReadPointer (0), t->l.data(), outLen);
        li[1].process (ratio, src.getReadPointer (1), t->r.data(), outLen);

        if (threadShouldExit() || wanted.load() != k) continue;   // the choice changed while loading
        std::atomic_store (&track, std::shared_ptr<Track> (t));
        loaded = k;
    }

    // 2. stretch it to the chosen speed and key
    while (! threadShouldExit())
    {
        auto t = std::atomic_load (&track);
        const float sp = wantSpeed.load(), ky = wantKey.load();
        if (t == nullptr || (std::abs (sp - 1.0f) < 0.004f && std::abs (ky) < 0.01f)) return;
        auto s = std::atomic_load (&stretched);
        if (s != nullptr && s->name == t->name && s->l.size() > 0 && std::abs (s->speed - sp) < 0.004f && std::abs (s->key - ky) < 0.01f) return;
        auto out = render (*t, sp, ky);
        if (out == nullptr) continue;   // the knobs moved while stretching: start again
        out->name = t->name; out->speed = sp; out->key = ky;
        std::atomic_store (&stretched, out);
    }
}

// Phase vocoder with phase locking (each harmonic keeps its neighbours' phases, which stops the
// "phasey" smear of a plain vocoder), stereo kept intact by turning both channels by the same
// phase. Time is stretched by key / speed, then resampled by the key: same length as Speed asks,
// pitch moved by Key.
std::shared_ptr<BackingTrackBlock::Track> BackingTrackBlock::render (const Track& src, float speed, float semis)
{
    const double rho = std::exp2 ((double) semis / 12.0);
    const double T = rho / (double) speed;
    constexpr int order = 12, N = 1 << order, Hs = N / 4, B = N / 2 + 1;
    const double Ha = Hs / T;
    const int len = (int) src.l.size();
    if (len < N) return nullptr;

    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N), bl ((size_t) (2 * N)), br ((size_t) (2 * N));
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N);
    std::vector<double> ph ((size_t) B), prevPh ((size_t) B), syn ((size_t) B), synPrev ((size_t) B);
    std::vector<float> mag ((size_t) B), rot ((size_t) B);
    std::vector<int> peaks, owner ((size_t) B);

    const int frames = (int) ((len - N) / Ha) + 1;
    const size_t S = (size_t) frames * Hs + N;
    std::vector<float> sl (S, 0.0f), sr2 (S, 0.0f);
    int prevA = 0;
    const float norm = 1.0f / 1.5f;   // Hann x Hann at 4x overlap
    for (int m = 0; m < frames; ++m)
    {
        if ((m & 63) == 0 && (threadShouldExit() || std::abs (wantSpeed.load() - speed) > 0.004f || std::abs (wantKey.load() - semis) > 0.01f))
            return nullptr;
        const int a = (int) std::llround (m * Ha);
        std::fill (bl.begin(), bl.end(), 0.0f); std::fill (br.begin(), br.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const size_t j = (size_t) juce::jmin (len - 1, a + i);
            bl[(size_t) i] = src.l[j] * win[(size_t) i];
            br[(size_t) i] = src.r[j] * win[(size_t) i];
        }
        fft.performRealOnlyForwardTransform (bl.data(), true);
        fft.performRealOnlyForwardTransform (br.data(), true);
        for (int k = 0; k < B; ++k)
        {
            const float re = bl[(size_t) (2 * k)] + br[(size_t) (2 * k)], im = bl[(size_t) (2 * k + 1)] + br[(size_t) (2 * k + 1)];
            mag[(size_t) k] = std::sqrt (re * re + im * im);
            ph[(size_t) k] = std::atan2 ((double) im, (double) re);
        }
        // peaks, and which peak each bin belongs to
        peaks.clear();
        for (int k = 2; k < B - 2; ++k)
            if (mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1]
                && mag[(size_t) k] > mag[(size_t) k - 2] && mag[(size_t) k] >= mag[(size_t) k + 2] && mag[(size_t) k] > 1.0e-6f)
                peaks.push_back (k);
        const int H = m == 0 ? 0 : a - prevA;
        if (peaks.empty() || m == 0 || H <= 0)
        {
            for (int k = 0; k < B; ++k) syn[(size_t) k] = m == 0 ? ph[(size_t) k] : synPrev[(size_t) k] + 6.283185307179586 * k / N * Hs;
        }
        else
        {
            size_t q = 0;
            for (int k = 0; k < B; ++k)
            {
                while (q + 1 < peaks.size())
                {
                    // boundary: the quietest bin between this peak and the next
                    int lo = peaks[q], hi = peaks[q + 1], mn = lo;
                    for (int j = lo; j <= hi; ++j) if (mag[(size_t) j] < mag[(size_t) mn]) mn = j;
                    if (k > mn) ++q; else break;
                }
                owner[(size_t) k] = peaks[q];
            }
            for (int pk : peaks)
            {
                const double omega = 6.283185307179586 * pk / N;
                const double dev = princarg (ph[(size_t) pk] - prevPh[(size_t) pk] - omega * H);
                syn[(size_t) pk] = synPrev[(size_t) pk] + (omega + dev / H) * Hs;
            }
            for (int k = 0; k < B; ++k)
            {
                const int pk = owner[(size_t) k];
                if (pk != k) syn[(size_t) k] = syn[(size_t) pk] + (ph[(size_t) k] - ph[(size_t) pk]);
            }
        }
        for (int k = 0; k < B; ++k)
        {
            const double r = syn[(size_t) k] - ph[(size_t) k];
            const float c = (float) std::cos (r), s = (float) std::sin (r);
            for (auto* b : { &bl, &br })
            {
                float& re = (*b)[(size_t) (2 * k)];
                float& im = (*b)[(size_t) (2 * k + 1)];
                const float x = re * c - im * s, y = re * s + im * c;
                re = x; im = y;
            }
        }
        prevPh = ph; synPrev = syn; prevA = a;
        fft.performRealOnlyInverseTransform (bl.data());
        fft.performRealOnlyInverseTransform (br.data());
        const size_t o = (size_t) m * Hs;
        for (int i = 0; i < N; ++i)
        {
            sl[o + (size_t) i] += bl[(size_t) i] * win[(size_t) i] * norm;
            sr2[o + (size_t) i] += br[(size_t) i] * win[(size_t) i] * norm;
        }
    }

    // resample by the key: the length ends up len / speed
    auto t = std::make_shared<Track>();
    const size_t outLen = (size_t) ((double) len / speed);
    if (std::abs (semis) < 0.01f)   // same key: already the right length
    {
        sl.resize (std::min (S, outLen)); sr2.resize (sl.size());
        t->l = std::move (sl); t->r = std::move (sr2);
        return t;
    }
    t->l.resize (outLen); t->r.resize (outLen);
    for (size_t j = 0; j < outLen; ++j)
    {
        const double x = (double) j * rho;
        const size_t i = (size_t) x;
        const float f = (float) (x - (double) i);
        if (i + 1 >= S) { t->l[j] = t->r[j] = 0; continue; }
        t->l[j] = sl[i] + f * (sl[i + 1] - sl[i]);
        t->r[j] = sr2[i] + f * (sr2[i + 1] - sr2[i]);
    }
    return t;
}

void BackingTrackBlock::process (float* L, float* R, int n)
{
    // load the chosen track (in the background) when the choice changes
    const int k = files.isEmpty() ? -1 : juce::jlimit (0, files.size() - 1, (int) p (bTrack));
    if (k >= 0 && loaded.load() != k && wanted.load() != k && ! isThreadRunning())
    {
        wanted = k;
        startThread (juce::Thread::Priority::low);
    }
    else if (k >= 0 && loaded.load() != k && wanted.load() == k && ! isThreadRunning())
        startThread (juce::Thread::Priority::low);   // a previous load was cut short

    const bool restart = p (bRestart) > 0.5f;
    if (restart && ! lastRestart) pos = (params[(size_t) bLoopSec]->on() ? p (bA) * sampleRate : 0.0);
    lastRestart = restart;

    // Speed / Key: stretch again in the background once the knobs stop moving
    const float sp = p (bSpeed) * 0.01f, ky = p (bKey);
    if (sp != lastSpeed || ky != lastKey) { lastSpeed = sp; lastKey = ky; stableFor = 0; }
    else stableFor += n;
    const bool ident = std::abs (sp - 1.0f) < 0.004f && std::abs (ky) < 0.01f;
    if (! ident && stableFor > (int) (0.4f * sampleRate) && ! isThreadRunning() && loaded.load() == k)
    {
        auto s = std::atomic_load (&stretched);
        auto t0 = std::atomic_load (&track);
        if (t0 != nullptr && (s == nullptr || s->name != t0->name || std::abs (s->speed - sp) > 0.004f || std::abs (s->key - ky) > 0.01f))
        {
            wantSpeed = sp; wantKey = ky;
            startThread (juce::Thread::Priority::low);
        }
    }

    auto t = std::atomic_load (&track);
    if (t == nullptr || t->l.empty() || loaded.load() != k) return;
    const double len = (double) t->l.size();
    lenSec = (float) (len / sampleRate);

    // set the loop / jump around (works while stopped too)
    auto edge = [this] (int idx, bool& last) { const bool v = p (idx) > 0.5f; const bool e = v && ! last; last = v; return e; };
    if (edge (bSetA, lastSetA)) params[(size_t) bA]->set ((float) (pos / sampleRate));
    if (edge (bSetB, lastSetB)) params[(size_t) bB]->set ((float) (pos / sampleRate));
    if (edge (bBack, lastBack)) { pos = std::max (0.0, pos - 5.0 * sampleRate); jumpFade = 0; }
    if (edge (bFwd, lastFwd)) { pos = std::min (len - 1.0, pos + 5.0 * sampleRate); jumpFade = 0; }
    posSec = (float) (pos / sampleRate);
    if (p (bPlay) < 0.5f) return;   // your guitar passes as it is

    // what to play: the stretched version when it's ready (it keeps its own speed until the new one is)
    auto s = std::atomic_load (&stretched);
    const bool useS = ! ident && s != nullptr && s->name == t->name && ! s->l.empty();
    const std::vector<float>& bl = useS ? s->l : t->l;
    const std::vector<float>& br = useS ? s->r : t->r;
    const double step = useS ? (double) s->speed : 1.0;
    const double bufLen = (double) bl.size();

    const float g = dbToGain (p (bVolume));
    const bool loop = p (bLoop) > 0.5f;
    const bool section = params[(size_t) bLoopSec]->on();
    const double A = (double) p (bA) * sampleRate, Bp = std::min (len, (double) p (bB) * sampleRate);
    const bool useSection = section && Bp > A + 0.2 * sampleRate;
    const double fadeLen = 0.008 * sampleRate;   // tiny fades so loop jumps don't click
    const float fadeStep = 1.0f / (float) fadeLen;
    for (int i = 0; i < n; ++i)
    {
        if (useSection && (pos >= Bp || pos < A - sampleRate * 0.5)) { pos = A; jumpFade = 0; }
        if (pos >= len)
        {
            if (! loop) { params[(size_t) bPlay]->set (0.0f); pos = 0; break; }
            pos = 0; jumpFade = 0;
        }
        double ix = pos / step;
        if (ix >= bufLen - 2) ix = bufLen - 2;
        const size_t j = (size_t) ix;
        const float f = (float) (ix - (double) j);
        float gg = g;
        if (useSection) gg *= (float) juce::jlimit (0.0, 1.0, (Bp - pos) / (fadeLen * step));
        jumpFade = std::min (1.0f, jumpFade + fadeStep);
        gg *= jumpFade;
        L[i] += (bl[j] + f * (bl[j + 1] - bl[j])) * gg;
        R[i] += (br[j] + f * (br[j + 1] - br[j])) * gg;
        pos += step;
    }
    posSec = (float) (pos / sampleRate);
}
