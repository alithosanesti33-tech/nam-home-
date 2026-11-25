#include "Rhythm.h"
#include <cmath>
#include <set>

using namespace rhythm;

namespace
{
const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
std::mutex rootLock;
juce::File root;
constexpr float twoPi = 6.28318530718f;
}

void rhythm::setLibraryFolder (const juce::File& f) { std::lock_guard<std::mutex> l (rootLock); root = f; }
juce::File rhythm::libraryFolder() { std::lock_guard<std::mutex> l (rootLock); return root; }

const char* rhythm::drumFileName (int d)
{
    static const char* n[NumDrums] = { "kick", "snare", "rim", "hat", "hatopen", "hatpedal", "ride", "bell", "crash", "tom1", "tom2", "tom3" };
    return n[juce::jlimit (0, NumDrums - 1, d)];
}

//==============================================================================
DrumBank& DrumBank::get() { static DrumBank b; return b; }

juce::StringArray DrumBank::kitNames() const
{
    juce::StringArray n;
    const auto dir = libraryFolder().getChildFile ("Drums").getChildFile ("Kits");
    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findDirectories))
        if (e.getFile().getChildFile ("kick_1.wav").existsAsFile()) n.add (e.getFile().getFileName());
    n.sort (true);
    return n;
}

juce::Array<juce::File> DrumBank::loopFiles() const
{
    juce::Array<juce::File> f;
    const auto dir = libraryFolder().getChildFile ("Drums").getChildFile ("Loops");
    for (const auto& e : juce::RangedDirectoryIterator (dir, true, "*.wav;*.aif;*.aiff", juce::File::findFiles))
        if (! e.getFile().getFileName().startsWithChar ('.')) f.add (e.getFile());
    std::sort (f.begin(), f.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return f;
}

void DrumBank::requestKit (const juce::String& name)
{
    std::lock_guard<std::mutex> l (m);
    if (name.isEmpty() || kits.count (name) || wantKits.contains (name)) return;
    wantKits.add (name);
    if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
}

void DrumBank::requestLoop (const juce::String& path)
{
    std::lock_guard<std::mutex> l (m);
    if (path.isEmpty() || loops.count (path) || wantLoops.contains (path)) return;
    wantLoops.add (path);
    if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
}

const DrumKit* DrumBank::kit (const juce::String& name) const
{
    std::lock_guard<std::mutex> l (m);
    auto it = kits.find (name);
    return it == kits.end() ? nullptr : it->second.get();
}

const LoopFile* DrumBank::loop (const juce::String& path) const
{
    std::lock_guard<std::mutex> l (m);
    auto it = loops.find (path);
    return it == loops.end() ? nullptr : it->second.get();
}

static bool readStereo (juce::AudioFormatManager& fm, const juce::File& f, juce::AudioBuffer<float>& out, double& rate, double maxSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples < 16) return false;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (maxSeconds * r->sampleRate));
    juce::AudioBuffer<float> tmp ((int) std::max (1u, r->numChannels), len);
    r->read (&tmp, 0, len, 0, true, true);
    out.setSize (2, len);
    out.copyFrom (0, 0, tmp, 0, 0, len);
    out.copyFrom (1, 0, tmp, tmp.getNumChannels() > 1 ? 1 : 0, 0, len);
    rate = r->sampleRate;
    return true;
}

void DrumBank::run()
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    while (! threadShouldExit())
    {
        juce::String kitName, loopPath;
        {
            std::lock_guard<std::mutex> l (m);
            if (! wantKits.isEmpty()) kitName = wantKits[0];
            else if (! wantLoops.isEmpty()) loopPath = wantLoops[0];
            else return;
        }
        if (kitName.isNotEmpty())
        {
            auto k = std::make_unique<DrumKit>();
            k->name = kitName;
            const auto dir = libraryFolder().getChildFile ("Drums").getChildFile ("Kits").getChildFile (kitName);
            for (int d = 0; d < NumDrums; ++d)
                for (int layer = 1; layer <= 4; ++layer)
                {
                    DrumSample s;
                    if (readStereo (fm, dir.getChildFile (juce::String (drumFileName (d)) + "_" + juce::String (layer) + ".wav"), s.data, s.rate, 8.0))
                        k->layers[d].push_back (std::move (s));
                }
            std::lock_guard<std::mutex> l (m);
            kits[kitName] = std::move (k);
            wantKits.removeString (kitName);
        }
        else
        {
            auto lf = std::make_unique<LoopFile>();
            lf->path = loopPath;
            const bool ok = readStereo (fm, juce::File (loopPath), lf->data, lf->rate, 120.0);
            std::lock_guard<std::mutex> l (m);
            if (ok) loops[loopPath] = std::move (lf);
            wantLoops.removeString (loopPath);
        }
    }
}

//==============================================================================
// Saves loops to the SSD in the background, so the audio never waits for the disk.
namespace
{
class LoopWriter : private juce::Thread
{
public:
    static LoopWriter& get() { static LoopWriter w; return w; }
    void add (LooperBlock* b) { std::lock_guard<std::mutex> l (m); loopers.insert (b); if (! isThreadRunning()) startThread (juce::Thread::Priority::low); }
    void remove (LooperBlock* b) { std::lock_guard<std::mutex> l (m); loopers.erase (b); }
private:
    LoopWriter() : juce::Thread ("Loop writer") {}
    ~LoopWriter() override { stopThread (4000); }
    void run() override
    {
        while (! threadShouldExit())
        {
            {
                std::lock_guard<std::mutex> l (m);
                for (auto* b : loopers) b->writeIfNeeded();
            }
            wait (300);
        }
    }
    std::mutex m;
    std::set<LooperBlock*> loopers;
};

enum LpP { lRec, lStop, lClear, lTrack, lNext, lUndo, lReverse, lHalf, lSync, lAfter, lFeedback, lLv0, lMix = lLv0 + 4, lDry, lAuto, lSave };
}

LooperBlock::LooperBlock() : Block (Type::Looper)
{
    currentGroup = "Footswitches";
    addToggle ("rec", "Record / Play / Overdub", false);
    addToggle ("stop", "Stop / Play", false);
    addToggle ("clear", "Clear track", false);
    addChoice ("track", "Track", { "1", "2", "3", "4" }, 0);
    addToggle ("next", "Next track", false);
    addToggle ("undo", "Undo last track", false);
    addToggle ("reverse", "Reverse", false);
    addToggle ("half", "Half speed", false);
    currentGroup = "Loop";
    addChoice ("sync", "Length", { "Free", "Whole bars at the tempo" }, 0);
    addChoice ("after", "After recording", { "Play", "Overdub" }, 0);
    addFloat ("feedback", "Overdub keeps", 0, 100, 100, "%", -1, 1);
    currentGroup = "Levels";
    for (int t = 0; t < tracks; ++t) addFloat ("lv" + juce::String (t + 1), "Track " + juce::String (t + 1), 0, 150, 100, "%", -1, 1);
    addFloat ("mix", "Loops", 0, 150, 100, "%", -1, 1);
    addToggle ("dry", "Hear your playing", true);
    currentGroup = "Saving";
    addToggle ("autosave", "Save every loop to the SSD (WAV)", true);
    addToggle ("save", "Save now", false);
    currentGroup = {};
    for (int k : { (int) lRec, (int) lStop, (int) lClear, (int) lNext, (int) lUndo, (int) lSave }) params[(size_t) k]->momentary = true;
    for (int i = 0; i < 8; ++i) lastPress[i] = 0;
    LoopWriter::get().add (this);
}

LooperBlock::~LooperBlock() { LoopWriter::get().remove (this); }

std::vector<Block::StartingPoint> LooperBlock::startingPoints() const
{
    const char* tip = "Right-click Record / Play / Overdub (and Stop) and choose MIDI learn to put them on footswitches.";
    return {
        { "Free loops", tip, { { "sync", 0 }, { "after", 0 }, { "feedback", 100 } } },
        { "Locked to the tempo", juce::String ("Loop length rounds to whole bars, so it stays in time with the drums. ") + tip,
          { { "sync", 1 }, { "after", 0 }, { "feedback", 100 } } },
        { "Layering (overdub straight away)", tip, { { "sync", 1 }, { "after", 1 }, { "feedback", 100 } } },
        { "Fading layers", juce::String ("Older layers fade a little on every pass, like a tape loop. ") + tip,
          { { "sync", 0 }, { "after", 1 }, { "feedback", 85 } } },
    };
}

juce::String LooperBlock::getTitle() const
{
    const char* names[] = { "Empty", "Recording", "Playing", "Overdubbing", "Stopped" };
    const auto st = state.load();
    juce::String t = "Looper" + dot + names[(int) st];
    if (st == State::Recording || st == State::Overdub) t << " track " << (recTrack + 1);
    if (loopLen > 0) t << dot << juce::String ((double) loopLen / sampleRate, 1) << " s";
    return t;
}

void LooperBlock::prepare (double sr, int)
{
    if (std::abs (sr - prepRate) < 1.0 && maxLen > 0) return;   // keep your loops when the audio restarts
    prepRate = sr;
    maxLen = (int) (maxSeconds * sr);
    for (auto& b : buf) { b.setSize (2, maxLen); b.clear(); }
    for (auto& u : used) u = false;
    loopLen = recLen = targetLen = 0;
    pos = 0;
    state = State::Empty;
}

void LooperBlock::finishFirstTake()
{
    if (recLen < (int) (0.15 * sampleRate)) { buf[recTrack].clear(); recLen = 0; state = State::Empty; return; }
    if ((int) p (lSync) == 1 && targetLen == 0)
    {
        const double barLen = 4.0 * 60.0 / std::max (30.0f, context().bpm.load()) * sampleRate;
        const int bars = std::max (1, (int) std::lround (recLen / barLen));
        targetLen = std::min (maxLen, (int) std::lround (bars * barLen));
        if (targetLen > recLen) return;   // keep recording to the end of the bar
    }
    loopLen = targetLen > 0 ? std::min (targetLen, recLen) : recLen;
    // short fades at the seam so the loop doesn't click
    const int f = std::min (loopLen / 4, (int) (0.004 * sampleRate));
    for (int c = 0; c < 2; ++c)
    {
        auto* d = buf[recTrack].getWritePointer (c);
        for (int i = 0; i < f; ++i) { const float g = (float) i / (float) f; d[i] *= g; d[loopLen - 1 - i] *= g; }
    }
    used[recTrack] = true;
    lastRecorded = recTrack;
    pos = 0;
    state = (int) p (lAfter) == 1 ? State::Overdub : State::Playing;
    wantSave = true;
}

void LooperBlock::press (int which)
{
    auto& ctx = context();
    const auto st = state.load();
    const int sel = (int) p (lTrack);
    switch (which)
    {
        case lRec:
            if (st == State::Empty) { recTrack = sel; buf[recTrack].clear(); recLen = 0; targetLen = 0; state = State::Recording; ++ctx.looperStarts; }
            else if (st == State::Recording) finishFirstTake();
            else if (st == State::Playing) { recTrack = sel; state = State::Overdub; }
            else if (st == State::Overdub) { lastRecorded = recTrack; state = State::Playing; wantSave = true; }
            else if (st == State::Stopped) { pos = 0; state = State::Playing; ++ctx.looperStarts; }
            break;
        case lStop:
            if (st == State::Recording) { finishFirstTake(); if (state.load() != State::Empty) { state = State::Stopped; pos = 0; ++ctx.looperStops; } }
            else if (st == State::Playing || st == State::Overdub) { if (st == State::Overdub) { lastRecorded = recTrack; wantSave = true; } state = State::Stopped; pos = 0; ++ctx.looperStops; }
            else if (st == State::Stopped) { pos = 0; state = State::Playing; ++ctx.looperStarts; }
            break;
        case lClear:
        case lUndo:
        {
            const int t = which == lClear ? sel : lastRecorded;
            if (t < 0 || st == State::Recording) break;
            if (st == State::Overdub && t == recTrack) state = State::Playing;
            buf[t].clear(); used[t] = false;
            if (t == lastRecorded) lastRecorded = -1;
            bool any = false; for (auto u : used) any = any || u;
            if (! any) { state = State::Empty; loopLen = 0; session = {}; ++ctx.looperStops; }
            wantSave = true;
            break;
        }
        case lNext:
            params[(size_t) lTrack]->set ((float) ((sel + 1) % tracks));
            break;
        case lSave:
            wantSaveNow = true;
            break;
        default: break;
    }
}

void LooperBlock::process (float* L, float* R, int n)
{
    // footswitches: every press (any change of the switch) is one press
    if (! primed)
    {
        primed = true;
        int slot = 0;
        for (int k : { (int) lRec, (int) lStop, (int) lClear, (int) lNext, (int) lUndo, (int) lSave }) lastPress[slot++] = p (k);
    }
    for (int k : { (int) lRec, (int) lStop, (int) lClear, (int) lNext, (int) lUndo, (int) lSave })
    {
        const int slot = k == lRec ? 0 : k == lStop ? 1 : k == lClear ? 2 : k == lNext ? 3 : k == lUndo ? 4 : 5;
        const float v = p (k);
        if (v != lastPress[slot]) { lastPress[slot] = v; press (k); }
    }
    if (maxLen == 0) return;
    if (state.load() == State::Overdub) recTrack = (int) p (lTrack);   // switch tracks while overdubbing (like the Teleport)

    const bool dry = p (lDry) > 0.5f;
    const float mix = p (lMix) * 0.01f, fb = p (lFeedback) * 0.01f;
    float lv[tracks]; for (int t = 0; t < tracks; ++t) lv[t] = p (lLv0 + t) * 0.01f * mix;
    const double speed = (p (lHalf) > 0.5f ? 0.5 : 1.0) * (p (lReverse) > 0.5f ? -1.0 : 1.0);

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        float oL = dry ? inL : 0.0f, oR = dry ? inR : 0.0f;
        const auto st = state.load();
        if (st == State::Recording)
        {
            if (recLen < maxLen) { buf[recTrack].setSample (0, recLen, inL); buf[recTrack].setSample (1, recLen, inR); ++recLen; }
            if ((targetLen > 0 && recLen >= targetLen) || recLen >= maxLen) finishFirstTake();
        }
        else if ((st == State::Playing || st == State::Overdub) && loopLen > 0)
        {
            const int i0 = (int) pos, i1 = (i0 + 1) % loopLen;
            const float fr = (float) (pos - i0);
            for (int t = 0; t < tracks; ++t)
            {
                if (! used[t] && ! (st == State::Overdub && t == recTrack)) continue;
                const float* a = buf[t].getReadPointer (0); const float* b = buf[t].getReadPointer (1);
                oL += (a[i0] + (a[i1] - a[i0]) * fr) * lv[t];
                oR += (b[i0] + (b[i1] - b[i0]) * fr) * lv[t];
            }
            if (st == State::Overdub)
            {
                float* a = buf[recTrack].getWritePointer (0); float* b = buf[recTrack].getWritePointer (1);
                a[i0] = a[i0] * fb + inL; b[i0] = b[i0] * fb + inR;
                used[recTrack] = true;
            }
            pos += speed;
            while (pos >= loopLen) pos -= loopLen;
            while (pos < 0) pos += loopLen;
        }
        L[i] = oL; R[i] = oR;
    }
}

void LooperBlock::writeIfNeeded()
{
    const bool manual = wantSaveNow.exchange (false);
    const bool automatic = wantSave.exchange (false) && p (lAuto) > 0.5f;
    if (! manual && ! automatic) return;
    const int len = loopLen;
    if (len <= 0) return;
    const auto base = libraryFolder();
    if (base == juce::File()) return;
    if (session.isEmpty()) session = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");
    const auto dir = base.getChildFile ("Loops").getChildFile (session);
    dir.createDirectory();
    juce::WavAudioFormat wav;
    for (int t = 0; t < tracks; ++t)
    {
        auto f = dir.getChildFile ("Track " + juce::String (t + 1) + ".wav");
        if (! used[t]) { f.deleteFile(); continue; }
        auto tmp = f.getSiblingFile (f.getFileName() + ".part");
        tmp.deleteFile();
        if (auto os = std::unique_ptr<juce::OutputStream> (tmp.createOutputStream()))
        {
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sampleRate, 2, 24, {}, 0));
            if (w == nullptr) continue;
            os.release();   // the writer owns the stream now
            const float* ch[2] = { buf[t].getReadPointer (0), buf[t].getReadPointer (1) };
            w->writeFromFloatArrays (ch, 2, len);
            w = nullptr;
            tmp.moveFileTo (f);
        }
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("bpm", context().bpm.load());
    o->setProperty ("seconds", (double) len / sampleRate);
    o->setProperty ("sampleRate", sampleRate);
    dir.getChildFile ("loop.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    savedLen = len;
}

//==============================================================================
// Drums
namespace
{
enum DrP { dPlay, dGroove, dKit, dVolume, dSwing, dHuman, dFill, dRuns, dSource, dLoop, dFit, dKickLv, dSnareLv, dHatLv, dCymLv, dTomLv };

struct Groove
{
    const char* name;
    int grid;   // steps per bar: 16 (straight) or 12 (triplets)
    float swing; // default feel
    std::vector<std::pair<int, const char*>> rows;
};

// '.' rest  'g' ghost  'x' normal  'X' accent.  Two bars each ('|' is only for reading).
const std::vector<Groove>& grooves()
{
    static const std::vector<Groove> g = {
        { "Rock", 16, 0, {
            { Hat,   "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x." },
            { Kick,  "X.....x.X.......|X.....x.X.x....." },
            { Snare, "....X.......X...|....X.......X..." } } },
        { "Pop", 16, 0, {
            { Hat,   "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x." },
            { Kick,  "X...X...X...X...|X...X...X...X..." },
            { Snare, "....X.......X...|....X.......X..." } } },
        { "Funk", 16, 0.1f, {
            { Hat,   "xgxgxgxgxgxgxgxg|xgxgxgxgxgxgxg.g" },
            { HatOpen,"................|..............x." },
            { Kick,  "X.....X...X..x..|X.x...X......x.." },
            { Snare, "....X..g.g..X..g|.g..X..g....X.g." } } },
        { "Blues shuffle", 12, 0, {
            { Hat,   "x.xx.xx.xx.x|x.xx.xx.xx.x" },
            { Kick,  "X.....x.....|X.....x....." },
            { Snare, "...X.....X..|...X.....X.." } } },
        { "Slow blues (12/8)", 12, 0, {
            { Hat,   "Xxxxxxxxxxxx|Xxxxxxxxxxxx" },
            { Kick,  "X.....X.....|X.....X..x.." },
            { Snare, "...X.....X..|...X.....X.." } } },
        { "Texas shuffle", 12, 0, {
            { Ride,  "X.xX.xX.xX.x|X.xX.xX.xX.x" },
            { Kick,  "X.xX.xX.xX.x|X.xX.xX.xX.x" },
            { Snare, "...X.....X..|...X.....X.." } } },
        { "Jazz swing", 12, 0, {
            { Ride,  "x..X.xx..X.x|x..X.xx..X.x" },
            { HatPedal,"...x.....x..|...x.....x.." },
            { Kick,  "g..g..g..g..|g..g..g..g.." },
            { Snare, "............|.....g.....g" } } },
        { "Bossa nova", 16, 0, {
            { Hat,   "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x." },
            { Rim,   "X..X..X...X..X..|..X..X...X..X..." },
            { Kick,  "X..xX..xX..xX..x|X..xX..xX..xX..x" } } },
        { "Metal (double kick)", 16, 0, {
            { Ride,  "X...X...X...X...|X...X...X...X..." },
            { Kick,  "xxxxxxxxxxxxxxxx|xxxxxxxxxxxxxxxx" },
            { Snare, "....X.......X...|....X.......X..." } } },
        { "Thrash (skank beat)", 16, 0, {
            { Hat,   "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x." },
            { Kick,  "X...X...X...X...|X...X...X...X..." },
            { Snare, "..X...X...X...X.|..X...X...X...X." } } },
        { "Metal breakdown", 16, 0, {
            { Crash, "X.......X.......|X.......X......." },
            { Kick,  "X..x..X...X..x..|X..x..X..x.xX..." },
            { Snare, "........X.......|........X......." } } },
        { "Hip-hop", 16, 0.35f, {
            { Hat,   "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x." },
            { Kick,  "X......x..X.....|X......x..X..x.." },
            { Snare, "....X.......X...|....X.......X..." } } },
        { "Reggae one drop", 12, 0, {
            { Hat,   "x.xx.xx.xx.x|x.xx.xx.xx.x" },
            { Rim,   "......X.....|......X....." },
            { Kick,  "......X.....|......X....." } } },
        { "Punk", 16, 0, {
            { Hat,   "x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x." },
            { Kick,  "X...X...X...X...|X...X...X...X..." },
            { Snare, "..X...X...X...X.|..X...X...X...X." } } },
        { "Ballad", 16, 0, {
            { Hat,   "g.g.g.g.g.g.g.g.|g.g.g.g.g.g.g.g." },
            { Kick,  "X.......X.x.....|X.......X......." },
            { Rim,   "....x.......x...|....x.......x..." } } },
        { "Country train beat", 16, 0, {
            { Snare, "ggggXgggggggXggg|ggggXgggggggXggg" },
            { Kick,  "X.......X.......|X.......X......." } } },
    };
    return g;
}

float velOf (char c) { return c == 'X' ? 1.0f : c == 'x' ? 0.72f : c == 'g' ? 0.3f : 0.0f; }
std::string clean (const char* s) { std::string o; for (const char* p = s; *p; ++p) if (*p != '|') o += *p; return o; }
}

juce::StringArray DrumsBlock::grooveNames()
{
    juce::StringArray n;
    for (auto& g : grooves()) n.add (g.name);
    return n;
}

DrumsBlock::DrumsBlock() : Block (Type::Drums)
{
    kitChoices.add ("Electronic (built in)");
    for (auto& k : DrumBank::get().kitNames()) kitChoices.add (k);
    loopChoices.add ("None"); loopPaths.add ({});
    for (auto& f : DrumBank::get().loopFiles()) { loopChoices.add (f.getFileNameWithoutExtension()); loopPaths.add (f.getFullPathName()); }

    currentGroup = "Drums";
    addToggle ("play", "Start / stop", false);
    addChoice ("groove", "Groove", grooveNames(), 0);
    int defKit = kitChoices.size() > 1 ? 1 : 0;
    for (int i = 1; i < kitChoices.size(); ++i) if (kitChoices[i].startsWithIgnoreCase ("Acoustic")) defKit = i;
    addChoice ("kit", "Kit", kitChoices, defKit);
    addFloat ("volume", "Volume", 0, 150, 70, "%", -1, 1);
    addFloat ("swing", "Swing", 0, 100, 0, "%", -1, 1);
    addFloat ("human", "Human feel", 0, 100, 25, "%", -1, 1);
    addToggle ("fill", "Fill", false);
    addChoice ("runs", "Starts", { "With Start / stop", "With MIDI start / stop (or Logic's transport)", "With the looper" }, 0);
    currentGroup = "Your drum loops";
    addChoice ("source", "Play", { "Groove", "My drum loop" }, 0);
    addChoice ("loop", "Loop", loopChoices, 0);
    addToggle ("fit", "Fit loop to the tempo", true);
    currentGroup = "Mix";
    addFloat ("kickLv", "Kick", 0, 150, 100, "%", -1, 1);
    addFloat ("snareLv", "Snare", 0, 150, 100, "%", -1, 1);
    addFloat ("hatLv", "Hi-hat", 0, 150, 85, "%", -1, 1);
    addFloat ("cymLv", "Cymbals", 0, 150, 80, "%", -1, 1);
    addFloat ("tomLv", "Toms", 0, 150, 90, "%", -1, 1);
    currentGroup = {};
    params[(size_t) dPlay]->momentary = params[(size_t) dFill]->momentary = true;
    params[(size_t) dLoop]->visible = [] (const Block& b) { return b.params[(size_t) dSource]->getInt() == 1; };
    params[(size_t) dFit]->visible = [] (const Block& b) { return b.params[(size_t) dSource]->getInt() == 1; };
}

juce::String DrumsBlock::getTitle() const
{
    const int src = (int) p (dSource);
    juce::String t = "Drums" + dot + (src == 1 ? loopChoices[(int) p (dLoop)] : grooveNames()[(int) p (dGroove)]);
    if (src == 1 && userLoop == nullptr && (int) p (dLoop) > 0) t << " (loading...)";
    if (running) t << dot << "playing";
    return t;
}

juce::var DrumsBlock::saveExtra() const
{
    // choices are lists of files, so remember the names too (the lists change as you add files)
    auto* o = new juce::DynamicObject();
    o->setProperty ("kit", kitChoices[(int) p (dKit)]);
    o->setProperty ("loop", loopChoices[(int) p (dLoop)]);
    return juce::var (o);
}

void DrumsBlock::loadExtra (const juce::var& v)
{
    const int k = kitChoices.indexOf (v.getProperty ("kit", "").toString());
    if (k >= 0) params[(size_t) dKit]->set ((float) k);
    const int l = loopChoices.indexOf (v.getProperty ("loop", "").toString());
    if (l >= 0) params[(size_t) dLoop]->set ((float) l);
}

void DrumsBlock::reset()
{
    for (auto& v : voices) v = {};
    running = false; beat = 0; lastStep = -1; fillBar = -1;
}

// The built-in electronic kit, drawn with simple synthesis (always available, even before the
// recorded kits are downloaded).
void DrumsBlock::buildSynthKit()
{
    const float sr = (float) sampleRate;
    juce::Random r (7);
    auto make = [&] (int d, float seconds, std::function<float (float, int)> gen)
    {
        DrumSample s; s.rate = sr;
        const int n = (int) (seconds * sr);
        s.data.setSize (2, n);
        for (int i = 0; i < n; ++i) { const float x = gen ((float) i / sr, i); s.data.setSample (0, i, x); s.data.setSample (1, i, x); }
        synth.layers[d].clear();
        synth.layers[d].push_back (std::move (s));
    };
    float ph = 0, lp = 0, hp = 0, prev = 0;
    make (Kick, 0.5f, [&] (float t, int i) { if (i == 0) ph = 0; const float f = 48.0f + 110.0f * std::exp (-t * 30.0f); ph += twoPi * f / sr; return std::sin (ph) * std::exp (-t * 7.0f) * 0.95f + (t < 0.002f ? 0.3f : 0.0f); });
    make (Snare, 0.35f, [&] (float t, int i) { if (i == 0) { ph = 0; lp = 0; } ph += twoPi * 185.0f / sr; const float nz = r.nextFloat() * 2 - 1; lp += 0.45f * (nz - lp);
                                             return std::sin (ph) * std::exp (-t * 25.0f) * 0.5f + (nz - lp * 0.6f) * std::exp (-t * 14.0f) * 0.55f; });
    make (Rim, 0.08f, [&] (float t, int i) { if (i == 0) ph = 0; ph += twoPi * 1700.0f / sr; return std::sin (ph) * std::exp (-t * 90.0f) * 0.7f; });
    auto metal = [&] (float t, float decay, float level) { const float nz = r.nextFloat() * 2 - 1; hp = 0.85f * (hp + nz - prev); prev = nz;
                                                           float sq = 0; for (float f : { 3150.0f, 4230.0f, 5410.0f, 6780.0f }) sq += std::sin (twoPi * f * t) > 0 ? 0.25f : -0.25f;
                                                           return (hp * 0.7f + sq * 0.3f) * std::exp (-t * decay) * level; };
    make (Hat, 0.09f, [&] (float t, int) { return metal (t, 60.0f, 0.5f); });
    make (HatPedal, 0.06f, [&] (float t, int) { return metal (t, 80.0f, 0.3f); });
    make (HatOpen, 0.7f, [&] (float t, int) { return metal (t, 6.0f, 0.45f); });
    make (Ride, 1.6f, [&] (float t, int) { return metal (t, 2.5f, 0.28f) + std::sin (twoPi * 520.0f * t) * std::exp (-t * 3.0f) * 0.08f; });
    make (Bell, 1.2f, [&] (float t, int) { return (std::sin (twoPi * 830.0f * t) + 0.6f * std::sin (twoPi * 1245.0f * t)) * std::exp (-t * 3.5f) * 0.25f; });
    make (Crash, 2.4f, [&] (float t, int) { return metal (t, 1.6f, 0.5f); });
    const float tomHz[3] = { 210.0f, 150.0f, 105.0f };
    for (int k = 0; k < 3; ++k)
        make (Tom1 + k, 0.6f, [&, k] (float t, int i) { if (i == 0) ph = 0; ph += twoPi * tomHz[k] * (1.0f + 0.5f * std::exp (-t * 20.0f)) / sr; return std::sin (ph) * std::exp (-t * 6.0f) * 0.8f; });
}

void DrumsBlock::prepare (double sr, int)
{
    if (std::abs (sr - prepRate) > 1.0) { prepRate = sr; buildSynthKit(); }
    reset();
}

void DrumsBlock::trigger (int drum, float vel, int offset)
{
    const DrumKit* kitNow = (recorded != nullptr && ! recorded->layers[drum].empty()) ? recorded : &synth;
    const auto& layers = kitNow->layers[drum];
    if (layers.empty()) return;
    const float hum = p (dHuman) * 0.01f;
    vel = juce::jlimit (0.05f, 1.0f, vel * (1.0f + (rng.nextFloat() - 0.5f) * 0.3f * hum));
    const int li = std::min ((int) layers.size() - 1, (int) (vel * (float) layers.size()));
    if (drum == Hat || drum == HatPedal)
        for (auto& v : voices) if (v.s != nullptr && v.drum == HatOpen) v.gain *= 0.0f + 0.15f;   // closing the hat chokes the open hat
    Voice* slot = &voices[0];
    for (auto& v : voices) { if (v.s == nullptr) { slot = &v; break; } if (v.pos > slot->pos) slot = &v; }
    static const float pans[NumDrums] = { 0, 0, 0, 0.25f, 0.25f, 0.25f, -0.3f, -0.3f, -0.35f, 0.2f, -0.05f, -0.3f };
    float lv = 1.0f;
    switch (drum)
    {
        case Kick: lv = p (dKickLv); break;
        case Snare: case Rim: lv = p (dSnareLv); break;
        case Hat: case HatOpen: case HatPedal: lv = p (dHatLv); break;
        case Ride: case Bell: case Crash: lv = p (dCymLv); break;
        default: lv = p (dTomLv); break;
    }
    slot->s = &layers[(size_t) li];
    slot->pos = 0;
    slot->inc = slot->s->rate / sampleRate;
    slot->gain = (kitNow == &synth ? vel : 0.35f + 0.65f * vel) * lv * 0.01f;
    slot->pan = pans[drum];
    slot->drum = drum;
    slot->delay = offset + (int) (rng.nextFloat() * hum * 0.012f * sampleRate);   // human feel: a few ms late now and then
}

void DrumsBlock::process (float* L, float* R, int n)
{
    auto& ctx = context();
    auto& bank = DrumBank::get();
    // kit and loop files (loaded in the background)
    const int kitIdx = (int) p (dKit);
    const auto kitName = kitIdx > 0 ? kitChoices[kitIdx] : juce::String();
    if (kitName != (recorded != nullptr ? recorded->name : juce::String()))
    {
        recorded = kitName.isEmpty() ? nullptr : bank.kit (kitName);
        if (recorded == nullptr && kitName.isNotEmpty() && wantKit != kitName) { wantKit = kitName; bank.requestKit (kitName); }
    }
    const int loopIdx = (int) p (dLoop);
    const bool loopMode = (int) p (dSource) == 1 && loopIdx > 0;
    if (loopMode)
    {
        const auto& path = loopPaths[loopIdx];
        if (userLoop == nullptr || userLoop->path != path)
        {
            userLoop = bank.loop (path);
            if (userLoop == nullptr && wantLoop != path) { wantLoop = path; bank.requestLoop (path); }
        }
    }

    // start / stop
    if (! primed) { primed = true; lastPlay = p (dPlay); lastFill = p (dFill); }
    const float play = p (dPlay);
    const int runs = (int) p (dRuns);
    bool startNow = false;
    if (play != lastPlay) { lastPlay = play; running = ! running; startNow = running; }
    if (runs == 1)
    {
        // follows MIDI Start / Stop (or Logic's play button)
        const bool r = ctx.running.load();
        startNow = r && ! running;
        if (ctx.starts.load() != lastStarts) { if (lastStarts >= 0 && r) startNow = true; lastStarts = ctx.starts.load(); }
        running = r;
    }
    if (runs == 2)
    {
        if (ctx.looperStarts.load() != lastLooperStarts) { if (lastLooperStarts >= 0) { running = true; startNow = true; } lastLooperStarts = ctx.looperStarts.load(); }
        if (ctx.looperStops.load() != lastLooperStops) { if (lastLooperStops >= 0) running = false; lastLooperStops = ctx.looperStops.load(); }
    }
    if (startNow) { beat = 0; lastStep = -1; loopPos = 0; fillBar = -1; }
    if (runs == 1 && ctx.hostBeats.load() >= 0.0) beat = ctx.hostBeats.load();

    const float fill = p (dFill);
    if (fill != lastFill) { lastFill = fill; if (running) { const int bar = (int) (beat / 4.0); fillBar = std::fmod (beat, 4.0) < 2.0 ? bar : bar + 1; } }

    const double bpm = std::max (30.0f, ctx.bpm.load());
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    const double b0 = beat, b1 = beat + beatsPerSample * n;

    if (running && ! loopMode)
    {
        const auto& g = grooves()[(size_t) juce::jlimit (0, (int) grooves().size() - 1, (int) p (dGroove))];
        const double stepBeats = 4.0 / g.grid;
        const int steps = g.grid * 2;
        const double patBeats = stepBeats * steps;
        const float swing = std::max (g.swing, p (dSwing) * 0.01f);
        const double p0 = std::fmod (b0, patBeats);
        for (int s = 0; s < steps; ++s)
        {
            double t = s * stepBeats;
            if (g.grid == 16 && (s & 1)) t += swing * stepBeats / 3.0;
            // the step's time, in this block's beat range (the pattern may wrap inside the block)
            double tb = b0 - p0 + t;
            if (tb < b0) tb += patBeats;
            if (tb >= b1) continue;
            const int off = (int) ((tb - b0) / beatsPerSample);
            const int bar = (int) std::floor (tb / 4.0 + 1e-9);
            const double inBar = tb - bar * 4.0;
            const bool inFill = bar == fillBar && inBar >= 2.0 - 1e-9;
            if (inFill)
            {
                // the last two beats: snare run, then down the toms
                const int fs = (int) std::lround ((inBar - 2.0) / stepBeats);
                const int half = g.grid / 2;
                const int drum = fs < half / 2 ? Snare : fs < half * 3 / 4 ? Tom1 : fs < half - 1 ? Tom2 : Tom3;
                if (g.grid == 12 || (s & 1) == 0 || fs >= half / 2) trigger (drum, fs == 0 ? 0.9f : 0.7f, off);
                if (fs == 0) trigger (Kick, 0.8f, off);
                continue;
            }
            const bool barAfterFill = fillBar >= 0 && bar == fillBar + 1 && inBar < 1e-6;
            const bool firstHit = (lastStep < 0 && tb < stepBeats * 0.5) || barAfterFill;
            for (auto& [drum, row] : g.rows)
            {
                const auto r = clean (row);
                const float v = velOf (r[(size_t) (s % (int) r.size())]);
                if (v > 0) trigger (drum, v, off);
            }
            if (firstHit) { trigger (Crash, 0.85f, off); trigger (Kick, 0.9f, off); if (barAfterFill) fillBar = -1; }
            lastStep = s;
        }
    }

    // render
    const float vol = p (dVolume) * 0.01f;
    for (auto& v : voices)
    {
        if (v.s == nullptr) continue;
        const int len = v.s->data.getNumSamples();
        const float* a = v.s->data.getReadPointer (0); const float* b = v.s->data.getReadPointer (1);
        const float gl = v.gain * vol * std::min (1.0f, 1.0f - v.pan), gr = v.gain * vol * std::min (1.0f, 1.0f + v.pan);
        for (int i = 0; i < n; ++i)
        {
            if (v.delay > 0) { --v.delay; continue; }
            const int i0 = (int) v.pos;
            if (i0 + 1 >= len) { v.s = nullptr; break; }
            const float fr = (float) (v.pos - i0);
            L[i] += (a[i0] + (a[i0 + 1] - a[i0]) * fr) * gl;
            R[i] += (b[i0] + (b[i0 + 1] - b[i0]) * fr) * gr;
            v.pos += v.inc;
        }
    }

    // your own drum loop
    if (running && loopMode && userLoop != nullptr)
    {
        const int len = userLoop->data.getNumSamples();
        const double seconds = len / userLoop->rate;
        const float* a = userLoop->data.getReadPointer (0); const float* b = userLoop->data.getReadPointer (1);
        const bool fit = p (dFit) > 0.5f;
        const int bars = std::max (1, (int) std::lround (seconds * bpm / 240.0));
        const double loopBeats = bars * 4.0;
        for (int i = 0; i < n; ++i)
        {
            double pos;
            if (fit) pos = std::fmod (beat + i * beatsPerSample, loopBeats) / loopBeats * len;   // locked to the beat
            else { pos = loopPos; loopPos += userLoop->rate / sampleRate; if (loopPos >= len) loopPos -= len; }
            const int i0 = (int) pos, i1 = (i0 + 1) % len;
            const float fr = (float) (pos - i0);
            L[i] += (a[i0] + (a[i1] - a[i0]) * fr) * vol;
            R[i] += (b[i0] + (b[i1] - b[i0]) * fr) * vol;
        }
    }
    if (running) beat = b1;
}
