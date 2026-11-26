// Instrument blocks driven by your guitar: PhatTrack (ported from the PhatTrack plugin).
#include "BlockTypes.h"

PhatTrackBlock::PhatTrackBlock() : Block (Type::PhatTrack)
{
    // same controls, ranges and defaults ("Thing of Gold") as the PhatTrack plugin
    currentGroup = "Input";
    addFloat ("gate", "Noise gate", -80, -20, -52, "dB", -1, 0.1f);
    addFloat ("sens", "Sensitivity", -12, 24, 0, "dB", -1, 0.1f);
    addFloat ("dry", "Dry guitar", 0, 100, 0, "%", -1, 1);
    addToggle ("retrig", "Envelope retrigger", true);
    currentGroup = "Master";
    addChoice ("octave", "Octave", { "-2", "-1", "0", "+1", "+2" }, 2);
    addFloat ("tune", "Fine tune", -100, 100, 0, "cents", -1, 0.1f);
    addFloat ("glide", "Glide", 0, 2, 0.06f, "s", 0.25f, 0.001f);
    addToggle ("glideOn", "Glide on", true);
    addFloat ("volume", "Volume", 0, 150, 80, "%", -1, 1);
    currentGroup = "Osc 1";
    addChoice ("o1oct", "Octave", { "16'", "8'", "4'", "2'" }, 1);
    addChoice ("o1wave", "Wave", { "Triangle", "Sawtooth", "Square", "Pulse" }, 1);
    addFloat ("o1semi", "Semitone", -24, 24, 0, "st", -1, 1);
    addFloat ("o1det", "Detune", -50, 50, 0, "cents", -1, 0.1f);
    addFloat ("o1lvl", "Level", 0, 100, 50, "%", -1, 1);
    currentGroup = "Osc 2";
    addChoice ("o2oct", "Octave", { "16'", "8'", "4'", "2'" }, 2);
    addChoice ("o2wave", "Wave", { "Triangle", "Sawtooth", "Square", "Pulse" }, 1);
    addFloat ("o2semi", "Semitone", -24, 24, 0, "st", -1, 1);
    addFloat ("o2det", "Detune", -50, 50, 0, "cents", -1, 0.1f);
    addFloat ("o2lvl", "Level", 0, 100, 50, "%", -1, 1);
    currentGroup = "Mixer";
    addToggle ("sync", "Osc sync", true);
    addFloat ("sub", "Sub", 0, 100, 0, "%", -1, 1);
    addFloat ("noise", "Noise", 0, 100, 0, "%", -1, 1);
    currentGroup = "Filter";
    addFloat ("cutoff", "Cutoff", 20, 18000, 900, "Hz", 900, 0.1f);
    addFloat ("res", "Resonance", 0, 100, 30, "%", -1, 1);
    addFloat ("keytrk", "Key track", 0, 100, 50, "%", -1, 1);
    addFloat ("fenvamt", "Env amount", -100, 100, 35, "%", -1, 1);
    addFloat ("drive", "Drive", 0, 100, 30, "%", -1, 1);
    currentGroup = "Filter env";
    addFloat ("fatk", "Attack", 0.001f, 8, 0.005f, "s", 0.2f, 0.001f);
    addFloat ("fdec", "Decay", 0.002f, 8, 0.6f, "s", 0.5f, 0.001f);
    addFloat ("fsus", "Sustain", 0, 100, 65, "%", -1, 1);
    addFloat ("frel", "Release", 0.005f, 8, 0.05f, "s", 0.4f, 0.001f);
    currentGroup = "Amp env";
    addFloat ("aatk", "Attack", 0.001f, 8, 0.003f, "s", 0.2f, 0.001f);
    addFloat ("adec", "Decay", 0.002f, 8, 0.6f, "s", 0.5f, 0.001f);
    addFloat ("asus", "Sustain", 0, 100, 100, "%", -1, 1);
    addFloat ("arel", "Release", 0.005f, 8, 0.06f, "s", 0.4f, 0.001f);
    currentGroup = "Modulation";
    addFloat ("lforate", "Rate", 0.05f, 60, 5.5f, "Hz", 4, 0.01f);
    addChoice ("modsrc", "Source", { "Tri", "Square", "Saw", "Ramp", "Filt env", "Osc 2", "S&H", "Noise" }, 0);
    addChoice ("moddst", "Destination", { "Pitch", "Osc 2", "Filter", "Wave" }, 0);
    addFloat ("modamt", "Amount", 0, 100, 6, "%", -1, 1);
    currentGroup = "Input";
    addToggle ("clean", "Track clean input", true);   // index 39: listens to your dry guitar wherever it sits
    currentGroup = {};
}

void PhatTrackBlock::prepare (double sr, int)
{
    rate = sr;
    tracker.prepare (sr);
    voice.prepare (sr);
    validCoef = phat::coefFromSeconds (0.001f, sr);
    dryAtk    = phat::coefFromSeconds (0.002f, sr);
    dryRel    = phat::coefFromSeconds (0.040f, sr);
    reset();
}

void PhatTrackBlock::reset()
{
    tracker.reset(); voice.reset();
    glideMidi = 60.0f; lastHz = 0; glideOff = 0; lastNote = 60.0f;
    everValid = false;
    validGain = dryGate = 0;
}

void PhatTrackBlock::process (float* L, float* R, int n)
{
    // parameters by position (same order as added above); no lookups by name on the audio thread
    tracker.setThresholdDb (p (0));

    phat::VoiceParams vp;
    static const float wavePos[4] = { 0.0f, 0.34f, 0.67f, 1.0f };
    vp.osc1Oct = (int) p (9);   vp.osc2Oct = (int) p (14);
    vp.osc1Semi = p (11);       vp.osc2Semi = p (16);
    vp.osc1Detune = p (12);      vp.osc2Detune = p (17);
    vp.osc1Wave = wavePos[juce::jlimit (0, 3, (int) p (10))];
    vp.osc2Wave = wavePos[juce::jlimit (0, 3, (int) p (15))];
    vp.osc1Level = p (13) * 0.01f; vp.osc2Level = p (18) * 0.01f;
    vp.sub = p (20) * 0.01f;       vp.noise = p (21) * 0.01f;
    vp.sync = p (19) > 0.5f;
    vp.cutoff = p (22);         vp.resonance = p (23) * 0.01f;
    vp.keyTrack = p (24) * 0.01f; vp.filterEnvAmt = p (25) * 0.01f;
    vp.drive = p (26) * 0.01f;
    vp.fA = p (27); vp.fD = p (28); vp.fS = p (29) * 0.01f; vp.fR = p (30);
    vp.aA = p (31); vp.aD = p (32); vp.aS = p (33) * 0.01f; vp.aR = p (34);
    vp.lfoRate = p (35);
    vp.modSource = (int) p (36); vp.modDest = (int) p (37);
    vp.modAmount = p (38) * 0.01f;
    vp.fineTune = p (5);
    vp.octave = (int) p (4) - 2;
    vp.volume = p (8) * 0.01f;
    voice.setParams (vp);

    const bool retrig = p (3) > 0.5f;
    const float sensGain = phat::dbToGain (p (1));
    const float dryLevel = p (2) * 0.01f;
    const bool glideOn = p (7) > 0.5f;
    const float glideTime = p (6);
    const bool clean = p (39) > 0.5f && cleanInput != nullptr;
    const float glideCoef = (glideOn && glideTime > 0.001f) ? phat::coefFromSeconds (glideTime * 0.35f, rate) : 1.0f;
    bool gateIsOpen = false;

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        const float detect = (clean ? cleanInput[i] : 0.5f * (inL + inR)) * sensGain;
        const auto f = tracker.process (detect);
        gateIsOpen = f.open;

        if (f.attack) { everValid = false; voice.noteOn(); }
        else if (f.repick && retrig) voice.noteOn();
        if (f.noteChange && retrig) voice.noteOn();
        if (f.released) voice.noteOff();

        if (f.valid)
        {
            if (! everValid) { glideOff = 0; lastNote = f.midi; }   // first pitch after silence: no glide from the last note
            everValid = true;
        }
        // glide only between notes (a jump of more than ~2/3 semitone is a new note); bends and
        // vibrato are followed exactly, with no smoothing lag
        const float jump = f.midi - lastNote;
        if (everValid && std::abs (jump) >= 0.7f) glideOff -= jump;
        lastNote = f.midi;
        glideOff += glideCoef * (0.0f - glideOff);
        glideMidi = f.midi + glideOff;
        if (f.valid) lastHz = tracker.hz();

        validGain += validCoef * ((everValid ? 1.0f : 0.0f) - validGain);
        dryGate += (f.open ? dryAtk : dryRel) * ((f.open ? 1.0f : 0.0f) - dryGate);

        const float synth = voice.tick (glideMidi) * validGain;
        const float dryAmt = dryLevel * dryGate;
        L[i] = synth + inL * dryAmt;
        R[i] = synth + inR * dryAmt;
    }
    gateOpen = gateIsOpen ? 1.0f : 0.0f;
    detectedHz = (gateIsOpen && everValid) ? lastHz : 0.0f;
}

//==============================================================================
// Saxophone
namespace
{
    enum SaxP
    {
        sModel, sCharacter, sOctave, sAutoRange, sVolume, sDry, sClean,
        sGate, sSens, sDynamics, sSustain, sAttack, sLegato, sTongue,
        sBright, sBreath, sGrowl, sSubtone, sBody, sClicks,
        sScoop, sFall, sPitchMode, sGuitarVib,
        sVibMode, sVibAmount, sVibDepth, sVibRate, sVibDelay, sSound
    };
    const int saxDefaultOctave[4] = { 2, 2, 1, 1 };   // index into { -2, -1, 0, +1, +2 }
}

SaxBlock::SaxBlock() : Block (Type::Sax)
{
    currentGroup = "Saxophone";
    addChoice ("model", "Saxophone", { "Soprano", "Alto", "Tenor", "Baritone" }, 0);
    addChoice ("character", "Character", { "80s pop", "Smooth", "R&B honk", "Breathy ballad", "Dark vintage" }, 0);
    addChoice ("octave", "Octave", { "-2", "-1", "0", "+1", "+2" }, 2);
    addToggle ("autoRange", "Keep in range", true);
    addFloat ("volume", "Volume", 0, 150, 80, "%", -1, 1);
    addFloat ("dry", "Dry guitar", 0, 100, 0, "%", -1, 1);
    addToggle ("clean", "Track clean input", true);
    currentGroup = "Playing";
    addFloat ("gate", "Noise gate", -80, -20, -52, "dB", -1, 0.1f);
    addFloat ("sens", "Sensitivity", -12, 24, 0, "dB", -1, 0.1f);
    addFloat ("dynamics", "Pick dynamics", 0, 100, 85, "%", -1, 1);
    addFloat ("sustain", "Sustain", 0, 100, 65, "%", -1, 1);
    addFloat ("attack", "Attack bite", 0, 100, 60, "%", -1, 1);
    addFloat ("legato", "Legato glide", 0, 200, 15, "ms", -1, 1);
    addToggle ("tongue", "Tongue re-picks", true);
    currentGroup = "Tone";
    addFloat ("bright", "Brightness", 0, 100, 65, "%", -1, 1);
    addFloat ("breath", "Breath", 0, 100, 30, "%", -1, 1);
    addFloat ("growl", "Growl", 0, 100, 0, "%", -1, 1);
    addFloat ("subtone", "Subtone", 0, 100, 0, "%", -1, 1);
    addFloat ("body", "Body", 0, 100, 50, "%", -1, 1);
    addFloat ("clicks", "Key clicks", 0, 100, 30, "%", -1, 1);
    currentGroup = "Expression";
    addFloat ("scoop", "Scoop", 0, 100, 0, "%", -1, 1);
    addFloat ("fall", "Fall-off", 0, 100, 0, "%", -1, 1);
    addChoice ("pitchMode", "Pitch", { "Follow guitar", "Snap to notes" }, 0);
    addFloat ("guitarVib", "Your vibrato", 0, 100, 100, "%", -1, 1);
    currentGroup = "Vibrato";
    addChoice ("vibMode", "Sax vibrato", { "Off", "Delayed", "Always", "Pedal / MIDI", "From your vibrato" }, 3);
    addFloat ("vibAmount", "Pedal amount", 0, 100, 0, "%", -1, 1);
    addFloat ("vibDepth", "Depth", 0, 60, 25, "cents", -1, 0.1f);
    addFloat ("vibRate", "Rate", 3, 8, 5.3f, "Hz", -1, 0.01f);
    addFloat ("vibDelay", "Delay", 0, 1500, 350, "ms", -1, 1);
    currentGroup = "Saxophone";
    // index 29: real recordings (alto and soprano; tenor and bari borrow the alto) or the modeled horn
    addChoice ("sound", "Sound", { "Recorded", "Modeled" }, 0);
    currentGroup = {};
    ndsp::SaxSampleBank::get().requestLoad();
}

std::vector<Block::StartingPoint> SaxBlock::startingPoints() const
{
    return {
        { "Smooth soprano (Kenny G style)",
          "Soprano, sweet and breathy, slow singing vibrato, gentle glides between notes.",
          { { "model", 0 }, { "octave", 2 }, { "character", 1 }, { "sound", 0 }, { "bright", 55 }, { "breath", 45 }, { "growl", 0 }, { "subtone", 15 },
            { "body", 55 }, { "attack", 40 }, { "legato", 40 }, { "scoop", 25 }, { "fall", 10 },
            { "vibMode", 1 }, { "vibDepth", 22 }, { "vibRate", 5.0f }, { "vibDelay", 450 }, { "sustain", 75 } } },
        { "Bright modal soprano (Coltrane style)",
          "Soprano with a hard, bright, focused tone, little vibrato and quick articulation.",
          { { "model", 0 }, { "octave", 2 }, { "character", 0 }, { "sound", 0 }, { "bright", 85 }, { "breath", 15 }, { "growl", 5 }, { "subtone", 0 },
            { "body", 45 }, { "attack", 80 }, { "legato", 8 }, { "scoop", 0 }, { "fall", 0 },
            { "vibMode", 4 }, { "vibDepth", 12 }, { "sustain", 60 } } },
        { "Pink Floyd tenor",
          "Warm, smoky tenor for slow solos: subtone, breath and a delayed vibrato. Add a long reverb after it.",
          { { "model", 2 }, { "octave", 1 }, { "character", 3 }, { "sound", 0 }, { "bright", 45 }, { "breath", 40 }, { "growl", 10 }, { "subtone", 40 },
            { "body", 70 }, { "attack", 45 }, { "legato", 30 }, { "scoop", 15 }, { "fall", 20 },
            { "vibMode", 1 }, { "vibDepth", 28 }, { "vibRate", 5.2f }, { "vibDelay", 500 }, { "sustain", 80 } } },
        { "80s pop alto",
          "Bright, punchy alto that cuts through a mix.",
          { { "model", 1 }, { "octave", 2 }, { "character", 0 }, { "sound", 0 }, { "bright", 75 }, { "breath", 25 }, { "growl", 15 }, { "subtone", 0 },
            { "body", 50 }, { "attack", 70 }, { "legato", 15 }, { "vibMode", 1 }, { "vibDepth", 25 } } },
        { "R&B alto",
          "Alto with grit and growl on hard picks, and fall-offs at the end of notes.",
          { { "model", 1 }, { "octave", 2 }, { "character", 2 }, { "sound", 0 }, { "bright", 65 }, { "breath", 30 }, { "growl", 45 }, { "subtone", 10 },
            { "body", 60 }, { "attack", 75 }, { "legato", 20 }, { "fall", 35 }, { "scoop", 20 }, { "vibMode", 4 } } },
    };
}

juce::String SaxBlock::getTitle() const { return params[sModel]->text() + " sax"; }
juce::String SaxBlock::getSubtitle() const
{
    // recorded horns: say so plainly when the recordings aren't there, instead of quietly playing a synth
    static const int hornFor[4] = { 1, 0, 0, 0 };
    auto& bank = ndsp::SaxSampleBank::get();
    if ((int) params[sSound]->get() == 0 && ! bank.has (hornFor[juce::jlimit (0, 3, (int) params[sModel]->get())]))
    {
        bank.retryIfMissing();
        return bank.isReady() ? "This horn's recordings are missing" : bank.status();
    }
    return params[sCharacter]->text();
}

void SaxBlock::setModel (int m)
{
    m = juce::jlimit (0, 3, m);
    params[sModel]->set ((float) m);
    params[sOctave]->set ((float) saxDefaultOctave[m]);
}

void SaxBlock::prepare (double sr, int)
{
    rate = (float) sr;
    tracker.prepare (sr);
    voice.prepare (rate);
    sampler.prepare (rate);
    reset();
}

void SaxBlock::reset()
{
    tracker.reset(); voice.reset(); sampler.reset(); recBright.reset(); recSub.reset(); lastBright = lastSub = -1;
    sounding = pending = pendingFromSilence = releasing = false;
    pendingAge = 0;
    fold = glideOff = scoopOff = fallOff = fallTarget = 0;
    breath = bite = tongue = noteTime = vibPhase = vibFromGuitar = dipEnv = 0;
    noteVel = 0.7f; lastPitch = outPitch = samplerNote = 60.0f; bendSm = growlRnd = 0;
}

void SaxBlock::process (float* L, float* R, int n)
{
    auto p = [this] (int i) { return params[(size_t) i]->get(); };
    const int model = (int) p (sModel);
    const auto& horn = ndsp::saxModel (model);

    ndsp::SaxVoice::Settings vs;
    vs.model = model;
    vs.character = (int) p (sCharacter);
    vs.brightness = p (sBright) * 0.01f;
    vs.breathNoise = p (sBreath) * 0.01f;
    vs.growl = p (sGrowl) * 0.01f;
    vs.subtone = p (sSubtone) * 0.01f;
    vs.body = p (sBody) * 0.01f;
    vs.keyClicks = p (sClicks) * 0.01f;
    voice.settings (vs);
    tracker.setThresholdDb (p (sGate));

    const float sens = ndsp::dbToGain (p (sSens));
    const float octave = 12.0f * (float) ((int) p (sOctave) - 2);
    const bool autoRange = p (sAutoRange) > 0.5f;
    const float volume = p (sVolume) * 0.01f * 0.6f;   // 80% sits level with PhatTrack and a -18 dB NAM rig
    const float dry = p (sDry) * 0.01f;
    const bool clean = p (sClean) > 0.5f && cleanInput != nullptr;
    const float dyn = p (sDynamics) * 0.01f, sus = p (sSustain) * 0.01f, atk = p (sAttack) * 0.01f;
    const float legatoMs = p (sLegato);
    const bool tongueOn = p (sTongue) > 0.5f;
    const float scoopSemis = p (sScoop) * 0.01f * 1.5f;
    const float fallSemis = p (sFall) * 0.01f * 7.0f;
    const bool snap = (int) p (sPitchMode) == 1;
    const float follow = p (sGuitarVib) * 0.01f;
    const int vibMode = (int) p (sVibMode);
    const float vibDepth = p (sVibDepth) * 0.01f, vibRate = p (sVibRate), vibDelay = p (sVibDelay) * 0.001f;
    const float vibAmount = p (sVibAmount) * 0.01f;

    const float dtS = 1.0f / rate;
    auto coef = [this] (float seconds) { return 1.0f - std::exp (-1.0f / (std::max (seconds, 0.0002f) * rate)); };
    const float glideC = coef (std::max (0.003f, legatoMs * 0.001f / 3.0f));
    const float scoopC = coef (0.045f - 0.02f * atk);
    const float atkC = coef (0.022f - 0.017f * atk), relC = coef (fallSemis > 0.01f ? 0.16f : 0.045f), holdC = coef (0.03f);
    const float biteC = coef (0.03f), tongueC = coef (0.006f), dipC = coef (0.02f);
    const float vibGC = coef (0.12f), fallC = coef (0.09f);
    // recorded horns: soprano and alto are real; tenor and bari borrow the alto recordings, lowered
    auto& bank = ndsp::SaxSampleBank::get();
    static const int hornFor[4] = { 1, 0, 0, 0 };
    static const float shiftFor[4] = { 0.0f, 0.0f, 5.0f, 10.0f };
    // "Recorded" never falls back to the modeled horn: no recordings = silence (and the tile says why)
    const bool recorded = (int) p (sSound) == 0;
    const bool haveRecordings = bank.has (hornFor[model]);
    sampler.setHorn (hornFor[model], shiftFor[model]);
    const float growl = p (sGrowl) * 0.01f;
    {
        const float br = p (sBright), sb = p (sSubtone);
        if (br != lastBright) { recBright.set (ndsp::Biquad::Type::HighShelf, rate, 3000.0f, 0.7f, (br - 65.0f) * 0.2f); lastBright = br; }
        if (sb != lastSub) { recSub.set (ndsp::Biquad::Type::LowPass, rate, 16000.0f * std::pow (0.15f, sb * 0.01f), 0.6f); lastSub = sb; }
    }

    auto foldFor = [&] (float m)
    {
        float f = 0;
        if (autoRange)
        {
            while (m + f < horn.lowMidi && f < 36) f += 12;
            while (m + f > horn.highMidi && f > -36) f -= 12;
        }
        return f;
    };

    // how hard a note is played, from the pick (same curve as the breath below)
    auto breathFor = [&] (float vel, float lvl)
    {
        float x = std::max (sus * vel + (1.0f - sus) * lvl, lvl);
        x = dyn * x + (1.0f - dyn) * 0.72f;
        return 0.15f + 0.85f * std::pow (juce::jlimit (0.0f, 1.0f, x), 0.8f);
    };
    const float bendSmC = coef (0.012f);

    float lastHz = 0;
    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        const float detect = (clean ? cleanInput[i] : 0.5f * (inL + inR)) * sens;
        const auto f = tracker.process (detect);

        // where the guitar is: note centre plus as much of your own vibrato/bends as you want
        const float centre = snap ? std::round (f.centre) : f.centre;
        // the tracker's tiny pitch flutter is smoothed away (it is what made the horn warble like a synth);
        // your real vibrato and bends are slow enough to pass straight through
        bendSm += bendSmC * ((f.midi - f.centre) - bendSm);
        const float played = centre + follow * bendSm + octave;

        if (f.attack || (f.repick && sounding))
        {
            pending = true; pendingAge = 0; pendingFromSilence = f.attack || ! sounding;
            if (! pendingFromSilence && tongueOn) tongue = 0.7f;   // a quick tongue dip, never a gap
            voice.keyClick (0.35f + 0.4f * atk);                    // the breath "chiff" starts right away
        }
        if (pending)
        {
            ++pendingAge;
            if (f.valid)
            {
                // the note speaks: set pitch, dynamics, scoop
                pending = false;
                const bool fresh = pendingFromSilence || ! sounding;
                noteVel = juce::jmax (f.velocity, f.level);
                if (fresh || std::abs (played + fold - lastPitch) > 0.5f) fold = foldFor (played);
                const float target = played + fold;
                if (fresh) { scoopOff = -scoopSemis * (0.6f + 0.4f * noteVel); glideOff = 0; breath = 0.0f; }
                else if (std::abs (target - lastPitch) > 0.5f) { glideOff = 0; scoopOff = -scoopSemis * 0.4f; voice.keyClick (0.6f); }
                sampler.noteStart (target, fresh ? 0 : (tongueOn ? 2 : 1), breathFor (noteVel, f.level));
                samplerNote = target;
                bite = atk * (0.15f + 0.25f * noteVel);
                sounding = true; releasing = false; fallOff = 0; fallTarget = 0;
                noteTime = 0;
            }
            // no timeout: a note that is slow to detect still plays the moment its pitch is known
        }
        if (f.noteChange && sounding && ! pending)
        {
            // legato: glide from where the horn is, with a fingering click and a tiny breath dip
            const float newFold = foldFor (played);
            const float target = played + newFold;
            glideOff = lastPitch - target;
            fold = newFold;
            voice.keyClick (0.8f);
            dipEnv = 0.18f;
            sampler.noteStart (target, 1, breath);
            samplerNote = target;
            noteTime = 0;
        }
        if (f.released && sounding)
        {
            releasing = true; sounding = false; pending = false;
            fallTarget = -fallSemis;
            sampler.noteStop (fallSemis > 0.01f ? 0.25f : 0.08f);
        }

        // breath from your picking: "Pick dynamics" sets how much it matters, "Sustain" holds the
        // attack level instead of following the string's decay
        float x = sus * noteVel + (1.0f - sus) * f.level;
        x = std::max (x, f.level);
        x = dyn * x + (1.0f - dyn) * 0.72f;
        float target = (sounding && ! pending) ? 0.15f + 0.85f * std::pow (juce::jlimit (0.0f, 1.0f, x), 0.8f) : 0.0f;
        breath += (target > breath ? (noteTime < 0.06f ? atkC : holdC) : (releasing ? relC : holdC)) * (target - breath);
        bite += biteC * (0.0f - bite);
        tongue += tongueC * (0.0f - tongue);
        dipEnv += dipC * (0.0f - dipEnv);
        const float blow = juce::jmax (0.0f, (breath + bite) * (1.0f - 0.85f * tongue) * (1.0f - dipEnv));

        // pitch: guitar + horn fold + legato glide + scoop + fall
        glideOff += glideC * (0.0f - glideOff);
        scoopOff += scoopC * (0.0f - scoopOff);
        fallOff += fallC * (fallTarget - fallOff);
        float pitch = (sounding || releasing) ? played + fold + glideOff + scoopOff + fallOff : outPitch;

        // the sax's own vibrato
        noteTime += dtS;
        float vd = 0.0f;
        switch (vibMode)
        {
            case 1:  vd = vibDepth * juce::jlimit (0.0f, 1.0f, (noteTime - vibDelay) / 0.25f); break;
            case 2:  vd = vibDepth * juce::jlimit (0.0f, 1.0f, noteTime / 0.08f); break;
            case 3:  vd = vibDepth * vibAmount; break;
            case 4:  vibFromGuitar += vibGC * (f.vibrato - vibFromGuitar); vd = vibDepth * juce::jlimit (0.0f, 1.0f, vibFromGuitar * 1.6f); break;
            default: break;
        }
        vibPhase += vibRate * dtS;
        if (vibPhase >= 1.0f) vibPhase -= 1.0f;
        const float vs1 = std::sin (6.2831853f * vibPhase);
        pitch += vd * vs1;                               // depth is in semitones (cents / 100)
        const float ampVib = 1.0f + 0.25f * vd * vs1;    // a little jaw pressure with it

        outPitch = pitch;
        if (sounding && ! pending) lastPitch = played + fold;
        float y;
        if (recorded)
        {
            float r = haveRecordings ? sampler.tick (pitch - samplerNote, blow * ampVib) : 0.0f;
            if (growl > 0.001f)
            {
                // growl: a hummed, uneven throat buzz (wandering rate and depth), not a steady tremolo
                growlRnd += 0.002f * ((growlNoise() * 0.5f + 0.5f) - growlRnd);
                growlPh += (27.0f + 14.0f * growlRnd) * dtS; if (growlPh >= 1.0f) growlPh -= 1.0f;
                const float buzz = std::sin (6.2831853f * growlPh) * (0.6f + 0.4f * growlRnd) + 0.25f * growlNoise();
                r *= 1.0f + growl * 0.45f * buzz * blow;
                r = std::tanh (r * (1.0f + growl * 3.0f)) / (1.0f + growl * 1.2f);
            }
            y = recSub.process (recBright.process (r)) * volume;
        }
        else y = (blow > 0.0005f || sounding) ? voice.tick (pitch, blow * ampVib) * volume : voice.tick (pitch, 0.0f) * volume;
        L[i] = y + inL * dry;
        R[i] = y + inR * dry;
        if (f.valid) lastHz = tracker.hz();
    }
    detectedHz = sounding ? lastHz : 0.0f;
}
