// Talk box (Heil style), whisper mode.
//
// A real talk box plays the amp into your mouth through a tube; your mouth's shape filters it and a
// mic picks that up. Here your mic hears you whisper, and the block reads your mouth's resonances
// the way speech analysis does (linear prediction: about 18 resonance stages, a new reading every
// 4 ms from the last 16 ms of your whisper). The Heil-voiced guitar is then run through that same
// mouth filter, so the guitar says the words, consonants and all.
//
// Your mic is only listened to, never played, so it can't feed back. An echo canceller takes your
// speakers out of what it listens to, and the gate only opens for the breath of your whisper.
#include "BlockTypes.h"

using namespace ndsp;

namespace
{
    enum TbP { tMic, tSens, tGate, tAec, tClarity, tTone, tDrive, tTube, tClosed, tArtic, tLevel, tMix, tCons, tFill, tRec,
                tTalk, tDry, tDriver, tMicModel, tTubeLen, tFormant };

    // Levinson-Durbin: autocorrelation r[0..p] -> reflection coefficients k[0..p-1]
    void levinson (const double* r, int p, float* k)
    {
        double a[TalkBoxBlock::maxOrder + 1] {}, tmp[TalkBoxBlock::maxOrder + 1] {};
        a[0] = 1.0;
        double e = r[0];
        for (int i = 1; i <= p; ++i)
        {
            double acc = r[i];
            for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
            double ki = e > 1.0e-18 ? -acc / e : 0.0;
            ki = juce::jlimit (-0.995, 0.995, ki);
            for (int j = 1; j < i; ++j) tmp[j] = a[j] + ki * a[i - j];
            for (int j = 1; j < i; ++j) a[j] = tmp[j];
            a[i] = ki;
            k[i - 1] = (float) ki;
            e *= (1.0 - ki * ki);
        }
    }
}

TalkBoxBlock::TalkBoxBlock() : Block (Type::TalkBox), juce::Thread ("NAM Home talk box test clip")
{
    currentGroup = "Mic";
    // set-and-forget: the mic is whichever input your guitar isn't on, and the gate sets its own
    // level from your room
    addChoice ("mic", "Mic input", { "Auto (the other input)", "Input 1", "Input 2" }, 0);
    addFloat ("sens2", "Mic level", -12, 40, 20, "dB", -1, 0.5f);   // the SD-1 is a quiet dynamic mic
    addFloat ("gate2", "Gate", 2, 24, 6, "dB", -1, 0.5f);
    addToggle ("aec4", "Echo cancel", true);
    currentGroup = "Talk box";
    addFloat ("words", "Clarity", 0, 100, 85, "%", -1, 1);          // how sharp the mouth's resonances are
    addFloat ("tone3", "Tone", 2000, 7000, 5000, "Hz", 4000, 10);   // the Heil horn's top end
    addFloat ("drive", "Drive", 0, 100, 25, "%", -1, 1);
    addFloat ("tube", "Tube", 0, 100, 40, "%", -1, 1);
    addFloat ("closed3", "Closed mouth", 0, 100, 30, "%", -1, 1);
    addFloat ("artic2", "Articulation", 0, 100, 60, "%", -1, 1);
    addFloat ("level", "Level", -24, 12, 0, "dB", -1, 0.1f);
    addFloat ("mix", "Mix", 0, 100, 100, "%", -1, 1);
    // consonants: like the mic on a real talk box, the little bursts of your actual "t", "k", "p",
    // "s", "ch" are let through (only those instants, only what isn't your speakers), so the words
    // are crisp and understandable, not just vowels
    currentGroup = "Talk box";
    addFloat ("cons", "Consonants", 0, 100, 50, "%", -1, 1);
    // fill: grit only in the talking range (about 0.8-4 kHz), so every note, even a low or clean one,
    // gives your mouth something to shape (why Frampton runs the talk box with some dirt)
    addFloat ("fill", "Fill", 0, 100, 50, "%", -1, 1);
    // record a 10 s test clip (your mic, the guitar going in, and the talk box) to
    // Music/NAM Home/Talk Box Tests, to send for tuning
    currentGroup = "Mic";
    addToggle ("rectest", "Record test clip", false);
    params[(size_t) tRec]->momentary = true;
    currentGroup = "Footswitch";
    // the talk box on and off (MIDI-learnable, fades in and out), and Frampton's dry talk box:
    // while it's on, your Delay and Reverb blocks fade their echoes out so every word is clear
    addToggle ("talk", "Talk box on", true);
    addToggle ("drytalk", "Dry while talking", true);
    currentGroup = "Character";
    addChoice ("driver", "Driver", { "Heil (honky, gritty)", "MXR (tighter, brighter)", "Banshee (fat low mids)" }, 0);
    addChoice ("micmodel", "Mic", { "SD-1", "SM57 style", "Ribbon", "Off" }, 0);
    addFloat ("tubelen", "Tube length", 0.6f, 3.0f, 1.8f, "m", -1, 0.05f);
    addFloat ("formant", "Formant", -30, 30, 0, "%", -1, 1);   // - = bigger/deeper, + = smaller/more nasal
}

TalkBoxBlock::~TalkBoxBlock() { context().talkDuck = 1.0f; stopThread (4000); }

juce::File TalkBoxBlock::testFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("NAM Home").getChildFile ("Talk Box Tests");
}

// writes the finished test clip (off the audio thread)
void TalkBoxBlock::run()
{
    auto dir = testFolder();
    dir.createDirectory();
    const auto name = "Talk box test " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav";
    auto file = dir.getChildFile (name);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr) { recState = 0; return; }
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), recRate, 3, 24, {}, 0));
    if (w == nullptr) { recState = 0; return; }
    os.release();
    const float* chans[3] = { recMic.data(), recDry.data(), recOut.data() };
    w->writeFromFloatArrays (chans, 3, recLen);
    w.reset();
    savedName = name;
    recState = 3;   // saved
}

juce::String TalkBoxBlock::getSubtitle() const
{
    if (recState.load() == 1) return "Recording test clip... " + juce::String (juce::jmax (0, (recLen - recPos.load()) / juce::jmax (1, (int) recRate)) + 1) + " s";
    if (recState.load() == 2) return "Saving test clip...";
    if (recState.load() == 3) return "Saved: Music/NAM Home/Talk Box Tests";
    const int micIn = micUsed + 1;
    switch (status.load())
    {
        case 2:  return "Needs a mic on input " + juce::String (micIn);
        case 3:  return "Mic input is your guitar input";
        case 1:  return "Hearing you (breath " + juce::String ((int) std::round (micLevelDb.load())) + " dB)";
        case 4:  return "Learning your speakers... keep playing";
        default: break;
    }
    const float db = micLevelDb.load();
    return db < -99.0f ? "Whisper to talk" : "Whisper to talk (breath " + juce::String ((int) std::round (db)) + " dB)";
}

void TalkBoxBlock::prepare (double sr, int)
{
    decim = std::max (1, (int) std::lround (sr / 16000.0));
    aRate = (float) sr / (float) decim;
    const float aa = std::min (0.45f * aRate, 7000.0f);
    for (auto& f : micAA) f.set (Biquad::Type::LowPass, (float) sr, aa, 0.707f);
    for (auto& f : refAA) f.set (Biquad::Type::LowPass, (float) sr, aa, 0.707f);
    for (auto& f : carAA) f.set (Biquad::Type::LowPass, (float) sr, aa, 0.707f);
    for (auto& f : upLP)  f.set (Biquad::Type::LowPass, (float) sr, aa, 0.707f);
    micHP.set (Biquad::Type::HighPass, (float) sr, 100.0f, 0.707f);

    aecTaps = (int) std::lround (0.064f * aRate);            // speaker-to-mic paths up to 64 ms
    aecW.assign ((size_t) aecTaps, 0.0f);
    aecX.assign ((size_t) aecTaps * 2, 0.0f);
    refHist.assign ((size_t) aecTaps + 4, 0.0f);

    breathHP.set (Biquad::Type::HighPass, aRate, std::min (6200.0f, 0.4f * aRate), 0.707f);
    breathHP2.set (Biquad::Type::HighPass, aRate, std::min (6200.0f, 0.4f * aRate), 0.707f);
    breathLP.set (Biquad::Type::LowPass, aRate, 0.45f * aRate, 0.707f);
    refBHP = breathHP; refBHP2 = breathHP2; refBLP = breathLP;
    breathC = 1.0f - std::exp (-1.0f / (0.01f * aRate));
    artBP.set (Biquad::Type::BandPass, aRate, 1200.0f, 0.35f);
    consHP.set (Biquad::Type::HighPass, aRate, 1500.0f, 0.707f);
    consHP2.set (Biquad::Type::HighPass, aRate, 1500.0f, 0.707f);
    artFC = 1.0f - std::exp (-1.0f / (0.003f * aRate));
    artSC = 1.0f - std::exp (-1.0f / (0.06f * aRate));

    // mouth analysis order: about one resonance stage per kHz, plus a couple
    order = juce::jlimit (12, maxOrder, (int) std::lround (aRate / 1000.0f) + 2);
    for (int i = 0; i < frameLen; ++i) window[i] = 0.5f - 0.5f * std::cos (twoPi * (i + 0.5f) / frameLen);

    // closed mouth ("mm"): a soft, dark shape
    for (int i = 0; i < maxOrder; ++i) kClosed[i] = 0;
    kClosed[0] = -0.92f; kClosed[1] = 0.35f;
    gClosed = 0.075f;   // a closed mouth is quieter than your guitar (about 6 dB)

    // Heil horn driver: no real lows, a honky mid push, rolled off above the Tone
    hp.set (Biquad::Type::HighPass, (float) sr, 140.0f, 0.707f);
    presence.set (Biquad::Type::Peak, (float) sr, 1800.0f, 0.8f, 4.0f);
    tube.set (Biquad::Type::LowPass, (float) sr, 3000.0f, 0.5f);
    proximity.set (Biquad::Type::LowShelf, (float) sr, 250.0f, 0.7f, 2.5f);   // a mic right at your lips
    tubeLine.setMaxSamples ((int) (0.02 * sr));
    fillBP.set (Biquad::Type::BandPass, (float) sr, 1000.0f, 0.5f);
    fillHP.set (Biquad::Type::HighPass, (float) sr, 800.0f, 0.707f);
    fillLP.set (Biquad::Type::LowPass, (float) sr, 4500.0f, 0.707f);
    fillEnvC = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
    // test clip buffers (10 s), allocated here, never on the audio thread
    recRate = sr;
    recLen = (int) (10.0 * sr);
    recMic.assign ((size_t) recLen, 0.0f); recDry.assign ((size_t) recLen, 0.0f); recOut.assign ((size_t) recLen, 0.0f);
    reset();
}

void TalkBoxBlock::reset()
{
    for (auto& f : micAA) f.reset();
    for (auto& f : refAA) f.reset();
    for (auto& f : carAA) f.reset();
    for (auto& f : upLP) f.reset();
    for (auto& f : outLP) f.reset();
    micHP.reset(); hp.reset(); lp1.reset(); lp2.reset(); presence.reset(); tube.reset(); proximity.reset(); tubeLine.clear();
    breathHP.reset(); breathHP2.reset(); breathLP.reset(); refBHP.reset(); refBHP2.reset(); refBLP.reset(); artBP.reset();
    std::fill (aecW.begin(), aecW.end(), 0.0f);
    std::fill (aecX.begin(), aecX.end(), 0.0f);
    std::fill (refHist.begin(), refHist.end(), 0.0f);
    aecPos = 0; refHistPos = 0; refPow = 1.0e-6f; echoDelay = 0;
    aecLearned = false; learnTime = 0; micPowSm = resPowSm = 0;
    breathPow = refBreathPow = leakB = 0; breathDb = -120; floorDb = -60;
    open = openSm = holdLeft = 0;
    std::fill (std::begin (frame), std::end (frame), 0.0f); framePos = 0; preEm = 0;
    for (int i = 0; i < maxOrder; ++i) kOld[i] = kNew[i] = kClosed[i];
    std::fill (std::begin (lat), std::end (lat), 0.0f);
    gOld = gNew = gClosed; interp = 0;
    artFast = artSlow = artFloor = 0; artGain = artSm = 1.0f;
    fillBP.reset(); fillHP.reset(); fillLP.reset(); fillEnv = 0; lastRec = false;
    carHold = outHold = 0; deEm = 0; prevCar = lastMouth = 0; fPhase = 0; talkSm = 1.0f; lastDriver = lastMic = -1;
    presence2.reset(); micEq1.reset(); micEq2.reset(); consSample = consEnv = consTarget = 0; consHissRef = 1.0f; consHP.reset(); consHP2.reset(); dryE = wetE = 0; levelComp = 1.0f; limEnv = 0;
    decimCount = hopCount = delayCount = 0;
}

// one analysis-rate sample of mic and speakers
void TalkBoxBlock::analyse (float mic, float ref)
{
    // echo canceller: what the speakers put into the mic, learned only while you're not whispering
    float e = mic;
    const bool aec = p (tAec) > 0.5f;
    {
        if (--aecPos < 0) aecPos = aecTaps - 1;
        const float dropped = aecX[(size_t) aecPos];
        aecX[(size_t) aecPos] = ref; aecX[(size_t) (aecPos + aecTaps)] = ref;
        const float* x = &aecX[(size_t) aecPos];
        refPow = std::max (1.0e-9f, refPow + ref * ref - dropped * dropped);
        if (aec)
        {
            float yhat = 0;
            for (int k = 0; k < aecTaps; ++k) yhat += aecW[(size_t) k] * x[k];
            e = mic - yhat;
            if (refPow > 1.0e-7f)
            {
                const bool quiet = open < 0.5f && breathDb < floorDb + 3.0f;
                const float mu = (! aecLearned ? 0.5f : (quiet ? 0.15f : 0.0f)) * e / (refPow + 1.0e-6f);
                if (mu != 0.0f) for (int k = 0; k < aecTaps; ++k) aecW[(size_t) k] += mu * x[k];
            }
            const float c = 1.0f / (0.3f * aRate);
            micPowSm += c * (mic * mic - micPowSm);
            resPowSm += c * (e * e - resPowSm);
            if (! aecLearned && refPow / aecTaps > 1.0e-6f)
            {
                learnTime += 1.0f / aRate;
                const bool cancelled = resPowSm < 0.03f * micPowSm;
                const bool noBleed = 10.0f * std::log10 (micPowSm + 1.0e-12f) < -75.0f;
                if ((cancelled && learnTime > 0.5f) || (noBleed && learnTime > 1.5f) || learnTime > 4.0f) aecLearned = true;
            }
        }
        refHist[(size_t) refHistPos] = ref;
        refHistPos = (refHistPos + 1) % (int) refHist.size();
    }
    const int hs = (int) refHist.size();
    const float refD = refHist[(size_t) (((refHistPos - 1 - echoDelay) % hs + hs) % hs)];
    if (aec && ++delayCount >= (int) aRate / 2)   // twice a second: where is the speakers' echo?
    {
        delayCount = 0;
        int best = 0; float bv = 0;
        for (int k = 0; k < aecTaps; ++k) if (std::abs (aecW[(size_t) k]) > bv) { bv = std::abs (aecW[(size_t) k]); best = k; }
        echoDelay = best;
    }

    // the breath of your whisper (gate) and its level (articulation)
    {
        const float hb = breathLP.process (breathHP2.process (breathHP.process (e)));
        breathPow += breathC * (hb * hb - breathPow);
        const float hr = refBLP.process (refBHP2.process (refBHP.process (refD)));
        refBreathPow += breathC * (hr * hr - refBreathPow);
        const float vb = artBP.process (e);
        artFast += artFC * (vb * vb - artFast);
        artSlow += artSC * (vb * vb - artSlow);
    }

    // the consonant sound itself: your echo-cancelled whisper, above ~1.5 kHz
    consSample = consHP2.process (consHP.process (e));
    consEnv += (consTarget > consEnv ? 0.25f : 0.008f) * (consTarget - consEnv);   // ~0.5 ms on, ~8 ms off

    // the mouth: your whisper (echo-cancelled) into the analysis frame
    const float pe = e - 0.4f * preEm;   // a light pre-emphasis (undone on the way out, so the words aren't shrill)
    preEm = e;
    frame[framePos] = pe;
    framePos = (framePos + 1) % frameLen;

    if (++hopCount >= hop) { hopCount = 0; newFrame(); }
}

// every 4 ms: gate, articulation and a new mouth reading
void TalkBoxBlock::newFrame()
{
    const float hopSec = (float) hop / aRate;

    // gate: breath above the room, with the speakers' own treble taken out
    if (refBreathPow > 1.0e-12f && open < 0.5f)
    {
        const float q = breathPow / refBreathPow;
        leakB += (q < leakB ? 0.08f : 0.004f) * (q - leakB);
    }
    const float breathYou = std::max (0.1f * breathPow, breathPow - 3.0f * leakB * refBreathPow);
    breathDb = 10.0f * std::log10 (breathYou + 1.0e-12f);
    micLevelDb = breathDb;
    const bool aec = p (tAec) > 0.5f;
    const float wF = std::max (breathDb, -100.0f);
    if (aec && ! aecLearned) floorDb += 0.2f * (wF - floorDb);
    else if (open < 0.5f)    floorDb += (wF < floorDb ? 0.04f : 0.0016f) * (wF - floorDb);
    const float thr = std::max (-90.0f, floorDb + p (tGate));   // the SD-1 is quiet: a soft whisper still opens it
    if (aec && ! aecLearned) { open = 0.0f; status = 4; }
    else
    {
        if (breathDb > thr + 3.0f) { open = 1.0f; holdLeft = 0.18f; }
        else
        {
            holdLeft -= hopSec;
            if (breathDb > thr - 6.0f) holdLeft = std::max (holdLeft, 0.05f);
            if (holdLeft <= 0.0f && breathDb < thr - 6.0f) open = 0.0f;
        }
        status = open > 0.5f ? 1 : 0;
    }
    openSm += (open > openSm ? 0.35f : 0.06f) * (open - openSm);   // opens ~10 ms, closes ~65 ms

    // articulation: consonant stops and syllable gaps dip the sound
    if (open < 0.5f) artFloor += (artFast < artFloor ? 0.2f : 0.008f) * (artFast - artFloor);
    const float af = std::max (0.0f, artFast - 1.5f * artFloor), as = std::max (1.0e-12f, artSlow - 1.5f * artFloor);
    const bool sounding = open > 0.5f && af > 0.2f * as;
    const float r = open > 0.5f ? clamp (std::sqrt (af / as), 0.1f, 1.3f) : 1.0f;
    artGain = 1.0f + p (tArtic) * 0.01f * (r - 1.0f);

    // consonant detector: a sudden burst (a "t", "k", "p" release) or breath that is hissy rather than
    // voweled (an "s", "sh", "f", "ch": lots of breath against the vowel band)
    {
        const float burst = af / as;                                   // >1: louder than the last ~60 ms
        const float hiss = breathYou / (af + 1.0e-12f);                // breath against the vowel band
        const bool cons = open > 0.5f && (burst > 2.2f || hiss > consHissRef * 3.0f);
        if (open > 0.5f && ! cons) consHissRef += 0.02f * (hiss - consHissRef);   // your normal vowel balance
        consTarget = cons ? 1.0f : 0.0f;
    }

    // where the filter is now becomes the start of the next glide
    for (int i = 0; i < order; ++i) kOld[i] = kNew[i];
    gOld = gNew;

    // the mouth reading, only while you're actually making sound (in the gaps the last shape is kept)
    if (sounding)
    {
        float w[frameLen];
        for (int i = 0; i < frameLen; ++i) w[i] = frame[(framePos + i) % frameLen] * window[i];
        double rr[maxOrder + 1];
        for (int lag = 0; lag <= order; ++lag)
        {
            double acc = 0;
            for (int i = lag; i < frameLen; ++i) acc += (double) w[i] * w[i - lag];
            rr[lag] = acc;
        }
        if (rr[0] > 1.0e-14)
        {
            // Clarity: how sharp the resonances are (a lag window widens them; 100% = sharpest)
            const double bw = 40.0 + 260.0 * (1.0 - p (tClarity) * 0.01);   // resonance width in Hz
            rr[0] *= 1.0001;
            for (int lag = 1; lag <= order; ++lag)
            {
                const double x = twoPi * bw * lag / aRate;
                rr[lag] *= std::exp (-0.5 * x * x);
            }
            float k[maxOrder];
            levinson (rr, order, k);
            for (int i = 0; i < order; ++i) kNew[i] = k[i];
            // the mouth filter's own loudness is normalised away (the level comes from the guitar)
            float st[maxOrder + 1] {}, en = 0;
            for (int s = 0; s < 160; ++s)
            {
                float f = s == 0 ? 1.0f : 0.0f;
                for (int i = order - 1; i >= 0; --i) { f -= k[i] * st[i]; st[i + 1] = st[i] + k[i] * f; }
                st[0] = f;
                en += f * f;
            }
            gNew = 1.0f / std::sqrt (std::max (en, 1.0e-6f));
        }
    }
    else if (open < 0.5f)
    {
        // closed: settle toward the closed-mouth "mm"
        for (int i = 0; i < order; ++i) kNew[i] += 0.15f * (kClosed[i] - kNew[i]);
        gNew += 0.15f * (gClosed - gNew);
    }
    interp = 0;
}

// one carrier sample at the analysis rate through the mouth (the reflection coefficients glide
// from the last reading to the new one over the 4 ms, so it never clicks)
float TalkBoxBlock::mouthStep (float x, float t)
{
    float f = x;
    for (int i = order - 1; i >= 0; --i)
    {
        const float ki = kOld[i] + (kNew[i] - kOld[i]) * t;
        f -= ki * lat[i];
        lat[i + 1] = lat[i] + ki * f;
    }
    lat[0] = f;
    if (! std::isfinite (f) || std::abs (f) > 1.0e4f) { std::fill (std::begin (lat), std::end (lat), 0.0f); f = 0; }
    return f * (gOld + (gNew - gOld) * t);
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
    const int driver = (int) p (tDriver), micModel = (int) p (tMicModel);
    if (driver != lastDriver)
    {
        lastDriver = driver;
        switch (driver)
        {
            case 1:  hp.set (Biquad::Type::HighPass, sampleRate, 180.0f, 0.707f); presence.set (Biquad::Type::Peak, sampleRate, 2500.0f, 0.9f, 3.0f); presence2.set (Biquad::Type::Peak, sampleRate, 600.0f, 0.8f, -1.5f); break;   // MXR
            case 2:  hp.set (Biquad::Type::HighPass, sampleRate, 110.0f, 0.707f); presence.set (Biquad::Type::Peak, sampleRate, 1300.0f, 0.8f, 4.5f); presence2.set (Biquad::Type::Peak, sampleRate, 450.0f, 0.8f, 3.0f); break;   // Banshee
            default: hp.set (Biquad::Type::HighPass, sampleRate, 140.0f, 0.707f); presence.set (Biquad::Type::Peak, sampleRate, 1800.0f, 0.8f, 4.0f); presence2.set (Biquad::Type::Peak, sampleRate, 700.0f, 0.8f, 0.0f); break;     // Heil
        }
    }
    if (micModel != lastMic)
    {
        lastMic = micModel;
        switch (micModel)
        {
            case 0:  micEq1.set (Biquad::Type::LowShelf, sampleRate, 250.0f, 0.7f, 2.5f); micEq2.set (Biquad::Type::Peak, sampleRate, 3000.0f, 0.9f, 1.5f); break;     // SD-1: close-mic warmth, a touch of presence
            case 1:  micEq1.set (Biquad::Type::HighPass, sampleRate, 150.0f, 0.707f); micEq2.set (Biquad::Type::Peak, sampleRate, 4000.0f, 1.0f, 4.5f); break;   // SM57: tight lows, presence bump
            case 2:  micEq1.set (Biquad::Type::LowShelf, sampleRate, 220.0f, 0.7f, 2.0f); micEq2.set (Biquad::Type::HighShelf, sampleRate, 3500.0f, 0.7f, -5.0f); break;   // ribbon: warm and dark
            default: micEq1.set (Biquad::Type::Peak, sampleRate, 1000.0f, 0.7f, 0.0f); micEq2.set (Biquad::Type::Peak, sampleRate, 1000.0f, 0.7f, 0.0f); break;
        }
    }
    // the footswitch, and Frampton's dry talk box (the delays and reverbs fade out while it's on)
    const bool talkOn = p (tTalk) > 0.5f;
    const float talkC = 1.0f / (0.03f * sampleRate);
    context().talkDuck = (talkOn && p (tDry) > 0.5f) ? 0.0f : 1.0f;
    context().talkStamp = context().talkStamp.load() + 1;
    // tube length: the tube's resonances and how dark it is
    const float tubeLen = p (tTubeLen);
    tube.set (Biquad::Type::LowPass, sampleRate, 3000.0f * std::sqrt (1.8f / tubeLen), 0.5f);
    // formant shift: the mouth filter runs a little faster or slower than the guitar
    formantRatio = std::exp2 (p (tFormant) * 0.01f);
    const float level = dbToGain (p (tLevel)), mix = p (tMix) * 0.01f;
    const float closedLv = 0.3f + 0.7f * p (tClosed) * 0.01f;
    const float consAmt = p (tCons) * 0.01f;
    const float fillAmt = p (tFill) * 0.01f;
    // test clip: start on a press (if the last one isn't still being saved)
    const bool recPress = p (tRec) > 0.5f;
    if (recPress && ! lastRec && recState.load() != 1 && recState.load() != 2 && recLen > 0) { recPos = 0; recState = 1; }
    lastRec = recPress;
    const float tone = std::min (p (tTone), 0.42f * aRate);
    lp1.set (Biquad::Type::LowPass, sampleRate, tone, 0.65f);
    lp2.set (Biquad::Type::LowPass, sampleRate, std::min (1.25f * tone, 0.45f * sampleRate), 0.7f);
    outLP[0].set (Biquad::Type::LowPass, sampleRate, std::min (1.15f * tone, 5600.0f), 0.54f);
    outLP[1].set (Biquad::Type::LowPass, sampleRate, std::min (1.15f * tone, 5600.0f), 1.31f);
    const float limRel = 1.0f - std::exp (-1.0f / (0.1f * sampleRate));
    const float tubeDelay = 2.0f * tubeLen / 343.0f * sampleRate;   // there and back down the tube
    const float lmC = 1.0f / (0.3f * sampleRate);
    const float artC = 1.0f - std::exp (-1.0f / (0.002f * sampleRate));
    auto& ring = OutputRing::get();

    for (int i = 0; i < n; ++i)
    {
        const float dryL = L[i], dryR = R[i];

        // the guitar through the Heil horn driver and tube (full rate)
        float v = hp.process (0.5f * (dryL + dryR));
        if (drive > 0.001f) v = fastTanh (v * (1.0f + 6.0f * drive)) / (1.0f + 1.5f * drive);
        // fill: harmonics only in the talking range, generated from the guitar itself and kept at a
        // steady share of it, so a low or clean note still has sound where the words live
        if (fillAmt > 0.001f)
        {
            // from the whole note (a low E has almost nothing up there to start from), driven hard
            // whatever the level, so it always makes harmonics up into the talking range
            const float fb = fillBP.process (v);
            fillEnv += fillEnvC * (std::abs (v) - fillEnv);
            const float g = clamp (10.0f * v / (fillEnv + 1.0e-6f) + 3.0f * fb / (fillEnv + 1.0e-6f), -1.0f, 1.0f) * fillEnv * 1.2f;
            v += fillAmt * 4.0f * fillLP.process (fillHP.process (g));
        }
        v = lp2.process (lp1.process (presence2.process (presence.process (v))));
        const float back = tube.process (tubeLine.read (tubeDelay));
        v = v + 0.45f * tubeAmt * back;
        tubeLine.push (v);
        const float car = carAA[1].process (carAA[0].process (v));

        // mic and speakers, down to the analysis rate
        const float m = micAA[1].process (micAA[0].process (micHP.process (mic[i] * sens)));
        const float ref = refAA[1].process (refAA[0].process (ring.at (n - 1 - i)));

        // at the analysis rate: read the mouth, and put the guitar through it
        float up = 0.0f;
        if (++decimCount >= decim)
        {
            decimCount = 0;
            analyse (m, ref);
            // formant shift: the mouth filter runs at formantRatio x the guitar's rate (resampled
            // in and out), which moves every resonance by that ratio without changing your notes
            const float t = std::min (1.0f, (float) (++interp) / (float) hop);
            float mo = 0; int made = 0;
            fPhase += formantRatio;
            while (fPhase >= 1.0f)
            {
                fPhase -= 1.0f;
                const float frac = 1.0f - fPhase / std::max (1.0e-3f, formantRatio);
                lastMouth = mouthStep (prevCar + (car - prevCar) * clamp (frac, 0.0f, 1.0f), t);
                mo += lastMouth; ++made;
            }
            mo = made > 0 ? mo / (float) made : lastMouth;
            prevCar = car;
            deEm = mo + 0.4f * deEm;             // undo the analysis pre-emphasis: words sit in the mids
            mo = deEm * 0.6f;
            outHold = mo * (closedLv + (1.0f - closedLv) * openSm);
            // consonants: your actual consonant burst, matched to the guitar's level (never more
            // than a set ceiling, so it can't run away through the speakers)
            if (consAmt > 0.001f && consEnv > 0.001f)
            {
                const float sc = std::min (40.0f, std::sqrt (dryE + 1.0e-12f) / std::sqrt (artSlow + 1.0e-12f));
                outHold += consAmt * 0.6f * consEnv * consSample * sc / std::max (0.2f, levelComp);
            }
            up = outHold * (float) decim;   // back up to the full rate (zero-stuffed, then smoothed)
        }
        float y = upLP[1].process (upLP[0].process (up));
        // the Heil's roll-off after the mouth too: nothing comes out above ~5.6 kHz, which is also
        // where the gate listens for your breath, so the talk box can never hold the gate open
        y = outLP[1].process (outLP[0].process (y));
        y = micEq2.process (micEq1.process (y));

        // articulation: the whole sound dips on your consonants and syllable gaps
        artSm += artC * (artGain - artSm);
        y *= artSm;

        // level match (~0.3 s, while you talk): as loud as the guitar going in
        if (openSm > 0.5f && artSm > 0.8f)
        {
            const float d = 0.5f * (dryL + dryR);
            dryE += lmC * (d * d - dryE);
            wetE += lmC * (y * y - wetE);
            if (wetE > 1.0e-10f) levelComp += lmC * (clamp (0.9f * std::sqrt (dryE / wetE), 0.05f, 12.0f) - levelComp);
        }
        y *= levelComp * level;

        // safety limiter
        const float a = std::abs (y);
        limEnv = a > limEnv ? a : limEnv + limRel * (a - limEnv);
        if (limEnv > 0.9f) y *= 0.9f / limEnv;

        talkSm += (talkOn ? talkC : -talkC);
        talkSm = clamp (talkSm, 0.0f, 1.0f);
        const float wet = mix * talkSm;
        L[i] = y * wet + dryL * (1.0f - wet);
        R[i] = y * wet + dryR * (1.0f - wet);

        if (recState.load() == 1)
        {
            const int rp = recPos.load();
            if (rp < recLen)
            {
                recMic[(size_t) rp] = mic[i];
                recDry[(size_t) rp] = cleanInput != nullptr ? cleanInput[i] : 0.5f * (dryL + dryR);
                recOut[(size_t) rp] = L[i];
                recPos = rp + 1;
            }
            else { recState = 2; startThread (juce::Thread::Priority::low); }
        }
    }
}
