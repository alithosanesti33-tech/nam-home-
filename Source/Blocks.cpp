#include "Blocks.h"
#include "BlockTypes.h"

juce::String Param::text() const
{
    const float v = get();
    if (kind == Kind::Choice) return choices[juce::jlimit (0, choices.size() - 1, getInt())];
    if (kind == Kind::Toggle) return on() ? "On" : "Off";
    if (unit == "Hz")
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    if (unit == "ms")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (unit == "s") return juce::String (v, v < 10.0f ? 2 : 1) + " s";
    if (unit == "%") return juce::String (juce::roundToInt (v)) + "%";
    if (unit == "dB") return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
    if (unit == "pan")
    {
        const int pc = juce::roundToInt (v * 100.0f);
        return pc == 0 ? juce::String ("C") : pc < 0 ? "L" + juce::String (-pc) : "R" + juce::String (pc);
    }
    if (unit == "x") return juce::String (v, 1) + ":1";
    if (unit.isEmpty()) return juce::String (v, std::abs (v) < 10.0f ? 1 : 0);
    return juce::String (v, std::abs (v) < 10.0f ? 2 : 1) + " " + unit;
}

//==============================================================================
EngineContext& Block::context()
{
    static EngineContext c;
    return c;
}

Block::Block (Type t) : type (t)
{
    static std::atomic<int> nextId { 1 };
    id = nextId++;
}

Param* Block::param (const juce::String& pid) const
{
    for (auto& prm : params)
        if (prm->id == pid) return prm.get();
    return nullptr;
}

Param& Block::addFloat (const juce::String& pid, const juce::String& name, float lo, float hi, float def,
                        const juce::String& unit, float centre, float step)
{
    auto prm = std::make_unique<Param>();
    prm->id = pid; prm->name = name; prm->unit = unit; prm->kind = Param::Kind::Float;
    prm->range = juce::NormalisableRange<float> (lo, hi, step);
    if (centre > lo && centre < hi) prm->range.setSkewForCentre (centre);
    prm->def = def;
    prm->value = def;
    prm->group = currentGroup;
    params.push_back (std::move (prm));
    return *params.back();
}

Param& Block::addChoice (const juce::String& pid, const juce::String& name, const juce::StringArray& choices, int def)
{
    auto prm = std::make_unique<Param>();
    prm->id = pid; prm->name = name; prm->kind = Param::Kind::Choice; prm->choices = choices;
    prm->range = juce::NormalisableRange<float> (0.0f, (float) juce::jmax (1, choices.size() - 1), 1.0f);
    prm->def = (float) def; prm->value = (float) def;
    prm->group = currentGroup;
    params.push_back (std::move (prm));
    return *params.back();
}

Param& Block::addToggle (const juce::String& pid, const juce::String& name, bool def)
{
    auto prm = std::make_unique<Param>();
    prm->id = pid; prm->name = name; prm->kind = Param::Kind::Toggle;
    prm->range = juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f);
    prm->def = def ? 1.0f : 0.0f; prm->value = prm->def;
    prm->group = currentGroup;
    params.push_back (std::move (prm));
    if (pid == "trails") trailsParam = params.back().get();
    return *params.back();
}

juce::String Block::getSubtitle() const
{
    for (auto& prm : params)
        if (prm->id == "type") return prm->text();
    return {};
}

void Block::prepareBlock (double sr, int maxBlockSize)
{
    sampleRate = (float) sr;
    maxBlock = juce::jmax (32, maxBlockSize);
    for (auto* v : { &dryL, &dryR, &zL, &zR }) v->assign ((size_t) maxBlock, 0.0f);
    prepare (sr, maxBlock);
}

void Block::run (float* L, float* R, int n)
{
    if (n > maxBlock) return;
    const bool en = enabled.load();
    const float target = en ? 1.0f : 0.0f;
    const bool trails = hasTails() && trailsParam != nullptr && trailsParam->on();

    if (fade == target)
    {
        if (en) { process (L, R, n); return; }
        if (! trails) return;
        // bypassed with trails: the effect hears silence, its tail keeps ringing out
        std::fill (zL.begin(), zL.begin() + n, 0.0f);
        std::fill (zR.begin(), zR.begin() + n, 0.0f);
        process (zL.data(), zR.data(), n);
        for (int i = 0; i < n; ++i) { L[i] += zL[(size_t) i]; R[i] += zR[(size_t) i]; }
        return;
    }

    // crossfade between bypassed and active (about 10 ms) so switching never clicks
    std::copy (L, L + n, dryL.begin());
    std::copy (R, R + n, dryR.begin());
    process (L, R, n);
    const float step = 1.0f / (0.010f * sampleRate);
    for (int i = 0; i < n; ++i)
    {
        fade = target > fade ? std::min (target, fade + step) : std::max (target, fade - step);
        L[i] = dryL[(size_t) i] + (L[i] - dryL[(size_t) i]) * fade;
        R[i] = dryR[(size_t) i] + (R[i] - dryR[(size_t) i]) * fade;
    }
}

//==============================================================================
juce::var Block::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", typeName (type));
    o->setProperty ("enabled", enabled.load());
    auto* ps = new juce::DynamicObject();
    for (auto& prm : params) ps->setProperty (prm->id, prm->get());
    o->setProperty ("params", juce::var (ps));
    o->setProperty ("extra", saveExtra());
    return juce::var (o);
}

// The old all-in-one "Modulation" block was replaced by dedicated blocks; old presets load into
// the matching new block with their settings carried over.
static juce::var migrateModulation (const juce::var& v)
{
    auto* ps = v.getProperty ("params", {}).getDynamicObject();
    auto get = [ps] (const char* id, float def) { return ps != nullptr && ps->hasProperty (id) ? (float) ps->getProperty (id) : def; };
    const int kind = (int) get ("type", 0);
    auto* np = new juce::DynamicObject();
    juce::String newType;
    switch (kind)
    {
        case 0: newType = "Chorus"; np->setProperty ("type", 0); np->setProperty ("rate", get ("rate", 0.8f)); np->setProperty ("depth", get ("depth", 50)); np->setProperty ("mix", get ("mix", 50)); np->setProperty ("width", get ("width", 70)); np->setProperty ("killDry", get ("killDry", 0)); break;
        case 1: newType = "Flanger"; np->setProperty ("type", 0); np->setProperty ("rate", get ("rate", 0.25f)); np->setProperty ("depth", get ("depth", 50)); np->setProperty ("feedback", get ("feedback", 0)); np->setProperty ("mix", get ("mix", 50)); np->setProperty ("killDry", get ("killDry", 0)); break;
        case 2: newType = "Phaser"; np->setProperty ("type", 2); np->setProperty ("rate", get ("rate", 0.5f)); np->setProperty ("depth", get ("depth", 50)); np->setProperty ("feedback", std::abs (get ("feedback", 0))); np->setProperty ("stages", get ("stages", 0)); np->setProperty ("width", get ("width", 70)); np->setProperty ("mix", get ("mix", 50)); break;
        case 3: newType = "Tremolo"; np->setProperty ("rate", get ("rate", 4)); np->setProperty ("sync", get ("sync", 0)); np->setProperty ("depth", get ("depth", 50)); np->setProperty ("wave", get ("wave", 0)); np->setProperty ("width", get ("width", 0)); break;
        case 4: newType = "Vibrato"; np->setProperty ("rate", get ("rate", 5)); np->setProperty ("sync", get ("sync", 0)); np->setProperty ("depth", get ("depth", 50)); break;
        case 5: newType = "Rotary"; np->setProperty ("fast", get ("speed", 0)); break;
        default: newType = "Vibe"; np->setProperty ("rate", get ("rate", 2)); np->setProperty ("sync", get ("sync", 0)); np->setProperty ("depth", get ("depth", 70)); np->setProperty ("mode", get ("mix", 50) >= 90 ? 1 : 0); break;
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", newType);
    o->setProperty ("enabled", v.getProperty ("enabled", true));
    o->setProperty ("params", juce::var (np));
    return juce::var (o);
}

std::unique_ptr<Block> Block::fromVar (const juce::var& vIn)
{
    const juce::var v = vIn.getProperty ("type", {}).toString() == "Modulation" ? migrateModulation (vIn) : vIn;
    const auto tn = v.getProperty ("type", {}).toString();
    for (auto t : allTypes())
    {
        if (typeName (t) != tn) continue;
        auto b = create (t);
        b->enabled = (bool) v.getProperty ("enabled", true);
        if (auto* ps = v.getProperty ("params", {}).getDynamicObject())
            for (auto& prm : b->params)
                if (ps->hasProperty (prm->id)) prm->set ((float) ps->getProperty (prm->id));
        b->loadExtra (v.getProperty ("extra", {}));
        return b;
    }
    return nullptr;
}

//==============================================================================
juce::Array<Block::Type> Block::allTypes()
{
    juce::Array<Type> a;
    for (int i = 0; i < (int) Type::NumTypes; ++i) a.add ((Type) i);
    return a;
}

juce::String Block::typeName (Type t)
{
    switch (t)
    {
        case Type::NamAmp:       return "NAM amp";
        case Type::NamPedal:     return "NAM pedal";
        case Type::Cab:          return "Cab";
        case Type::Gate:         return "Noise gate";
        case Type::Compressor:   return "Compressor";
        case Type::Drive:        return "Drive";
        case Type::Wah:          return "Wah";
        case Type::Volume:       return "Volume";
        case Type::GraphicEQ:    return "Graphic EQ";
        case Type::ParametricEQ: return "Parametric EQ";
        case Type::Delay:        return "Delay";
        case Type::Reverb:       return "Reverb";
        case Type::TriChorus:    return "Tri-Chorus";
        case Type::Utility:      return "Utility";
        case Type::PhatTrack:    return "PhatTrack";
        case Type::Sax:          return "Saxophone";
        case Type::Chorus:       return "Chorus";
        case Type::Phaser:       return "Phaser";
        case Type::Flanger:      return "Flanger";
        case Type::Whammy:       return "Whammy";
        case Type::Octave:       return "Octave";
        case Type::Harmonizer:   return "Harmonizer";
        case Type::EnvFilter:    return "Envelope filter";
        case Type::Swell:        return "Auto-swell";
        case Type::Doubler:      return "Doubler";
        case Type::Fuzz:         return "Fuzz";
        case Type::ATDrive:      return "@ Drive";
        case Type::Freeze:       return "Freeze";
        case Type::Tremolo:      return "Tremolo";
        case Type::Vibrato:      return "Vibrato";
        case Type::Rotary:       return "Rotary";
        case Type::Vibe:         return "Vibe";
        case Type::NumTypes:     break;
    }
    return {};
}

juce::String Block::categoryName (Type t)
{
    switch (t)
    {
        case Type::NamAmp: return "Amps";
        case Type::NamPedal: case Type::Drive: case Type::Wah: case Type::Volume: case Type::Compressor:
        case Type::Whammy: case Type::Octave: case Type::EnvFilter: case Type::Swell: case Type::Fuzz: case Type::ATDrive: return "Pedals";
        case Type::Cab: return "Cabs";
        case Type::GraphicEQ: case Type::ParametricEQ: return "EQ";
        case Type::Gate: case Type::Utility: return "Utility";
        case Type::PhatTrack: return "Synthesizers";
        case Type::Sax: return "Saxophone";
        default: return "Effects";
    }
}

juce::Colour Block::colour (Type t)
{
    switch (t)
    {
        case Type::NamAmp:       return juce::Colour (0xff2fb5a8);
        case Type::Cab:          return juce::Colour (0xff9b7be0);
        case Type::NamPedal: case Type::Drive: case Type::Wah: case Type::Volume: case Type::Compressor:
        case Type::Whammy: case Type::Octave: case Type::EnvFilter: case Type::Swell: case Type::Fuzz: case Type::ATDrive:
                                 return juce::Colour (0xffe0a030);
        case Type::GraphicEQ: case Type::ParametricEQ:
                                 return juce::Colour (0xff5dbb63);
        case Type::Gate: case Type::Utility:
                                 return juce::Colour (0xff8a8f98);
        case Type::PhatTrack:    return juce::Colour (0xffff7a59);
        case Type::Sax:          return juce::Colour (0xffe8c547);
        default:                 return juce::Colour (0xffe26ba8);
    }
}

std::unique_ptr<Block> Block::create (Type t)
{
    switch (t)
    {
        case Type::NamAmp:       return std::make_unique<NamBlock> (true);
        case Type::NamPedal:     return std::make_unique<NamBlock> (false);
        case Type::Cab:          return std::make_unique<CabBlock>();
        case Type::Gate:         return std::make_unique<GateBlock>();
        case Type::Compressor:   return std::make_unique<CompressorBlock>();
        case Type::Drive:        return std::make_unique<DriveBlock>();
        case Type::Wah:          return std::make_unique<WahBlock>();
        case Type::Volume:       return std::make_unique<VolumeBlock>();
        case Type::GraphicEQ:    return std::make_unique<GraphicEQBlock>();
        case Type::ParametricEQ: return std::make_unique<ParametricEQBlock>();
        case Type::Delay:        return std::make_unique<DelayBlock>();
        case Type::Reverb:       return std::make_unique<ReverbBlock>();
        case Type::TriChorus:    return std::make_unique<TriChorusBlock>();
        case Type::Utility:      return std::make_unique<UtilityBlock>();
        case Type::PhatTrack:    return std::make_unique<PhatTrackBlock>();
        case Type::Sax:          return std::make_unique<SaxBlock>();
        case Type::Chorus:       return std::make_unique<ChorusBlock>();
        case Type::Phaser:       return std::make_unique<PhaserBlock>();
        case Type::Flanger:      return std::make_unique<FlangerBlock>();
        case Type::Whammy:       return std::make_unique<WhammyBlock>();
        case Type::Octave:       return std::make_unique<OctaveBlock>();
        case Type::Harmonizer:   return std::make_unique<HarmonizerBlock>();
        case Type::EnvFilter:    return std::make_unique<EnvFilterBlock>();
        case Type::Swell:        return std::make_unique<SwellBlock>();
        case Type::Doubler:      return std::make_unique<DoublerBlock>();
        case Type::Fuzz:         return std::make_unique<FuzzBlock>();
        case Type::ATDrive:      return std::make_unique<ATDriveBlock>();
        case Type::Freeze:       return std::make_unique<FreezeBlock>();
        case Type::Tremolo:      return std::make_unique<TremoloBlock>();
        case Type::Vibrato:      return std::make_unique<VibratoBlock>();
        case Type::Rotary:       return std::make_unique<RotaryBlock>();
        case Type::Vibe:         return std::make_unique<VibeBlock>();
        case Type::NumTypes:     break;
    }
    return nullptr;
}
