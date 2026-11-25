#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>

// Small, allocation-free DSP building blocks used by the effects.
namespace ndsp
{
constexpr float pi = 3.14159265358979f;
constexpr float twoPi = 6.28318530717958f;

inline float dbToGain (float db) { return db <= -99.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return g <= 1.0e-5f ? -100.0f : 20.0f * std::log10 (g); }
inline float clamp (float x, float lo, float hi) { return std::min (hi, std::max (lo, x)); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

// Simple linear parameter smoother.
struct Smoothed
{
    void reset (float sr, float seconds = 0.03f) { steps = std::max (1, (int) (sr * seconds)); }
    void setTarget (float t)
    {
        if (t == target) return;
        target = t;
        if (! started) { current = t; started = true; left = 0; return; }
        left = steps;
        inc = (target - current) / (float) steps;
    }
    void snap (float v) { current = target = v; left = 0; started = true; }
    float next() { if (left > 0) { current += inc; if (--left == 0) current = target; } return current; }
    bool isSmoothing() const { return left > 0; }
    float current = 0, target = 0, inc = 0;
    int steps = 1000, left = 0;
    bool started = false;
};

// RBJ biquad.
struct Biquad
{
    enum class Type { HighPass, LowPass, BandPass, Peak, LowShelf, HighShelf, AllPass, Notch };
    void set (Type t, float sr, float freq, float q, float gainDb = 0.0f)
    {
        freq = clamp (freq, 10.0f, sr * 0.45f);
        q = std::max (0.05f, q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * 3.141592653589793 * freq / sr;
        const double cw = std::cos (w), sw = std::sin (w);
        const double al = sw / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case Type::HighPass:  B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = B0; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case Type::LowPass:   B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case Type::BandPass:  B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case Type::Notch:     B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case Type::AllPass:   B0 = 1 - al; B1 = -2 * cw; B2 = 1 + al; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            case Type::Peak:      B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; break;
            case Type::LowShelf:
            {
                const double s = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s; break;
            }
            case Type::HighShelf:
            {
                const double s = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s; break;
            }
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    // Magnitude response at a frequency (for drawing EQ curves).
    double magnitude (double freq, double sr) const
    {
        const double w = 2.0 * 3.141592653589793 * freq / sr;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// One-pole lowpass / highpass.
struct OnePole
{
    void setLowpass (float sr, float hz) { a = std::exp (-twoPi * clamp (hz, 1.0f, sr * 0.49f) / sr); }
    inline float lp (float x) { z = x + a * (z - x); return z; }
    inline float hp (float x) { return x - lp (x); }
    void reset() { z = 0; }
    float a = 0, z = 0;
};

// Topology-preserving state variable filter (Zavalishin). Stable under fast modulation (wah, phaser).
struct SVF
{
    void set (float sr, float hz, float q)
    {
        g = std::tan (pi * clamp (hz, 10.0f, sr * 0.48f) / sr);
        k = 1.0f / std::max (0.1f, q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void tick (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0; }
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
};

// Circular delay line with fractional (cubic) reads.
struct DelayLine
{
    void setMaxSamples (int n) { buf.assign ((size_t) n + 4, 0.0f); size = (int) buf.size(); w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; if (++w >= size) w = 0; }
    // read 'delay' samples behind the most recently pushed sample (delay >= 1)
    inline float read (float delay) const
    {
        delay = clamp (delay, 1.0f, (float) (size - 3));
        const float rp = (float) w - delay;
        float fl = std::floor (rp);
        const float f = rp - fl;
        int i = (int) fl;
        auto at = [this] (int k) { k %= size; if (k < 0) k += size; return buf[(size_t) k]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    inline float readInt (int delay) const
    {
        int i = w - std::max (1, std::min (delay, size - 1));
        if (i < 0) i += size;
        return buf[(size_t) i];
    }
    std::vector<float> buf;
    int size = 1, w = 0;
};

// Schroeder allpass (for diffusion).
struct Allpass
{
    void init (int len) { d.setMaxSamples (len + 8); length = (float) len; }
    inline float process (float x, float g, float mod = 0.0f)
    {
        const float delayed = d.read (length + mod);
        const float v = x + g * delayed;
        d.push (v);
        return delayed - g * v;
    }
    void clear() { d.clear(); }
    DelayLine d;
    float length = 1;
};

// Sine LFO with phase offset support.
struct LFO
{
    void setRate (float sr, float hz) { inc = hz / sr; }
    inline float sine (float offset = 0.0f) const { return std::sin (twoPi * (phase + offset)); }
    inline float tri (float offset = 0.0f) const { float p = phase + offset; p -= std::floor (p); return 4.0f * std::abs (p - 0.5f) - 1.0f; }
    inline void advance() { phase += inc; if (phase >= 1.0f) phase -= 1.0f; }
    float phase = 0, inc = 0;
};

// Smooth random wander (for tape wow and modulated reverbs).
struct Wander
{
    void setRate (float sr, float hz) { inc = hz / sr; }
    inline float next()
    {
        ph += inc;
        if (ph >= 1.0f) { ph -= 1.0f; a = b; b = rnd(); }
        const float t = ph * ph * (3 - 2 * ph);
        return a + (b - a) * t;
    }
    float rnd() { seed = seed * 1664525u + 1013904223u; return ((seed >> 9) * (1.0f / 8388608.0f)) * 2.0f - 1.0f; }
    uint32_t seed = 22222;
    float ph = 0, inc = 0, a = 0, b = 0;
};

// Peak envelope follower.
struct Envelope
{
    void set (float sr, float attackMs, float releaseMs)
    {
        at = std::exp (-1.0f / (0.001f * std::max (0.01f, attackMs) * sr));
        rt = std::exp (-1.0f / (0.001f * std::max (0.01f, releaseMs) * sr));
    }
    inline float process (float x)
    {
        x = std::abs (x);
        env = x > env ? at * env + (1 - at) * x : rt * env + (1 - rt) * x;
        return env;
    }
    float at = 0, rt = 0, env = 0;
};

// Note-value tempo sync.
inline float syncToMs (int choice, float bpm)
{
    // 0 = off, then 1/1, 1/2, 1/4., 1/4, 1/4T, 1/8., 1/8, 1/8T, 1/16
    static const float beats[] = { 0, 4.0f, 2.0f, 1.5f, 1.0f, 2.0f / 3.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f };
    if (choice <= 0 || choice > 9) return -1.0f;
    return 60000.0f / std::max (20.0f, bpm) * beats[choice];
}

inline float softClip (float x) { return std::tanh (x); }
inline float fastTanh (float x)
{
    x = clamp (x, -4.5f, 4.5f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
} // namespace ndsp
