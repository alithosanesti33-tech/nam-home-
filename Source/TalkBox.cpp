// Talk box (Heil style), whisper mode.
//
// A real talk box plays the amp into your mouth through a tube and a mic picks up the result. Here
// it works the other way round: your mic hears you whisper the words, the block reads the shape of
// your mouth from it (28 bands, about 500 times a second) and gives the guitar that shape, after a
// Heil-style horn-driver and tube voicing. Put it right after the amp, where a Heil goes.
//
// Feedback: your mic is never played through the speakers, so there is no loop to ring. What the
// speakers CAN do is leak into the mic and confuse the reading, so:
//  * an echo canceller learns the path from your speakers to the mic and subtracts it
//  * a bleed filter removes what is left in each band that still follows the speakers
//  * a whisper gate only opens for you, with the closed-mouth sound in between
#include "BlockTypes.h"

using namespace ndsp;

namespace
{
    enum TbP { tMic, tSens, tGate, tAec, tBleed, tClarity, tTone, tDrive, tTube, tClosed, tLevel, tMix, tBody };
    constexpr float bandLo = 150.0f, bandHi = 6000.0f;
    inline float bandFreq (int b) { return bandLo * std::pow (bandHi / bandLo, (float) b / (float) (TalkBoxBlock::numBands - 1)); }
}

TalkBoxBlock::TalkBoxBlock() : Block (Type::TalkBox)
{
    currentGroup = "Mic";
    // set-and-forget: the mic is whichever input your guitar isn't on, and the whisper gate sets its
    // own level from your room (how much the speakers leave behind), so you can just play
    addChoice ("mic", "Mic input", { "Auto (the other input)", "Input 1", "Input 2" }, 0);
    addFloat ("sens", "Mic level", -12, 36, 12, "dB", -1, 0.5f);
    addFloat ("gate", "Gate", 3, 24, 9, "dB", -1, 0.5f);
    addToggle ("aec3", "Echo cancel", true);
    addFloat ("bleed3", "Bleed cut", 0, 100, 0, "%", -1, 1);
    currentGroup = "Talk box";
    addFloat ("clarity3", "Clarity", 0, 100, 70, "%", -1, 1);   // new id: blocks already placed get the new default
    addFloat ("tone2", "Tone", 1500, 9000, 5500, "Hz", 4500, 10);
    addFloat ("drive", "Drive", 0, 100, 25, "%", -1, 1);
    addFloat ("tube", "Tube", 0, 100, 40, "%", -1, 1);
    addFloat ("closed", "Closed mouth", 0, 100, 25, "%", -1, 1);
    addFloat ("level", "Level", -24, 12, 0, "dB", -1, 0.1f);
    addFloat ("mix", "Mix", 0, 100, 100, "%", -1, 1);
    // the low end: the horn and tube still push the low notes through your mouth, and a mic right at
    // your lips adds proximity bass, so the bottom isn't shaped away
    currentGroup = "Talk box";
    addFloat ("body", "Body", 0, 100, 60, "%", -1, 1);
}

juce::String TalkBoxBlock::getSubtitle() const
{
    const int micIn = micUsed + 1;
    switch (status.load())
    {
        case 2:  return "Needs a mic on input " + juce::String (micIn);
        case 3:  return "Mic input is your guitar input";
        case 1:  return "Hearing you";
        case 4:  return "Learning your speakers... keep playing";
        default: break;
    }
    const float db = micLevelDb.load();
    return db < -90.0f ? "Whisper to talk" : "Whisper to talk (mic " + juce::String ((int) std::round (db)) + " dB)";
}

void TalkBoxBlock::prepare (double sr, int)
{
    decim = std::max (1, (int) std::lround (sr / 16000.0));
    aRate = (float) sr / (float) decim;
    const float aa = std::min (0.45f * aRate, 7000.0f);
    for (auto& f : micAA) f.set (Biquad::Type::LowPass, (float) sr, aa, 0.707f);
    for (auto& f : refAA) f.set (Biquad::Type::LowPass, (float) sr, aa, 0.707f);
    micHP.set (Biquad::Type::HighPass, (float) sr, 100.0f, 0.707f);
    breathHP.set (Biquad::Type::HighPass, aRate, 5000.0f, 0.707f);
    breathHP2.set (Biquad::Type::HighPass, aRate, 5000.0f, 0.707f);
    breathLP.set (Biquad::Type::LowPass, aRate, 0.45f * aRate, 0.707f);
    breathC = 1.0f - std::exp (-1.0f / (0.01f * aRate));
    aecTaps = (int) std::lround (0.064f * aRate);            // speaker-to-mic paths up to 64 ms
    aecW.assign ((size_t) aecTaps, 0.0f);
    aecX.assign ((size_t) aecTaps * 2, 0.0f);
    refHist.assign ((size_t) aecTaps + 4, 0.0f);
    for (int b = 0; b < numBands; ++b)
    {
        const float fc = std::min (bandFreq (b), 0.44f * aRate);
        anMic[b].set (Biquad::Type::BandPass, aRate, fc, 4.0f);
        // a whisper is noise: each band is read over ~2 cycles of its bandwidth (at least 8 ms), so the
        // reading is steady instead of flickering (the flicker is what sounded choppy and warbly)
        const float bw = fc / 4.0f;
        bandC[b] = 1.0f - std::exp (-1.0f / (std::max (0.008f, 2.0f / bw) * aRate));
        anRef[b].set (Biquad::Type::BandPass, aRate, fc, 4.0f);
        carrier[b][0].set (Biquad::Type::BandPass, (float) sr, bandFreq (b), 3.2f);
        carrier[b][1].set (Biquad::Type::BandPass, (float) sr, bandFreq (b), 3.2f);
        // closed mouth ("mm"): a muffled hum, dark above ~600 Hz
        const float f = bandFreq (b);
        closed[b] = f < 600.0f ? 1.0f : std::pow (600.0f / f, 2.0f);
    }
    hp.set (Biquad::Type::HighPass, (float) sr, 70.0f, 0.707f);
    bodyLP.set (Biquad::Type::LowPass, (float) sr, 220.0f, 0.707f);
    bodyLP2.set (Biquad::Type::LowPass, (float) sr, 220.0f, 0.707f);
    proximity.set (Biquad::Type::LowShelf, (float) sr, 200.0f, 0.7f, 4.0f);
    presence.set (Biquad::Type::Peak, (float) sr, 2400.0f, 1.0f, 4.0f);
    tube.set (Biquad::Type::LowPass, (float) sr, 3000.0f, 0.5f);
    tubeLine.setMaxSamples ((int) (0.02 * sr));

    // level of the band bank with every band fully open, so "open mouth" = about unity gain
    {
        uint32_t seed = 12345u;
        double inE = 0, outE = 0;
        Biquad tmp[numBands][2];
        for (int b = 0; b < numBands; ++b) { tmp[b][0] = carrier[b][0]; tmp[b][1] = carrier[b][1]; tmp[b][0].reset(); tmp[b][1].reset(); }
        Biquad shape; shape.set (Biquad::Type::LowPass, (float) sr, 3000.0f, 0.707f);   // guitar-like spectrum
        for (int i = 0; i < (int) sr; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float x = shape.process ((float) (seed >> 8) / 8388608.0f - 1.0f);
            float y = 0;
            for (int b = 0; b < numBands; ++b) y += tmp[b][1].process (tmp[b][0].process (x));
            if (i > (int) (0.1 * sr)) { inE += (double) x * x; outE += (double) y * y; }
        }
        makeup = (float) std::sqrt (inE / std::max (1.0e-12, outE));
    }
    reset();
}

void TalkBoxBlock::reset()
{
    for (auto& f : micAA) f.reset();
    for (auto& f : refAA) f.reset();
    bodyLP.reset(); bodyLP2.reset(); proximity.reset();
    micHP.reset(); hp.reset(); lp1.reset(); lp2.reset(); presence.reset(); tube.reset(); tubeLine.clear();
    std::fill (aecW.begin(), aecW.end(), 0.0f);
    std::fill (aecX.begin(), aecX.end(), 0.0f);
    std::fill (refHist.begin(), refHist.end(), 0.0f);
    aecLearned = false; learnTime = 0; micPowSm = resPowSm = 0;
    aecPos = 0; refHistPos = 0; refPow = 1.0e-6f; refPeakHold = 0; micPeak = 0; echoDelay = 0;
    for (int b = 0; b < numBands; ++b)
    {
        anMic[b].reset(); anRef[b].reset(); carrier[b][0].reset(); carrier[b][1].reset();
        eMic[b] = eRef[b] = leak[b] = 0;
        target[b] = gain[b] = gainSm[b] = closed[b];
    }
    peakWhisper = 1.0e-9f; bloom = 1.0f; dryE = wetE = 0; levelComp = 1.0f;
    open = openSm = 0; whisperDb = -100; floorDb = -60; holdLeft = 0; likeSm = 0; breathPow = 0; breathDb = -120;
    breathHP.reset(); breathHP2.reset(); breathLP.reset();
    for (int b = 0; b < numBands; ++b) lastShaped[b] = 1.0f; limEnv = 0; decimCount = 0;
}

// one analysis-rate sample: echo cancel, bands, and every ~2 ms a new mouth shape
void TalkBoxBlock::analyse (float mic, float ref)
{
    // echo canceller: what the speakers put into the mic, learned while you aren't whispering
    float e = mic;
    const bool aec = p (tAec) > 0.5f;
    {
        if (--aecPos < 0) aecPos = aecTaps - 1;
        const float droppedRef = aecX[(size_t) aecPos];   // the oldest sample leaving the window
        aecX[(size_t) aecPos] = ref; aecX[(size_t) (aecPos + aecTaps)] = ref;
        const float* x = &aecX[(size_t) aecPos];
        refPow += ref * ref - droppedRef * droppedRef;
        refPow = std::max (refPow, 1.0e-9f);
        if (aec)
        {
            float yhat = 0;
            for (int k = 0; k < aecTaps; ++k) yhat += aecW[(size_t) k] * x[k];
            e = mic - yhat;
            // learns fast while you aren't whispering, and almost not at all while you are, so your
            // whisper never gets learned as "speaker"
            if (refPow > 1.0e-7f)
            {
                // frozen while you whisper (it only learns the speakers between your words)
                // never while you might be whispering (gate open, or any breath above the room), so
                // it can't learn your voice as "speaker" and cancel it
                const bool quiet = open < 0.5f && breathDb < floorDb + 3.0f;
                const float mu = (! aecLearned ? 0.5f : (quiet ? 0.15f : 0.0f)) * e / (refPow + 1.0e-6f);
                for (int k = 0; k < aecTaps; ++k) aecW[(size_t) k] += mu * x[k];
            }
        }
        // has it learned your speakers yet? (until then it only learns, so your speakers can't open the gate)
        const float c = 1.0f / (0.3f * aRate);
        micPowSm += c * (mic * mic - micPowSm);
        resPowSm += c * (e * e - resPowSm);
        if (! aecLearned && refPow / aecTaps > 1.0e-6f)
        {
            learnTime += 1.0f / aRate;
            const bool cancelled = resPowSm < 0.03f * micPowSm;                                    // 15 dB of the speakers removed
            const bool noBleed = 10.0f * std::log10 (micPowSm + 1.0e-12f) < -75.0f;               // headphones / speakers far away
            if ((cancelled && learnTime > 0.5f) || (noBleed && learnTime > 1.5f)) aecLearned = true;
            if (learnTime > 4.0f) aecLearned = true;                                               // never wait forever
        }
        refHist[(size_t) refHistPos] = ref;
        refHistPos = (refHistPos + 1) % (int) refHist.size();
    }

    // bands: your mouth (after echo cancelling) and the speakers (lined up with their echo)
    const int hs = (int) refHist.size();
    const float refD = refHist[(size_t) (((refHistPos - 1 - echoDelay) % hs + hs) % hs)];
    {
        const float hb = breathLP.process (breathHP2.process (breathHP.process (e)));
        breathPow += breathC * (hb * hb - breathPow);
    }
    for (int b = 0; b < numBands; ++b)
    {
        const float m = anMic[b].process (e), r = anRef[b].process (refD);
        const float pm = m * m, pr = r * r;
        eMic[b] += bandC[b] * (pm - eMic[b]);
        eRef[b] += bandC[b] * (pr - eRef[b]);
    }

    if (++delayCount >= (int) aRate / 2 && aec)   // twice a second: where is the speakers' echo?
    {
        delayCount = 0;
        int best = 0; float bv = 0;
        for (int k = 0; k < aecTaps; ++k) if (std::abs (aecW[(size_t) k]) > bv) { bv = std::abs (aecW[(size_t) k]); best = k; }
        echoDelay = best;
    }
    if (++hopCount < (int) (0.001f * aRate)) return;   // a new mouth shape every 1 ms
    hopCount = 0;

    // bleed filter: whatever in a band still follows the speakers is taken out
    // gentle: it only turns down what follows the speakers, never cuts a band out completely
    // (cutting bands to nothing is what made it choppy)
    const float bleedK = 1.5f * p (tBleed) * 0.01f;
    float refMax = 1.0e-12f;
    for (int b = 0; b < numBands; ++b) refMax = std::max (refMax, eRef[b]);
    float clean[numBands], total = 0;
    for (int b = 0; b < numBands; ++b)
    {
        // learned only between your words, and only in bands where the speakers are really playing
        // (the talk box puts almost no highs out, so in the highs the mic is you, not the speakers)
        if (eRef[b] > 1.0e-3f * refMax && eRef[b] > 1.0e-10f && open < 0.5f && breathDb < floorDb + 3.0f)
        {
            const float q = eMic[b] / eRef[b];
            leak[b] += (q < leak[b] ? 0.025f : 0.0004f) * (q - leak[b]);   // the quietest the mic gets per unit of speaker
        }
        clean[b] = eRef[b] > 1.0e-3f * refMax ? std::max (0.5f * eMic[b], eMic[b] - bleedK * leak[b] * eRef[b]) : eMic[b];
        total += clean[b];
    }

    // whisper gate (with hysteresis): your level after the speakers are taken out
    whisperDb = 10.0f * std::log10 (total + 1.0e-12f);
    micLevelDb = whisperDb;
    // the room's floor: falls quickly to the quietest moments, rises only slowly (~5 s), so it settles
    // on what your speakers and the room leave in the mic between your words
    // The gate listens to the breath band (above ~5 kHz): your whisper's breath and "s" sounds live
    // there, while the talk box's own sound (Heil low-pass, then the cab) has almost nothing there.
    // So the speakers can't open it, however loud, and it can't lock itself open.
    breathDb = 10.0f * std::log10 (breathPow + 1.0e-12f);
    const float wF = std::max (breathDb, -100.0f);   // a moment of total silence isn't the room's level
    // (while it's learning your speakers it follows the leftover directly, so it starts at the right level)
    if (aec && ! aecLearned) floorDb += 0.05f * (wF - floorDb);
    else if (open < 0.5f)    floorDb += (wF < floorDb ? 0.01f : 0.0004f) * (wF - floorDb);   // frozen while you whisper
    const float thr = std::max (-95.0f, floorDb + p (tGate));
    if (aec && ! aecLearned) { open = 0.0f; status = 4; }
    else
    {
        if (breathDb > thr + 3.0f) { open = 1.0f; holdLeft = 0.18f; }
        else
        {
            holdLeft -= 0.001f;                                         // a hop is 1 ms
            // once open it stays open down to 6 dB under the opening level, so a held vowel (less breath
            // than a consonant) keeps its shape; closes ~180 ms after you really stop
            if (breathDb > thr - 6.0f) holdLeft = std::max (holdLeft, 0.05f);
            if (holdLeft <= 0.0f && breathDb < thr - 6.0f) open = 0.0f;
        }
        status = open > 0.5f ? 1 : 0;
    }

    // the mouth opens in ~10 ms and closes over ~80 ms, like a real mouth, never in one step
    openSm += (open > openSm ? 0.1f : 0.0125f) * (open - openSm);

    // mouth shape. A mouth's resonances are broad, so the shape is smoothed over ~5 bands (about an
    // octave) and keeps a floor of full-range guitar: vowels bloom instead of sounding like a
    // cocked wah. Clarity sets how far it goes.
    const float mean = total / numBands + 1.0e-12f;
    const float clarity = p (tClarity) * 0.01f;
    const float ex = 0.5f * (0.7f + 1.9f * clarity);
    float a[numBands], sm[numBands];
    for (int b = 0; b < numBands; ++b) a[b] = std::pow (clean[b] / mean + 1.0e-4f, ex);
    static const float kern[5] = { 0.1f, 0.22f, 0.36f, 0.22f, 0.1f };
    float rms = 0;
    for (int b = 0; b < numBands; ++b)
    {
        float acc = 0, wsum = 0;
        for (int k = -2; k <= 2; ++k)
        {
            const int j = b + k;
            if (j < 0 || j >= numBands) continue;
            acc += kern[k + 2] * a[j]; wsum += kern[k + 2];
        }
        sm[b] = acc / wsum;
        rms += sm[b] * sm[b];
    }
    rms = std::sqrt (rms / numBands) + 1.0e-9f;
    const float body = 0.3f - 0.22f * clarity;   // full-range guitar kept under the vowel

    // bloom: how open / loud your whisper is right now, against your recent loudest. Opening up
    // and whispering harder makes it bigger and brighter, like pushing more air through a Heil.
    peakWhisper = std::max (total, peakWhisper * 0.9995f);
    const float rel = clamp (total / (peakWhisper + 1.0e-12f), 0.0f, 1.0f);
    bloom += 0.05f * ((0.45f + 0.55f * std::sqrt (std::sqrt (rel))) - bloom);

    const float cl = p (tClosed) * 0.01f * 0.6f;
    for (int b = 0; b < numBands; ++b)
    {
        const float shaped = (body + (1.0f - body) * sm[b] / rms) * bloom;
        lastShaped[b] = open > 0.5f ? std::min (shaped, 3.0f) : lastShaped[b];   // when you stop, it closes from your last shape
        target[b] = openSm * lastShaped[b] + (1.0f - openSm) * closed[b] * cl;
        if (aec && ! aecLearned) target[b] = 0.7f;   // learning: full range, so every frequency gets learned
    }
}

void TalkBoxBlock::process (float* L, float* R, int n)
{
    // Auto: the input your guitar isn't on
    const int choice = (int) p (tMic);
    const int micIn = choice == 0 ? (cleanInput != nullptr && cleanInput == interfaceIn[1] ? 0 : 1) : choice - 1;
    micUsed = micIn;
    const float* mic = interfaceIn[micIn];
    if (mic == nullptr || interfaceInputs < micIn + 1) { status = 2; return; }   // leave the guitar as it is
    if (mic == cleanInput && cleanInput != nullptr) { status = 3; return; }

    const float sens = dbToGain (p (tSens));
    const float drive = p (tDrive) * 0.01f, tubeAmt = p (tTube) * 0.01f;
    const float level = dbToGain (p (tLevel)), mix = p (tMix) * 0.01f;
    lp1.set (Biquad::Type::LowPass, sampleRate, p (tTone), 0.65f);
    lp2.set (Biquad::Type::LowPass, sampleRate, std::min (2.0f * p (tTone), 0.45f * sampleRate), 0.6f);
    const float gC = 1.0f - std::exp (-1.0f / (0.006f * sampleRate));
    const float limRel = 1.0f - std::exp (-1.0f / (0.1f * sampleRate));
    const float tubeDelay = 0.0105f * sampleRate;   // a ~1.8 m tube: resonances every ~95 Hz
    const float lmC = 1.0f / (0.7f * sampleRate);
    const float bodyAmt = p (tBody) * 0.01f;
    const float openSmA = openSm;
    auto& ring = OutputRing::get();

    for (int i = 0; i < n; ++i)
    {
        // analysis side (mic and what went to the speakers, at ~16 kHz)
        const float m = micAA[1].process (micAA[0].process (micHP.process (mic[i] * sens)));
        const float ref = refAA[1].process (refAA[0].process (ring.at (n - 1 - i)));
        if (++decimCount >= decim) { decimCount = 0; analyse (m, ref); }

        // guitar side: Heil horn driver and tube, then your mouth
        const float dryL = L[i], dryR = R[i];
        float v = hp.process (0.5f * (dryL + dryR));
        if (drive > 0.001f) v = fastTanh (v * (1.0f + 6.0f * drive)) / (1.0f + 1.5f * drive);
        v = lp2.process (lp1.process (presence.process (v)));
        const float back = tube.process (tubeLine.read (tubeDelay));
        v = v + 0.45f * tubeAmt * back;
        tubeLine.push (v);

        float y = 0;
        for (int b = 0; b < numBands; ++b)
        {
            gainSm[b] += gC * (target[b] - gainSm[b]);
            y += gainSm[b] * carrier[b][1].process (carrier[b][0].process (v));
        }
        y *= makeup;
        // body: the low end under the vowels, and the close mic's proximity bass
        y += bodyAmt * 1.6f * (0.55f + 0.45f * openSmA) * bodyLP2.process (bodyLP.process (v));
        y = proximity.process (y);
        // level match (slow, ~0.7 s): while your mouth is open, the talk box is as loud as the
        // guitar going into it, so switching it on never drops your volume
        if (openSm > 0.5f)
        {
            const float d = 0.5f * (dryL + dryR);
            dryE += lmC * (d * d - dryE);
            wetE += lmC * (y * y - wetE);
            if (wetE > 1.0e-9f) levelComp += lmC * (clamp (std::sqrt (dryE / wetE), 0.08f, 6.0f) - levelComp);
        }
        y *= levelComp * level;

        // safety limiter: nothing the talk box does can jump out loud
        const float a = std::abs (y);
        limEnv = a > limEnv ? a : limEnv + limRel * (a - limEnv);
        if (limEnv > 0.9f) y *= 0.9f / limEnv;

        L[i] = y * mix + dryL * (1.0f - mix);
        R[i] = y * mix + dryR * (1.0f - mix);
    }
}
