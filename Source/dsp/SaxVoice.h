#pragma once
// Saxophone voice: a signal model of a reed on a conical bore.
// A conical bore driven by a beating reed produces a pressure pulse whose open fraction ("duty")
// is set by how much of the cone the mouthpiece cuts off; that is what gives each sax size its
// harmonic pattern, and the duty grows as notes go up the horn. Breath pressure opens the
// spectrum (soft = round and dark, hard = bright and edgy), turbulence noise rides on the reed
// pulses, and a fixed body/bell stage gives each horn its formants. Unlike a waveguide it speaks
// instantly and is always in tune, which matters when a guitar is driving it.
#include "Common.h"
#include <random>

namespace ndsp
{
struct SaxModel
{
    const char* name;
    float lowMidi, highMidi;     // written range, concert pitch
    float dutyRef;               // duty = f / (f + dutyRef): smaller = brighter low notes
    float f1, g1, q1, f2, g2, q2, f3, g3, q3;   // body formants (Hz, dB, Q)
    float bellHz;                // bell radiation high-pass
    float noiseHz;               // centre of the breath turbulence
    float brightMax;             // top of the spectrum at full breath, in harmonics
};

inline const SaxModel& saxModel (int i)
{
    static const SaxModel models[4] = {
        { "Soprano", 56, 88, 1150.0f,  900.0f, 4.0f, 1.2f, 2900.0f, 5.0f, 1.6f, 5200.0f, 2.0f, 2.0f, 260.0f, 5200.0f, 16.0f },
        { "Alto",    49, 80,  820.0f,  700.0f, 4.5f, 1.1f, 2600.0f, 5.5f, 1.5f, 4300.0f, 2.5f, 2.0f, 190.0f, 4300.0f, 20.0f },
        { "Tenor",   44, 76,  600.0f,  500.0f, 5.0f, 1.0f, 1700.0f, 4.0f, 1.4f, 2700.0f, 4.0f, 1.8f, 130.0f, 3400.0f, 22.0f },
        { "Baritone",37, 68,  420.0f,  330.0f, 5.5f, 0.9f, 1100.0f, 4.0f, 1.3f, 2200.0f, 3.5f, 1.6f,  80.0f, 2600.0f, 24.0f },
    };
    return models[std::max (0, std::min (3, i))];
}

class SaxVoice
{
public:
    struct Settings
    {
        int model = 1;
        int character = 0;          // 0 80s pop, 1 smooth, 2 R&B honk, 3 breathy ballad, 4 dark vintage
        float brightness = 0.65f;   // 0..1
        float breathNoise = 0.25f;  // 0..1
        float growl = 0.0f;         // 0..1
        float subtone = 0.0f;       // 0..1
        float body = 0.5f;          // 0..1 formant strength
        float keyClicks = 0.3f;     // 0..1
    };

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        reset();
        settings (st, true);
    }

    void reset()
    {
        phase = 0; lastFc = 0; fcCountdown = 0; lp1.reset(); lp2.reset(); noiseBp.reset(); click.reset(); dc = 0; dcIn = 0;
        for (auto* b : { &f1, &f2, &f3, &bell, &tone, &air }) b->reset();
        clickEnv = 0; growlPh = 0; growlRnd = 0; jitter = 0; shimmer = 0;
    }

    void settings (const Settings& s, bool force = false)
    {
        const bool modelChanged = force || s.model != st.model || s.character != st.character || std::abs (s.body - st.body) > 0.002f;
        st = s;
        if (! modelChanged) return;
        const auto& m = saxModel (st.model);
        const float bodyAmt = 0.3f + 1.4f * st.body;
        f1.set (Biquad::Type::Peak, sr, m.f1, m.q1, m.g1 * bodyAmt);
        f2.set (Biquad::Type::Peak, sr, m.f2, m.q2, m.g2 * bodyAmt);
        f3.set (Biquad::Type::Peak, sr, m.f3, m.q3, m.g3 * bodyAmt);
        bell.set (Biquad::Type::HighPass, sr, m.bellHz, 0.6f);
        noiseBp.set (Biquad::Type::BandPass, sr, m.noiseHz, 0.9f);
        click.set (Biquad::Type::BandPass, sr, 2600.0f, 2.5f);
        // character voicings
        switch (st.character)
        {
            case 0:  tone.set (Biquad::Type::Peak, sr, 3200.0f, 1.2f, 4.0f);  air.set (Biquad::Type::HighShelf, sr, 7000.0f, 0.7f, -2.0f); break;   // 80s pop: bite + presence
            case 1:  tone.set (Biquad::Type::Peak, sr, 2500.0f, 1.0f, -3.0f); air.set (Biquad::Type::HighShelf, sr, 5000.0f, 0.7f, -6.0f); break;  // smooth
            case 2:  tone.set (Biquad::Type::Peak, sr, 1800.0f, 1.4f, 5.0f);  air.set (Biquad::Type::HighShelf, sr, 6000.0f, 0.7f, -3.0f); break;  // honk
            case 3:  tone.set (Biquad::Type::Peak, sr, 900.0f, 0.8f, 2.0f);   air.set (Biquad::Type::HighShelf, sr, 4500.0f, 0.7f, -7.0f); break;  // breathy ballad
            default: tone.set (Biquad::Type::Peak, sr, 650.0f, 0.9f, 3.0f);   air.set (Biquad::Type::HighShelf, sr, 3500.0f, 0.7f, -10.0f); break; // dark vintage
        }
    }

    // Character presets also lean on breath and edge.
    float characterBreath() const { static const float b[5] = { 1.0f, 1.3f, 0.9f, 1.8f, 1.2f }; return b[std::max (0, std::min (4, st.character))]; }
    float characterEdge() const   { static const float e[5] = { 1.15f, 0.8f, 1.3f, 0.7f, 0.75f }; return e[std::max (0, std::min (4, st.character))]; }

    void keyClick (float strength) { clickEnv = std::max (clickEnv, strength); }

    // pitch in MIDI notes (fractional), breath 0..1 (0 = silent), returns one sample
    inline float tick (float midi, float breath)
    {
        const auto& m = saxModel (st.model);
        // tiny human pitch jitter and amplitude shimmer
        jitter += 0.0005f * (rnd() * 0.6f - jitter);
        shimmer += 0.002f * (rnd() * 0.5f - shimmer);
        const float hz = 440.0f * std::exp2 ((midi + jitter * 0.03f - 69.0f) / 12.0f);
        const float dt = clamp (hz / sr, 0.0f, 0.45f);

        const float b = clamp (breath, 0.0f, 1.2f);
        const float sub = st.subtone;
        // duty grows with pitch; subtone (soft, lower lip) pushes toward a rounder wave
        float duty = 0.08f + 0.24f * hz / (hz + m.dutyRef);
        duty = clamp (duty + sub * 0.08f, 0.06f, 0.36f);
        const float duty2 = std::min (0.45f, duty * 1.45f);

        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        // two reed pulses with different open fractions, so their spectral zeros do not line up:
        // a real bore is never a perfect cone, and the deep combs of a single pulse sound hollow
        const float pulse = 0.62f * blepPulse (phase, dt, duty) + 0.38f * blepPulse (phase, dt, duty2);

        // breath opens the spectrum: soft = 1.5 harmonics, full = brightMax harmonics
        const float edge = characterEdge() * (0.35f + 0.9f * st.brightness);
        const float harmonics = 1.4f + (m.brightMax - 1.4f) * std::pow (b, 1.6f) * edge * (1.0f - 0.65f * sub);
        const float fc = clamp (hz * harmonics, 200.0f, sr * 0.42f);
        if (--fcCountdown <= 0 && std::abs (fc - lastFc) > 1.0f) { lp1.set (Biquad::Type::LowPass, sr, fc, 0.62f); lp2.set (Biquad::Type::LowPass, sr, fc * 1.3f, 0.55f); lastFc = fc; fcCountdown = 16; }
        float x = lp2.process (lp1.process (pulse));

        // reed nonlinearity: hard blowing adds the rasp
        const float drive = 1.0f + 2.5f * b * b * characterEdge() + st.growl * 3.0f;
        x = std::tanh (x * drive) / std::tanh (drive);

        // growl: throat hum roughens the tone
        if (st.growl > 0.001f)
        {
            growlPh += 33.0f / sr; if (growlPh >= 1.0f) { growlPh -= 1.0f; growlRnd = rnd(); }
            const float hum = std::sin (6.2831853f * growlPh) * (0.7f + 0.3f * growlRnd);
            x *= 1.0f + st.growl * 0.55f * hum * b;
        }

        // breath turbulence, pulsed by the reed (louder while the reed is open)
        const float open = phase < duty2 ? 1.0f : 0.35f;
        const float breathAmt = st.breathNoise * characterBreath() * (0.25f + 0.75f * sub + 0.4f * (1.0f - b));
        float n = noiseBp.process (rnd()) * breathAmt * open * 0.9f;

        // key click on fingering changes
        float c = 0.0f;
        if (clickEnv > 0.0005f) { c = click.process (rnd()) * clickEnv * st.keyClicks * 0.5f; clickEnv *= clickDecay(); }

        // even out the horn: without this the top of the range is ~6 dB louder than the bottom
        const float even = std::pow (clamp (hz, 50.0f, 2000.0f) / 300.0f, -0.35f);
        float y = (x * (1.0f + shimmer * 0.04f)) * level (b) * even + n * std::sqrt (b) + c;
        y = bell.process (y);
        y = f3.process (f2.process (f1.process (y)));
        y = air.process (tone.process (y));
        // DC blocker
        dc = y - dcIn + 0.9995f * dc; dcIn = y;
        return dc;
    }

private:
    static inline float naivePulse (float p, float d) { return p < d ? 1.0f : 0.0f; }
    static inline float blepPulse (float p, float dt, float d)
    {
        float y = naivePulse (p, d) - d + polyBlep (p, dt);
        float t2 = p - d; if (t2 < 0.0f) t2 += 1.0f;
        return y - polyBlep (t2, dt);
    }
    static inline float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    // soft notes are quieter but not proportionally (a sax is never really "pianissimo")
    static inline float level (float b) { return std::pow (b, 0.75f) * 0.55f; }
    float clickDecay() const { return std::exp (-1.0f / (0.004f * sr)); }
    inline float rnd() { return dist (gen); }

    float sr = 48000.0f;
    Settings st;
    float phase = 0, lastFc = 0, dc = 0, dcIn = 0;
    int fcCountdown = 0;
    Biquad lp1, lp2, noiseBp, click, f1, f2, f3, bell, tone, air;
    float clickEnv = 0, growlPh = 0, growlRnd = 0, jitter = 0, shimmer = 0;
    std::minstd_rand gen { 12345 };
    std::uniform_real_distribution<float> dist { -1.0f, 1.0f };
};
} // namespace ndsp
