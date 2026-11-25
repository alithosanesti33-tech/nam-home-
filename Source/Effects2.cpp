// More effects: chorus, phaser and flanger models, whammy, polyphonic octave, harmonizer,
// envelope filter, auto-swell, doubler, fuzz, "@ Drive" and freeze.
#include "BlockTypes.h"

using namespace ndsp;

namespace
{
    inline float tri01 (float ph) { ph -= std::floor (ph); return ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f; }   // 0..1..0
    inline float sin01 (float ph) { return 0.5f + 0.5f * std::sin (twoPi * ph); }
    inline float msToSamples (float ms, float sr) { return ms * 0.001f * sr; }
    inline float apCoef (float hz, float sr) { const float t = std::tan (pi * clamp (hz, 20.0f, sr * 0.45f) / sr); return (t - 1.0f) / (t + 1.0f); }
}

//==============================================================================
// Chorus
ChorusBlock::ChorusBlock() : Block (Type::Chorus)
{
    addChoice ("type", "Type", { "Analog", "Dimension", "80s rack", "Ensemble", "Detune" }, 0);
    auto& rate = addFloat ("rate", "Rate", 0.05f, 8, 0.8f, "Hz", 1, 0.01f);
    auto& depth = addFloat ("depth", "Depth", 0, 100, 50, "%", -1, 1);
    auto& mode = addChoice ("mode", "Mode", { "1", "2", "3", "4", "1 + 4" }, 1);
    auto& det = addFloat ("detune", "Detune", 1, 25, 8, "cents", -1, 0.1f);
    addFloat ("tone", "Tone", 1500, 16000, 7000, "Hz", 5000, 1);
    addFloat ("width", "Width", 0, 100, 70, "%", -1, 1);
    addFloat ("mix", "Mix", 0, 100, 50, "%", -1, 1);
    addToggle ("killDry", "Kill dry (100% wet)", false);
    auto isT = [] (std::initializer_list<int> ts) { return [ts] (const Block& b) { const int t = b.params[0]->getInt(); for (int x : ts) if (x == t) return true; return false; }; };
    rate.visible = depth.visible = isT ({ 0, 2, 3 });
    mode.visible = isT ({ 1 });
    det.visible = isT ({ 4 });
}

void ChorusBlock::prepare (double sr, int)
{
    for (auto& l : line) l.setMaxSamples ((int) (0.06 * sr));
    for (int i = 0; i < 4; ++i) { wander[i].seed = 1234u + 777u * (uint32_t) i; wander[i].setRate ((float) sr, 0.35f + 0.17f * (float) i); }
    reset();
}

void ChorusBlock::reset()
{
    for (auto& l : line) l.clear();
    for (auto& f : bbdLp) f.reset();
    for (auto& f : preHp) f.reset();
    for (float& x : ph) x = 0;
}

void ChorusBlock::process (float* L, float* R, int n)
{
    const int t = (int) p (0);
    const float rate = p (1), depth = p (2) * 0.01f, tone = p (5), width = p (6) * 0.01f, mix = p (7) * 0.01f;
    const bool killDry = p (8) > 0.5f;
    const float sr = sampleRate;
    for (int c = 0; c < 2; ++c)
    {
        bbdLp[c].set (Biquad::Type::LowPass, sr, t == 0 ? std::min (tone, 9000.0f) : tone, 0.6f);
        preHp[c].set (Biquad::Type::HighPass, sr, t == 2 ? 120.0f : 60.0f, 0.7f);
    }
    // Dimension modes: fixed slow rates and shallow depths, like the four buttons
    static const float dimRate[5] = { 0.25f, 0.25f, 0.5f, 0.5f, 0.5f }, dimDepth[5] = { 0.35f, 0.55f, 0.55f, 0.8f, 1.0f };
    const int dm = (int) p (3);
    const float detCents = p (4);
    const float wet = mix, dry = killDry ? 0.0f : 1.0f - 0.5f * mix;

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        const float mono = 0.5f * (inL + inR);
        line[0].push (preHp[0].process (inL));
        line[1].push (preHp[1].process (inR));
        float wl = 0, wr = 0;
        switch (t)
        {
            case 0:   // analog BBD: one voice, triangle LFO, dark, stereo by phase offset
            {
                ph[0] += rate / sr; if (ph[0] >= 1) ph[0] -= 1;
                const float base = msToSamples (7.0f, sr), sw = msToSamples (4.5f, sr) * depth;
                wl = line[0].read (base + sw * (tri01 (ph[0]) - 0.5f));
                wr = line[1].read (base + sw * (tri01 (ph[0] + 0.5f * width) - 0.5f));
                wl = bbdLp[0].process (wl); wr = bbdLp[1].process (wr);
                break;
            }
            case 1:   // Dimension: two voices in antiphase, cross-mixed inverted: wide, no obvious wobble
            {
                ph[0] += dimRate[dm] / sr; if (ph[0] >= 1) ph[0] -= 1;
                const float base = msToSamples (8.0f, sr), sw = msToSamples (2.2f, sr) * dimDepth[dm];
                const float s = std::sin (twoPi * ph[0]);
                const float a = line[0].read (base + sw * s), b = line[1].read (base - sw * s);
                wl = a - 0.45f * b * width; wr = b - 0.45f * a * width;
                wl = bbdLp[0].process (wl); wr = bbdLp[1].process (wr);
                break;
            }
            case 2:   // 80s rack: bright stereo, triangle, 90 degrees apart, a little pre-emphasis
            {
                ph[0] += rate / sr; if (ph[0] >= 1) ph[0] -= 1;
                const float base = msToSamples (12.0f, sr), sw = msToSamples (6.0f, sr) * depth;
                wl = line[0].read (base + sw * (tri01 (ph[0]) - 0.5f));
                wr = line[1].read (base + sw * (tri01 (ph[0] + 0.25f + 0.25f * width) - 0.5f));
                wl = bbdLp[0].process (wl); wr = bbdLp[1].process (wr);
                break;
            }
            case 3:   // ensemble: three voices 120 degrees apart, slow sweep plus a faster shimmer
            {
                ph[0] += rate * 0.75f / sr; if (ph[0] >= 1) ph[0] -= 1;
                ph[1] += 6.2f / sr; if (ph[1] >= 1) ph[1] -= 1;
                const float base = msToSamples (10.0f, sr), sw = msToSamples (5.0f, sr) * depth, sf = msToSamples (0.35f, sr) * depth;
                float v[3];
                for (int k = 0; k < 3; ++k)
                {
                    const float o = (float) k / 3.0f;
                    const float d = base + sw * (std::sin (twoPi * (ph[0] + o))) * 0.5f + sf * std::sin (twoPi * (ph[1] + o));
                    v[k] = line[k == 1 ? 1 : 0].read (d);
                }
                wl = bbdLp[0].process (v[0] + 0.5f * v[1] * (1 - width));
                wr = bbdLp[1].process (v[2] + 0.5f * v[1] * (1 - width));
                wl *= 0.8f; wr *= 0.8f;
                break;
            }
            default:  // detune: two slowly drifting voices, a few cents apart, no audible wobble
            {
                const float cents = detCents;
                const float base = msToSamples (11.0f, sr);
                // a delay moving at 'speed' samples/sample shifts pitch by -speed; ramp gently between random targets
                const float amp = msToSamples (3.0f, sr) * (cents / 10.0f);
                wl = line[0].read (base + amp * wander[0].next());
                wr = line[1].read (base * 1.3f + amp * wander[1].next());
                wl = bbdLp[0].process (wl); wr = bbdLp[1].process (wr);
                juce::ignoreUnused (mono);
                break;
            }
        }
        L[i] = inL * dry + wl * wet;
        R[i] = inR * dry + wr * wet;
    }
}

//==============================================================================
// Phaser
PhaserBlock::PhaserBlock() : Block (Type::Phaser)
{
    addChoice ("type", "Type", { "Orange 4-stage", "Script", "Studio stereo" }, 0);
    addFloat ("rate", "Rate", 0.05f, 10, 0.5f, "Hz", 1, 0.01f);
    addFloat ("depth", "Depth", 0, 100, 85, "%", -1, 1);
    auto& fb = addFloat ("feedback", "Feedback", 0, 90, 50, "%", -1, 1);
    auto& st = addChoice ("stages", "Stages", { "4", "6", "8", "12" }, 2);
    auto& w = addFloat ("width", "Width", 0, 100, 60, "%", -1, 1);
    addFloat ("mix", "Mix", 0, 100, 50, "%", -1, 1);
    auto studio = [] (const Block& b) { return b.params[0]->getInt() == 2; };
    fb.visible = st.visible = w.visible = studio;
}

void PhaserBlock::reset() { for (auto& c : ap) for (auto& a : c) a.z = 0; fb[0] = fb[1] = 0; ph = 0; }

void PhaserBlock::process (float* L, float* R, int n)
{
    const int t = (int) p (0);
    const float rate = p (1), depth = p (2) * 0.01f, mix = p (6) * 0.01f;
    static const int stageCount[4] = { 4, 6, 8, 12 };
    const int stages = t == 2 ? stageCount[(int) p (4)] : 4;
    const float feedback = t == 0 ? 0.35f : (t == 1 ? 0.0f : p (3) * 0.01f);
    const float width = t == 2 ? p (5) * 0.01f : 0.0f;
    // sweep range: the orange pedal sweeps ~150 Hz..1.8 kHz, the studio phaser wider
    const float lo = t == 2 ? 120.0f : 150.0f, hi = t == 2 ? 3500.0f : 1800.0f;
    const float sr = sampleRate;
    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        ph += rate / sr; if (ph >= 1) ph -= 1;
        for (int c = 0; c < 2; ++c)
        {
            // exponential sweep, like an LDR/JFET phaser (spends time low, zips up)
            const float lfo = t == 1 ? sin01 (ph + 0.25f * width * (float) c) : tri01 (ph + 0.25f * width * (float) c);
            const float f = lo * std::pow (hi / lo, depth * lfo + (1.0f - depth) * 0.35f);
            const float a = apCoef (f, sr);
            float x = ch[c][i] + fb[c] * feedback;
            for (int s = 0; s < stages; ++s) x = ap[c][s].process (x, a);
            fb[c] = fastTanh (x);
            ch[c][i] = (ch[c][i] * (1.0f - mix) + x * mix) * (1.0f + 0.6f * mix);
        }
    }
}

//==============================================================================
// Flanger
FlangerBlock::FlangerBlock() : Block (Type::Flanger)
{
    addChoice ("type", "Type", { "Jet", "Filter matrix", "Through-zero" }, 0);
    auto& rate = addFloat ("rate", "Rate", 0.02f, 5, 0.25f, "Hz", 0.5f, 0.01f);
    addFloat ("depth", "Depth", 0, 100, 70, "%", -1, 1);
    addFloat ("manual", "Manual", 0, 100, 40, "%", -1, 1);
    auto& fbk = addFloat ("feedback", "Feedback", -95, 95, 60, "%", -1, 1);
    auto& mat = addToggle ("matrix", "Filter matrix (no sweep)", false);
    addFloat ("mix", "Mix", 0, 100, 50, "%", -1, 1);
    addToggle ("killDry", "Kill dry (100% wet)", false);
    mat.visible = [] (const Block& b) { return b.params[0]->getInt() == 1; };
    fbk.visible = [] (const Block& b) { return b.params[0]->getInt() != 2; };
    juce::ignoreUnused (rate);
}

void FlangerBlock::prepare (double sr, int)
{
    for (auto& l : line) l.setMaxSamples ((int) (0.03 * sr));
    for (auto& l : ref) l.setMaxSamples ((int) (0.03 * sr));
    for (auto& h : hp) h.set (Biquad::Type::HighPass, (float) sr, 40.0f, 0.7f);
    reset();
}

void FlangerBlock::reset() { for (auto& l : line) l.clear(); for (auto& l : ref) l.clear(); fb[0] = fb[1] = 0; ph = 0; }

void FlangerBlock::process (float* L, float* R, int n)
{
    const int t = (int) p (0);
    const float rate = p (1), depth = p (2) * 0.01f, manual = p (3) * 0.01f, feedback = p (4) * 0.01f * (t == 1 ? 0.6f : 1.0f);
    const bool frozen = t == 1 && p (5) > 0.5f;
    const float mix = p (6) * 0.01f;
    const bool killDry = p (7) > 0.5f;
    const float sr = sampleRate;
    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        if (! frozen) { ph += rate / sr; if (ph >= 1) ph -= 1; }
        for (int c = 0; c < 2; ++c)
        {
            const float lfo = tri01 (ph + 0.25f * (float) c);
            const float x = ch[c][i];
            float wet;
            if (t == 2)
            {
                // through-zero: a fixed 3 ms copy against a copy sweeping 0..6 ms; they cross and cancel
                ref[c].push (x);
                line[c].push (x);
                const float centre = msToSamples (3.0f, sr);
                const float d = centre + centre * (depth * (2.0f * lfo - 1.0f)) + centre * (manual - 0.5f) * 0.4f;
                wet = ref[c].read (centre) - line[c].read (std::max (1.0f, d));
                wet *= 0.75f;
            }
            else
            {
                const float minD = msToSamples (0.25f + 2.5f * manual, sr), sweep = msToSamples (t == 1 ? 1.5f : 5.0f, sr) * depth;
                const float d = minD + sweep * (frozen ? 0.0f : lfo);
                line[c].push (hp[c].process (x + fb[c] * feedback));
                wet = line[c].read (std::max (1.0f, d));
                fb[c] = fastTanh (wet);
            }
            const float dry = killDry ? 0.0f : 1.0f - 0.5f * mix;
            ch[c][i] = x * dry + wet * mix;
        }
    }
}

//==============================================================================
// Whammy
namespace { const float whammySemis[11] = { 24, 12, 7, 5, 2, -2, -5, -7, -12, -24, 0 }; }

WhammyBlock::WhammyBlock() : Block (Type::Whammy)
{
    addChoice ("type", "Interval", { "+2 octaves", "+1 octave", "+5th", "+4th", "+Whole step", "-Whole step", "-4th", "-5th", "-1 octave", "-2 octaves", "Detune" }, 1);
    addFloat ("position", "Pedal", 0, 100, 0, "%", -1, 0.1f);
    addChoice ("mode", "Mode", { "Whammy (bend)", "Harmony (dry + shift)" }, 0);
    auto& det = addFloat ("detune", "Detune", 1, 50, 12, "cents", -1, 0.1f);
    addFloat ("glide", "Pedal glide", 0, 500, 30, "ms", 100, 1);
    addFloat ("shiftLevel", "Shifted level", 0, 100, 100, "%", -1, 1);
    addFloat ("level", "Level", -12, 12, 0, "dB", -1, 0.1f);
    det.visible = [] (const Block& b) { return b.params[0]->getInt() == 10; };
}

void WhammyBlock::prepare (double sr, int)
{
    for (auto& s : shifter) s.prepare ((float) sr, 0.040f, 0.32f);
    posSm = -1;
}

void WhammyBlock::process (float* L, float* R, int n)
{
    const int iv = (int) p (0);
    const float target = p (1) * 0.01f;
    const bool harmony = (int) p (2) == 1;
    const float glideMs = p (4);
    const float shiftLvl = p (5) * 0.01f, lvl = dbToGain (p (6));
    if (posSm < 0) posSm = target;
    const float coef = glideMs < 1 ? 1.0f : 1.0f - std::exp (-1.0f / (glideMs * 0.001f * sampleRate));
    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        posSm += coef * (target - posSm);
        float semis = iv == 10 ? p (3) * 0.01f * posSm : whammySemis[iv] * posSm;
        const float ratio = std::exp2 (semis / 12.0f);
        for (int c = 0; c < 2; ++c)
        {
            const float x = ch[c][i];
            // detune: one side up, the other down
            const float r = iv == 10 && c == 1 ? 1.0f / ratio : ratio;
            const float s = std::abs (r - 1.0f) < 1.0e-5f ? (shifter[c].process (x, 1.0f), x) : shifter[c].process (x, r);
            const float y = harmony || iv == 10 ? x + s * shiftLvl : s * shiftLvl + x * (1.0f - shiftLvl);
            ch[c][i] = y * lvl;
        }
    }
}

//==============================================================================
// Polyphonic octave (Micro POG / POG2 style). The guitar is analysed in short overlapping
// FFT frames; every partial is moved to 2x / 4x (up) or 1/2x / 1/4x (down) its exact measured
// frequency with its phase kept coherent (phase-locked vocoder), so single notes come out as
// chords stay chords. "Single notes" uses pitch-synchronous shifters: the cleanest octaves on
// single notes (and the least delay), like playing a POG one note at a time.
OctaveBlock::OctaveBlock() : Block (Type::Octave)
{
    addChoice ("mode", "Mode", { "Micro POG", "POG2" }, 0);
    addFloat ("dry", "Dry", 0, 100, 100, "%", -1, 1);
    addFloat ("sub", "Sub octave", 0, 100, 0, "%", -1, 1);
    addFloat ("up", "Octave up", 0, 100, 60, "%", -1, 1);
    auto& sub2 = addFloat ("sub2", "2 octaves down", 0, 100, 0, "%", -1, 1);
    auto& up2 = addFloat ("up2", "2 octaves up", 0, 100, 0, "%", -1, 1);
    auto& det = addFloat ("detune", "Detune", 0, 100, 0, "%", -1, 1);
    auto& atk = addFloat ("attack", "Attack", 0, 100, 0, "%", -1, 1);
    addFloat ("filter", "Filter", 300, 12000, 12000, "Hz", 2500, 1);
    auto& fq = addFloat ("filterQ", "Filter resonance", 0, 100, 0, "%", -1, 1);
    addChoice ("engine", "Tracking", { "Single notes (cleanest)", "Chords (polyphonic)" }, 0);
    auto pog2 = [] (const Block& b) { return b.params[0]->getInt() == 1; };
    sub2.visible = up2.visible = det.visible = atk.visible = fq.visible = pog2;
}

void OctaveBlock::prepare (double sr, int)
{
    fft = std::make_unique<juce::dsp::FFT> (order);
    inRing.assign (N, 0.0f); outAcc.assign (N * 2, 0.0f); buf.assign (N * 2, 0.0f);
    window.resize (N);
    for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) N);
    lastPhase.assign (N / 2 + 1, 0.0f); mag.assign (N / 2 + 1, 0.0f); freq.assign (N / 2 + 1, 0.0f);
    outRe.assign (N / 2 + 1, 0.0f); outIm.assign (N / 2 + 1, 0.0f);
    for (auto& v : synPhase) v.assign (N / 2 + 1, 0.0f);
    for (auto& f : fast) f.prepare ((float) sr, 0.030f, 0.25f);
    hp.set (Biquad::Type::HighPass, (float) sr, 35.0f, 0.7f);
    reset();
}

void OctaveBlock::reset()
{
    std::fill (inRing.begin(), inRing.end(), 0.0f); std::fill (outAcc.begin(), outAcc.end(), 0.0f);
    std::fill (lastPhase.begin(), lastPhase.end(), 0.0f);
    for (auto& v : synPhase) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& f : fast) f.clear();
    inPos = hopCount = outPos = 0; lp.reset(); hp.reset();
    fastEnv = slowEnv = 0; attackGain = 1; lastFilter = -1;
}

void OctaveBlock::spectralFrame (const float ratios[voices], const float gains[voices])
{
    const int half = N / 2;
    for (int i = 0; i < N; ++i) buf[(size_t) i] = inRing[(size_t) ((inPos + i) % N)] * window[(size_t) i];
    std::fill (buf.begin() + N, buf.end(), 0.0f);
    fft->performRealOnlyForwardTransform (buf.data(), true);
    const float expect = twoPi * (float) hop / (float) N;
    for (int k = 0; k <= half; ++k)
    {
        const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt (re * re + im * im);
        const float ph = std::atan2 (im, re);
        float d = ph - lastPhase[(size_t) k] - expect * (float) k;
        d = std::remainder (d, twoPi);
        lastPhase[(size_t) k] = ph;
        freq[(size_t) k] = (float) k + d / expect;           // exact frequency, in bins
    }
    std::fill (outRe.begin(), outRe.end(), 0.0f); std::fill (outIm.begin(), outIm.end(), 0.0f);
    for (int v = 0; v < voices; ++v)
    {
        if (gains[v] <= 0.0f) continue;
        const float r = ratios[v];
        auto& sp = synPhase[v];
        // peaks carry the phase; the bins around a peak keep their offset to it (identity phase locking)
        int k = 1;
        while (k < half)
        {
            if (! (mag[(size_t) k] > mag[(size_t) (k - 1)] && mag[(size_t) k] >= mag[(size_t) (k + 1)])) { ++k; continue; }
            const int peak = k;
            int lo = peak, hi = peak;
            while (lo > 1 && mag[(size_t) (lo - 1)] < mag[(size_t) lo]) --lo;
            while (hi < half - 1 && mag[(size_t) (hi + 1)] < mag[(size_t) hi]) ++hi;
            const float target = freq[(size_t) peak] * r;
            const int tp = (int) std::lround (target);
            if (tp >= 1 && tp < half)
            {
                sp[(size_t) tp] = std::remainder (sp[(size_t) tp] + expect * target, twoPi);
                const float basePh = sp[(size_t) tp];
                for (int j = lo; j <= hi; ++j)
                {
                    const int tj = tp + (int) std::lround ((float) (j - peak) * std::min (1.0f, r));
                    if (tj < 1 || tj >= half) continue;
                    const float ph = basePh + (lastPhase[(size_t) j] - lastPhase[(size_t) peak]);
                    const float a = mag[(size_t) j] * gains[v];
                    outRe[(size_t) tj] += a * std::cos (ph);
                    outIm[(size_t) tj] += a * std::sin (ph);
                }
            }
            k = hi + 1;
        }
    }
    for (int k = 0; k <= half; ++k) { buf[(size_t) (2 * k)] = outRe[(size_t) k]; buf[(size_t) (2 * k + 1)] = outIm[(size_t) k]; }
    fft->performRealOnlyInverseTransform (buf.data());
    const float norm = 1.0f / 3.0f;   // Hann analysis x Hann synthesis at 8x overlap sums to 3
    for (int i = 0; i < N; ++i)
        outAcc[(size_t) ((outPos + i) % (2 * N))] += buf[(size_t) i] * window[(size_t) i] * norm;
}

void OctaveBlock::process (float* L, float* R, int n)
{
    const bool pog2 = (int) p (0) == 1;
    const float dry = p (1) * 0.01f;
    const float detune = pog2 ? p (6) * 0.01f : 0.0f;
    // voices: sub, up, 2 down, 2 up (detune spreads them a few cents, like POG2's detune)
    const float dc = std::exp2 ((detune * 12.0f) / 1200.0f);
    const float ratios[voices] = { 0.5f * dc, 2.0f / dc, 0.25f, 4.0f };
    float gains[voices] = { p (2) * 0.01f, p (3) * 0.01f, pog2 ? p (4) * 0.01f : 0.0f, pog2 ? p (5) * 0.01f : 0.0f };
    const bool fastMode = (int) p (10) == 0;   // single-note shifters: cleanest octaves, least delay
    const float filt = p (8), fq = pog2 ? p (9) * 0.01f : 0.0f;
    if (filt != lastFilter || fq > 0) { lp.set (Biquad::Type::LowPass, sampleRate, filt, 0.7f + fq * 5.0f); lastFilter = filt; }
    const float attackSec = pog2 ? p (7) * 0.01f * 1.5f : 0.0f;
    const float rise = attackSec > 0.005f ? 1.0f / (attackSec * sampleRate) : 1.0f;
    const float fA = 1.0f - std::exp (-1.0f / (0.001f * sampleRate)), fR = 1.0f - std::exp (-1.0f / (0.02f * sampleRate));
    const float sA = 1.0f - std::exp (-1.0f / (0.03f * sampleRate)), sR = 1.0f - std::exp (-1.0f / (0.2f * sampleRate));
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (L[i] + R[i]);
        float wet = 0.0f;
        if (fastMode)
        {
            for (int v = 0; v < voices; ++v) { const float y = fast[v].process (x, ratios[v]); wet += y * gains[v]; }
        }
        else
        {
            inRing[(size_t) inPos] = x;
            inPos = (inPos + 1) % N;
            if (++hopCount >= hop) { hopCount = 0; spectralFrame (ratios, gains); }
            wet = outAcc[(size_t) outPos];
            outAcc[(size_t) outPos] = 0.0f;
            outPos = (outPos + 1) % (2 * N);
        }
        // POG2 attack: the octaves swell in after each pick
        const float a = std::abs (x);
        fastEnv += (a > fastEnv ? fA : fR) * (a - fastEnv);
        slowEnv += (a > slowEnv ? sA : sR) * (a - slowEnv);
        if (fastEnv > 2.5f * slowEnv + 0.002f) attackGain = 0.0f;
        attackGain = std::min (1.0f, attackGain + rise);
        wet = hp.process (lp.process (wet)) * (attackSec > 0.005f ? attackGain * attackGain : 1.0f);
        const float y = x * dry + wet;
        L[i] = R[i] = y;
    }
}

//==============================================================================
// Harmonizer
namespace
{
    const int scaleSteps[8][8] = {
        { 0, 2, 4, 5, 7, 9, 11, -1 },   // major
        { 0, 2, 3, 5, 7, 8, 10, -1 },   // natural minor
        { 0, 2, 3, 5, 7, 9, 10, -1 },   // dorian
        { 0, 2, 4, 5, 7, 9, 10, -1 },   // mixolydian
        { 0, 2, 3, 5, 7, 8, 11, -1 },   // harmonic minor
        { 0, 2, 4, 7, 9, -1, -1, -1 },  // major pentatonic
        { 0, 3, 5, 7, 10, -1, -1, -1 }, // minor pentatonic
        { -1 }                          // chromatic (fixed intervals)
    };
    // interval choices: scale steps (in a 7-note scale) and fixed semitones for chromatic
    const int ivSteps[14] = { 0, -7, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 7 };
    const int ivSemis[14] = { 0, -12, -8, -7, -5, -4, -2, 0, 2, 4, 5, 7, 9, 12 };

    int harmonySemis (int note, int key, int scale, int choice)
    {
        if (choice <= 0) return 0;
        if (scale == 7) return ivSemis[choice];
        int count = 0; while (count < 8 && scaleSteps[scale][count] >= 0) ++count;
        int steps = ivSteps[choice];
        if (count == 5)   // pentatonic: a "3rd" is one scale step, a "5th" is three, an octave five
        {
            static const int pent[14] = { 0, -5, -4, -3, -2, -1, -1, 0, 1, 1, 2, 3, 4, 5 };
            steps = pent[choice];
        }
        const int rel = ((note - key) % 12 + 12) % 12;
        int deg = 0;   // nearest scale degree at or below the played note
        for (int d = 0; d < count; ++d) if (scaleSteps[scale][d] <= rel) deg = d;
        const int octOff = (note - key) - rel;
        const int base = octOff + scaleSteps[scale][deg];
        const int tdeg = deg + steps;
        const int to = (int) std::floor ((float) tdeg / (float) count);
        const int target = octOff + to * 12 + scaleSteps[scale][((tdeg % count) + count) % count];
        return target - base;
    }
}

HarmonizerBlock::HarmonizerBlock() : Block (Type::Harmonizer)
{
    const juce::StringArray ivs { "Off", "Octave down", "6th down", "5th down", "4th down", "3rd down", "2nd down", "Unison", "2nd up", "3rd up", "4th up", "5th up", "6th up", "Octave up" };
    addChoice ("key", "Key", { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 4);
    addChoice ("scale", "Scale", { "Major", "Minor", "Dorian", "Mixolydian", "Harmonic minor", "Major pentatonic", "Minor pentatonic", "Chromatic" }, 1);
    addChoice ("v1", "Voice 1", ivs, 9);
    addFloat ("v1Level", "Voice 1 level", 0, 100, 70, "%", -1, 1);
    addChoice ("v2", "Voice 2", ivs, 0);
    addFloat ("v2Level", "Voice 2 level", 0, 100, 60, "%", -1, 1);
    addFloat ("dry", "Dry", 0, 100, 100, "%", -1, 1);
    addFloat ("width", "Width", 0, 100, 60, "%", -1, 1);
    addFloat ("glide", "Glide", 0, 100, 12, "ms", -1, 1);
    addFloat ("gate", "Tracking gate", -80, -20, -55, "dB", -1, 0.1f);
}

void HarmonizerBlock::prepare (double sr, int)
{
    tracker.prepare (sr);
    for (auto& v : voice) v.prepare ((float) sr, 0.035f, 0.2f);
    reset();
}

void HarmonizerBlock::reset() { tracker.reset(); for (auto& v : voice) v.clear(); ratioSm[0] = ratioSm[1] = 1; heldNote = -1; }

void HarmonizerBlock::process (float* L, float* R, int n)
{
    const int key = (int) p (0), scale = (int) p (1), c1 = (int) p (2), c2 = (int) p (4);
    const float l1 = p (3) * 0.01f, l2 = p (5) * 0.01f, dry = p (6) * 0.01f, width = p (7) * 0.01f;
    const float gc = 1.0f - std::exp (-1.0f / (std::max (0.5f, p (8)) * 0.001f * sampleRate));
    tracker.setThresholdDb (p (9));
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (L[i] + R[i]);
        const auto f = tracker.process (x);
        if (f.valid) heldNote = (int) std::lround (f.centre);
        float out[2] = { 0, 0 };
        const int choices[2] = { c1, c2 };
        const float lv[2] = { l1, l2 };
        for (int v = 0; v < 2; ++v)
        {
            if (choices[v] <= 0) { voice[v].process (x, 1.0f); continue; }
            const int semis = heldNote >= 0 ? harmonySemis (heldNote, key, scale, choices[v]) : 0;
            ratioSm[v] += gc * (std::exp2 ((float) semis / 12.0f) - ratioSm[v]);
            out[v] = voice[v].process (x, ratioSm[v]) * lv[v];
        }
        // voice 1 leans left, voice 2 right
        const float a = 0.5f + 0.5f * width, b = 1.0f - a;
        L[i] = x * dry + out[0] * a + out[1] * b;
        R[i] = x * dry + out[0] * b + out[1] * a;
    }
}

//==============================================================================
// Envelope filter
EnvFilterBlock::EnvFilterBlock() : Block (Type::EnvFilter)
{
    addChoice ("mode", "Filter", { "Low-pass", "Band-pass", "High-pass" }, 1);
    addChoice ("dir", "Direction", { "Up", "Down" }, 0);
    addFloat ("sens", "Sensitivity", -12, 30, 8, "dB", -1, 0.1f);
    addFloat ("low", "Range low", 100, 1200, 250, "Hz", 400, 1);
    addFloat ("high", "Range high", 800, 6000, 2600, "Hz", 2000, 1);
    addFloat ("q", "Resonance", 0.7f, 14, 5, {}, 4, 0.1f);
    addFloat ("attack", "Attack", 1, 60, 5, "ms", 10, 0.1f);
    addFloat ("decay", "Decay", 30, 1200, 180, "ms", 200, 1);
    addFloat ("mix", "Mix", 0, 100, 100, "%", -1, 1);
    addFloat ("level", "Level", -12, 12, 0, "dB", -1, 0.1f);
}

void EnvFilterBlock::process (float* L, float* R, int n)
{
    const int mode = (int) p (0);
    const bool down = (int) p (1) == 1;
    const float sens = dbToGain (p (2)), lo = p (3), hi = std::max (p (4), lo * 1.2f), q = p (5);
    const float aC = 1.0f - std::exp (-1.0f / (p (6) * 0.001f * sampleRate));
    const float rC = 1.0f - std::exp (-1.0f / (p (7) * 0.001f * sampleRate));
    const float mix = p (8) * 0.01f, lvl = dbToGain (p (9));
    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (0.5f * (L[i] + R[i])) * sens * 4.0f;
        env += (a > env ? aC : rC) * (a - env);
        if ((i & 3) == 0)
        {
            float e = clamp (env, 0.0f, 1.0f);
            e = std::sqrt (e);
            if (down) e = 1.0f - e;
            const float fc = lo * std::pow (hi / lo, e);
            for (auto& f : svf) f.set (sampleRate, fc, q);
        }
        for (int c = 0; c < 2; ++c)
        {
            float lp, bp, hpv;
            svf[c].tick (ch[c][i], lp, bp, hpv);
            const float y = mode == 0 ? lp : (mode == 1 ? bp * 2.8f : hpv);
            ch[c][i] = (ch[c][i] * (1.0f - mix) + y * mix) * lvl;
        }
    }
}

//==============================================================================
// Auto-swell
SwellBlock::SwellBlock() : Block (Type::Swell)
{
    addFloat ("attack", "Swell time", 50, 3000, 600, "ms", 600, 1);
    addFloat ("thresh", "Sensitivity", -70, -20, -48, "dB", -1, 0.1f);
    addFloat ("curve", "Curve", 0, 100, 60, "%", -1, 1);
}

void SwellBlock::process (float* L, float* R, int n)
{
    const float rise = 1.0f / (p (0) * 0.001f * sampleRate);
    const float thr = dbToGain (p (1)), curve = p (2) * 0.01f;
    const float fA = 1.0f - std::exp (-1.0f / (0.0005f * sampleRate)), fR = 1.0f - std::exp (-1.0f / (0.02f * sampleRate));
    const float sA = 1.0f - std::exp (-1.0f / (0.02f * sampleRate)), sR = 1.0f - std::exp (-1.0f / (0.15f * sampleRate));
    for (int i = 0; i < n; ++i)
    {
        const float a = std::max (std::abs (L[i]), std::abs (R[i]));
        fast += (a > fast ? fA : fR) * (a - fast);
        slow += (a > slow ? sA : sR) * (a - slow);
        if (cooldown > 0) --cooldown;
        // each new pick (a sudden jump above the recent level) starts the swell again
        if (cooldown == 0 && fast > thr && fast > 2.2f * slow + thr * 0.5f) { gain = 0; cooldown = (int) (0.06f * sampleRate); }
        if (slow < thr * 0.5f) gain = 0;
        gain = std::min (1.0f, gain + rise);
        const float g = curve * gain * gain + (1.0f - curve) * gain;
        L[i] *= g; R[i] *= g;
    }
}

//==============================================================================
// Doubler
DoublerBlock::DoublerBlock() : Block (Type::Doubler)
{
    addFloat ("time", "Time", 5, 40, 18, "ms", -1, 0.1f);
    addFloat ("detune", "Detune", 0, 25, 6, "cents", -1, 0.1f);
    addFloat ("width", "Width", 0, 100, 100, "%", -1, 1);
    addFloat ("tone", "Tone", 2000, 16000, 9000, "Hz", 6000, 1);
    addFloat ("mix", "Double level", 0, 100, 70, "%", -1, 1);
    addToggle ("killDry", "Kill dry (100% wet)", false);
}

void DoublerBlock::prepare (double sr, int)
{
    line.setMaxSamples ((int) (0.08 * sr));
    drift[0].seed = 991; drift[1].seed = 4441;
    drift[0].setRate ((float) sr, 0.7f); drift[1].setRate ((float) sr, 0.55f);
    reset();
}

void DoublerBlock::process (float* L, float* R, int n)
{
    const float base = msToSamples (p (0), sampleRate);
    // a delay swinging by A samples at f Hz bends pitch by about 2*pi*f*A/sr: size A for the cents asked
    const float cents = p (1);
    const float amp = (cents / 1731.0f) * sampleRate / (twoPi * 0.6f);
    const float width = p (2) * 0.01f, mix = p (4) * 0.01f;
    const bool killDry = p (5) > 0.5f;
    for (auto& t : tone) t.set (Biquad::Type::LowPass, sampleRate, p (3), 0.6f);
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (L[i] + R[i]);
        line.push (x);
        const float a = tone[0].process (line.read (base + amp * drift[0].next()));
        const float b = tone[1].process (line.read (base * 1.37f + amp * drift[1].next()));
        const float dry = killDry ? 0.0f : 1.0f;
        // width 100%: original left of centre, the doubles to the right and left
        const float pl = 0.5f + 0.5f * width, pr = 1.0f - pl;
        L[i] = x * dry * (1.0f - 0.3f * width) + mix * (a * pl + b * pr);
        R[i] = x * dry * (1.0f - 0.3f * width) + mix * (b * pl + a * pr);
    }
}

//==============================================================================
// Fuzz
FuzzBlock::FuzzBlock() : Block (Type::Fuzz)
{
    addChoice ("type", "Type", { "Big Muff style", "Fuzz Face style", "Germanium (Buffalo FX style)" }, 0);
    addFloat ("fuzz", "Sustain", 0, 10, 7, {}, -1, 0.1f);
    auto& tn = addFloat ("tone", "Tone", 0, 10, 5, {}, -1, 0.1f);
    auto& bias = addFloat ("bias", "Bias", 0, 10, 6, {}, -1, 0.1f);
    addFloat ("volume", "Volume", 0, 10, 5, {}, -1, 0.1f);
    tn.visible = [] (const Block& b) { return b.params[0]->getInt() == 0; };
    bias.visible = [] (const Block& b) { return b.params[0]->getInt() >= 1; };
}

void FuzzBlock::prepare (double sr, int maxBlockSize)
{
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) maxBlockSize);
    const float osr = (float) sr * 4.0f;
    for (int c = 0; c < 2; ++c)
    {
        inHp[c].set (Biquad::Type::HighPass, (float) sr, 60.0f, 0.7f);
        post[c].set (Biquad::Type::HighPass, (float) sr, 25.0f, 0.7f);
        stageLp[c][0].setLowpass (osr, 7000.0f);
        stageLp[c][1].setLowpass (osr, 5500.0f);
        biasEnv[c] = 0;
    }
    level.reset ((float) sr);
}

void FuzzBlock::process (float* L, float* R, int n)
{
    const int t = (int) p (0);
    const float fz = p (1) * 0.1f, tone = p (2) * 0.1f, bias = p (3) * 0.1f;
    float* ch[2] = { L, R };
    for (int c = 0; c < 2; ++c)
    {
        // Big Muff tone stack: a 1-pole low-pass and high-pass blended, with the classic mid scoop
        muffLp[c].set (Biquad::Type::LowPass, sampleRate, 480.0f, 0.5f);
        muffHp[c].set (Biquad::Type::HighPass, sampleRate, 1100.0f, 0.5f);
        faceLp[c].set (Biquad::Type::LowPass, sampleRate, t == 2 ? 4200.0f : 5200.0f, 0.6f);
        bigLow[c].set (Biquad::Type::LowShelf, sampleRate, 140.0f, 0.7f, 5.0f);
        if (t == 2) muffLp[c].set (Biquad::Type::Peak, sampleRate, 750.0f, 0.8f, 2.5f);
        inHp[c].set (Biquad::Type::HighPass, sampleRate, t == 0 ? 60.0f : 30.0f, 0.7f);   // germanium keeps every bit of low end
        for (int i = 0; i < n; ++i) ch[c][i] = inHp[c].process (ch[c][i]);
    }
    // plenty of gain: these should really fuzz (a guitar's level sits well below full scale)
    const float fz3 = fz * fz * fz;   // low settings stay gritty, high settings go fully fuzzed
    const float g = t == 0 ? 2.0f + 2200.0f * fz3 : (t == 1 ? 1.5f + 700.0f * fz3 : 1.5f + 1100.0f * fz3);
    for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; }

    juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
    auto up = os->processSamplesUp (block);
    const int m = (int) up.getNumSamples();
    const float envC = 1.0f - std::exp (-1.0f / (0.03f * sampleRate * 4.0f));
    for (int c = 0; c < 2; ++c)
    {
        float* x = up.getChannelPointer ((size_t) c);
        for (int i = 0; i < m; ++i)
        {
            float v = x[i];
            if (t == 2)
            {
                // germanium Fuzz Face, built huge: soft asymmetric clipping that compresses as you dig
                // in (sag), the bias opening up the low end; still cleans up with the guitar's volume
                biasEnv[c] += envC * (std::abs (v) - biasEnv[c]);
                const float sagged = v / (1.0f + 0.15f * std::min (8.0f, biasEnv[c]));
                const float b = 0.10f + 0.35f * bias;
                v = fastTanh (sagged * 0.7f + b) - fastTanh (b);
                v = v > 0 ? v * 1.15f : v * (0.6f + 0.35f * bias);
                v = stageLp[c][1].lp (v);
            }
            else if (t == 0)
            {
                // two clipping transistor stages with diodes, filtered between
                v = stageLp[c][0].lp (v);
                v = v / std::sqrt (1.0f + v * v);
                v = stageLp[c][1].lp (v * 12.0f);
                v = v / std::sqrt (1.0f + v * v);
            }
            else
            {
                // germanium pair: asymmetric, bias shifts with level (sputter at low bias, cleans up with guitar volume)
                biasEnv[c] += envC * (std::abs (v) - biasEnv[c]);
                const float b = (0.15f + 0.45f * bias) - 0.25f * std::min (1.0f, biasEnv[c] * 0.2f);
                v = fastTanh (v + b) - fastTanh (b);
                v = v > 0 ? v : v * (0.55f + 0.4f * bias);
                v = stageLp[c][0].lp (v);
            }
            x[i] = v;
        }
    }
    os->processSamplesDown (block);
    const float makeup = t == 0 ? dbToGain (-13.0f) : (t == 1 ? dbToGain (-16.0f) : dbToGain (-20.0f));
    level.setTarget (dbToGain ((p (4) - 5.0f) * 4.0f) * makeup);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            float y = ch[c][i];
            if (t == 0) y = muffLp[c].process (y) * (1.0f - tone) * 1.3f + muffHp[c].process (y) * tone * 1.6f;
            else if (t == 1) y = faceLp[c].process (y);
            else y = muffLp[c].process (bigLow[c].process (faceLp[c].process (y)));   // germanium: huge low end, smooth top
            ch[c][i] = post[c].process (y);
        }
    for (int i = 0; i < n; ++i) { const float lv = level.next(); L[i] *= lv; R[i] *= lv; }
}

//==============================================================================
// "@ Drive": British-voiced two-stage drive (the AT descends from a Marshall-in-a-box), with
// EQ and Air low-pass controls, a 25/50/100 W headroom switch and a TS808-style boost in front.
ATDriveBlock::ATDriveBlock() : Block (Type::ATDrive)
{
    addFloat ("volume", "Volume", 0, 10, 5, {}, -1, 0.1f);
    addFloat ("drive", "Drive", 0, 10, 6, {}, -1, 0.1f);
    addFloat ("eq", "EQ", 0, 10, 6, {}, -1, 0.1f);
    addFloat ("air", "Air", 0, 10, 5, {}, -1, 0.1f);
    addChoice ("headroom", "Headroom", { "25 W", "50 W", "100 W" }, 2);
    addToggle ("boost", "Boost (TS)", false);
}

void ATDriveBlock::prepare (double sr, int maxBlockSize)
{
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) maxBlockSize);
    const float osr = (float) sr * 4.0f;
    for (int c = 0; c < 2; ++c)
    {
        tight[c].set (Biquad::Type::HighPass, (float) sr, 95.0f, 0.65f);
        mid[c].set (Biquad::Type::Peak, (float) sr, 850.0f, 0.8f, 5.0f);
        boostHp[c].set (Biquad::Type::HighPass, (float) sr, 720.0f, 0.6f);
        boostMid[c].set (Biquad::Type::LowPass, (float) sr, 3800.0f, 0.6f);
        post[c].set (Biquad::Type::HighPass, (float) sr, 30.0f, 0.7f);
        interLp[c][0].setLowpass (osr, 9000.0f);
        interLp[c][1].setLowpass (osr, 6500.0f);
        sag[c] = 0;
    }
    level.reset ((float) sr); drive.reset ((float) sr);
    lastEq = lastAir = lastHead = -1;
}

void ATDriveBlock::updateFilters()
{
    const float e = p (2) * 0.1f, a = p (3) * 0.1f, h = p (4);
    if (e == lastEq && a == lastAir && h == lastHead) return;
    lastEq = e; lastAir = a; lastHead = h;
    for (int c = 0; c < 2; ++c)
    {
        eq[c].set (Biquad::Type::LowPass, sampleRate, 1200.0f * std::pow (9.0f, e), 0.62f);      // EQ: dark .. bright
        air[c].set (Biquad::Type::HighShelf, sampleRate, 5000.0f, 0.7f, (a - 0.5f) * 14.0f);     // Air: presence-like top end
    }
}

void ATDriveBlock::process (float* L, float* R, int n)
{
    updateFilters();
    const float d = p (1) * 0.1f;
    const int head = (int) p (4);
    const bool boost = p (5) > 0.5f;
    // smaller "amps" clip earlier and compress more; 100 W is the open, loud setting
    static const float clipAt[3] = { 0.55f, 0.8f, 1.15f }, sagAmt[3] = { 0.45f, 0.25f, 0.1f };
    drive.setTarget (2.0f + 160.0f * d * d);
    float* ch[2] = { L, R };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            float x = ch[c][i];
            if (boost)
            {
                // TS808-style: mid-hump boost with soft clipping, into the drive
                const float hb = boostHp[c].process (x) * 18.0f;
                x = boostMid[c].process (x + 0.5f * fastTanh (hb)) * 2.0f;
            }
            ch[c][i] = mid[c].process (tight[c].process (x));
        }
    for (int i = 0; i < n; ++i) { const float g = drive.next(); L[i] *= g; R[i] *= g; }

    juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
    auto up = os->processSamplesUp (block);
    const int m = (int) up.getNumSamples();
    const float sagC = 1.0f - std::exp (-1.0f / (0.08f * sampleRate * 4.0f));
    const float ca = clipAt[head], sa = sagAmt[head];
    for (int c = 0; c < 2; ++c)
    {
        float* x = up.getChannelPointer ((size_t) c);
        for (int i = 0; i < m; ++i)
        {
            // stage 1: soft, slightly asymmetric (preamp tube)
            float v = interLp[c][0].lp (x[i]);
            v = fastTanh (v * 0.5f + 0.08f) - 0.0798f;
            // power-section sag: louder playing lowers the clip point a little
            sag[c] += sagC * (std::abs (v) - sag[c]);
            const float th = ca * (1.0f - sa * std::min (1.0f, sag[c]));
            // stage 2: harder clip at the headroom setting
            v = interLp[c][1].lp (v * 4.0f);
            v = th * fastTanh (v / th);
            x[i] = v;
        }
    }
    os->processSamplesDown (block);
    level.setTarget (dbToGain ((p (0) - 5.0f) * 4.0f - 17.0f));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] = post[c].process (air[c].process (eq[c].process (ch[c][i])));
    for (int i = 0; i < n; ++i) { const float lv = level.next(); L[i] *= lv; R[i] *= lv; }
}

//==============================================================================
// Freeze
namespace
{
    const char* noteNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    // note sets relative to the root: bit i = semitone i
    const int noteSets[13] = {
        -1,                                                   // custom
        0xFFF,                                                // all
        1 << 0,                                               // root
        (1 << 0) | (1 << 7),                                  // power
        (1 << 0) | (1 << 4) | (1 << 7),                       // major
        (1 << 0) | (1 << 3) | (1 << 7),                       // minor
        (1 << 0) | (1 << 2) | (1 << 7),                       // sus2
        (1 << 0) | (1 << 5) | (1 << 7),                       // sus4
        (1 << 0) | (1 << 4) | (1 << 7) | (1 << 11),           // maj7
        (1 << 0) | (1 << 3) | (1 << 7) | (1 << 10),           // min7
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7),            // add9
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),// minor pentatonic
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9)  // major pentatonic
    };
    enum FzP { fHold, fMode, fSource, fPitch, fQuant, fVolume, fFadeIn, fFadeOut, fTone, fDry, fSet, fRoot, fNote0, fLevel0 = fNote0 + 12 };
}

FreezeBlock::FreezeBlock() : Block (Type::Freeze)
{
    currentGroup = "Freeze";
    addToggle ("hold", "Hold", false);
    addChoice ("mode", "Mode", { "Freeze", "Infinite" }, 0);
    addChoice ("source", "Sound", { "Notes you played", "Drone on chosen notes" }, 0);
    addFloat ("pitch", "Pitch", -24, 24, 0, "st", -1, 0.01f);
    addChoice ("quant", "Pitch steps", { "Smooth", "Semitones" }, 1);
    addFloat ("volume", "Volume", 0, 150, 100, "%", -1, 1);
    addFloat ("fadeIn", "Fade in", 5, 5000, 400, "ms", 600, 1);
    addFloat ("fadeOut", "Fade out", 5, 8000, 1500, "ms", 1500, 1);
    addFloat ("tone", "Tone", 1000, 16000, 9000, "Hz", 5000, 1);
    addFloat ("dry", "Dry", 0, 100, 100, "%", -1, 1);
    currentGroup = "Notes";
    addChoice ("set", "Note set", { "Custom", "All notes", "Root only", "Power (1 5)", "Major", "Minor", "Sus2", "Sus4", "Major 7", "Minor 7", "Add9", "Minor pentatonic", "Major pentatonic" }, 1);
    addChoice ("root", "Root", { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 4);
    for (int i = 0; i < 12; ++i) addToggle ("n" + juce::String (i), noteNames[i], true);
    currentGroup = "Note levels";
    for (int i = 0; i < 12; ++i) addFloat ("lv" + juce::String (i), juce::String (noteNames[i]) + " level", 0, 100, 100, "%", -1, 1);
    currentGroup = {};
}

void FreezeBlock::prepare (double, int)
{
    fft = std::make_unique<juce::dsp::FFT> (order);
    inRing.assign ((size_t) fftSize, 0.0f);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) fftSize);
    mag.assign ((size_t) fftSize / 2 + 1, 0.0f);
    frozen.assign ((size_t) fftSize / 2 + 1, 0.0f);
    envSmooth.assign ((size_t) fftSize / 2 + 1, 0.0f);
    noteWeight.assign ((size_t) fftSize / 2 + 1, 0.0f);
    work.assign ((size_t) fftSize * 2, 0.0f);
    for (int c = 0; c < 2; ++c) { outAcc[c].assign ((size_t) fftSize, 0.0f); phase[c].assign ((size_t) fftSize / 2 + 1, 0.0f); }
    reset();
}

void FreezeBlock::reset()
{
    std::fill (inRing.begin(), inRing.end(), 0.0f);
    for (auto& o : outAcc) std::fill (o.begin(), o.end(), 0.0f);
    inPos = hopCount = outRead = 0;
    holding = haveFrozen = false;
    level = 0;
    lastNoteSet = -1;
}

void FreezeBlock::capture (bool add)
{
    // window the newest fftSize input samples and take the magnitude spectrum
    for (int i = 0; i < fftSize; ++i) work[(size_t) i] = inRing[(size_t) ((inPos + i) % fftSize)] * window[(size_t) i];
    std::fill (work.begin() + fftSize, work.end(), 0.0f);
    fft->performRealOnlyForwardTransform (work.data(), true);
    const float norm = 2.0f / (0.5f * (float) fftSize);
    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
        const float m = std::sqrt (re * re + im * im) * norm;
        frozen[(size_t) k] = add ? std::max (frozen[(size_t) k] * 0.999f, m) : m;
    }
    // a smooth version (about a third of an octave wide) for drone mode
    const int half = fftSize / 2;
    for (int k = 1; k < half; ++k)
    {
        const int w = std::max (1, k / 12);
        float s = 0; int cnt = 0;
        for (int j = std::max (1, k - w); j <= std::min (half, k + w); j += std::max (1, w / 4)) { s += frozen[(size_t) j]; ++cnt; }
        envSmooth[(size_t) k] = s / (float) std::max (1, cnt);
    }
    haveFrozen = true;
}

void FreezeBlock::synthFrame()
{
    const float sr = sampleRate;
    float semis = p (fPitch);
    if ((int) p (fQuant) == 1) semis = std::round (semis);
    const float ratio = std::exp2 (semis / 12.0f);
    const bool drone = (int) p (fSource) == 1;
    const float toneHz = p (fTone);
    const int half = fftSize / 2;
    float noteOn[12], noteLv[12];
    for (int i = 0; i < 12; ++i) { noteOn[i] = p (fNote0 + i) > 0.5f ? 1.0f : 0.0f; noteLv[i] = p (fLevel0 + i) * 0.01f; }
    const float sigma = drone ? 0.16f : 0.30f;   // in semitones: drone notes are pure, played notes keep their width

    // note mask by input bin (before the pitch shift: you choose which of your notes to keep)
    for (int k = 1; k < half; ++k)
    {
        const float f = (float) k * sr / (float) fftSize;
        const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
        float w = 0;
        const int nearest = (int) std::round (midi);
        for (int d = -1; d <= 1; ++d)
        {
            const int note = nearest + d;
            const int pc = ((note % 12) + 12) % 12;
            if (noteOn[pc] <= 0) continue;
            const float dist = (midi - (float) note) / sigma;
            w = std::max (w, noteLv[pc] * std::exp (-0.5f * dist * dist));
        }
        // below ~130 Hz the bins are wider than a semitone: let the mask fade toward "keep"
        if (f < 130.0f && ! drone) w = std::max (w, 0.6f * (1.0f - f / 130.0f));
        noteWeight[(size_t) k] = w;
    }

    for (int c = 0; c < 2; ++c)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int k = 1; k < half; ++k)
        {
            const float src = (float) k / ratio;   // output bin k takes from input bin k / ratio
            const int s0 = (int) src;
            if (s0 < 1 || s0 >= half - 1) continue;
            const float fr = src - (float) s0;
            const float* m = drone ? envSmooth.data() : frozen.data();
            float a = m[s0] + (m[s0 + 1] - m[s0]) * fr;
            a *= noteWeight[(size_t) s0] + (noteWeight[(size_t) s0 + 1] - noteWeight[(size_t) s0]) * fr;
            if (drone) a *= 3.0f;
            const float f = (float) k * sr / (float) fftSize;
            if (f > toneHz) a *= std::exp (-(f - toneHz) / (0.35f * toneHz));
            // each bin turns at its own frequency plus a little random drift, so the hold shimmers
            // instead of buzzing; left and right drift differently for width
            auto& ph = phase[c][(size_t) k];
            ph += twoPi * (float) k * (float) hop / (float) fftSize + (rng.nextFloat() - 0.5f) * 1.1f;
            ph = std::remainder (ph, twoPi);
            work[(size_t) (2 * k)] = a * std::cos (ph);
            work[(size_t) (2 * k + 1)] = a * std::sin (ph);
        }
        fft->performRealOnlyInverseTransform (work.data());
        // overlap-add with the synthesis window; 4x overlapped Hann^2 sums to 1.5
        const float g = (0.5f * (float) fftSize) / 1.5f * 1.05f;
        for (int i = 0; i < fftSize; ++i)
            outAcc[c][(size_t) ((outRead + i) % fftSize)] += work[(size_t) i] * window[(size_t) i] * g;
    }
}

void FreezeBlock::process (float* L, float* R, int n)
{
    // note sets: picking a set (or a new root) switches the 12 note buttons
    const int set = (int) p (fSet), root = (int) p (fRoot);
    const float key = (float) (set * 16 + root);
    if (key != lastNoteSet)
    {
        if (set > 0 && lastNoteSet >= 0)
            for (int i = 0; i < 12; ++i) params[(size_t) (fNote0 + ((i + root) % 12))]->set ((noteSets[set] >> i) & 1 ? 1.0f : 0.0f);
        lastNoteSet = key;
    }

    const bool hold = p (fHold) > 0.5f;
    const bool infinite = (int) p (fMode) == 1;
    const float vol = p (fVolume) * 0.01f, dry = p (fDry) * 0.01f;
    const float upStep = 1.0f / (std::max (5.0f, p (fFadeIn)) * 0.001f * sampleRate);
    const float downStep = 1.0f / (std::max (5.0f, p (fFadeOut)) * 0.001f * sampleRate);
    if (hold && ! holding) capture (false);
    holding = hold;

    for (int i = 0; i < n; ++i)
    {
        inRing[(size_t) inPos] = 0.5f * (L[i] + R[i]);
        inPos = (inPos + 1) % fftSize;
        if (++hopCount >= hop)
        {
            hopCount = 0;
            if (holding && infinite) capture (true);
            if (haveFrozen && (holding || level > 0)) synthFrame();
        }
        level = holding ? std::min (1.0f, level + upStep) : std::max (0.0f, level - downStep);
        const float a = outAcc[0][(size_t) outRead], b = outAcc[1][(size_t) outRead];
        outAcc[0][(size_t) outRead] = 0; outAcc[1][(size_t) outRead] = 0;
        outRead = (outRead + 1) % fftSize;
        const float g = level * level * vol;   // squared: fades sound even
        L[i] = L[i] * dry + a * g;
        R[i] = R[i] * dry + b * g;
    }
    if (! holding && level <= 0.0f) haveFrozen = false;
}

//==============================================================================
// Tremolo
TremoloBlock::TremoloBlock() : Block (Type::Tremolo)
{
    addChoice ("wave", "Type", { "Optical (sine)", "Triangle", "Choppy (square)", "Harmonic" }, 0);
    addFloat ("rate", "Rate", 0.5f, 15, 4.5f, "Hz", 4, 0.01f);
    addChoice ("sync", "Sync", { "Off", "1/1", "1/2", "1/4 dot", "1/4", "1/4 trip", "1/8 dot", "1/8", "1/8 trip", "1/16" }, 0);
    addFloat ("depth", "Depth", 0, 100, 60, "%", -1, 1);
    addFloat ("width", "Stereo (pan)", 0, 100, 0, "%", -1, 1);
}

void TremoloBlock::prepare (double sr, int)
{
    split[0].set (Biquad::Type::LowPass, (float) sr, 650.0f, 0.6f); split[1].set (Biquad::Type::HighPass, (float) sr, 650.0f, 0.6f);
    split[2].set (Biquad::Type::LowPass, (float) sr, 650.0f, 0.6f); split[3].set (Biquad::Type::HighPass, (float) sr, 650.0f, 0.6f);
    reset();
}

void TremoloBlock::process (float* L, float* R, int n)
{
    const int wave = (int) p (0);
    float rate = p (1);
    const float syncMs = syncToMs ((int) p (2), context().bpm.load());
    if (syncMs > 0) rate = 1000.0f / syncMs;
    const float depth = p (3) * 0.01f, width = p (4) * 0.01f;
    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        ph += rate / sampleRate; if (ph >= 1) ph -= 1;
        for (int c = 0; c < 2; ++c)
        {
            const float o = 0.5f * width * (float) c;
            const float s = std::sin (twoPi * (ph + o));
            float w;   // 0..1
            switch (wave)
            {
                case 1:  w = tri01 (ph + o); break;
                case 2:  w = 0.5f + 0.5f * fastTanh (s * 8.0f); break;
                default: w = std::pow (0.5f + 0.5f * s, 1.4f); break;   // optical: lingers at the bottom
            }
            const float x = ch[c][i];
            if (wave == 3)
            {
                // harmonic: lows and highs pulse in opposite directions
                const float lo = split[c * 2].process (x), hi = split[c * 2 + 1].process (x);
                const float g1 = 1.0f - depth * (0.5f + 0.5f * s), g2 = 1.0f - depth * (0.5f - 0.5f * s);
                ch[c][i] = lo * g1 + hi * g2;
            }
            else ch[c][i] = x * (1.0f - depth * (1.0f - w));
        }
    }
}

//==============================================================================
// Vibrato
VibratoBlock::VibratoBlock() : Block (Type::Vibrato)
{
    addFloat ("rate", "Rate", 0.5f, 12, 5.5f, "Hz", 4, 0.01f);
    addChoice ("sync", "Sync", { "Off", "1/1", "1/2", "1/4 dot", "1/4", "1/4 trip", "1/8 dot", "1/8", "1/8 trip", "1/16" }, 0);
    addFloat ("depth", "Depth", 0, 100, 40, "%", -1, 1);
    addChoice ("wave", "Wave", { "Sine", "Triangle" }, 0);
}

void VibratoBlock::prepare (double sr, int) { for (auto& l : line) l.setMaxSamples ((int) (0.03 * sr)); reset(); }

void VibratoBlock::process (float* L, float* R, int n)
{
    float rate = p (0);
    const float syncMs = syncToMs ((int) p (1), context().bpm.load());
    if (syncMs > 0) rate = 1000.0f / syncMs;
    const float depth = p (2) * 0.01f;
    const bool tri = (int) p (3) == 1;
    const float ms = 0.001f * sampleRate;
    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        ph += rate / sampleRate; if (ph >= 1) ph -= 1;
        const float l = tri ? tri01 (ph) : sin01 (ph);
        for (int c = 0; c < 2; ++c)
        {
            line[c].push (ch[c][i]);
            ch[c][i] = line[c].read ((1.0f + depth * 5.0f * l) * ms);
        }
    }
}

//==============================================================================
// Rotary (BOSS RT-20 / Leslie 122 style)
RotaryBlock::RotaryBlock() : Block (Type::Rotary)
{
    addChoice ("mode", "Mode", { "I (Leslie 122)", "II (wide)", "III (bright)" }, 0);
    addToggle ("fast", "Fast", false);
    addToggle ("brake", "Brake", false);
    addFloat ("rise", "Rise time", 0.2f, 5, 1.4f, "s", 1.2f, 0.01f);
    addFloat ("slow", "Slow speed", 0.3f, 2, 0.8f, "Hz", -1, 0.01f);
    addFloat ("fastHz", "Fast speed", 4, 9, 6.7f, "Hz", -1, 0.01f);
    addFloat ("balance", "Balance (bass/horn)", -100, 100, 0, "%", -1, 1);
    addFloat ("drive", "Overdrive", 0, 100, 15, "%", -1, 1);
    addFloat ("effect", "Effect level", 0, 100, 100, "%", -1, 1);
    addFloat ("direct", "Direct level", 0, 100, 0, "%", -1, 1);
}

void RotaryBlock::prepare (double sr, int)
{
    hornLine.setMaxSamples ((int) (0.01 * sr)); drumLine.setMaxSamples ((int) (0.01 * sr));
    xLow.set (Biquad::Type::LowPass, (float) sr, 800.0f, 0.707f);
    xHigh.set (Biquad::Type::HighPass, (float) sr, 800.0f, 0.707f);
    for (auto& c : cabinet) c.set (Biquad::Type::LowPass, (float) sr, 7500.0f, 0.6f);
    reset();
}

void RotaryBlock::reset()
{
    hornLine.clear(); drumLine.clear(); xLow.reset(); xHigh.reset();
    for (auto& t : hornTone) t.reset();
    for (auto& c : cabinet) c.reset();
    hornPh = 0; drumPh = 0.3f; hornRate = p (4); drumRate = p (4) * 0.85f;
}

void RotaryBlock::process (float* L, float* R, int n)
{
    const int mode = (int) p (0);
    const bool fast = p (1) > 0.5f, brake = p (2) > 0.5f;
    const float rise = p (3);
    const float hornTarget = brake ? 0.0f : (fast ? p (5) : p (4));
    const float drumTarget = brake ? 0.0f : (fast ? p (5) * 0.88f : p (4) * 0.83f);
    const float bal = p (6) * 0.01f, drive = p (7) * 0.01f;
    const float eff = p (8) * 0.01f, dir = p (9) * 0.01f;
    // the horn is light and spins up quickly; the heavy drum takes about 3.5x longer
    const float hornAcc = 1.0f / (rise * sampleRate), drumAcc = 1.0f / (rise * 3.5f * sampleRate);
    const float spread = mode == 1 ? 1.0f : 0.7f, doppler = mode == 2 ? 0.5f : 0.35f;
    const float ms = 0.001f * sampleRate;
    const float hornGain = 1.0f + std::min (0.0f, -bal) * -0.0f + (bal > 0 ? bal : 0) * 0.6f - (bal < 0 ? -bal : 0) * 0.6f;
    const float drumGain = 1.0f - (bal > 0 ? bal : 0) * 0.6f + (bal < 0 ? -bal : 0) * 0.6f;
    for (int i = 0; i < n; ++i)
    {
        const float dryIn = 0.5f * (L[i] + R[i]);
        float m = dryIn;
        if (drive > 0.001f) m = fastTanh (m * (1.0f + 8.0f * drive)) / (1.0f + 2.0f * drive);   // tube amp in the cabinet
        hornRate += (hornTarget - hornRate) * hornAcc * (hornTarget > hornRate ? 1.0f : 1.4f);
        drumRate += (drumTarget - drumRate) * drumAcc;
        hornPh += hornRate / sampleRate; hornPh -= std::floor (hornPh);
        drumPh += drumRate / sampleRate; drumPh -= std::floor (drumPh);
        const float lo = xLow.process (m), hi = xHigh.process (m);
        hornLine.push (hi); drumLine.push (lo);
        const float hs = std::sin (twoPi * hornPh), hc = std::cos (twoPi * hornPh);
        const float ds = std::sin (twoPi * drumPh), dcs = std::cos (twoPi * drumPh);
        // horn: doppler (pitch) + loudness and brightness as it points toward / away from each mic
        const float horn = hornLine.read ((0.5f + doppler * (1.0f + hs)) * ms + 2.0f);
        const float drum = drumLine.read ((0.4f + 0.15f * (1.0f + ds)) * ms + 2.0f);
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            const float side = c == 0 ? 1.0f : -1.0f;
            const float facing = 0.5f + 0.5f * hc * side * spread;          // 1 = pointing at this mic
            float h = horn * (0.45f + 0.75f * facing);
            hornTone[c].set (Biquad::Type::LowPass, sampleRate, (mode == 2 ? 5000.0f : 3000.0f) + 6000.0f * facing, 0.6f);
            h = hornTone[c].process (h);
            const float d = drum * (1.0f + 0.35f * dcs * side * spread);
            out[c] = cabinet[c].process (h * hornGain + d * drumGain) * 0.8f;
        }
        L[i] = out[0] * eff + dryIn * dir;
        R[i] = out[1] * eff + dryIn * dir;
    }
}

//==============================================================================
// Vibe
VibeBlock::VibeBlock() : Block (Type::Vibe)
{
    addChoice ("mode", "Mode", { "Chorus", "Vibrato" }, 0);
    addFloat ("rate", "Speed", 0.3f, 10, 2.0f, "Hz", 2, 0.01f);
    addChoice ("sync", "Sync", { "Off", "1/1", "1/2", "1/4 dot", "1/4", "1/4 trip", "1/8 dot", "1/8", "1/8 trip", "1/16" }, 0);
    addFloat ("depth", "Intensity", 0, 100, 70, "%", -1, 1);
    addFloat ("level", "Volume", -12, 12, 0, "dB", -1, 0.1f);
}

void VibeBlock::process (float* L, float* R, int n)
{
    const bool vibrato = (int) p (0) == 1;
    float rate = p (1);
    const float syncMs = syncToMs ((int) p (2), context().bpm.load());
    if (syncMs > 0) rate = 1000.0f / syncMs;
    const float depth = p (3) * 0.01f, lvl = dbToGain (p (4));
    static const float stage[4] = { 1.0f, 1.35f, 1.9f, 2.8f };   // the four staggered phase stages
    const float up = 1.0f - std::exp (-1.0f / (0.006f * sampleRate)), down = 1.0f - std::exp (-1.0f / (0.02f * sampleRate));
    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        ph += rate / sampleRate; if (ph >= 1) ph -= 1;
        // the lamp heats fast and cools slowly: that lopsided throb is the Vibe sound
        const float target = std::pow (sin01 (ph), 1.6f);
        lamp += (target - lamp) * (target > lamp ? up : down);
        for (int c = 0; c < 2; ++c)
        {
            float v = ch[c][i];
            for (int s = 0; s < 4; ++s)
            {
                const float f = 110.0f * stage[s] * std::pow (12.0f, lamp * depth);
                v = ap[c][s].process (v, apCoef (f, sampleRate));
            }
            ch[c][i] = (vibrato ? v : 0.5f * (ch[c][i] + v)) * lvl * (vibrato ? 1.0f : 1.3f);
        }
    }
}
