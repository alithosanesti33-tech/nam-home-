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
    currentGroup = {};
}

void PhatTrackBlock::prepare (double sr, int)
{
    rate = sr;
    tracker.prepare (sr);
    gateTrigger.prepare (sr);
    voice.prepare (sr);
    smoothCoefSlow = phat::coefFromSeconds (0.003f, sr);
    smoothCoefFast = phat::coefFromSeconds (0.0005f, sr);
    slowFollowCoef = phat::coefFromSeconds (0.005f, sr);
    validCoef      = phat::coefFromSeconds (0.001f, sr);
    dryAtk         = phat::coefFromSeconds (0.002f, sr);
    dryRel         = phat::coefFromSeconds (0.040f, sr);
    reset();
}

void PhatTrackBlock::reset()
{
    tracker.reset(); gateTrigger.reset(); voice.reset();
    rawMidi = slowMidi = smoothMidi = glideMidi = 60.0f;
    lastHz = 0; haveRaw = pitchReady = false;
    samplesSinceResult = 1000000; jumpCooldown = 0;
    validGain = dryGate = 0;
}

void PhatTrackBlock::process (float* L, float* R, int n)
{
    // parameters by position (same order as added above); no lookups by name on the audio thread
    gateTrigger.setThresholdDb (p (0));

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
    const float glideCoef = (glideOn && glideTime > 0.001f) ? phat::coefFromSeconds (glideTime * 0.35f, rate) : 1.0f;
    const int resultFreshSamples = (int) (0.03 * rate);
    const int jumpCooldownSamples = (int) (0.04 * rate);
    phat::PitchTracker::Result result;
    bool gateIsOpen = false;

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        const float detect = 0.5f * (inL + inR) * sensGain;
        const auto g = gateTrigger.process (detect);
        gateIsOpen = g.open;
        if (samplesSinceResult < 1000000) ++samplesSinceResult;

        if (tracker.push (detect, g.trackerActive, result))
        {
            rawMidi = phat::hzToMidi (result.hz);
            lastHz = result.hz;
            haveRaw = true;
            samplesSinceResult = 0;
            if (! g.open || ! pitchReady) smoothMidi = glideMidi = slowMidi = rawMidi;
            if (g.open) pitchReady = true;
        }
        if (g.opened)
        {
            pitchReady = haveRaw && samplesSinceResult < resultFreshSamples;
            jumpCooldown = jumpCooldownSamples;
            voice.noteOn();
        }
        else if (g.onset && retrig) voice.noteOn();
        if (g.closed) voice.noteOff();

        slowMidi += slowFollowCoef * (rawMidi - slowMidi);
        if (jumpCooldown > 0) --jumpCooldown;
        else if (g.open && pitchReady && std::fabs (rawMidi - slowMidi) > 0.6f)
        {
            slowMidi = rawMidi;
            jumpCooldown = jumpCooldownSamples;
            if (retrig) voice.noteOn();
        }

        const float err = rawMidi - smoothMidi, aerr = std::fabs (err);
        if (aerr > 1.5f) smoothMidi = rawMidi;
        else
        {
            const float t = phat::clampf ((aerr - 0.03f) / 0.30f, 0.0f, 1.0f);
            smoothMidi += (smoothCoefSlow + (smoothCoefFast - smoothCoefSlow) * t) * err;
        }
        glideMidi += glideCoef * (smoothMidi - glideMidi);

        validGain += validCoef * (((pitchReady && haveRaw) ? 1.0f : 0.0f) - validGain);
        dryGate += (g.open ? dryAtk : dryRel) * ((g.open ? 1.0f : 0.0f) - dryGate);

        const float synth = voice.tick (glideMidi) * validGain;
        const float dryAmt = dryLevel * dryGate;
        L[i] = synth + inL * dryAmt;
        R[i] = synth + inR * dryAmt;
    }
    gateOpen = gateIsOpen ? 1.0f : 0.0f;
    detectedHz = (gateIsOpen && pitchReady) ? lastHz : 0.0f;
}
