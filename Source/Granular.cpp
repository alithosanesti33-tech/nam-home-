// Granular, after the Hologram Microcosm and Chase Bliss pedals (Mood, Onward, Habit).
// Everything you play is recorded into a minute-long memory; little pieces ("grains") of it are
// played back: scattered (Clouds), repeated in time (Glitch), looped into a pad (Swell freeze),
// reversed or at half / double speed (Slip), or pulled from further back (Memory echo).
#include "BlockTypes.h"

using namespace ndsp;

namespace
{
    enum GrP { gType, gMix, gSize, gDensity, gPitch, gShimmer, gReverse, gSpread, gSmear, gTone, gFeedback,
               gHold, gSync, gPlayback, gChance, gRepeats, gSwell, gSustain, gWidth, gKillDry, gTrails };
    enum Mode { mClouds, mGlitch, mSwell, mSlip, mMemory };
    const char* modeNames[] = { "Clouds", "Glitch", "Swell freeze", "Slip / reverse", "Memory echo" };
}

GranularBlock::GranularBlock() : Block (Type::Granular)
{
    addChoice ("type", "Mode", { "Clouds", "Glitch", "Swell freeze", "Slip / reverse", "Memory echo" }, 0);
    addFloat ("mix", "Mix", 0, 100, 50, "%", -1, 1);
    addFloat ("size", "Size", 0, 100, 50, "%", -1, 1);
    auto& density = addFloat ("density", "Density", 0, 100, 50, "%", -1, 1);
    addFloat ("pitch", "Pitch", -24, 24, 0, "st", -1, 1);
    auto& shimmer = addFloat ("shimmer", "Octave up chance", 0, 100, 0, "%", -1, 1);
    auto& reverse = addFloat ("reverse", "Reverse chance", 0, 100, 0, "%", -1, 1);
    auto& spread = addFloat ("spread", "Spread", 0, 100, 40, "%", -1, 1);
    addFloat ("smear", "Smear", 0, 100, 30, "%", -1, 1);
    addFloat ("tone", "Tone", 1000, 16000, 8000, "Hz", 5000, 1);
    auto& fb = addFloat ("feedback", "Feedback", 0, 95, 20, "%", -1, 1);
    addToggle ("hold", "Hold", false);
    auto& sync = addChoice ("sync", "Sync", { "Off", "1/1", "1/2", "1/4 dot", "1/4", "1/4 trip", "1/8 dot", "1/8", "1/8 trip", "1/16" }, 7);
    auto& play = addChoice ("playback", "Playback", { "Reverse", "Half speed", "Half speed reverse", "Octave up", "Octave up reverse" }, 0);
    auto& chance = addFloat ("chance", "Auto glitch chance", 0, 100, 0, "%", -1, 1);
    auto& reps = addFloat ("repeats", "Repeats", 2, 16, 4, {}, -1, 1);
    auto& swell = addFloat ("swell", "Swell time", 20, 4000, 600, "ms", 600, 1);
    auto& sus = addFloat ("sustain", "Sustain", 1, 30, 30, "s", 8, 0.1f);
    addFloat ("width", "Width", 0, 100, 70, "%", -1, 1);
    addToggle ("killDry", "Kill dry (100% wet)", false);
    addToggle ("trails", "Trails", true);

    auto isM = [] (std::initializer_list<int> ms) { return [ms] (const Block& b) { const int t = b.params[0]->getInt(); for (int x : ms) if (x == t) return true; return false; }; };
    density.visible = spread.visible = isM ({ mClouds, mMemory });
    shimmer.visible = isM ({ mClouds, mSwell, mMemory });
    reverse.visible = isM ({ mClouds, mGlitch, mMemory });
    fb.visible = isM ({ mClouds, mSlip, mMemory });
    sync.visible = isM ({ mGlitch, mSlip });
    play.visible = isM ({ mSlip });
    chance.visible = reps.visible = isM ({ mGlitch });
    swell.visible = sus.visible = isM ({ mSwell });
}

juce::String GranularBlock::getSubtitle() const
{
    return juce::String (modeNames[juce::jlimit (0, 4, (int) p (gType))]) + (p (gHold) > 0.5f ? "  (held)" : "");
}

void GranularBlock::prepare (double sr, int)
{
    ringLen = (int) (62.0 * sr);
    ring.assign ((size_t) ringLen, 0.0f);
    for (auto& nb : note) nb.assign ((size_t) (0.7 * sr) + 16, 0.0f);
    const float apMs[2][4] { { 4.7f, 7.9f, 11.3f, 17.1f }, { 5.3f, 8.6f, 12.4f, 16.2f } };
    for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) ap[c][k].init ((int) (apMs[c][k] * 0.001 * sr));
    for (auto& h : wetHp) h.set (Biquad::Type::HighPass, (float) sr, 60.0f, 0.7f);
    envFast.set ((float) sr, 1.0f, 40.0f);
    envSlow.set ((float) sr, 40.0f, 400.0f);
    lastTone = -1;
    reset();
}

void GranularBlock::reset()
{
    for (auto& g : grains) g.on = false;
    std::fill (ring.begin(), ring.end(), 0.0f);
    now = 0; spawnWait = 0; fbSample = 0;
    glitching = false; repsLeft = 0; repWait = 0; beatWait = 0; dryDuck = 0;
    activeSlot = -1; noteLen[0] = noteLen[1] = 0;
    slotGain[0] = slotGain[1] = slotTarget[0] = slotTarget[1] = 0;
    capStart = -1; lastOnset = -1000000;
    envFast.env = envSlow.env = 0;
    for (auto& c : ap) for (auto& a : c) a.clear();
    for (auto& f : toneLp) f.reset();
    for (auto& f : wetHp) f.reset();
}

void GranularBlock::spawn (int source, double pos, float rate, float len, float fade, float pan, float amp)
{
    if (len < 16.0f) return;
    for (auto& g : grains)
        if (! g.on)
        {
            g.on = true; g.source = source; g.pos = pos; g.rate = rate; g.len = len; g.t = 0;
            g.fade = fade; g.amp = amp; g.kill = 1.0f;
            const float a = (clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * pi;   // equal power
            g.gl = std::cos (a) * 1.41421356f; g.gr = std::sin (a) * 1.41421356f;
            return;
        }
}

float GranularBlock::readRing (double a) const
{
    if (a > (double) (now - 3) || a < (double) (now - ringLen + 4) || a < 1.0) return 0.0f;
    const juce::int64 i = (juce::int64) std::floor (a);
    const float f = (float) (a - (double) i);
    auto at = [this] (juce::int64 k) { return ring[(size_t) (k % ringLen)]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

float GranularBlock::readNote (int slot, double pos) const
{
    if (pos < 0.0 || pos > (double) (noteLen[slot] - 2)) return 0.0f;
    const int i = (int) pos;
    const float f = (float) (pos - i);
    return note[slot][(size_t) i] + f * (note[slot][(size_t) i + 1] - note[slot][(size_t) i]);
}

void GranularBlock::process (float* L, float* R, int n)
{
    const int mode = juce::jlimit (0, 4, (int) p (gType));
    const float sr = sampleRate;
    const float mix = p (gMix) * 0.01f, sizeN = p (gSize) * 0.01f, dens = p (gDensity) * 0.01f;
    const float r0 = std::exp2 (p (gPitch) / 12.0f);
    const float shim = p (gShimmer) * 0.01f, rev = p (gReverse) * 0.01f, spr = p (gSpread) * 0.01f;
    const float smear = p (gSmear) * 0.01f, width = p (gWidth) * 0.01f;
    const float fb = (mode == mClouds || mode == mSlip || mode == mMemory) ? p (gFeedback) * 0.01f : 0.0f;
    const bool killDry = p (gKillDry) > 0.5f;
    const float bpm = context().bpm.load();
    holding = p (gHold) > 0.5f;
    const bool freezeWrite = holding && (mode == mClouds || mode == mSlip || mode == mMemory);
    const float syncMs = syncToMs ((int) p (gSync), bpm);

    const float tn = p (gTone);
    if (tn != lastTone) { for (auto& f : toneLp) f.set (Biquad::Type::LowPass, sr, tn, 0.7f); lastTone = tn; }

    // glitch: holding stutters the last slice for as long as you hold
    if (mode == mGlitch)
    {
        const float slice = syncMs > 0 ? syncMs * 0.001f * sr : 0.03f * std::pow (16.7f, sizeN) * sr;
        if (holding && ! lastHold) { glitching = true; sliceLen = slice; sliceStart = now - (juce::int64) slice - 4; repsLeft = -1; repWait = 0; }
        if (! holding && lastHold && repsLeft < 0) glitching = false;
    }
    else glitching = false;
    lastHold = holding;

    const float swellStep = 1.0f / (std::max (0.02f, p (gSwell) * 0.001f) * sr);
    const float susS = p (gSustain);
    const float susDec = susS >= 29.9f ? 1.0f : std::exp (-1.0f / (susS * sr));

    for (int i = 0; i < n; ++i)
    {
        const float in = 0.5f * (L[i] + R[i]);

        // ---- record (Hold freezes the memory in Clouds, Slip and Memory)
        if (! freezeWrite)
        {
            ring[(size_t) (now % ringLen)] = in + fb * fbSample;
            ++now;
        }

        // ---- spawn grains for the mode
        switch (mode)
        {
            case mClouds:
            {
                if ((spawnWait -= 1.0f) > 0) break;
                const float gps = 1.5f + 60.0f * dens * dens;
                spawnWait += sr / gps * (0.5f + rnd());
                const float len = 0.04f * std::pow (12.5f, sizeN) * sr * (0.7f + 0.6f * rnd());
                float r = r0;
                if (rnd() < shim) r *= 2.0f;
                if (rnd() < rev) r = -r;
                const double scatter = (0.01 + spr * 2.5) * sr * rnd();
                const double hi = (double) (now - 4) - scatter;
                double lo = hi - std::abs (r) * len;
                const double oldest = (double) (now - ringLen) + len + 16.0;
                const double shiftUp = std::max (0.0, oldest - lo);
                lo += shiftUp;
                const double start = r > 0 ? lo : lo + std::abs (r) * len;
                const float overlap = gps * len / sr;
                spawn (0, start, r, len, 0.5f, (rnd() * 2.0f - 1.0f) * width, 1.1f / std::sqrt (std::max (1.0f, overlap)));
                break;
            }
            case mGlitch:
            {
                if (! holding && p (gChance) > 0)
                    if ((beatWait -= 1.0f) <= 0)
                    {
                        beatWait += 60.0f / std::max (20.0f, bpm) * sr;
                        if (! glitching && rnd() < p (gChance) * 0.005f)
                        {
                            sliceLen = syncMs > 0 ? syncMs * 0.001f * sr : 0.03f * std::pow (16.7f, sizeN) * sr;
                            glitching = true; sliceStart = now - (juce::int64) sliceLen - 4;
                            repsLeft = (int) p (gRepeats); repWait = 0;
                        }
                    }
                if (! glitching) break;
                if ((repWait -= 1.0f) > 0) break;
                repWait += sliceLen;
                if (repsLeft == 0) { glitching = false; break; }
                if (repsLeft > 0) --repsLeft;
                float r = r0;
                if (rnd() < rev) r = -r;
                double lo = (double) sliceStart;
                const double hiLimit = (double) (now - 4);
                if (lo + std::abs (r) * sliceLen > hiLimit) lo = hiLimit - std::abs (r) * sliceLen;
                const double start = r > 0 ? lo : lo + std::abs (r) * sliceLen;
                spawn (0, start, r, sliceLen, 0.04f, ((repsLeft & 1) ? 0.3f : -0.3f) * width, 1.0f);
                break;
            }
            case mSwell:
            {
                const float ef = envFast.process (in), es = envSlow.process (in);
                if (! holding && ef > es * 1.8f && ef > 0.003f && now - lastOnset > (juce::int64) (0.12f * sr) && capStart < 0)
                {
                    lastOnset = now;
                    capStart = now + (juce::int64) (0.06f * sr);   // just after the pick attack
                    capLen = juce::jmin ((int) note[0].size() - 8, (int) ((0.12f + 0.48f * sizeN) * sr));
                }
                if (capStart >= 0 && now >= capStart + capLen)
                {
                    const int slot = activeSlot < 0 ? 0 : 1 - activeSlot;
                    for (auto& g : grains) if (g.on && g.source == slot + 1) g.on = false;
                    for (int k = 0; k < capLen; ++k) note[slot][(size_t) k] = ring[(size_t) ((capStart + k) % ringLen)];
                    noteLen[slot] = capLen;
                    if (activeSlot >= 0) slotTarget[activeSlot] = 0;
                    slotTarget[slot] = 1; slotGain[slot] = 0;
                    activeSlot = slot; capStart = -1; spawnWait = 0;
                }
                for (int s = 0; s < 2; ++s)
                {
                    if (s == activeSlot) slotTarget[s] *= susDec;
                    if (slotGain[s] < slotTarget[s]) slotGain[s] = std::min (slotTarget[s], slotGain[s] + swellStep);
                    else slotGain[s] = std::max (slotTarget[s], slotGain[s] - swellStep * 1.4f);
                }
                if (activeSlot < 0 || slotGain[activeSlot] + slotTarget[activeSlot] < 0.0005f) break;
                if ((spawnWait -= 1.0f) > 0) break;
                const float nl = (float) noteLen[activeSlot];
                float r = r0;
                if (rnd() < shim) r *= 2.0f;
                const float len = std::min (std::max (0.06f * sr, nl * 0.5f), (nl - 8.0f) / std::abs (r));
                spawnWait += len * 0.25f;
                const float room = nl - 4.0f - std::abs (r) * len;
                const double lo = 2.0 + rnd() * std::max (0.0f, room);
                spawn (activeSlot + 1, r > 0 ? lo : lo + std::abs (r) * len, r, len, 0.5f, (rnd() * 2.0f - 1.0f) * width * 0.6f, 0.5f);
                break;
            }
            case mSlip:
            {
                if ((spawnWait -= 1.0f) > 0) break;
                const float D = syncMs > 0 ? syncMs * 0.001f * sr : 0.15f * std::pow (13.3f, sizeN) * sr;
                spawnWait += D * 0.5f;
                static const float rates[] = { -1.0f, 0.5f, -0.5f, 2.0f, -2.0f };
                const float r = rates[juce::jlimit (0, 4, (int) p (gPlayback))] * r0;
                const double start = r < 0 ? (double) (now - 4) : (double) (now - 4) - (double) (r * D);
                slipSide = ! slipSide;
                spawn (0, start, r, D, 0.5f, (slipSide ? 0.5f : -0.5f) * width, 1.0f);
                break;
            }
            case mMemory:
            {
                if ((spawnWait -= 1.0f) > 0) break;
                const float mean = (0.4f + 6.0f * (1.0f - dens)) * sr;
                spawnWait += -std::log (std::max (1.0e-4f, rnd())) * mean;
                const float len = 0.25f * std::pow (12.0f, sizeN) * sr * (0.6f + 0.8f * rnd());
                float r = r0;
                if (rnd() < shim) r *= 2.0f;
                if (rnd() < rev) r = -r;
                const double have = (double) std::min<juce::int64> (now, (juce::int64) ringLen) - len * 2.0 - 16.0;
                const double need = std::abs (r) * len + 0.3 * sr;
                if (have < need) break;
                const double scan = std::min (have, (2.0 + spr * 58.0) * sr);
                const double ago = need + rnd() * std::max (0.0, scan - need);
                const double lo = (double) (now - 4) - ago;
                spawn (0, r > 0 ? lo : lo + std::abs (r) * len, r, len, 0.15f, (rnd() * 2.0f - 1.0f) * width, 0.8f);
                break;
            }
            default: break;
        }

        // ---- play the grains
        float wl = 0, wr = 0;
        for (auto& g : grains)
        {
            if (! g.on) continue;
            const float u = g.t / g.len;
            float w;
            if (g.fade >= 0.5f) { const float s = std::sin (pi * u); w = s * s; }
            else w = std::min (1.0f, std::min (u, 1.0f - u) / g.fade);
            float s = g.source == 0 ? readRing (g.pos) : readNote (g.source - 1, g.pos) * slotGain[g.source - 1];
            s *= w * g.amp;
            wl += s * g.gl; wr += s * g.gr;
            g.pos += g.rate;
            if ((g.t += 1.0f) >= g.len) g.on = false;
        }

        // ---- smear, tone
        if (smear > 0.001f)
        {
            float dl = wl, dr = wr;
            for (int k = 0; k < 4; ++k) { dl = ap[0][k].process (dl, 0.65f); dr = ap[1][k].process (dr, 0.65f); }
            wl += smear * (dl - wl); wr += smear * (dr - wr);
        }
        wl = wetHp[0].process (toneLp[0].process (wl));
        wr = wetHp[1].process (toneLp[1].process (wr));
        fbSample = fastTanh (0.5f * (wl + wr));

        // ---- out: the glitch replaces your playing while it stutters
        dryDuck += 0.01f * ((glitching ? 1.0f : 0.0f) - dryDuck);
        const float dryG = killDry ? 0.0f : 1.0f - dryDuck * mix;
        L[i] = L[i] * dryG + wl * mix;
        R[i] = R[i] * dryG + wr * mix;
    }
}

std::vector<Block::StartingPoint> GranularBlock::startingPoints() const
{
    return {
        { "Microcosm clouds", "Scattered grains of what you just played. Hold freezes the cloud so you can play over it.",
          { { "type", 0 }, { "mix", 50 }, { "size", 55 }, { "density", 55 }, { "spread", 40 }, { "smear", 45 }, { "feedback", 25 }, { "tone", 8000 }, { "pitch", 0 }, { "shimmer", 0 }, { "reverse", 15 } } },
        { "Shimmer clouds", "Clouds with octave-up grains, very ambient.",
          { { "type", 0 }, { "mix", 55 }, { "size", 70 }, { "density", 65 }, { "spread", 55 }, { "smear", 70 }, { "feedback", 35 }, { "tone", 9000 }, { "shimmer", 40 }, { "reverse", 0 } } },
        { "Stutter on hold", "Hold (or a footswitch) to repeat the last 1/16 in time. Mix 100 replaces your playing like a real stutter.",
          { { "type", 1 }, { "mix", 100 }, { "sync", 9 }, { "chance", 0 }, { "smear", 0 }, { "reverse", 0 } } },
        { "Random glitches", "Now and then it grabs a beat and stutters it by itself.",
          { { "type", 1 }, { "mix", 80 }, { "sync", 7 }, { "chance", 30 }, { "repeats", 4 }, { "reverse", 25 }, { "smear", 10 } } },
        { "Onward-style swell freeze", "Each note you pick swells into a sustained pad, replaced by the next one. Hold keeps the current one.",
          { { "type", 2 }, { "mix", 60 }, { "size", 50 }, { "swell", 700 }, { "sustain", 30 }, { "smear", 50 }, { "tone", 7500 }, { "shimmer", 0 } } },
        { "Mood reverse echo", "What you play comes back reversed underneath you.",
          { { "type", 3 }, { "playback", 0 }, { "sync", 0 }, { "size", 45 }, { "mix", 45 }, { "feedback", 30 }, { "smear", 30 } } },
        { "Half-speed ambient", "A slowed, octave-down echo of your playing, like tape at half speed.",
          { { "type", 3 }, { "playback", 1 }, { "sync", 0 }, { "size", 60 }, { "mix", 45 }, { "feedback", 35 }, { "smear", 55 }, { "tone", 6000 } } },
        { "Habit-style memory echo", "Remembers the last minute and brings old phrases back at random.",
          { { "type", 4 }, { "mix", 40 }, { "size", 45 }, { "density", 45 }, { "spread", 50 }, { "smear", 35 }, { "feedback", 15 }, { "reverse", 20 }, { "shimmer", 10 } } },
    };
}
