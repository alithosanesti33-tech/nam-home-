// Guitar sims: 12-string and acoustic.
#include "BlockTypes.h"

using namespace ndsp;

//==============================================================================
// 12-string. A real 12-string pairs the four low strings with thin strings an octave up, and doubles
// the B and high E at the same pitch, a little out of tune with each other. The low register gets an
// octave-up copy (it can only see pitch, not strings, so it splits by frequency), a hair late like a
// pick crossing two strings; the upper register gets a slowly wandering unison copy.
namespace { enum TwP { tOct, tSplit, tShimmer, tSpread, tTone, tWidth, tMix, tLevel }; }

TwelveStringBlock::TwelveStringBlock() : Block (Type::TwelveString)
{
    addFloat ("octave", "Octave strings", 0, 100, 65, "%", -1, 1);
    addFloat ("split", "Octave range", 200, 900, 450, "Hz", 450, 1);
    addFloat ("shimmer", "Shimmer", 0, 100, 40, "%", -1, 1);
    addFloat ("spread", "Strum spread", 0, 20, 5, "ms", -1, 0.1f);
    addFloat ("tone", "Tone", -6, 12, 5, "dB", -1, 0.1f);
    addFloat ("width", "Width", 0, 100, 50, "%", -1, 1);
    addFloat ("mix", "Mix", 0, 100, 100, "%", -1, 1);
    addFloat ("level", "Level", -12, 6, -1.5f, "dB", -1, 0.1f);
}

void TwelveStringBlock::prepare (double sr, int)
{
    shift.prepare ((float) sr, 0.028f, 0.1f);
    octLine.setMaxSamples ((int) (0.03 * sr) + 8);
    uniLine.setMaxSamples ((int) (0.012 * sr) + 8);
    octHp.set (Biquad::Type::HighPass, (float) sr, 140.0f, 0.7f);
    octLp.set (Biquad::Type::LowPass, (float) sr, 9000.0f, 0.7f);
    lastSplit = -1; lastTone = -100;
    reset();
}

void TwelveStringBlock::reset()
{
    shift.clear(); octLine.clear(); uniLine.clear();
    for (auto& b : split) b.reset();
    octHp.reset(); octShelf.reset(); octLp.reset(); uniHp.reset();
}

void TwelveStringBlock::process (float* L, float* R, int n)
{
    const float sp = p (tSplit), tn = p (tTone);
    if (sp != lastSplit)
    {
        for (auto& b : split) b.set (Biquad::Type::LowPass, sampleRate, sp, 0.7071f);
        uniHp.set (Biquad::Type::HighPass, sampleRate, sp * 0.8f, 0.6f);
        lastSplit = sp;
    }
    if (tn != lastTone) { octShelf.set (Biquad::Type::HighShelf, sampleRate, 2500.0f, 0.7f, tn); lastTone = tn; }

    const float octG = p (tOct) * 0.012f;
    const float shim = p (tShimmer) * 0.01f;
    const float uniG = 0.55f * std::min (1.0f, shim * 4.0f);
    const float depth = (0.3f + 1.5f * shim) * 0.001f * sampleRate;   // samples of delay swing: a few cents of detune
    const float base = 0.0035f * sampleRate;
    const float spread = 1.0f + p (tSpread) * 0.001f * sampleRate;
    const float w = p (tWidth) * 0.01f, mix = p (tMix) * 0.01f, g = dbToGain (p (tLevel));
    const float inc[2] { 0.31f / sampleRate, 0.23f / sampleRate };

    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        const float lo = split[1].process (split[0].process (m));
        float o = shift.process (lo, 2.0f);
        o = octLp.process (octShelf.process (octHp.process (o)));
        octLine.push (o);
        const float oct = octLine.read (spread) * octG;

        uniLine.push (uniHp.process (m));
        float u[2];
        for (int k = 0; k < 2; ++k)
        {
            uniPh[k] += inc[k]; if (uniPh[k] >= 1.0f) uniPh[k] -= 1.0f;
            u[k] = uniLine.read (base + depth * (0.5f + 0.5f * std::sin (twoPi * uniPh[k]))) * uniG;
        }
        const float uc = 0.5f * (u[0] + u[1]);
        const float uL = uc + (u[0] - uc) * w, uR = uc + (u[1] - uc) * w;
        L[i] = (L[i] + mix * (oct * (1.0f - 0.5f * w) + uL)) * g;
        R[i] = (R[i] + mix * (oct * (1.0f + 0.5f * w) + uR)) * g;
    }
}

std::vector<Block::StartingPoint> TwelveStringBlock::startingPoints() const
{
    return {
        { "Rickenbacker jangle", "Byrds / Tom Petty chime. Clean amp, bridge or middle pickup, light compression before it.",
          { { "octave", 65 }, { "split", 450 }, { "shimmer", 30 }, { "spread", 3 }, { "tone", 7 }, { "width", 40 }, { "mix", 100 } } },
        { "Acoustic 12", "Big strummed 12-string. Put it before the Acoustic sim block for a full acoustic 12.",
          { { "octave", 75 }, { "split", 500 }, { "shimmer", 55 }, { "spread", 8 }, { "tone", 4 }, { "width", 70 }, { "mix", 100 } } },
        { "Wide studio 12", "Spread across the stereo field, like a double-tracked 12-string.",
          { { "octave", 70 }, { "split", 450 }, { "shimmer", 45 }, { "spread", 6 }, { "tone", 5 }, { "width", 100 }, { "mix", 100 } } },
    };
}

//==============================================================================
// Acoustic sim, after the Boss AC-3's four voices (Standard, Jumbo, Enhanced, Piezo) plus Nylon.
// An electric pickup is mid-heavy, peaks around 3 kHz and sustains for ages; an acoustic is scooped,
// boomy at the body's air resonance, airy on top, with a bright pick attack and a quicker decay.
// So: undo the pickup, add the body's resonances (and a little wooden ring), sharpen the attack,
// shorten the sustain, then put it in a room so it sounds like a miked guitar, not a DI.
namespace
{
    enum AcP { aType, aBody, aTop, aRoom, aSize, aLevel };
    struct Voice { float helmF, helmG, m2F, m2G, m3F, m3G, m4F, m4G, midF, midG, peakG, airG, quackG, lowG, bodyScale, lp; };
    const Voice voices[] = {
        // Standard (dreadnought)
        { 105, 7, 205, 4, 410, 2.5f, 1100, 1.5f, 650, -5, -4, 7, 0, 0, 1.0f, 14000 },
        // Jumbo
        { 92, 9, 185, 5, 380, 2, 900, 1, 700, -6, -4, 6, 0, 2, 1.15f, 13000 },
        // Enhanced (scooped and bright, for strumming in a band)
        { 100, 8, 210, 4, 420, 1, 1200, 1, 800, -8, -5, 9, 0, 1, 1.0f, 15000 },
        // Piezo (under-saddle pickup: less body, quacky)
        { 120, 2, 240, 1, 500, 1, 1400, 2, 600, -3, -2, 10, 5, 0, 0.7f, 15000 },
        // Nylon (classical)
        { 98, 7, 195, 4, 400, 2, 900, 1, 700, -5, -6, -2, 0, 1, 1.05f, 8000 },
    };
}

AcousticBlock::AcousticBlock() : Block (Type::Acoustic)
{
    addChoice ("type", "Type", { "Standard", "Jumbo", "Enhanced", "Piezo", "Nylon" }, 0);
    addFloat ("body", "Body", 0, 100, 50, "%", -1, 1);
    addFloat ("top", "Top", 0, 100, 50, "%", -1, 1);
    addFloat ("room", "Room", 0, 100, 30, "%", -1, 1);
    addFloat ("roomSize", "Room size", 0, 100, 40, "%", -1, 1);
    addFloat ("level", "Level", -12, 6, 0, "dB", -1, 0.1f);
}

void AcousticBlock::prepare (double sr, int)
{
    for (auto& d : body) d.setMaxSamples ((int) (0.01 * sr) + 8);
    for (auto& e : early) e.setMaxSamples ((int) (0.06 * sr) + 8);
    for (auto& d : roomLine) d.setMaxSamples ((int) (0.12 * sr) + 8);
    const int apLen[2][3] { { 142, 107, 379 }, { 151, 113, 397 } };
    for (int c = 0; c < 2; ++c) for (int k = 0; k < 3; ++k) roomAp[c][k].init ((int) (apLen[c][k] * sr / 48000.0));
    envFast.set ((float) sr, 0.5f, 20.0f);
    envSlow.set ((float) sr, 15.0f, 120.0f);
    attackHp.set (Biquad::Type::HighPass, (float) sr, 2500.0f, 0.7f);
    for (auto& f : roomHp) f.set (Biquad::Type::HighPass, (float) sr, 150.0f, 0.7f);
    lastKey = -1;
    reset();
}

void AcousticBlock::reset()
{
    for (auto& d : body) d.clear();
    for (auto& e : early) e.clear();
    for (auto& d : roomLine) d.clear();
    for (auto& c : roomAp) for (auto& a : c) a.clear();
    for (auto& z : bodyZ) z = 0;
    for (auto& z : roomZ) z = 0;
    hp.reset(); pickupMid.reset(); pickupPeak.reset(); air.reset(); quack.reset(); top.reset(); lowShelf.reset();
    for (auto& m : modes) m.reset();
    envFast.env = envSlow.env = 0; comp = 1; peakEnv = curEnv = 0;
}

void AcousticBlock::design()
{
    const auto& v = voices[juce::jlimit (0, 4, (int) p (aType))];
    const float sr = sampleRate;
    const float b = p (aBody) * 0.01f * 1.6f, t = p (aTop) * 0.01f;
    hp.set (Biquad::Type::HighPass, sr, 70.0f, 0.7f);
    pickupMid.set (Biquad::Type::Peak, sr, v.midF, 0.8f, v.midG);
    pickupPeak.set (Biquad::Type::Peak, sr, 3200.0f, 1.2f, v.peakG);
    modes[0].set (Biquad::Type::Peak, sr, v.helmF, 2.5f, v.helmG * b);
    modes[1].set (Biquad::Type::Peak, sr, v.m2F, 3.0f, v.m2G * b);
    modes[2].set (Biquad::Type::Peak, sr, v.m3F, 2.5f, v.m3G * b);
    modes[3].set (Biquad::Type::Peak, sr, v.m4F, 2.0f, v.m4G * b);
    quack.set (Biquad::Type::Peak, sr, 1400.0f, 1.5f, v.quackG);
    air.set (Biquad::Type::HighShelf, sr, 6000.0f, 0.7f, v.airG * (0.3f + 1.2f * t));
    top.set (Biquad::Type::LowPass, sr, v.lp, 0.7f);
    lowShelf.set (Biquad::Type::LowShelf, sr, 150.0f, 0.7f, v.lowG);
    const float ms[4] { 1.31f, 2.09f, 3.37f, 4.91f };
    for (int k = 0; k < 4; ++k) bodyLen[k] = ms[k] * v.bodyScale * 0.001f * sr;
    const float rs = 0.6f + 0.8f * p (aSize) * 0.01f;
    const float rms[4] { 29.7f, 37.1f, 41.3f, 53.9f };
    for (int k = 0; k < 4; ++k) roomLen[k] = rms[k] * rs * 0.001f * sr;
}

void AcousticBlock::process (float* L, float* R, int n)
{
    const float key = p (aType) * 1.0e6f + p (aBody) * 1.0e3f + p (aTop) + p (aSize) * 1.0e-3f;
    if (key != lastKey) { design(); lastKey = key; }

    const float t = p (aTop) * 0.01f, bodyAmt = p (aBody) * 0.01f;
    const float room = p (aRoom) * 0.01f, size = p (aSize) * 0.01f;
    const float g = dbToGain (p (aLevel)) * 0.85f;
    const float rt = 0.3f + 0.9f * size;   // seconds
    float fbG[4];
    for (int k = 0; k < 4; ++k) fbG[k] = std::pow (10.0f, -3.0f * roomLen[k] / (rt * sampleRate));
    const float dampC = 1.0f - std::exp (-twoPi * 6000.0f / sampleRate);
    const float bodyDamp = 1.0f - std::exp (-twoPi * 3000.0f / sampleRate);
    const float rs = 0.6f + 0.8f * size;
    static const float tapMs[2][5] { { 7.1f, 11.3f, 17.9f, 23.4f, 31.0f }, { 8.3f, 13.7f, 19.2f, 26.6f, 29.4f } };
    static const float tapG[5] { 0.7f, 0.55f, 0.45f, 0.38f, 0.3f };
    const float peakRel = std::exp (-1.0f / (0.6f * sampleRate)), peakAtt = std::exp (-1.0f / (0.001f * sampleRate));
    const float curRel = std::exp (-1.0f / (0.05f * sampleRate)), curAtt = std::exp (-1.0f / (0.005f * sampleRate));

    for (int i = 0; i < n; ++i)
    {
        float x = hp.process (0.5f * (L[i] + R[i]));
        x = pickupPeak.process (pickupMid.process (x));
        float y = x;
        for (auto& m : modes) y = m.process (y);
        y = lowShelf.process (quack.process (y));

        // a little wooden ring from the body
        float ring = 0;
        for (int k = 0; k < 4; ++k)
        {
            const float d = body[k].read (bodyLen[k]);
            bodyZ[k] += bodyDamp * (d - bodyZ[k]);
            body[k].push (y + 0.45f * bodyZ[k]);
            ring += (k & 1 ? -d : d);
        }
        y += ring * 0.08f * bodyAmt;

        // pick attack (an acoustic's bright "chick") and a quicker decay than an electric
        const float hi = attackHp.process (x);
        const float ef = envFast.process (x), es = envSlow.process (x);
        const float tr = clamp (ef / (es + 1.0e-5f) - 1.0f, 0.0f, 2.0f);
        y += hi * tr * t * 0.8f;
        const float ax = std::abs (x);
        peakEnv = ax > peakEnv ? peakAtt * peakEnv + (1 - peakAtt) * ax : peakRel * peakEnv + (1 - peakRel) * ax;
        curEnv = ax > curEnv ? curAtt * curEnv + (1 - curAtt) * ax : curRel * curEnv + (1 - curRel) * ax;
        const float decayG = clamp (std::pow ((curEnv + 1.0e-6f) / (peakEnv + 1.0e-6f), 0.35f), 0.3f, 1.0f);
        comp += 0.002f * (decayG - comp);
        y *= decayG;
        y = top.process (air.process (y)) * g;

        // the room: early reflections from the walls, then a short soft tail
        float oL = y, oR = y;
        if (room > 0.001f)
        {
            const float rin = roomHp[0].process (y);
            early[0].push (rin);
            float eL = 0, eR = 0;
            for (int k = 0; k < 5; ++k)
            {
                eL += early[0].read (tapMs[0][k] * rs * 0.001f * sampleRate) * tapG[k];
                eR += early[0].read (tapMs[1][k] * rs * 0.001f * sampleRate) * tapG[k];
            }
            float a = 0.5f * (eL + eR);
            for (int k = 0; k < 3; ++k) a = roomAp[0][k].process (a, 0.6f);
            float o[4];
            for (int k = 0; k < 4; ++k) o[k] = roomLine[k].read (roomLen[k]);
            const float h0 = o[0] + o[1] + o[2] + o[3], h1 = o[0] - o[1] + o[2] - o[3];
            const float h2 = o[0] + o[1] - o[2] - o[3], h3 = o[0] - o[1] - o[2] + o[3];
            const float h[4] { h0, h1, h2, h3 };
            for (int k = 0; k < 4; ++k)
            {
                roomZ[k] += dampC * (h[k] * 0.5f * fbG[k] - roomZ[k]);
                roomLine[k].push (roomZ[k] + a * 0.4f);
            }
            const float tL = o[0] + o[2], tR = o[1] + o[3];
            const float wL = eL * 0.45f + tL * 0.5f, wR = eR * 0.45f + tR * 0.5f;
            oL = y * (1.0f - 0.35f * room) + wL * room;
            oR = y * (1.0f - 0.35f * room) + wR * room;
        }
        L[i] = oL;
        R[i] = oR;
    }
}

std::vector<Block::StartingPoint> AcousticBlock::startingPoints() const
{
    return {
        { "Strummed dreadnought", "Neck pickup, clean, no amp block (or a very clean one). Strum lightly.",
          { { "type", 0 }, { "body", 55 }, { "top", 50 }, { "room", 30 }, { "roomSize", 40 } } },
        { "Big jumbo", "Boomy and full, for open chords on their own.",
          { { "type", 1 }, { "body", 65 }, { "top", 45 }, { "room", 35 }, { "roomSize", 50 } } },
        { "Band strummer", "Scooped and bright so it cuts through drums and bass.",
          { { "type", 2 }, { "body", 40 }, { "top", 60 }, { "room", 20 }, { "roomSize", 30 } } },
        { "Stage piezo", "The plugged-in acoustic sound of a live gig.",
          { { "type", 3 }, { "body", 40 }, { "top", 55 }, { "room", 10 }, { "roomSize", 30 } } },
        { "Nylon classical", "Soft and warm, for fingerpicking.",
          { { "type", 4 }, { "body", 55 }, { "top", 35 }, { "room", 40 }, { "roomSize", 50 } } },
    };
}
