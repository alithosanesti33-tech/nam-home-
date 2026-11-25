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
    params.push_back (std::move (prm));
    return *params.back();
}

Param& Block::addChoice (const juce::String& pid, const juce::String& name, const juce::StringArray& choices, int def)
{
    auto prm = std::make_unique<Param>();
    prm->id = pid; prm->name = name; prm->kind = Param::Kind::Choice; prm->choices = choices;
    prm->range = juce::NormalisableRange<float> (0.0f, (float) juce::jmax (1, choices.size() - 1), 1.0f);
    prm->def = (float) def; prm->value = (float) def;
    params.push_back (std::move (prm));
    return *params.back();
}

Param& Block::addToggle (const juce::String& pid, const juce::String& name, bool def)
{
    auto prm = std::make_unique<Param>();
    prm->id = pid; prm->name = name; prm->kind = Param::Kind::Toggle;
    prm->range = juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f);
    prm->def = def ? 1.0f : 0.0f; prm->value = prm->def;
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

std::unique_ptr<Block> Block::fromVar (const juce::var& v)
{
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
        case Type::Modulation:   return "Modulation";
        case Type::TriChorus:    return "Tri-Chorus";
        case Type::Utility:      return "Utility";
        case Type::NumTypes:     break;
    }
    return {};
}

juce::String Block::categoryName (Type t)
{
    switch (t)
    {
        case Type::NamAmp: return "Amps";
        case Type::NamPedal: case Type::Drive: case Type::Wah: case Type::Volume: case Type::Compressor: return "Pedals";
        case Type::Cab: return "Cabs";
        case Type::GraphicEQ: case Type::ParametricEQ: return "EQ";
        case Type::Gate: case Type::Utility: return "Utility";
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
                                 return juce::Colour (0xffe0a030);
        case Type::GraphicEQ: case Type::ParametricEQ:
                                 return juce::Colour (0xff5dbb63);
        case Type::Gate: case Type::Utility:
                                 return juce::Colour (0xff8a8f98);
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
        case Type::Modulation:   return std::make_unique<ModulationBlock>();
        case Type::TriChorus:    return std::make_unique<TriChorusBlock>();
        case Type::Utility:      return std::make_unique<UtilityBlock>();
        case Type::NumTypes:     break;
    }
    return nullptr;
}
