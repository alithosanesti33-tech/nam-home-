// Built-in effects. Every control here changes the sound; nothing is a cosmetic knob.
#include "BlockTypes.h"

using namespace ndsp;

static inline void mixInto (float& out, float dry, float wet, float mix01, bool killDry)
{
    const float dg = killDry ? 0.0f : std::min (1.0f, 2.0f * (1.0f - mix01));
    const float wg = std::min (1.0f, 2.0f * mix01);
    out = dry * dg + wet * wg;
}

//==============================================================================
// Noise gate
GateBlock::GateBlock() : Block (Type::Gate)
{
    addFloat ("threshold", "Threshold", -90, 0, -58, "dB", -50, 0.5f);
    addFloat ("attack", "Attack", 0.1f, 50, 1, "ms", 5);
    addFloat ("hold", "Hold", 0, 500, 40, "ms", 100);
    addFloat ("release", "Release", 5, 1000, 150, "ms", 150);
    addFloat ("range", "Depth", -90, 0, -80, "dB", -40, 0.5f);
}

void GateBlock::process (float* L, float* R, int n)
{
    const float thr = p (0), att = p (1), hold = p (2), rel = p (3), floorGain = dbToGain (p (4));
    env.set (sampleRate, 0.3f, 25.0f);
    const float aC = std::exp (-1.0f / (0.001f * att * sampleRate));
    const float rC = std::exp (-1.0f / (0.001f * rel * sampleRate));
    const int holdSamples = (int) (hold * 0.001f * sampleRate);
    const float openLin = dbToGain (thr), closeLin = dbToGain (thr - 6.0f);   // 6 dB hysteresis

    for (int i = 0; i < n; ++i)
    {
        const float e = env.process (std::max (std::abs (L[i]), std::abs (R[i])));
        if (e > openLin) { open = true; holdLeft = holdSamples; }
        else if (e < closeLin) { if (holdLeft > 0) --holdLeft; else open = false; }
        const float target = open ? 1.0f : floorGain;
        const float c = target > gain ? aC : rC;
        gain = target + c * (gain - target);
        L[i] *= gain; R[i] *= gain;
    }
}

//==============================================================================
// Compressor (feed-forward, soft knee)
CompressorBlock::CompressorBlock() : Block (Type::Compressor)
{
    addChoice ("model", "Model", { "Studio", "FET '76" }, 0);
    auto studio = [] (const Block& b) { return b.params[0]->getInt() == 0; };
    auto fet = [] (const Block& b) { return b.params[0]->getInt() == 1; };
    addFloat ("threshold", "Threshold", -60, 0, -18, "dB", -20, 0.5f).visible = studio;
    addFloat ("ratio", "Ratio", 1, 20, 4, "x", 4, 0.1f).visible = studio;
    addFloat ("attack", "Attack", 0.1f, 100, 8, "ms", 10).visible = studio;
    addFloat ("release", "Release", 10, 1000, 150, "ms", 150).visible = studio;
    addFloat ("knee", "Knee", 0, 12, 6, "dB", -1, 0.5f).visible = studio;
    addFloat ("makeup", "Makeup", 0, 24, 0, "dB", -1, 0.5f).visible = studio;
    addFloat ("mix", "Mix", 0, 100, 100, "%", -1, 1).visible = studio;
    // FET '76 (Cali76 / 1176 style): fixed threshold, In drives into it, Out is makeup, Dry blends in
    addFloat ("in", "In", 0, 40, 14, "dB", -1, 0.1f).visible = fet;
    addFloat ("out", "Out", -30, 12, -4, "dB", -1, 0.1f).visible = fet;
    addChoice ("fetRatio", "Ratio", { "4:1", "8:1", "12:1", "20:1", "All buttons" }, 1).visible = fet;
    addFloat ("fetAttack", "Attack", 0.02f, 10, 1.5f, "ms", 1, 0.01f).visible = fet;
    addFloat ("fetRelease", "Release", 50, 1100, 200, "ms", 250, 1).visible = fet;
    addFloat ("dry", "Dry", 0, 100, 0, "%", -1, 1).visible = fet;
}

void CompressorBlock::process (float* L, float* R, int n)
{
    if ((int) p (0) == 1) { processFet (L, R, n); return; }
    const float thr = p (1), ratio = p (2), knee = p (5), makeup = p (6), mix = p (7) * 0.01f;
    const float aC = std::exp (-1.0f / (0.001f * p (3) * sampleRate));
    const float rC = std::exp (-1.0f / (0.001f * p (4) * sampleRate));
    const float slope = 1.0f / ratio - 1.0f;
    float maxGr = 0;

    for (int i = 0; i < n; ++i)
    {
        const float x = std::max (std::abs (L[i]), std::abs (R[i]));
        const float xdb = gainToDb (x);
        const float over = xdb - thr;
        float target = 0;
        if (2.0f * over <= -knee) target = 0;
        else if (knee > 0 && 2.0f * std::abs (over) < knee) target = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        else target = slope * over;
        // gain reduction smoothing: attack when reducing more, release when recovering
        grDb = target < grDb ? aC * grDb + (1 - aC) * target : rC * grDb + (1 - rC) * target;
        const float g = std::exp ((grDb + makeup) * 0.11512925f);
        L[i] = L[i] * (1 - mix) + L[i] * g * mix;
        R[i] = R[i] * (1 - mix) + R[i] * g * mix;
        maxGr = std::min (maxGr, grDb);
    }
    reductionDb = maxGr;
}

// 1176-style FET compression: feedback detection from the output, very fast attack, a release
// that recovers faster after short peaks than after long held notes, the ratio nudging the
// threshold up like the hardware, a touch of FET colour, and a Dry knob for parallel compression.
void CompressorBlock::processFet (float* L, float* R, int n)
{
    const float inG = dbToGain (p (8)), outG = dbToGain (p (9));
    const int rsel = (int) p (10);
    static const float ratios[5] = { 4, 8, 12, 20, 30 }, thrShift[5] = { 0, 1.5f, 3, 4.5f, 2 };
    const float ratio = ratios[rsel];
    const bool allButtons = rsel == 4;
    const float thr = -26.0f + thrShift[rsel];
    const float aC = std::exp (-1.0f / (0.001f * p (11) * sampleRate));
    const float relMs = p (12);
    const float rFast = std::exp (-1.0f / (0.001f * relMs * 0.35f * sampleRate));
    const float rSlow = std::exp (-1.0f / (0.001f * relMs * 1.6f * sampleRate));
    const float holdC = std::exp (-1.0f / (0.4f * sampleRate));
    const float dryAmt = p (13) * 0.01f;
    const float slope = 1.0f / ratio - 1.0f;
    float maxGr = 0;
    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i] * inG, xr = R[i] * inG;
        // feedback: the detector hears the output (previous gain)
        const float g0 = std::exp (grDb * 0.11512925f);
        const float det = std::max (std::abs (xl), std::abs (xr)) * g0;
        const float over = gainToDb (det + 1.0e-9f) - thr;
        float target = 0;
        if (over > -3.0f)   // 1176s have a soft knee of a few dB
        {
            const float o = over < 3.0f ? (over + 3.0f) * (over + 3.0f) / 12.0f : over;
            target = slope * o * (allButtons ? 1.4f : 1.0f);
        }
        target = std::max (target, -40.0f);
        // program-dependent release: long compression builds up "slow" and recovers gently
        sustained = target < -1.0f ? holdC * sustained + (1 - holdC) : holdC * sustained;
        const float rC = rFast + (rSlow - rFast) * sustained;
        grDb = target < grDb ? aC * grDb + (1 - aC) * target : rC * grDb + (1 - rC) * target;
        const float g = std::exp (grDb * 0.11512925f);
        float yl = xl * g, yr = xr * g;
        // FET colour: a little even-harmonic warmth that grows with gain reduction (and lots with all buttons in)
        const float colour = std::min (0.25f, -grDb * (allButtons ? 0.02f : 0.006f));
        yl = yl + colour * yl * std::abs (yl) * 0.5f;
        yr = yr + colour * yr * std::abs (yr) * 0.5f;
        if (allButtons) { yl = fastTanh (yl * 1.3f) / 1.3f; yr = fastTanh (yr * 1.3f) / 1.3f; }
        L[i] = yl * outG + L[i] * dryAmt;
        R[i] = yr * outG + R[i] * dryAmt;
        maxGr = std::min (maxGr, grDb);
    }
    reductionDb = maxGr;
}

//==============================================================================
// Drive pedals (4x oversampled clipping)
DriveBlock::DriveBlock() : Block (Type::Drive)
{
    addChoice ("type", "Type", { "Green OD", "Transparent OD", "Clean boost", "Distortion", "Fuzz" }, 0);
    addFloat ("drive", "Drive", 0, 10, 5, {}, -1, 0.1f);
    addFloat ("tone", "Tone", 0, 10, 5, {}, -1, 0.1f);
    addFloat ("level", "Level", 0, 10, 5, {}, -1, 0.1f);
}

void DriveBlock::prepare (double sr, int maxBlockSize)
{
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) maxBlockSize);
    osLatency = (int) std::round (os->getLatencyInSamples());
    for (int c = 0; c < 2; ++c)
    {
        clean[c].assign ((size_t) maxBlockSize, 0.0f);
        cleanDelay[c].setMaxSamples (64);
        clipHp[c].reset(); tone[c].reset(); postHp[c].reset(); clipLp[c].reset();
        postHp[c].set (Biquad::Type::HighPass, (float) sr, 20.0f, 0.707f);
    }
    level.reset ((float) sr); gain.reset ((float) sr);
    lastType = -1;
}

void DriveBlock::updateFilters()
{
    const int t = (int) p (0);
    const float d = p (1) * 0.1f, tn = p (2) * 0.1f;
    if (t == lastType && d == lastDrive && tn == lastTone) return;
    lastType = t; lastDrive = d; lastTone = tn;
    const float osr = sampleRate * 4.0f;
    for (int c = 0; c < 2; ++c)
    {
        switch (t)
        {
            case 0: // Green OD: mid-focused clipping path, sweepable low-pass tone
                clipHp[c].set (Biquad::Type::HighPass, sampleRate, 720.0f, 0.6f);
                clipLp[c].setLowpass (osr, 6000.0f);
                tone[c].set (Biquad::Type::LowPass, sampleRate, 650.0f * std::pow (8.0f, tn), 0.6f);
                break;
            case 1: // Transparent OD: full-range clipping blended with clean, treble shelf tone
                clipHp[c].set (Biquad::Type::HighPass, sampleRate, 120.0f, 0.7f);
                clipLp[c].setLowpass (osr, 9000.0f);
                tone[c].set (Biquad::Type::HighShelf, sampleRate, 1800.0f, 0.7f, (tn - 0.5f) * 18.0f);
                break;
            case 2: // Clean boost
                clipHp[c].set (Biquad::Type::HighPass, sampleRate, 40.0f, 0.7f);
                clipLp[c].setLowpass (osr, 16000.0f);
                tone[c].set (Biquad::Type::HighShelf, sampleRate, 2500.0f, 0.7f, (tn - 0.5f) * 12.0f);
                break;
            case 3: // Distortion: op-amp bandwidth shrinks as gain rises, hard clipping, low-pass "filter"
                clipHp[c].set (Biquad::Type::HighPass, sampleRate, 90.0f, 0.7f);
                clipLp[c].setLowpass (osr, 18000.0f / (1.0f + 30.0f * d * d));
                tone[c].set (Biquad::Type::LowPass, sampleRate, 600.0f * std::pow (20.0f, tn), 0.7f);
                break;
            default: // Fuzz
                clipHp[c].set (Biquad::Type::HighPass, sampleRate, 60.0f, 0.7f);
                clipLp[c].setLowpass (osr, 7000.0f);
                tone[c].set (Biquad::Type::LowPass, sampleRate, 400.0f * std::pow (25.0f, tn), 0.6f);
                break;
        }
    }
}

void DriveBlock::process (float* L, float* R, int n)
{
    updateFilters();
    const int t = (int) p (0);
    const float d = p (1) * 0.1f;
    float g = 1, cleanMix = 0, makeupDb = 0;
    switch (t)
    {
        case 0: g = 1.0f + 140.0f * d * d;  cleanMix = 0.6f; makeupDb = -7; break;
        case 1: g = 1.0f + 60.0f * d * d;   cleanMix = 0.9f - 0.6f * d; makeupDb = -12; break;
        case 2: g = dbToGain (24.0f * d);   cleanMix = 0; makeupDb = -6.0f * d; break;
        case 3: g = 1.0f + 2400.0f * d * d * d; cleanMix = 0; makeupDb = -19; break;
        default: g = 1.0f + 400.0f * d * d; cleanMix = 0; makeupDb = -17; break;
    }
    gain.setTarget (g);
    level.setTarget (dbToGain ((p (3) - 5.0f) * 5.0f + makeupDb));

    float* ch[2] = { L, R };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            clean[c][(size_t) i] = ch[c][i];
            ch[c][i] = clipHp[c].process (ch[c][i]);
        }
    // gain is applied before the clipper (smoothed per sample, same for both channels)
    for (int i = 0; i < n; ++i) { const float gg = gain.next(); L[i] *= gg; R[i] *= gg; }

    juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
    auto up = os->processSamplesUp (block);
    for (int c = 0; c < 2; ++c)
    {
        float* x = up.getChannelPointer ((size_t) c);
        const int m = (int) up.getNumSamples();
        for (int i = 0; i < m; ++i)
        {
            float v = clipLp[c].lp (x[i]);
            switch (t)
            {
                case 0: v = fastTanh (v) * 0.7f; break;                                   // soft, symmetric diodes
                case 1: v = v / (1.0f + std::abs (v)); break;                             // gentle
                case 2: v = fastTanh (v * 0.8f) / 0.8f; break;                            // only clips when pushed
                case 3: v = v / std::pow (1.0f + std::pow (std::abs (v), 2.5f), 0.4f); break; // hard-ish diodes to ground
                default:
                {
                    const float bias = 0.35f;                                             // asymmetric, sputtery
                    v = fastTanh (v + bias) - fastTanh (bias);
                    v = v > 0 ? v : v * 0.7f;
                    break;
                }
            }
            x[i] = v;
        }
    }
    os->processSamplesDown (block);

    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            cleanDelay[c].push (clean[c][(size_t) i]);
            const float dry = osLatency > 0 ? cleanDelay[c].readInt (osLatency + 1) : clean[c][(size_t) i];
            float y = ch[c][i] + dry * cleanMix;
            y = tone[c].process (y);
            y = postHp[c].process (y);
            ch[c][i] = y;
        }
    for (int i = 0; i < n; ++i) { const float lv = level.next(); L[i] *= lv; R[i] *= lv; }
}

//==============================================================================
// Wah (inductor-style resonant sweep)
WahBlock::WahBlock() : Block (Type::Wah)
{
    addFloat ("position", "Position", 0, 100, 50, "%", -1, 0.1f);
    addFloat ("low", "Heel freq", 250, 650, 380, "Hz", -1, 1);
    addFloat ("high", "Toe freq", 1200, 3500, 2200, "Hz", -1, 1);
    addFloat ("q", "Q", 1, 14, 5.5f, {}, 5, 0.1f);
    addFloat ("level", "Level", -12, 12, 0, "dB", -1, 0.1f);
}

void WahBlock::process (float* L, float* R, int n)
{
    const float target = p (0) * 0.01f, lo = p (1), hi = p (2), q = p (3);
    if (posSm < 0) posSm = target;
    level.reset (sampleRate);
    level.setTarget (dbToGain (p (4)));
    const float smooth = std::exp (-1.0f / (0.008f * sampleRate));
    for (int c = 0; c < 2; ++c) hp[c].set (Biquad::Type::HighPass, sampleRate, 70.0f, 0.7f);

    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        posSm = target + smooth * (posSm - target);
        if ((i & 7) == 0)
        {
            // exponential (log-pot style) sweep, Q falls slightly toward the toe like the real circuit
            const float f = lo * std::pow (hi / lo, std::pow (posSm, 0.85f));
            for (auto& s : svf) s.set (sampleRate, f, q * (1.0f - 0.25f * posSm));
        }
        const float lv = level.next();
        for (int c = 0; c < 2; ++c)
        {
            float lp, bp, hpo;
            const float x = hp[c].process (ch[c][i]);
            svf[c].tick (x, lp, bp, hpo);
            const float bpNorm = bp * svf[c].k;            // unity gain at the peak
            ch[c][i] = (bpNorm * 5.5f + lp * 0.25f) * lv;
        }
    }
}

//==============================================================================
VolumeBlock::VolumeBlock() : Block (Type::Volume)
{
    addFloat ("position", "Position", 0, 100, 100, "%", -1, 0.1f);
    addFloat ("min", "Heel level", -60, 0, -60, "dB", -1, 0.5f);
    addChoice ("taper", "Taper", { "Audio (log)", "Linear" }, 0);
}

void VolumeBlock::process (float* L, float* R, int n)
{
    const float pos = p (0) * 0.01f, minDb = p (1);
    float target;
    if (p (2) < 0.5f) target = (pos <= 0.001f && minDb <= -59.5f) ? 0.0f : dbToGain (lerp (minDb, 0.0f, pos));
    else target = lerp (minDb <= -59.5f ? 0.0f : dbToGain (minDb), 1.0f, pos);
    g.setTarget (target);
    for (int i = 0; i < n; ++i) { const float v = g.next(); L[i] *= v; R[i] *= v; }
}

//==============================================================================
const float GraphicEQBlock::freqs[GraphicEQBlock::numBands] = { 31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

GraphicEQBlock::GraphicEQBlock() : Block (Type::GraphicEQ)
{
    const char* names[] = { "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" };
    for (int b = 0; b < numBands; ++b)
        addFloat ("b" + juce::String (b), names[b], -12, 12, 0, "dB", -1, 0.1f);
    addFloat ("level", "Level", -12, 12, 0, "dB", -1, 0.1f);
}

void GraphicEQBlock::process (float* L, float* R, int n)
{
    for (int b = 0; b < numBands; ++b)
        if (dirty || p (b) != last[b])
        {
            last[b] = p (b);
            for (int c = 0; c < 2; ++c) f[c][b].set (Biquad::Type::Peak, sampleRate, freqs[b], 1.4f, p (b));
        }
    dirty = false;
    level.reset (sampleRate);
    level.setTarget (dbToGain (p (numBands)));
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        for (int b = 0; b < numBands; ++b)
            if (last[b] != 0.0f) { l = f[0][b].process (l); r = f[1][b].process (r); }
        const float lv = level.next();
        L[i] = l * lv; R[i] = r * lv;
    }
}

//==============================================================================
ParametricEQBlock::ParametricEQBlock() : Block (Type::ParametricEQ)
{
    addToggle ("hpOn", "Low cut", true);
    addFloat ("hpFreq", "Low cut freq", 20, 1000, 80, "Hz", 120, 1);
    addToggle ("lpOn", "High cut", true);
    addFloat ("lpFreq", "High cut freq", 1000, 20000, 10000, "Hz", 6000, 1);
    const float defF[] = { 100, 400, 1000, 3000, 8000 };
    for (int b = 0; b < numBands; ++b)
    {
        const auto s = juce::String (b + 1);
        addFloat ("f" + s, "Freq " + s, 20, 20000, defF[b], "Hz", 1000, 1);
        addFloat ("g" + s, "Gain " + s, -15, 15, 0, "dB", -1, 0.1f);
        addFloat ("q" + s, "Q " + s, 0.2f, 10, 0.9f, {}, 1, 0.01f);
    }
    addFloat ("level", "Level", -12, 12, 0, "dB", -1, 0.1f);
}

void ParametricEQBlock::design (Biquad* out, float sr) const
{
    out[0] = {};
    if (p (0) > 0.5f) out[0].set (Biquad::Type::HighPass, sr, p (1), 0.707f);
    for (int b = 0; b < numBands; ++b)
    {
        const float fr = p (4 + b * 3), g = p (5 + b * 3), q = p (6 + b * 3);
        const auto t = b == 0 ? Biquad::Type::LowShelf : b == numBands - 1 ? Biquad::Type::HighShelf : Biquad::Type::Peak;
        out[1 + b] = {};
        if (g != 0.0f) out[1 + b].set (t, sr, fr, t == Biquad::Type::Peak ? q : std::min (q, 1.2f), g);
    }
    out[numBands + 1] = {};
    if (p (2) > 0.5f) out[numBands + 1].set (Biquad::Type::LowPass, sr, p (3), 0.707f);
}

double ParametricEQBlock::responseDb (double freq) const
{
    Biquad tmp[numBands + 2];
    design (tmp, sampleRate);
    double mag = 1.0;
    for (auto& b : tmp) mag *= b.magnitude (freq, sampleRate);
    return 20.0 * std::log10 (std::max (1.0e-6, mag)) + p ((int) params.size() - 1);
}

void ParametricEQBlock::process (float* L, float* R, int n)
{
    const size_t np = params.size();
    if (last.size() != np) { last.assign (np, -9999.0f); dirty = true; }
    bool changed = dirty;
    for (size_t k = 0; k + 1 < np; ++k)
        if (last[k] != p ((int) k)) { last[k] = p ((int) k); changed = true; }
    if (changed)
    {
        Biquad tmp[numBands + 2];
        design (tmp, sampleRate);
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < numBands + 2; ++b)
            {
                tmp[b].z1 = f[c][b].z1; tmp[b].z2 = f[c][b].z2;   // keep filter state: no clicks while dragging
                f[c][b] = tmp[b];
            }
        dirty = false;
    }
    level.reset (sampleRate);
    level.setTarget (dbToGain (p ((int) np - 1)));
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        for (int b = 0; b < numBands + 2; ++b) { l = f[0][b].process (l); r = f[1][b].process (r); }
        const float lv = level.next();
        L[i] = l * lv; R[i] = r * lv;
    }
}

//==============================================================================
UtilityBlock::UtilityBlock() : Block (Type::Utility)
{
    addFloat ("level", "Level", -60, 24, 0, "dB", -1, 0.1f);
    addFloat ("pan", "Pan", -1, 1, 0, "pan", -1, 0.01f);
    addFloat ("width", "Width", 0, 200, 100, "%", -1, 1);
    addToggle ("polarity", "Polarity invert", false);
    addToggle ("mono", "Mono", false);
}

void UtilityBlock::process (float* L, float* R, int n)
{
    const float g = dbToGain (p (0)) * (p (3) > 0.5f ? -1.0f : 1.0f);
    const float pan = p (1);
    gl.setTarget (g * std::min (1.0f, 1.0f - pan));
    gr.setTarget (g * std::min (1.0f, 1.0f + pan));
    const float width = p (4) > 0.5f ? 0.0f : p (2) * 0.01f;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * width;
        L[i] = (m + s) * gl.next();
        R[i] = (m - s) * gr.next();
    }
}

//==============================================================================
// Delay
DelayBlock::DelayBlock() : Block (Type::Delay)
{
    addChoice ("type", "Type", { "Digital", "Tape", "Analog", "Ping-pong", "Dual", "Reverse", "Multi-tap", "Swell", "Crystal" }, 0);
    addFloat ("time", "Time", 20, 2000, 400, "ms", 400, 1);
    addChoice ("sync", "Sync", { "Off", "1/1", "1/2", "1/4 dot", "1/4", "1/4 trip", "1/8 dot", "1/8", "1/8 trip", "1/16" }, 0);
    addFloat ("feedback", "Feedback", 0, 110, 35, "%", -1, 1);
    addFloat ("mix", "Mix", 0, 100, 30, "%", -1, 1);
    addFloat ("lowCut", "Low cut", 20, 1000, 80, "Hz", 150, 1);
    addFloat ("highCut", "High cut", 1000, 20000, 9000, "Hz", 6000, 1);
    addFloat ("modDepth", "Mod depth", 0, 100, 15, "%", -1, 1);
    addFloat ("modRate", "Mod rate", 0.05f, 8, 0.6f, "Hz", 1, 0.01f);
    auto& w = addFloat ("wow", "Wow", 0, 100, 30, "%", -1, 1);
    auto& fl = addFloat ("flutter", "Flutter", 0, 100, 25, "%", -1, 1);
    auto& sat = addFloat ("saturation", "Saturation", 0, 100, 35, "%", -1, 1);
    auto& ag = addFloat ("age", "Age", 0, 100, 40, "%", -1, 1);
    auto& ratio = addFloat ("ratio", "Right time", 25, 150, 75, "%", -1, 1);
    addFloat ("spread", "Stereo spread", 0, 100, 50, "%", -1, 1);
    auto& sw = addFloat ("swell", "Swell time", 50, 3000, 600, "ms", 600, 1);
    addToggle ("killDry", "Kill dry (100% wet)", false);
    addToggle ("trails", "Trails", true);
    // pitch in the repeats: each echo climbs or falls by the interval (Crystal: reversed, an octave up)
    addChoice ("pitch", "Repeat pitch", { "Off", "+1 octave", "-1 octave", "+5th", "+4th", "-5th", "Detune" }, 0);
    addFloat ("pitchMix", "Pitch amount", 0, 100, 100, "%", -1, 1);

    auto isType = [] (std::initializer_list<int> ts) { return [ts] (const Block& b) { const int t = (int) b.params[0]->get(); for (int x : ts) if (x == t) return true; return false; }; };
    w.visible = fl.visible = isType ({ Tape });
    sat.visible = isType ({ Tape, Analog });
    ag.visible = isType ({ Tape, Analog });
    ratio.visible = isType ({ Dual });
    sw.visible = isType ({ Swell });
    params[7]->visible = params[8]->visible = isType ({ Digital, Analog, PingPong, Dual, MultiTap, Swell });
}

void DelayBlock::prepare (double sr, int)
{
    for (int c = 0; c < 2; ++c)
    {
        line[c].setMaxSamples ((int) (sr * 3.2));
        timeSm[c].reset ((float) sr, 0.18f);
        timeSm[c].started = false;
        wow[c].setRate ((float) sr, 0.9f + 0.25f * c);
        wow[c].seed = 1234u + (uint32_t) c * 777u;
        headBump[c].set (Biquad::Type::Peak, (float) sr, 110.0f, 0.8f, 1.5f);
    }
    swellEnv.set ((float) sr, 2.0f, 80.0f);
    for (auto& ps : pitchSh) ps.prepare ((float) sr, 0.045f, 0.2f);
    reset();
}

void DelayBlock::reset()
{
    for (int c = 0; c < 2; ++c) { line[c].clear(); fbLow[c].reset(); fbHigh[c].reset(); age[c].reset(); fbState[c] = 0; revPhase[c] = c * 0.5f; pitchSh[c].clear(); crystalRaw[c] = 0; }
    swellGain = 0;
}

void DelayBlock::process (float* L, float* R, int n)
{
    const int kind = (int) p (0);
    float ms = syncToMs ((int) p (2), context().bpm.load());
    if (ms < 0) ms = p (1);
    if (kind == Reverse || kind == Crystal) ms = std::min (ms, 1400.0f);
    {
        static const float semis[7] = { 0, 12, -12, 7, 5, -7, 0 };
        const int pc = (int) p (18);
        pitchRatio[0] = pitchRatio[1] = std::exp2 (semis[pc] / 12.0f);
        if (pc == 6) { pitchRatio[0] = std::exp2 (0.12f / 12.0f); pitchRatio[1] = std::exp2 (-0.12f / 12.0f); }
    }
    const float pitchMix = p (19) * 0.01f;
    const float baseD = ms * 0.001f * sampleRate;
    const float fb = std::min (p (3) * 0.01f, (kind == Tape || kind == Analog) ? 1.1f : 0.98f);
    const bool killDry = p (16) > 0.5f;
    // the talk box can fade the echoes out while you talk (the repeats keep running underneath)
    {
        const int st = context().talkStamp.load();
        duckStale = st == duckStamp ? duckStale + 1 : 0;
        duckStamp = st;
    }
    duckSm += 0.15f * ((killDry || duckStale > 8 ? 1.0f : context().talkDuck.load()) - duckSm);
    const float mix = p (4) * 0.01f * duckSm;
    const float modDepth = p (7) * 0.01f, wowAmt = p (9) * 0.01f, flutAmt = p (10) * 0.01f;
    const float sat = p (11) * 0.01f, ageAmt = p (12) * 0.01f, ratio = p (13) * 0.01f, spread = p (14) * 0.01f;
    const float swellCoef = 1.0f / std::max (1.0f, p (15) * 0.001f * sampleRate);

    for (int c = 0; c < 2; ++c)
    {
        fbLow[c].set (Biquad::Type::HighPass, sampleRate, p (5), 0.707f);
        fbHigh[c].set (Biquad::Type::LowPass, sampleRate, p (6), 0.707f);
        const float ageHz = kind == Tape ? 9000.0f - 6500.0f * ageAmt : 5000.0f - 3600.0f * ageAmt;
        age[c].setLowpass (sampleRate, ageHz);
        timeSm[c].setTarget (kind == Dual && c == 1 ? baseD * ratio : baseD);
    }
    mod.setRate (sampleRate, p (8));
    flutter.setRate (sampleRate, 6.5f);

    for (int i = 0; i < n; ++i)
    {
        float in[2] = { L[i], R[i] };
        if (kind == Swell)
        {
            // volume swell: each new phrase fades in instead of attacking
            const float e = swellEnv.process (0.5f * (in[0] + in[1]));
            if (e < 0.004f) swellGain = 0.0f;
            else swellGain = std::min (1.0f, swellGain + swellCoef);
            const float sg = swellGain * swellGain;
            in[0] *= sg; in[1] *= sg;
        }

        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            float d = timeSm[c].next();
            if (kind == Tape)
                d += wow[c].next() * wowAmt * 0.0035f * sampleRate + flutter.sine (c * 0.1f) * flutAmt * 0.00035f * sampleRate;
            else if (kind != Reverse && kind != Crystal)
                d += mod.sine (c * 0.25f) * modDepth * 0.0025f * sampleRate;
            d = std::max (2.0f, d);

            if (kind == Reverse || kind == Crystal)
            {
                const float W = std::max (64.0f, d);
                const float ph1 = revPhase[c], ph2 = ph1 + 0.5f - std::floor (ph1 + 0.5f);
                const float w1 = std::sin (pi * ph1), w2 = std::sin (pi * ph2);
                y[c] = line[c].read (2.0f + 2.0f * ph1 * W) * w1 * w1 + line[c].read (2.0f + 2.0f * ph2 * W) * w2 * w2;
                revPhase[c] += 1.0f / W;
                if (revPhase[c] >= 1.0f) revPhase[c] -= 1.0f;
                if (kind == Crystal) { crystalRaw[c] = y[c]; y[c] = pitchSh[c].process (y[c], 2.0f) * 1.1f; }
            }
            else if (kind == MultiTap)
            {
                static const float lv[4] = { 0.45f, 0.6f, 0.75f, 1.0f };
                float s = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const bool mine = (k % 2) == c || spread < 0.5f;
                    s += line[c].read (d * (k + 1) * 0.25f) * lv[k] * (mine ? 1.0f : 1.0f - spread);
                }
                y[c] = s * 0.6f;
                fbState[c] = line[c].read (d);
            }
            else
                y[c] = line[c].read (d);
        }

        for (int c = 0; c < 2; ++c)
        {
            float f = (kind == MultiTap) ? fbState[c] : (kind == Crystal ? crystalRaw[c] : y[c]);
            f = fbHigh[c].process (fbLow[c].process (f));
            if (kind == Tape)
            {
                f = headBump[c].process (age[c].lp (f));
                const float drv = 1.0f + sat * 4.0f;
                f = fastTanh (f * drv) / drv * (1.0f + sat * 0.5f);
            }
            else if (kind == Analog)
            {
                f = age[c].lp (f);
                f = fastTanh (f * (1.0f + sat * 2.0f)) / (1.0f + sat * 0.8f);
            }
            if (pitchRatio[c] != 1.0f && kind != Crystal)
                f = f * (1.0f - pitchMix) + pitchSh[c].process (f, pitchRatio[c]) * pitchMix;
            fbState[c] = f;
        }

        if (kind == PingPong)
        {
            const float m = 0.5f * (in[0] + in[1]);
            line[0].push (m + fb * fbState[1]);
            line[1].push (fb * fbState[0]);
        }
        else
        {
            for (int c = 0; c < 2; ++c) line[c].push (in[c] + fb * fbState[c]);
        }

        // stereo spread for the mono-ish types
        if (kind != PingPong && kind != Dual && kind != MultiTap)
        {
            const float m = 0.5f * (y[0] + y[1]), s = 0.5f * (y[0] - y[1]) * (0.5f + spread);
            y[0] = m + s; y[1] = m - s;
        }
        mixInto (L[i], in[0], y[0], mix, killDry);
        mixInto (R[i], in[1], y[1], mix, killDry);
        mod.advance();
        flutter.advance();
    }
}

//==============================================================================
// Reverb: 8-line feedback delay network, plus a separate spring model
ReverbBlock::ReverbBlock() : Block (Type::Reverb)
{
    addChoice ("type", "Type", { "Room", "Hall", "Plate", "Spring", "Shimmer", "Bloom", "Ambient" }, 1);
    addFloat ("decay", "Decay", 0.2f, 20, 2.5f, "s", 2.5f, 0.01f);
    addFloat ("predelay", "Pre-delay", 0, 250, 15, "ms", 50, 1);
    addFloat ("size", "Size", 0, 100, 60, "%", -1, 1);
    addFloat ("highCut", "High cut", 1000, 16000, 7000, "Hz", 5000, 1);
    addFloat ("lowCut", "Low cut", 20, 1000, 100, "Hz", 150, 1);
    addFloat ("mix", "Mix", 0, 100, 25, "%", -1, 1);
    auto& md = addFloat ("mod", "Modulation", 0, 100, 30, "%", -1, 1);
    auto& df = addFloat ("diffusion", "Diffusion", 0, 100, 70, "%", -1, 1);
    auto& sp = addChoice ("pitch", "Shimmer pitch", { "+1 octave", "+5th", "+2 octaves", "-1 octave", "+4th", "+2nd", "-5th", "Detuned" }, 0);
    auto& sa = addFloat ("shimmer", "Shimmer amount", 0, 100, 50, "%", -1, 1);
    auto& dw = addFloat ("dwell", "Dwell", 0, 100, 50, "%", -1, 1);
    auto& dr = addFloat ("drip", "Drip", 0, 100, 40, "%", -1, 1);
    auto& sw = addFloat ("swell", "Swell time", 50, 3000, 900, "ms", 600, 1);
    addToggle ("killDry", "Kill dry (100% wet)", false);
    addToggle ("trails", "Trails", true);

    auto is = [] (std::initializer_list<int> ts) { return [ts] (const Block& b) { const int t = (int) b.params[0]->get(); for (int x : ts) if (x == t) return true; return false; }; };
    auto isNot = [] (int t) { return [t] (const Block& b) { return (int) b.params[0]->get() != t; }; };
    sp.visible = sa.visible = is ({ Shimmer });
    dw.visible = dr.visible = is ({ Spring });
    sw.visible = is ({ Bloom });
    md.visible = isNot (Spring);
    df.visible = isNot (Spring);
}

void ReverbBlock::prepare (double sr, int)
{
    pre.setMaxSamples ((int) (0.3 * sr));
    for (int i = 0; i < N; ++i)
    {
        fdn[i].setMaxSamples ((int) (0.2 * sr));
        wander[i].setRate ((float) sr, 0.3f + 0.11f * i);
        wander[i].seed = 9001u + (uint32_t) i * 131u;
    }
    shimmerBuf.setMaxSamples ((int) (0.2 * sr));
    for (int c = 0; c < 2; ++c)
    {
        springLine[c].setMaxSamples ((int) (0.12 * sr));
        for (int k = 0; k < 12; ++k) springAp[c][k].init (2 + (k % 3) + c);
        springLp[c].set (Biquad::Type::LowPass, (float) sr, 4200.0f, 0.6f);
        springHp[c].set (Biquad::Type::HighPass, (float) sr, 140.0f, 0.7f);
        drip[c].set (Biquad::Type::BandPass, (float) sr, 2400.0f + 300.0f * c, 3.0f);
    }
    bloomEnv.set ((float) sr, 2.0f, 120.0f);
    configuredKind = -1;
    reset();
}

void ReverbBlock::Plate::init (float sr, float size)
{
    // Jon Dattorro, "Effect Design Part 1" (1997): figure-of-eight plate tank, lengths at 29761 Hz
    scale = sr / 29761.0f * (0.6f + 0.8f * size);
    const float s = sr / 29761.0f;
    static const int in[4] = { 142, 107, 379, 277 };
    for (int k = 0; k < 4; ++k) inDiff[k].init ((int) (in[k] * s));
    lenDD1[0] = 672.0f * scale; lenDD1[1] = 908.0f * scale;
    decDiff1[0].init ((int) (lenDD1[0] + 32 * s) + 4); decDiff1[1].init ((int) (lenDD1[1] + 32 * s) + 4);
    decDiff1[0].length = lenDD1[0]; decDiff1[1].length = lenDD1[1];
    lenD1[0] = (int) (4453 * scale); lenD1[1] = (int) (4217 * scale);
    decDiff2[0].init ((int) (1800 * scale)); decDiff2[1].init ((int) (2656 * scale));
    lenD2[0] = (int) (3720 * scale); lenD2[1] = (int) (3163 * scale);
    for (int k = 0; k < 2; ++k) { d1[k].setMaxSamples (lenD1[k] + 4); d2[k].setMaxSamples (lenD2[k] + 4); }
    lfo.setRate (sr, 1.0f);
    clear();
}

void ReverbBlock::Plate::clear()
{
    for (auto& a : inDiff) a.clear();
    for (int k = 0; k < 2; ++k) { decDiff1[k].clear(); decDiff2[k].clear(); d1[k].clear(); d2[k].clear(); damping[k].reset(); feedback[k] = 0; }
    bandwidth.reset();
}

void ReverbBlock::reset()
{
    plate.clear();
    pre.clear(); shimmerBuf.clear();
    for (int i = 0; i < N; ++i) { fdn[i].clear(); damp[i].reset(); fdnOut[i] = 0; }
    for (auto& d : diff) d.clear();
    for (int c = 0; c < 2; ++c) { springLine[c].clear(); springFb[c] = 0; for (auto& a : springAp[c]) a.clear(); lowCut[c].reset(); }
    bloomGain = 0;
}

void ReverbBlock::configure (int kind, float size)
{
    static const float room[N]  = { 8.3f, 9.7f, 11.3f, 12.9f, 14.6f, 16.2f, 18.1f, 19.7f };
    static const float hall[N]  = { 29.1f, 33.7f, 37.9f, 43.1f, 47.3f, 53.9f, 59.3f, 67.1f };
    static const float plate[N] = { 16.1f, 19.3f, 22.7f, 25.9f, 28.3f, 31.1f, 34.9f, 38.3f };
    const float* base = kind == Room ? room : kind == Plate ? plate : hall;
    const float k = (kind == Shimmer || kind == Bloom || kind == Ambient) ? 1.3f : 1.0f;
    const float scale = (0.5f + size) * k;
    for (int i = 0; i < N; ++i) len[i] = base[i] * scale * 0.001f * sampleRate;
    static const float dms[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
    for (int d = 0; d < 4; ++d) diff[d].init ((int) (dms[d] * (0.6f + 0.6f * size) * 0.001f * sampleRate));
    configuredKind = kind;
    configuredSize = size;
}

void ReverbBlock::process (float* L, float* R, int n)
{
    const int kind = (int) p (0);
    const float size = p (3) * 0.01f;
    if (kind != configuredKind || std::abs (size - configuredSize) > 0.02f) configure (kind, size);

    const float decay = p (1);
    const float preS = std::max (1.0f, p (2) * 0.001f * sampleRate);
    float hc = p (4);
    if (kind == Plate) hc = std::min (16000.0f, hc * 1.5f);
    const bool killDry = p (14) > 0.5f;
    {
        const int st = context().talkStamp.load();
        duckStale = st == duckStamp ? duckStale + 1 : 0;
        duckStamp = st;
    }
    duckSm += 0.15f * ((killDry || duckStale > 8 ? 1.0f : context().talkDuck.load()) - duckSm);   // talk box: dry while talking
    const float mix = p (6) * 0.01f * duckSm;
    const float modAmt = p (7) * 0.01f * (kind == Ambient ? 2.0f : 1.0f);
    const float diffG = 0.45f + 0.3f * p (8) * 0.01f;
    static const float ratios[8] = { 2.0f, 1.5f, 4.0f, 0.5f, 1.33484f, 1.12246f, 0.66742f, 1.00696f };
    const float shRatio = ratios[juce::jlimit (0, 7, (int) p (9))];
    const float shAmt = p (10) * 0.01f;
    const float dwell = p (11) * 0.01f, dripAmt = p (12) * 0.01f;
    const float bloomCoef = 1.0f / std::max (1.0f, p (13) * 0.001f * sampleRate);

    float g[N];
    for (int i = 0; i < N; ++i)
    {
        g[i] = std::pow (10.0f, -3.0f * len[i] / (decay * sampleRate));
        damp[i].setLowpass (sampleRate, hc);
    }
    for (int c = 0; c < 2; ++c) lowCut[c].set (Biquad::Type::HighPass, sampleRate, p (5), 0.707f);

    // spring loop timing
    const float sLen[2] = { (0.033f + 0.012f * size) * sampleRate, (0.039f + 0.012f * size) * sampleRate };
    const float sG[2] = { std::pow (10.0f, -3.0f * sLen[0] / (decay * sampleRate)), std::pow (10.0f, -3.0f * sLen[1] / (decay * sampleRate)) };
    const float shW = 0.05f * sampleRate;

    for (int i = 0; i < n; ++i)
    {
        float x = 0.5f * (L[i] + R[i]);
        if (kind == Bloom)
        {
            const float e = bloomEnv.process (x);
            if (e < 0.003f) bloomGain = 0; else bloomGain = std::min (1.0f, bloomGain + bloomCoef);
            x *= bloomGain * bloomGain;
        }
        pre.push (x);
        float xp = pre.read (preS);
        float wl = 0, wr = 0;

        if (kind == Plate)
        {
            if (std::abs (size - plateSize) > 0.02f) { plate.init (sampleRate, size); plateSize = size; }
            auto& P = plate;
            const float s = sampleRate / 29761.0f;
            P.bandwidth.setLowpass (sampleRate, 9000.0f);
            for (auto& d : P.damping) d.setLowpass (sampleRate, hc);
            // decay from the requested time: loop is ~ (d1 + d2 + diffusers) samples long
            const float loop = (float) (P.lenD1[0] + P.lenD2[0]) + P.lenDD1[0] + 1800.0f * P.scale;
            const float dec = std::min (0.9995f, std::pow (10.0f, -3.0f * loop / (decay * sampleRate)));
            float v = P.bandwidth.lp (xp);
            v = P.inDiff[0].process (v, 0.75f); v = P.inDiff[1].process (v, 0.75f);
            v = P.inDiff[2].process (v, 0.625f); v = P.inDiff[3].process (v, 0.625f);
            const float exc = (8.0f + 16.0f * modAmt) * s;
            float tankOut[2];
            for (int k = 0; k < 2; ++k)
            {
                float t = v + P.feedback[1 - k] * dec;
                t = P.decDiff1[k].process (t, -0.7f, exc * P.lfo.sine (k * 0.25f));
                P.d1[k].push (t);
                t = P.damping[k].lp (P.d1[k].readInt (P.lenD1[k])) * dec;
                t = P.decDiff2[k].process (t, 0.5f);
                P.d2[k].push (t);
                tankOut[k] = P.d2[k].readInt (P.lenD2[k]);
                P.feedback[k] = tankOut[k];
            }
            P.lfo.advance();
            // output taps (scaled from the paper's table)
            auto tap = [&P, s] (DelayLine& d, int n) { return d.readInt (std::max (1, (int) (n * s * (P.scale / s)))); };
            wl = 0.6f * (tap (P.d1[1], 266) + tap (P.d1[1], 2974) - tap (P.d2[1], 1913) + tap (P.d2[0], 1996)
                         - tap (P.d1[0], 1990) - tap (P.d2[0], 187) - tap (P.d2[1], 1066));
            wr = 0.6f * (tap (P.d1[0], 353) + tap (P.d1[0], 3627) - tap (P.d2[0], 1228) + tap (P.d2[1], 2673)
                         - tap (P.d1[1], 2111) - tap (P.d2[1], 335) - tap (P.d2[0], 121));
        }
        else if (kind == Spring)
        {
            for (int c = 0; c < 2; ++c)
            {
                float v = fastTanh (xp * (1.0f + dwell * 4.0f)) * 0.6f + springFb[c] * sG[c];
                for (auto& a : springAp[c]) v = a.process (v, 0.62f);     // dispersion: the "boing" chirp
                springLine[c].push (v);
                float o = springLine[c].read (sLen[c]);
                o = springHp[c].process (springLp[c].process (o));
                springFb[c] = o;
                const float out = o + drip[c].process (o) * dripAmt * 2.5f;
                (c == 0 ? wl : wr) = out * 0.5f;
            }
        }
        else
        {
            for (auto& d : diff) xp = d.process (xp, diffG);

            float o[N];
            for (int k = 0; k < N; ++k)
            {
                const float m = modAmt * wander[k].next() * 12.0f;
                o[k] = damp[k].lp (fdn[k].read (len[k] + m + 14.0f));
            }
            // Hadamard mixing (fast Walsh-Hadamard, normalised)
            float h[N];
            for (int k = 0; k < N; ++k) h[k] = o[k];
            for (int s = 1; s < N; s <<= 1)
                for (int a = 0; a < N; a += s << 1)
                    for (int b = a; b < a + s; ++b) { const float u = h[b], v = h[b + s]; h[b] = u + v; h[b + s] = u - v; }
            const float norm = 0.35355339f;

            float shimmerIn = 0;
            if (kind == Shimmer)
            {
                const float sum = (o[0] + o[3] + o[5] + o[6]) * 0.5f;
                shimmerBuf.push (sum);
                const float ph1 = shimmerPh, ph2 = ph1 + 0.5f - std::floor (ph1 + 0.5f);
                const float w1 = std::sin (pi * ph1), w2 = std::sin (pi * ph2);
                shimmerIn = (shimmerBuf.read (2.0f + ph1 * shW) * w1 * w1 + shimmerBuf.read (2.0f + ph2 * shW) * w2 * w2) * shAmt * 0.7f;
                shimmerPh += (1.0f - shRatio) / shW;
                shimmerPh -= std::floor (shimmerPh);
            }

            for (int k = 0; k < N; ++k)
            {
                const float inj = (xp + shimmerIn) * ((k & 1) ? -0.5f : 0.5f);
                fdn[k].push (h[k] * norm * g[k] + inj);
            }
            wl = (o[0] - o[2] + o[4] - o[6] + o[1] * 0.5f) * 0.45f;
            wr = (o[1] - o[3] + o[5] - o[7] + o[2] * 0.5f) * 0.45f;

            if (kind == Room)
            {
                static const float er[5] = { 7, 11, 17, 23, 29 };
                for (int e = 0; e < 5; ++e)
                {
                    const float t = pre.read (preS + er[e] * (0.6f + size) * 0.001f * sampleRate) * (0.3f - e * 0.05f);
                    (e & 1 ? wr : wl) += t;
                }
            }
        }

        wl = lowCut[0].process (wl);
        wr = lowCut[1].process (wr);
        mixInto (L[i], L[i], wl, mix, killDry);
        mixInto (R[i], R[i], wr, mix, killDry);
    }
}

//==============================================================================
// Tri-Chorus: three bucket-brigade style lines (left, centre, right), inspired by 80s rack tri-stereo choruses
TriChorusBlock::TriChorusBlock() : Block (Type::TriChorus)
{
    addFloat ("rate", "Rate", 0.1f, 5, 0.6f, "Hz", 0.8f, 0.01f);
    addFloat ("delay", "Delay", 3, 25, 9, "ms", -1, 0.1f);
    addFloat ("depthL", "Depth left", 0, 100, 60, "%", -1, 1);
    addFloat ("depthC", "Depth centre", 0, 100, 40, "%", -1, 1);
    addFloat ("depthR", "Depth right", 0, 100, 60, "%", -1, 1);
    addFloat ("feedback", "Feedback", 0, 70, 10, "%", -1, 1);
    addFloat ("tone", "Tone", 2000, 12000, 6500, "Hz", 5000, 1);
    addFloat ("mix", "Mix", 0, 100, 50, "%", -1, 1);
    addFloat ("width", "Width", 0, 100, 100, "%", -1, 1);
    addToggle ("killDry", "Kill dry (100% wet)", false);
}

void TriChorusBlock::prepare (double sr, int)
{
    for (auto& l : line) l.setMaxSamples ((int) (0.06 * sr));
    reset();
}

void TriChorusBlock::process (float* L, float* R, int n)
{
    lfo.setRate (sampleRate, p (0));
    const float base = p (1) * 0.001f * sampleRate;
    const float depth[3] = { p (2) * 0.01f, p (3) * 0.01f, p (4) * 0.01f };
    const float fbk = p (5) * 0.01f, mix = p (7) * 0.01f, width = p (8) * 0.01f;
    const bool killDry = p (9) > 0.5f;
    inLp.set (Biquad::Type::LowPass, sampleRate, p (6), 0.6f);
    for (auto& f : lp) f.set (Biquad::Type::LowPass, sampleRate, p (6), 0.7f);
    const float range = 0.004f * sampleRate;

    for (int i = 0; i < n; ++i)
    {
        const float m = inLp.process (0.5f * (L[i] + R[i]));
        float w[3];
        for (int k = 0; k < 3; ++k)
        {
            line[k].push (m + fbk * fbState);
            const float d = base + depth[k] * range * 0.5f * (1.0f + lfo.sine (k / 3.0f));
            w[k] = lp[k].process (line[k].read (d));
        }
        fbState = w[1];
        lfo.advance();
        float wl = w[0] + 0.6f * w[1], wr = w[2] + 0.6f * w[1];
        const float mm = 0.5f * (wl + wr), ss = 0.5f * (wl - wr) * width;
        wl = mm + ss; wr = mm - ss;
        mixInto (L[i], L[i], wl * 0.8f, mix, killDry);
        mixInto (R[i], R[i], wr * 0.8f, mix, killDry);
    }
}
