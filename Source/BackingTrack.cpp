// Backing track player. Tracks live in <NAM Home library>/Backing Tracks (MP3, WAV, M4A, AIFF).
// It plays from inside NAM Home on purpose: what it plays goes into the same output the talk box's
// echo canceller hears, so the singer on the track is taken back out of your mic.
#include "BlockTypes.h"

using namespace ndsp;

namespace
{
    enum BtP { bTrack, bPlay, bRestart, bLoop, bVolume };
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
    params[(size_t) bRestart]->momentary = true;
}

BackingTrackBlock::~BackingTrackBlock() { stopThread (4000); }

juce::String BackingTrackBlock::getSubtitle() const
{
    if (files.isEmpty()) return "Add tracks to Music/NAM Home/Backing Tracks";
    auto t = std::atomic_load (&track);
    if (t == nullptr) return "Loading...";
    return (params[(size_t) bPlay]->on() ? "Playing: " : "") + t->name;
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
    while (! threadShouldExit())
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
        return;
    }
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
    if (restart && ! lastRestart) pos = 0;
    lastRestart = restart;

    auto t = std::atomic_load (&track);
    if (t == nullptr || t->l.empty() || p (bPlay) < 0.5f || loaded.load() != k) return;   // your guitar passes as it is

    const float g = dbToGain (p (bVolume));
    const bool loop = p (bLoop) > 0.5f;
    const double len = (double) t->l.size();
    for (int i = 0; i < n; ++i)
    {
        if (pos >= len)
        {
            if (! loop) { params[(size_t) bPlay]->set (0.0f); pos = 0; break; }
            pos = 0;
        }
        const size_t j = (size_t) pos;
        L[i] += t->l[j] * g;
        R[i] += t->r[j] * g;
        pos += 1.0;
    }
}
