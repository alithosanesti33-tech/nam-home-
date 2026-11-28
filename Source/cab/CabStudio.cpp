#include "cab/CabStudio.h"
#include <cmath>

using ndsp::Biquad;
using ndsp::dbToGain;

namespace
{
constexpr double irRate = cab::Library::rate;    // every IR is built at 48 kHz (the convolution resamples)
constexpr int irLength = 8192;                   // 170 ms: longer than any close-mic cab IR needs
constexpr int onsetAt = 24;                      // measured IRs are lined up with their first sound here
constexpr double inchesPerSecond = 13504.0;      // speed of sound
const juce::StringArray micChoices { "SM57", "MD421", "R-121", "U87", "As recorded" };   // "As recorded": whatever mic the pack used, untouched

cab::Mic micOf (int choice) { return choice >= 4 ? cab::Mic::Other : (cab::Mic) juce::jlimit (0, 3, choice); }

// how each mic colours the sound, roughly (used only when a pack has no IR from the mic you pick:
// the IR of another mic is turned into yours by removing its colour and adding yours)
struct Band { Biquad::Type t; float f, q, db; };
std::vector<Band> micColour (cab::Mic m)
{
    switch (m)
    {
        case cab::Mic::SM57:  return { { Biquad::Type::Peak, 5500, 1.0f, 4.0f }, { Biquad::Type::LowShelf, 200, 0.7f, -2.0f }, { Biquad::Type::HighShelf, 12000, 0.7f, -4.0f } };
        case cab::Mic::MD421: return { { Biquad::Type::Peak, 4000, 0.8f, 3.0f }, { Biquad::Type::LowShelf, 150, 0.7f, 1.5f }, { Biquad::Type::HighShelf, 13000, 0.7f, -2.0f } };
        case cab::Mic::R121:  return { { Biquad::Type::HighShelf, 5000, 0.6f, -6.0f }, { Biquad::Type::LowShelf, 180, 0.7f, 2.0f }, { Biquad::Type::Peak, 900, 0.8f, 1.0f } };
        case cab::Mic::U87:   return { { Biquad::Type::HighShelf, 8000, 0.7f, 2.0f }, { Biquad::Type::Peak, 3000, 0.7f, 1.0f }, { Biquad::Type::LowShelf, 100, 0.7f, 1.0f } };
        default:              return {};
    }
}

void filter (std::vector<float>& x, Biquad::Type t, float f, float q, float db)
{
    if (std::abs (db) < 0.01f && (t == Biquad::Type::Peak || t == Biquad::Type::LowShelf || t == Biquad::Type::HighShelf)) return;
    Biquad b; b.set (t, (float) irRate, f, q, db); b.reset();
    for (auto& s : x) s = b.process (s);
}

// the part of an IR that decides whether two mics cancel (below ~1.5 kHz)
std::vector<float> lowpassed (const std::vector<float>& x, int len)
{
    std::vector<float> y (x.begin(), x.begin() + std::min ((int) x.size(), len));
    filter (y, Biquad::Type::LowPass, 1500, 0.707f, 0);
    return y;
}

// correlation of a with b delayed by lag (positive lag: b later)
double corrAt (const std::vector<float>& a, const std::vector<float>& b, double lag, int len)
{
    double s = 0, ea = 0, eb = 0;
    const int li = (int) std::floor (lag); const double fr = lag - li;
    for (int i = 0; i < len && i < (int) a.size(); ++i)
    {
        const int j = i - li;
        auto at = [&b] (int k) { return k >= 0 && k < (int) b.size() ? (double) b[(size_t) k] : 0.0; };
        const double bv = at (j) * (1 - fr) + at (j - 1) * fr;
        s += a[(size_t) i] * bv; ea += (double) a[(size_t) i] * a[(size_t) i]; eb += bv * bv;
    }
    return ea > 0 && eb > 0 ? s / std::sqrt (ea * eb) : 0.0;
}

// the lag (within +-range samples, to a fraction of a sample) that lines b up with a; sign tells
// whether b is upside down
double bestLag (const std::vector<float>& a, const std::vector<float>& b, int range, int len, bool allowFlip, double& corr)
{
    int best = 0; double bc = -2;
    for (int lag = -range; lag <= range; ++lag)
    {
        const double c = corrAt (a, b, lag, len);
        const double score = allowFlip ? std::abs (c) : c;
        if (score > bc) { bc = score; best = lag; }
    }
    // refine between samples (parabola through the three best points)
    const double c0 = corrAt (a, b, best - 1, len), c1 = corrAt (a, b, best, len), c2 = corrAt (a, b, best + 1, len);
    const double sgn = allowFlip && c1 < 0 ? -1.0 : 1.0;
    const double den = sgn * (c0 - 2 * c1 + c2);
    double frac = den < -1e-12 ? 0.5 * sgn * (c0 - c2) / den : 0.0;
    frac = juce::jlimit (-0.5, 0.5, frac);
    corr = corrAt (a, b, best + frac, len);
    return best + frac;
}

int onset (const std::vector<float>& x)
{
    float peak = 0;
    for (auto s : x) peak = std::max (peak, std::abs (s));
    for (size_t i = 0; i < x.size(); ++i) if (std::abs (x[i]) >= 0.1f * peak) return (int) i;
    return 0;
}

// the measured facts of an IR, with what a pack usually means when it doesn't say
float knownR (const cab::Ir& i)     { return i.r >= 0 ? i.r : 1.0f; }
float knownDist (const cab::Ir& i)  { return i.dist >= 0 ? i.dist : 1.0f; }
float knownAngle (const cab::Ir& i) { return i.angle >= 0 ? i.angle : 0.0f; }

juce::String describe (const cab::Ir& i)
{
    juce::String s = i.mic == cab::Mic::Other ? (i.micLabel.isNotEmpty() ? i.micLabel : juce::String ("mic")) : juce::String (cab::micShort (i.mic));
    if (i.r >= 0) s << (i.r < 1 ? " cap" : i.r < 3 ? " cap edge" : i.r < 4.5 ? " cone" : " edge");
    if (i.dist >= 0) s << " " << juce::String (i.dist, i.dist < 10 && std::fmod (i.dist, 1.0f) != 0 ? 1 : 0) << " in";
    if (i.angle > 0) s << " " << juce::String ((int) i.angle) << juce::String::fromUTF8 ("°");
    return s;
}
}

//==============================================================================
class CabStudioBlock::Builder : public juce::Thread
{
public:
    explicit Builder (CabStudioBlock& b) : juce::Thread ("Cab Studio builder"), owner (b) { startThread (juce::Thread::Priority::low); }
    ~Builder() override { stopThread (3000); }
    void run() override
    {
        juce::String done;
        int libVersion = -1;
        while (! threadShouldExit())
        {
            const auto sig = owner.signature();
            const int v = cab::Library::get().version();
            if (sig != done || v != libVersion)
            {
                wait (40);   // let a knob turn settle before rebuilding
                if (owner.signature() != sig) continue;
                owner.build();
                done = sig; libVersion = v;
            }
            wait (30);
        }
    }
private:
    CabStudioBlock& owner;
};

//==============================================================================
CabStudioBlock::CabStudioBlock() : Block (Type::CabStudio)
{
    mode = &addChoice ("mode", "Mode", { "Cab", "Enhance only" }, 0);
    for (int k = 0; k < numMics; ++k)
    {
        const juce::String s = k == 0 ? "a" : "b", N = k == 0 ? "Mic A " : "Mic B ";
        currentGroup = k == 0 ? "Mic A" : "Mic B";
        auto full = [this] (const Block&) { return mode->getInt() == 0; };
        auto& m = mp[(size_t) k];
        m.model = &addChoice (s + "Model", N + "model", micChoices, k == 0 ? 0 : 2);
        m.spk   = &addChoice (s + "Spk", N + "speaker", { "1", "2", "3", "4" }, 0);
        m.pos   = &addFloat (s + "Pos", N + "position", 0, 6, k == 0 ? 1.0f : 2.5f, "in", 2.0f, 0.05f);
        m.dist  = &addFloat (s + "Dist", N + "distance", 0, 12, k == 0 ? 1.0f : 2.0f, "in", 3.0f, 0.05f);
        m.angle = &addFloat (s + "Angle", N + "angle", 0, 60, 0, juce::String::fromUTF8 ("°"), -1, 1);
        m.level = &addFloat (s + "Level", N + "level", -24, 6, 0, "dB", -1, 0.1f);
        m.pol   = &addToggle (s + "Pol", N + "polarity", false);
        m.on    = &addToggle (s + "On", N + "on", k == 0);
        for (auto* p : { m.model, m.spk, m.pos, m.dist, m.angle, m.level, m.pol, m.on }) p->visible = full;
        m.spk->visible = [this, full] (const Block& b) { if (! full (b)) return false; cab::Entry e; return cab::Library::get().find (getCab(), e) && e.speakers > 1; };
    }
    currentGroup = "Mix";
    auto full = [this] (const Block&) { return mode->getInt() == 0; };
    listen    = &addChoice ("listen", "Listen", { "A + B", "A only", "B only" }, 0);
    blend     = &addFloat ("blend", "A/B blend", 0, 1, 0.5f, "", -1, 0.01f);
    alignB    = &addFloat ("alignB", "B time", -2, 2, 0, "ms", -1, 0.01f);
    autoAlign = &addToggle ("autoAlign", "Auto phase align", true);
    for (auto* p : { listen, blend, alignB, autoAlign }) p->visible = full;
    currentGroup = "Cabinet";
    lowCut    = &addFloat ("lowCut", "Low cut", 20, 300, 20, "Hz", 80, 1);
    highCut   = &addFloat ("highCut", "High cut", 3000, 20000, 20000, "Hz", 8000, 10);
    reso      = &addFloat ("reso", "Resonance", -6, 6, 0, "dB", -1, 0.1f);
    character = &addFloat ("character", "Character", -1, 1, 0, "", -1, 0.01f);
    room      = &addFloat ("room", "Room", 0, 1, 0, "", -1, 0.01f);
    roomSize  = &addFloat ("roomSize", "Room size", 0, 1, 0.4f, "", -1, 0.01f);
    low       = &addFloat ("low", "Low", -12, 12, 0, "dB", -1, 0.1f);
    mid       = &addFloat ("mid", "Mid", -12, 12, 0, "dB", -1, 0.1f);
    high      = &addFloat ("high", "High", -12, 12, 0, "dB", -1, 0.1f);
    out       = &addFloat ("out", "Output", -24, 12, 0, "dB", -1, 0.1f);
    mono      = &addToggle ("mono", "Mono", false);
    roomSize->visible = [this] (const Block&) { return room->get() > 0.001f; };
    currentGroup = {};
    clock[0] = 300.0f; clock[1] = 60.0f;
    builder = std::make_unique<Builder> (*this);
}

CabStudioBlock::~CabStudioBlock() { builder.reset(); }

void CabStudioBlock::setCab (const juce::String& id) { std::lock_guard<std::mutex> l (lock); cabId = id; }
juce::String CabStudioBlock::getCab() const { std::lock_guard<std::mutex> l (lock); return cabId; }
void CabStudioBlock::setSpeakerType (const juce::String& s) { std::lock_guard<std::mutex> l (lock); speakerType = s; }
juce::String CabStudioBlock::getSpeakerType() const { std::lock_guard<std::mutex> l (lock); return speakerType; }
CabStudioBlock::Info CabStudioBlock::info() const { std::lock_guard<std::mutex> l (lock); return last; }

bool CabStudioBlock::seedFrom (const juce::File& ir1, const juce::File& ir2, float level1, float level2)
{
    cab::Entry e; cab::Ir a;
    if (! cab::Library::get().locate (ir1, e, a)) return false;
    setCab (e.id);
    setSpeakerType (a.speakerType);
    auto place = [this] (int k, const cab::Ir& ir, float level)
    {
        auto& m = mp[(size_t) k];
        m.on->set (1);
        m.model->set (4);   // as recorded: exactly the IR
        m.pos->set (knownR (ir)); m.dist->set (knownDist (ir)); m.angle->set (knownAngle (ir));
        m.level->set (level);
        m.pol->set (0);
    };
    place (0, a, level1);
    mp[1].on->set (0);
    if (ir2.existsAsFile())
    {
        cab::Entry e2; cab::Ir b;
        if (cab::Library::get().locate (ir2, e2, b) && e2.id == e.id) place (1, b, level2);
    }
    autoAlign->set (0);   // keep the two IRs' own timing, as the IR loader played them
    {
        std::lock_guard<std::mutex> l (lock);
        fromIr[0] = ir1.getFullPathName(); fromIr[1] = ir2.existsAsFile() ? ir2.getFullPathName() : juce::String();
    }
    return true;
}

juce::File CabStudioBlock::originalIr (int k) const
{
    std::lock_guard<std::mutex> l (lock);
    if (fromIr[k & 1].isNotEmpty()) return juce::File (fromIr[k & 1]);
    // not started from an IR: the measured IR the mic is nearest to
    return last.file[k & 1].isNotEmpty() ? cab::Library::get().fileFor (last.file[k & 1]) : juce::File();
}

juce::String CabStudioBlock::getSubtitle() const
{
    if (mode->getInt() == 1) return "Enhance only";
    const auto i = info();
    if (getCab().isEmpty()) return "Pick a cab";
    if (i.cabMissing) return "Cab not found";
    auto s = i.cabName;
    if (mp[1].on->on() && mp[0].on->on()) s << " - " << micChoices[mp[0].model->getInt()] << " + " << micChoices[mp[1].model->getInt()];
    return s;
}

juce::var CabStudioBlock::saveExtra() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("cab", getCab());
    o->setProperty ("speaker", getSpeakerType());
    o->setProperty ("clockA", (double) clock[0].load());
    o->setProperty ("clockB", (double) clock[1].load());
    {
        std::lock_guard<std::mutex> l (lock);
        o->setProperty ("fromIr1", fromIr[0]);
        o->setProperty ("fromIr2", fromIr[1]);
    }
    return juce::var (o);
}

void CabStudioBlock::loadExtra (const juce::var& v)
{
    setCab (v.getProperty ("cab", {}).toString());
    setSpeakerType (v.getProperty ("speaker", {}).toString());
    clock[0] = (float) (double) v.getProperty ("clockA", 300.0);
    clock[1] = (float) (double) v.getProperty ("clockB", 60.0);
    std::lock_guard<std::mutex> l (lock);
    fromIr[0] = v.getProperty ("fromIr1", {}).toString();
    fromIr[1] = v.getProperty ("fromIr2", {}).toString();
}

std::vector<Block::StartingPoint> CabStudioBlock::startingPoints() const
{
    return {
        { "Classic 57 + 121", "SM57 near the cap edge, ribbon on the cone, phase aligned", { { "aOn", 1 }, { "bOn", 1 }, { "aModel", 0 }, { "bModel", 2 }, { "aPos", 1.5f }, { "aDist", 1 }, { "bPos", 3.5f }, { "bDist", 2 }, { "blend", 0.4f }, { "autoAlign", 1 } } },
        { "Bright 57", "One SM57 on the cap: tight and cutting", { { "aOn", 1 }, { "bOn", 0 }, { "aModel", 0 }, { "aPos", 0.3f }, { "aDist", 0.5f }, { "aAngle", 0 } } },
        { "Dark ribbon", "R-121 on the cone a little back: smooth top end", { { "aOn", 1 }, { "bOn", 0 }, { "aModel", 2 }, { "aPos", 3.5f }, { "aDist", 4 } } },
        { "57 + 421 rock", "Two dynamics, one on the cap edge and one angled", { { "aOn", 1 }, { "bOn", 1 }, { "aModel", 0 }, { "bModel", 1 }, { "aPos", 1.8f }, { "bPos", 2.5f }, { "bAngle", 30 }, { "blend", 0.5f } } },
        { "Tweed open back", "U87 back a little with a touch of room, for open-back combos", { { "aOn", 1 }, { "bOn", 1 }, { "aModel", 0 }, { "bModel", 3 }, { "aPos", 2 }, { "aDist", 1 }, { "bPos", 3 }, { "bDist", 8 }, { "blend", 0.35f }, { "room", 0.2f }, { "lowCut", 70 } } },
    };
}

juce::String CabStudioBlock::signature() const
{
    juce::String s;
    {
        std::lock_guard<std::mutex> l (lock);
        s << cabId << "|" << speakerType << "|";
    }
    s << mode->getInt() << "|" << autoAlign->getInt() << "|" << alignB->get();
    for (auto& m : mp) s << "|" << m.model->getInt() << m.spk->getInt() << "," << m.pos->get() << "," << m.dist->get() << "," << m.angle->get() << "," << m.pol->getInt() << m.on->getInt();
    return s;
}

//==============================================================================
// Builds both mics' IRs from the library (background thread)
void CabStudioBlock::build()
{
    Info inf;
    {
        std::lock_guard<std::mutex> l (lock);
        inf.builds = last.builds + 1;
    }
    const auto id = getCab();
    cab::Entry e;
    const bool found = id.isNotEmpty() && cab::Library::get().find (id, e);
    inf.cabMissing = id.isNotEmpty() && ! found;
    inf.cabName = found ? e.name : juce::String();
    openBack = found && e.back == 2;
    const auto wantSpeaker = getSpeakerType();

    std::vector<float> built[numMics];
    int firstSound[numMics] { 0, 0 };
    for (int k = 0; k < numMics; ++k)
    {
        auto& m = mp[(size_t) k];
        if (! found || ! m.on->on() || mode->getInt() == 1) continue;
        const auto mic = micOf (m.model->getInt());
        const float r = m.pos->get(), dist = m.dist->get(), angle = m.angle->get();
        const int spk = m.spk->getInt();

        // the close-mic IRs of this cab (and of the chosen speaker type, when it has several)
        std::vector<const cab::Ir*> pool;
        for (auto& i : e.irs)
            if (! i.room && (wantSpeaker.isEmpty() || e.speakerTypes().size() < 2 || i.speakerType == wantSpeaker || (i.speakerType.isEmpty() && wantSpeaker == e.speaker)))
                pool.push_back (&i);
        if (pool.empty()) for (auto& i : e.irs) if (! i.room) pool.push_back (&i);
        if (pool.empty()) continue;
        // prefer IRs of this mic, and of this speaker when the pack says which speaker each IR is
        auto narrow = [&pool] (auto pred) { std::vector<const cab::Ir*> o; for (auto* i : pool) if (pred (*i)) o.push_back (i); if (! o.empty()) pool = o; return ! o.empty(); };
        const bool asRecorded = m.model->getInt() >= 4;
        const bool sameMic = asRecorded || narrow ([mic] (const cab::Ir& i) { return i.mic == mic; });
        const bool sameSpeaker = narrow ([spk] (const cab::Ir& i) { return i.speaker == spk; });
        juce::ignoreUnused (sameSpeaker);

        // nearest measured spots
        struct Near { const cab::Ir* ir; float d; };
        std::vector<Near> near;
        for (auto* i : pool)
        {
            const float dr = (knownR (*i) - r) / 2.5f, dd = (knownDist (*i) - dist) / 3.0f, da = (knownAngle (*i) - angle) / 30.0f;
            near.push_back ({ i, std::sqrt (dr * dr + dd * dd + da * da) });
        }
        std::sort (near.begin(), near.end(), [] (const Near& a, const Near& b) { return a.d < b.d; });
        if (near.size() > 3) near.resize (3);
        if (near[0].d < 0.04f) near.resize (1);   // standing on a measured spot: just that IR

        std::vector<float> acc ((size_t) irLength, 0.0f);
        float wsum = 0, avgR = 0, avgDist = 0, avgAngle = 0;
        int used = 0;
        std::vector<float> refLow;
        for (auto& nb : near)
        {
            auto a = cab::Library::get().audio (nb.ir->file);
            if (a == nullptr || a->empty()) continue;
            const float w = 1.0f / ((nb.d + 0.05f) * (nb.d + 0.05f));
            const int on = onset (*a);
            std::vector<float> x ((size_t) irLength, 0.0f);
            double energy = 0;
            for (int i = 0; i < irLength - onsetAt; ++i)
            {
                const int src = on + i;
                if (src >= (int) a->size()) break;
                x[(size_t) (onsetAt + i)] = (*a)[(size_t) src];
                energy += (double) x[(size_t) (onsetAt + i)] * x[(size_t) (onsetAt + i)];
            }
            const float g = energy > 0 ? (float) (1.0 / std::sqrt (energy)) : 0.0f;   // equal loudness before blending
            // IRs from nearby spots are lined up with the first one (and turned the same way up) before
            // they're mixed, so blending doesn't cancel the lows
            double lag = 0; float sign = 1;
            if (used == 0) refLow = lowpassed (x, 2048);
            else
            {
                double c = 0;
                lag = bestLag (refLow, lowpassed (x, 2048), 48, 2048, true, c);
                if (c < 0) sign = -1;
            }
            const int li = (int) std::floor (lag); const float fr = (float) (lag - li);
            for (int i = 0; i < irLength; ++i)
            {
                const int j = i - li;
                const float v0 = j >= 0 && j < irLength ? x[(size_t) j] : 0.0f, v1 = j - 1 >= 0 && j - 1 < irLength ? x[(size_t) (j - 1)] : 0.0f;
                acc[(size_t) i] += sign * w * g * (v0 * (1 - fr) + v1 * fr);
            }
            if (used == 0) { inf.file[k] = nb.ir->file; }
            wsum += w; avgR += w * knownR (*nb.ir); avgDist += w * knownDist (*nb.ir); avgAngle += w * knownAngle (*nb.ir);
            ++used;
        }
        if (used == 0 || wsum <= 0) continue;
        avgR /= wsum; avgDist /= wsum; avgAngle /= wsum;
        for (auto& s : acc) s /= wsum;

        // what wasn't measured, modelled on top
        juce::StringArray modelled;
        const float dR = r - avgR, dDist = dist - avgDist, dAngle = angle - avgAngle;
        if (std::abs (dR) > 0.2f)
        {
            // towards the edge the top end falls away, towards the cap it gets brighter
            filter (acc, Biquad::Type::HighShelf, 2500, 0.6f, juce::jlimit (-9.0f, 6.0f, -1.6f * dR));
            filter (acc, Biquad::Type::Peak, 1200, 0.8f, juce::jlimit (-2.0f, 2.0f, 0.3f * dR));
            modelled.add ("position");
        }
        if (std::abs (dDist) > 0.2f)
        {
            // further back: less proximity bass (more for the ribbon), a little less air
            const float prox = mic == cab::Mic::R121 ? 2.2f : mic == cab::Mic::U87 ? 1.2f : 1.6f;
            filter (acc, Biquad::Type::LowShelf, 160, 0.7f, juce::jlimit (-10.0f, 8.0f, -prox * dDist * (dDist > 0 ? 1.0f / (1.0f + 0.15f * dDist) : 1.0f)));
            filter (acc, Biquad::Type::HighShelf, 7000, 0.7f, juce::jlimit (-4.0f, 2.0f, -0.25f * dDist));
            modelled.add ("distance");
        }
        if (std::abs (dAngle) > 2.0f)
        {
            filter (acc, Biquad::Type::HighShelf, 4000, 0.6f, juce::jlimit (-8.0f, 4.0f, -0.12f * dAngle));
            filter (acc, Biquad::Type::Peak, 2500, 1.0f, juce::jlimit (-2.0f, 2.0f, -0.03f * dAngle));
            modelled.add ("angle");
        }
        if (! sameMic)
        {
            for (auto& b : micColour (near[0].ir->mic)) filter (acc, b.t, b.f, b.q, -b.db);
            for (auto& b : micColour (mic)) filter (acc, b.t, b.f, b.q, b.db);
            modelled.add ("mic");
        }
        if (e.speakers > 1 && near[0].ir->speaker != spk && spk > 0)
        {
            // no IR of this speaker: speakers in one cab differ a little
            const float f = 2400.0f * (1.0f + 0.06f * (float) spk), db = spk % 2 ? 0.8f : -0.6f;
            filter (acc, Biquad::Type::Peak, f, 1.5f, db);
            modelled.add ("speaker");
        }

        // time of flight from the speaker to the mic
        const double delay = dist / inchesPerSecond * irRate;
        std::vector<float> shifted ((size_t) irLength, 0.0f);
        const int di = (int) std::lround (delay); const float fr = 0.0f;   // whole samples: a fractional delay would dull the top end
        for (int i = 0; i < irLength; ++i)
        {
            const int j = i - di;
            const float a0 = j >= 0 && j < irLength ? acc[(size_t) j] : 0.0f, a1 = j - 1 >= 0 && j - 1 < irLength ? acc[(size_t) (j - 1)] : 0.0f;
            shifted[(size_t) i] = a0 * (1 - fr) + a1 * fr;
        }
        if (m.pol->on()) for (auto& s : shifted) s = -s;
        firstSound[k] = onsetAt + di;
        built[k] = std::move (shifted);

        inf.measured[k] = used == 1 && modelled.isEmpty();
        if (inf.measured[k]) inf.source[k] = "Measured: " + describe (*near[0].ir);
        else if (modelled.isEmpty()) inf.source[k] = "Blended from " + juce::String (used) + " measured IRs";
        else inf.source[k] = (used == 1 ? "From " + describe (*near[0].ir) : "Blended from " + juce::String (used) + " measured") + ", modelled " + modelled.joinIntoString (", ");
    }

    // line B up with A (or leave the natural time difference), plus your own B time. Auto align
    // lines up the low end (where two mics cancel) to a fraction of a sample, and turns B the right
    // way up if it came in upside down
    const bool both = ! built[0].empty() && ! built[1].empty();
    double shiftB = alignB->get() * 0.001 * irRate;
    float signB = 1;
    if (both)
    {
        const auto la = lowpassed (built[0], 4096), lb = lowpassed (built[1], 4096);
        double c = 0;
        double best = 0;
        if (autoAlign->on())
        {
            best = bestLag (la, lb, (int) (0.002 * irRate), 4096, true, c);
            if (c < 0) { signB = -1; inf.flippedB = true; }
        }
        shiftB += best;
        inf.alignSamples = (int) std::lround (shiftB);
        auto lbs = lb; for (auto& v : lbs) v *= signB;
        inf.phaseMatch = (float) juce::jlimit (0.0, 1.0, corrAt (la, lbs, shiftB, 4096));
    }
    // apply B's move; take off the delay both share so Cab Studio adds as little latency as it can
    const double startA = firstSound[0], startB = firstSound[1] + shiftB;
    double common = both ? std::min (startA, startB) : (! built[0].empty() ? startA : startB);
    common = std::max (0.0, std::floor (common) - 8);
    auto finish = [&] (int k, double move, float sign)
    {
        if (built[k].empty()) { hasIr[k] = false; return; }
        juce::AudioBuffer<float> b (1, irLength);
        b.clear();
        double energy = 0;
        const double off = common - move;
        const int oi = (int) std::floor (off); const float fr = (float) (off - oi);
        auto at = [&] (int j) { return j >= 0 && j < irLength ? built[k][(size_t) j] : 0.0f; };
        for (int i = 0; i < irLength; ++i)
        {
            float s = sign * (at (i + oi) * (1 - fr) + at (i + oi + 1) * fr);
            if (i > irLength - 256) s *= (float) (irLength - i) / 256.0f;   // fade the end
            b.setSample (0, i, s);
            energy += (double) s * s;
        }
        if (energy > 0) b.applyGain ((float) (0.125 / std::sqrt (energy)));   // the same loudness as the IR loader
        conv[k].loadImpulseResponse (std::move (b), irRate, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        hasIr[k] = true;
    };
    finish (0, 0.0, 1.0f);
    finish (1, shiftB, signB);

    std::lock_guard<std::mutex> l (lock);
    last = inf;
}

//==============================================================================
void CabStudioBlock::Room::prepare (double rate)
{
    static const float ms[4] = { 13.7f, 19.3f, 23.9f, 29.1f };
    for (int k = 0; k < 4; ++k)
    {
        len[k] = (int) (ms[k] * 0.001 * rate * 1.5) + 2;
        line[k].assign ((size_t) len[k], 0.0f);
        pos[k] = 0; damp[k] = 0;
    }
}

void CabStudioBlock::Room::process (float inL, float inR, float size, float& outL, float& outR)
{
    // a short, damped early-reflection room: the size scales how far apart the walls are
    float y[4];
    const float scale = 0.35f + 0.65f * size;
    for (int k = 0; k < 4; ++k)
    {
        const int d = std::max (1, (int) ((float) (len[k] - 2) * scale));
        int r = pos[k] - d; if (r < 0) r += len[k];
        y[k] = line[k][(size_t) r];
    }
    // Householder mix
    const float s = 0.5f * (y[0] + y[1] + y[2] + y[3]);
    const float fb = 0.35f + 0.3f * size;
    const float in[4] = { inL, inR, inL, inR };
    for (int k = 0; k < 4; ++k)
    {
        float v = (y[k] - s) * fb + in[k] * 0.5f;
        damp[k] += 0.45f * (v - damp[k]);   // walls soak up the top end
        line[k][(size_t) pos[k]] = damp[k];
        pos[k] = (pos[k] + 1) % len[k];
    }
    outL = 0.6f * (y[0] - y[2]) + 0.3f * y[1];
    outR = 0.6f * (y[1] - y[3]) + 0.3f * y[2];
}

void CabStudioBlock::prepare (double rate, int maxBlockSize)
{
    sr = rate;
    monoIn.setSize (1, maxBlockSize);
    micOut.setSize (numMics, maxBlockSize);
    const juce::dsp::ProcessSpec spec { rate, (juce::uint32) maxBlockSize, 1 };
    for (auto& c : conv) { c.prepare (spec); c.reset(); }
    gainA.reset ((float) rate); gainB.reset ((float) rate); outGain.reset ((float) rate);
    for (int c = 0; c < 2; ++c) for (auto* b : { &lc[c], &hc[c], &res[c], &tiltLo[c], &tiltHi[c], &eqLo[c], &eqMid[c], &eqHi[c] }) b->reset();
    roomFx.prepare (rate);
}

void CabStudioBlock::process (float* L, float* R, int n)
{
    const bool enhance = mode->getInt() == 1;
    if (! enhance)
    {
        const bool a = hasIr[0].load() && mp[0].on->on(), b = hasIr[1].load() && mp[1].on->on();
        if (! a && ! b) return;   // no cab yet: pass the signal through, like an empty IR loader
        float* m = monoIn.getWritePointer (0);
        for (int i = 0; i < n; ++i) m[i] = 0.5f * (L[i] + R[i]);
        for (int k = 0; k < numMics; ++k)
        {
            float* o = micOut.getWritePointer (k);
            if (! (k == 0 ? a : b)) { std::fill (o, o + n, 0.0f); continue; }
            std::copy (m, m + n, o);
            float* chans[1] { o };
            juce::dsp::AudioBlock<float> blk (chans, 1, (size_t) n);
            conv[k].process (juce::dsp::ProcessContextReplacing<float> (blk));
        }
        // blend: in the middle both mics at full level; towards a side the other mic fades out
        const float bl = blend->get();
        float wA = std::min (1.0f, 2.0f * (1.0f - bl)), wB = std::min (1.0f, 2.0f * bl);
        if (! b) { wA = 1; wB = 0; }
        if (! a) { wA = 0; wB = 1; }
        const int ls = listen->getInt();
        if (ls == OnlyA) { wA = a ? 1.0f : 0.0f; wB = 0; }
        if (ls == OnlyB) { wB = b ? 1.0f : 0.0f; wA = 0; }
        if (a && b && ls == Both) { const float norm = 1.0f / std::sqrt (wA * wA + wB * wB); wA *= norm; wB *= norm; }   // about the same loudness across the blend
        gainA.setTarget (wA * dbToGain (mp[0].level->get()) * dbToGain (-0.35f * (mp[0].dist->get() - 1.0f)));
        gainB.setTarget (wB * dbToGain (mp[1].level->get()) * dbToGain (-0.35f * (mp[1].dist->get() - 1.0f)));
        const float* oa = micOut.getReadPointer (0);
        const float* ob = micOut.getReadPointer (1);
        for (int i = 0; i < n; ++i) { const float s = oa[i] * gainA.next() + ob[i] * gainB.next(); L[i] = s; R[i] = s; }
    }

    // the cabinet controls
    const float sr_ = (float) sr;
    const float lcf = lowCut->get(), hcf = highCut->get(), rs = reso->get(), ch = character->get();
    const float rm = room->get(), rsz = roomSize->get();
    const bool open = openBack.load();
    for (int c = 0; c < 2; ++c)
    {
        lc[c].set (Biquad::Type::HighPass, sr_, lcf, 0.707f);
        hc[c].set (Biquad::Type::LowPass, sr_, hcf, 0.707f);
        res[c].set (Biquad::Type::Peak, sr_, open ? 90.0f : 110.0f, open ? 0.9f : 1.4f, rs);
        tiltLo[c].set (Biquad::Type::LowShelf, sr_, 350, 0.6f, -3.0f * ch);
        tiltHi[c].set (Biquad::Type::HighShelf, sr_, 2500, 0.6f, 3.0f * ch);
        eqLo[c].set (Biquad::Type::LowShelf, sr_, 120, 0.7f, low->get());
        eqMid[c].set (Biquad::Type::Peak, sr_, 800, 0.7f, mid->get());
        eqHi[c].set (Biquad::Type::HighShelf, sr_, 4500, 0.7f, high->get());
    }
    outGain.setTarget (dbToGain (out->get()));
    const bool toMono = mono->on();
    for (int i = 0; i < n; ++i)
    {
        float x[2] = { L[i], R[i] };
        for (int c = 0; c < 2; ++c)
        {
            float v = x[c];
            if (lcf > 21) v = lc[c].process (v);
            if (hcf < 19900) v = hc[c].process (v);
            if (std::abs (rs) > 0.05f) v = res[c].process (v);
            if (std::abs (ch) > 0.005f) v = tiltHi[c].process (tiltLo[c].process (v));
            v = eqHi[c].process (eqMid[c].process (eqLo[c].process (v)));
            x[c] = v;
        }
        if (rm > 0.001f)
        {
            float rl, rr;
            roomFx.process (x[0], x[1], rsz, rl, rr);
            x[0] += rm * 0.8f * rl; x[1] += rm * 0.8f * rr;
        }
        const float g = outGain.next();
        if (toMono) { const float s = 0.5f * (x[0] + x[1]); x[0] = x[1] = s; }
        L[i] = x[0] * g; R[i] = x[1] * g;
    }
}
