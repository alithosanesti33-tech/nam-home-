#pragma once
// Shared guitar front end for instrument blocks (Saxophone, PhatTrack).
// Wraps PhatTrack's NSDF pitch tracker and gate with the parts that decide how an instrument feels:
//  * a pre-filter that keeps the pick click and upper harmonics out of the pitch search,
//  * an octave guard and a 2-in-a-row rule for big jumps, so a pluck never makes a wrong-note blip,
//  * note-change events (legato, hammer-ons) separate from small moves (vibrato, bends),
//  * a slow "note centre" so instruments can tell your finger vibrato from the note itself,
//  * the picking level and a per-note velocity for dynamics.
#include "Common.h"
#include "../phat/PhatCore.h"

namespace ndsp
{
class GuitarTracker
{
public:
    struct Frame
    {
        bool open = false;        // a note is sounding
        bool attack = false;      // gate opened this sample (first pick after silence)
        bool repick = false;      // a new pick while already sounding
        bool released = false;    // gate closed this sample
        bool noteChange = false;  // pitch moved to a new note (legato / hammer-on / new string)
        bool valid = false;       // we have a confirmed pitch for this note
        float midi = 60.0f;       // fine pitch (follows vibrato and bends)
        float centre = 60.0f;     // note centre (vibrato removed)
        float level = 0.0f;       // picking level 0..1 (36 dB above the gate = 1)
        float velocity = 0.0f;    // level of this note's attack 0..1
        float vibrato = 0.0f;     // how much finger vibrato you are doing, 0..1
    };

    void prepare (double sr)
    {
        rate = (float) sr;
        tracker.prepare (sr);
        gate.prepare (sr);
        pre1.set (Biquad::Type::HighPass, rate, 55.0f, 0.6f);
        pre2.set (Biquad::Type::LowPass, rate, 1600.0f, 0.55f);
        envA = 1.0f - std::exp (-1.0f / (0.0003f * rate));
        envR = 1.0f - std::exp (-1.0f / (0.120f * rate));
        fineCoef = 1.0f - std::exp (-1.0f / (0.0015f * rate));
        centreCoef = 1.0f - std::exp (-1.0f / (0.120f * rate));
        vibCoef = 1.0f - std::exp (-1.0f / (0.150f * rate));
        velWindow = (int) (0.015f * rate);
        guardWindow = (int) (0.060f * rate);
        settle = (int) (0.003f * rate);
        releaseHold = (int) (0.06f * rate);
        reset();
    }

    void reset()
    {
        tracker.reset(); gate.reset(); pre1.reset(); pre2.reset();
        env = 0; fine = centre = accepted = 60.0f;
        have = false; noteValid = false;
        candidate = 0; candCount = 0; startN = 0; startRef = 0;
        sinceAttack = 1 << 30; velPeak = 0; vibEnergy = 0; lastHz = 0;
        histN = 0; noteActive = false; notePeak = 0; quietFor = 0;
    }

    void setThresholdDb (float db) { gate.setThresholdDb (db); gateDb = db; }

    Frame process (float x)
    {
        Frame f;
        const float a = std::fabs (x);
        env += (a > env ? envA : envR) * (a - env);
        const float envDb = gainToDb (env + 1.0e-9f);
        f.level = clamp ((envDb - gateDb) / 36.0f, 0.0f, 1.0f);

        const auto g = gate.process (x);
        // A note is "active" from a pick until it closes at the gate OR fades well below how
        // hard it was picked. Hum or hiss can keep the gate open forever, so the fade test is
        // what makes notes end (and the next pick a fresh attack) on a noisy input.
        const bool pick = g.opened || g.onset;
        if (pick)
        {
            if (noteActive && ! g.opened) f.repick = true; else f.attack = true;
            noteActive = true; notePeak = env; quietFor = 0;
            sinceAttack = 0; velPeak = 0;
            // the pitch is unknown again until the tracker confirms it, so an instrument never
            // plays the old note on the new attack
            noteValid = false; histN = 0; candCount = 0; startN = 0;
        }
        if (noteActive)
        {
            notePeak = std::max (notePeak, env);
            quietFor = env < notePeak * releaseRatio ? quietFor + 1 : 0;
            if (g.closed || ! g.open || quietFor > releaseHold) { noteActive = false; f.released = true; }
        }
        if (sinceAttack < (1 << 30)) ++sinceAttack;
        if (sinceAttack < velWindow) velPeak = std::max (velPeak, f.level);
        f.velocity = velPeak;

        phat::PitchTracker::Result r;
        // pitch is only read while a note is active: a dying string or hum never moves the pitch
        if (tracker.push (pre2.process (pre1.process (x)), g.trackerActive, r) && r.hz > 0.0f && noteActive)
            onEstimate (phat::hzToMidi (r.hz), true, f);

        // fine pitch follows the accepted pitch with ~1.5 ms smoothing (results arrive every ~0.5 ms)
        fine += fineCoef * (accepted - fine);
        centre += centreCoef * (fine - centre);
        const float dev = fine - centre;
        vibEnergy += vibCoef * (dev * dev - vibEnergy);
        f.vibrato = clamp (std::sqrt (vibEnergy) / 0.25f, 0.0f, 1.0f);   // 25 cents rms = full

        f.open = noteActive;
        f.valid = noteValid && have;
        f.midi = fine;
        f.centre = centre;
        return f;
    }

    float hz() const { return lastHz; }

private:
    void onEstimate (float m, bool open, Frame& f)
    {
        lastHz = phat::midiToHz (m);
        // median of the last 3 estimates removes single-frame outliers
        hist[histN % 3] = m; ++histN;
        float med = m;
        if (histN >= 3)
        {
            const float x0 = hist[0], x1 = hist[1], x2 = hist[2];
            med = std::max (std::min (x0, x1), std::min (std::max (x0, x1), x2));
        }

        if (! noteValid)
        {
            // first pitch of a new note. A pick's first few cycles very often read an octave (or a
            // fifth) low, and taking that first reading is what made every pick start with a low,
            // bassy blip. So the note only speaks once 3 readings in a row agree (about 1.5 ms more);
            // if the string never settles, it plays the median after 60 ms rather than not at all.
            if (sinceAttack < settle) return;   // the first ~3 ms after a pick are click, not pitch
            if (startN > 0 && std::fabs (m - startRef) < 0.4f) { startRef = 0.5f * (startRef + m); ++startN; }
            else { startRef = m; startN = 1; }
            if (startN >= 3 || (sinceAttack >= guardWindow && histN >= 3))
            {
                accept (startN >= 3 ? startRef : med, true, f);
                noteValid = open;
                startN = 0;
            }
            return;
        }

        const float jump = med - accepted;
        const float aj = std::fabs (jump);
        if (aj < 0.7f) { accepted = med; candCount = 0; return; }   // vibrato, bends, small moves

        // a big move: needs 2 estimates in a row that agree (about 1 ms), and an exact octave
        // jump during the attack needs 4, because that is the classic pluck error
        const bool octave = std::fabs (aj - 12.0f) < 0.6f || std::fabs (aj - 24.0f) < 0.6f;
        const int need = (octave && sinceAttack < guardWindow) ? 4 : 2;
        if (candCount > 0 && std::fabs (m - candidate) < 0.5f) ++candCount;
        else { candidate = m; candCount = 1; }
        if (candCount >= need) { accept (candidate, false, f); candCount = 0; }
    }

    void accept (float m, bool first, Frame& f)
    {
        const bool changed = have && std::fabs (m - accepted) >= 0.7f;
        accepted = m;
        if (first || ! have || changed) { fine = centre = m; vibEnergy = 0; }
        if (changed && ! first) f.noteChange = true;
        have = true;
    }

    static float gainToDb (float g) { return 20.0f * std::log10 (g); }

    phat::PitchTracker tracker;
    phat::GateTrigger gate;
    Biquad pre1, pre2;
    float rate = 48000.0f, gateDb = -52.0f;
    float env = 0, envA = 0.1f, envR = 0.001f;
    float fine = 60, centre = 60, accepted = 60, fineCoef = 0.01f, centreCoef = 0.001f, vibCoef = 0.001f, vibEnergy = 0;
    bool have = false, noteValid = false;
    float candidate = 0; int candCount = 0;
    float startRef = 0; int startN = 0;
    int sinceAttack = 1 << 30, velWindow = 720, guardWindow = 2880, settle = 96;
    float velPeak = 0, lastHz = 0;
    float hist[3] {}; int histN = 0;
    bool noteActive = false; float notePeak = 0; int quietFor = 0, releaseHold = 2880;
    float releaseRatio = 0.025f;   // -32 dB below the note's own peak ends it
};
} // namespace ndsp
