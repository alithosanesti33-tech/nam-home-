#include "Tuner.h"
#include "../PluginProcessor.h"

using namespace juce;

static const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

PitchReading PitchDetector::analyse (const Engine& e, double sr, float a4)
{
    // window long enough for a low B on a 7-string / drop tunings (about 30 Hz)
    const int W = 2048;
    const int tauMax = jmin (1600, (int) (sr / 28.0));
    const int tauMin = (int) (sr / 1400.0);
    buf.resize ((size_t) (W + tauMax));
    diff.assign ((size_t) tauMax + 1, 0.0f);
    e.readTuner (buf.data(), (int) buf.size());

    float energy = 0;
    for (int i = 0; i < W; ++i) energy += buf[(size_t) i] * buf[(size_t) i];
    if (energy / W < 1.0e-6f)           // silence: hold the last note briefly, then clear
    {
        if (++heldFrames > 12) { last = {}; smoothedFreq = 0; }
        return last;
    }

    // YIN difference function + cumulative mean normalisation
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float s = 0;
        for (int i = 0; i < W; ++i) { const float d = buf[(size_t) i] - buf[(size_t) (i + tau)]; s += d * d; }
        diff[(size_t) tau] = s;
    }
    diff[0] = 1;
    float running = 0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += diff[(size_t) tau];
        diff[(size_t) tau] = running > 0 ? diff[(size_t) tau] * tau / running : 1.0f;
    }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (diff[(size_t) tau] < 0.12f)
        {
            while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0) { if (++heldFrames > 12) last = {}; return last; }

    // parabolic interpolation for sub-sample accuracy
    float t = (float) best;
    if (best > 1 && best < tauMax)
    {
        const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const float den = a - 2 * b + c;
        if (std::abs (den) > 1.0e-9f) t += 0.5f * (a - c) / den;
    }
    const float f = (float) sr / t;
    smoothedFreq = (smoothedFreq > 0 && std::abs (f / smoothedFreq - 1.0f) < 0.03f) ? smoothedFreq * 0.7f + f * 0.3f : f;
    heldFrames = 0;

    PitchReading r;
    r.valid = true;
    r.freq = smoothedFreq;
    const float midi = 69.0f + 12.0f * std::log2 (smoothedFreq / a4);
    const int n = (int) std::lround (midi);
    r.cents = (midi - (float) n) * 100.0f;
    r.targetFreq = a4 * std::pow (2.0f, (n - 69) / 12.0f);
    r.note = noteNames[((n % 12) + 12) % 12];
    r.octave = n / 12 - 1;
    last = r;
    return r;
}

static void drawNeedle (Graphics& g, Rectangle<float> area, float cents, bool valid, bool compact)
{
    const bool inTune = valid && std::abs (cents) < 3.0f;
    const auto good = theme::good, off = theme::amber;
    // tick scale
    const int ticks = compact ? 11 : 21;
    for (int k = 0; k < ticks; ++k)
    {
        const float x = area.getX() + area.getWidth() * k / (float) (ticks - 1);
        const bool centre = k == ticks / 2;
        const float h = centre ? area.getHeight() : (k % 5 == 0 ? area.getHeight() * 0.6f : area.getHeight() * 0.35f);
        g.setColour (centre ? (inTune ? good : theme::muted) : theme::line.brighter (0.2f));
        g.fillRoundedRectangle (x - (centre ? 1.5f : 1.0f), area.getCentreY() - h * 0.5f, centre ? 3.0f : 2.0f, h, 1.0f);
    }
    if (! valid) return;
    const float x = area.getCentreX() + jlimit (-50.0f, 50.0f, cents) / 50.0f * area.getWidth() * 0.5f;
    g.setColour (inTune ? good : off);
    g.fillRoundedRectangle (x - (compact ? 2.0f : 3.0f), area.getY() - 4, compact ? 4.0f : 6.0f, area.getHeight() + 8, 3.0f);
}

//==============================================================================
TunerPanel::TunerPanel (NAMHomeProcessor& p) : proc (p)
{
    proc.engine.tunerMute = proc.settings().getBoolValue ("tunerMute", true);
    proc.engine.tunerInput = proc.settings().getIntValue ("tunerInput", 1);
    proc.engine.tunerOn = true;
    muteBtn.onClick = [this] { proc.engine.tunerMute = ! proc.engine.tunerMute.load(); proc.settings().setValue ("tunerMute", proc.engine.tunerMute.load()); updateButtons(); };
    unitBtn.onClick = [this] { proc.settings().setValue ("tunerHz", ! proc.settings().getBoolValue ("tunerHz", false)); updateButtons(); };
    inputBtn.onClick = [this] { proc.engine.tunerInput = proc.engine.tunerInput.load() == 1 ? 2 : 1; proc.settings().setValue ("tunerInput", proc.engine.tunerInput.load()); updateButtons(); };
    refDown.onClick = [this] { proc.settings().setValue ("tunerA4", jmax (420, proc.settings().getIntValue ("tunerA4", 440) - 1)); repaint(); };
    refUp.onClick = [this] { proc.settings().setValue ("tunerA4", jmin (460, proc.settings().getIntValue ("tunerA4", 440) + 1)); repaint(); };
    liveBtn.setClickingTogglesState (true);
    liveBtn.setToggleState (proc.settings().getBoolValue ("liveTuner", false), dontSendNotification);
    liveBtn.onClick = [this] { proc.settings().setValue ("liveTuner", liveBtn.getToggleState()); if (proc.onNotice) proc.onNotice (liveBtn.getToggleState() ? "Live tuner on: it stays in the top bar while you play." : "Live tuner off."); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &muteBtn, &unitBtn, &inputBtn, &refDown, &refUp, &liveBtn, &closeBtn }) addAndMakeVisible (b);
    updateButtons();
    startTimerHz (20);
}

TunerPanel::~TunerPanel()
{
    stopTimer();
    proc.engine.tunerMute = false;
    proc.engine.tunerOn = proc.settings().getBoolValue ("liveTuner", false);
}

void TunerPanel::updateButtons()
{
    const bool muted = proc.engine.tunerMute.load();
    muteBtn.setButtonText (muted ? "Output muted" : "Output on");
    muteBtn.setToggleState (muted, dontSendNotification);
    muteBtn.setColour (TextButton::buttonOnColourId, theme::warn.withAlpha (0.85f));
    unitBtn.setButtonText (proc.settings().getBoolValue ("tunerHz", false) ? "Showing Hz" : "Showing cents");
    inputBtn.setButtonText ("Input " + String (proc.engine.tunerInput.load()));
    repaint();
}

void TunerPanel::timerCallback()
{
    reading = detector.analyse (proc.engine, proc.getSampleRateNow(), (float) proc.settings().getIntValue ("tunerA4", 440));
    needle += ((reading.valid ? reading.cents : 0.0f) - needle) * 0.35f;
    repaint (card());
}

void TunerPanel::mouseDown (const MouseEvent& e)
{
    if (! card().contains (e.getPosition()) && onClose) onClose();
}

void TunerPanel::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.65f));
    auto c = card().toFloat();
    g.setColour (theme::card);
    g.fillRoundedRectangle (c, 20.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (c.reduced (0.5f), 20.0f, 1.0f);

    g.setColour (theme::text);
    g.setFont (theme::font (18.0f, 2));
    g.drawText ("Tuner", c.reduced (24, 18).removeFromTop (26), Justification::centredLeft);

    auto body = c.reduced (30).withTrimmedTop (36).withTrimmedBottom (70);
    const bool inTune = reading.valid && std::abs (reading.cents) < 3.0f;
    g.setColour (reading.valid ? (inTune ? theme::good : theme::text) : theme::faint);
    g.setFont (theme::font (96.0f, 2));
    auto noteArea = body.removeFromTop (110);
    g.drawText (reading.valid ? reading.note : String ("-"), noteArea, Justification::centred);
    if (reading.valid)
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (20.0f, 1));
        g.drawText (String (reading.octave), noteArea.withTrimmedLeft (noteArea.getWidth() * 0.5f + 52).withTrimmedTop (40), Justification::centredLeft);
    }

    drawNeedle (g, body.removeFromTop (54).reduced (20, 8), needle, reading.valid, false);

    const bool hz = proc.settings().getBoolValue ("tunerHz", false);
    g.setFont (theme::font (17.0f, 1));
    g.setColour (reading.valid ? theme::text : theme::faint);
    String detail = "Play a single string";
    if (reading.valid)
    {
        if (hz) detail = String (reading.freq, 1) + " Hz   (target " + String (reading.targetFreq, 1) + " Hz, " + (reading.freq >= reading.targetFreq ? "+" : "") + String (reading.freq - reading.targetFreq, 1) + " Hz)";
        else detail = (reading.cents >= 0 ? "+" : "") + String (reading.cents, 1) + " cents";
    }
    g.drawText (detail, body.removeFromTop (30), Justification::centred);

    g.setColour (theme::muted);
    g.setFont (theme::font (13.0f));
    g.drawText ("A4 = " + String (proc.settings().getIntValue ("tunerA4", 440)) + " Hz", refDown.getRight() + 4, refDown.getY(), 96, refDown.getHeight(), Justification::centred);
}

void TunerPanel::resized()
{
    auto c = card().reduced (24);
    closeBtn.setBounds (c.getRight() - 80, c.getY() - 2, 80, 30);
    auto row = c.removeFromBottom (34);
    muteBtn.setBounds (row.removeFromLeft (130)); row.removeFromLeft (8);
    unitBtn.setBounds (row.removeFromLeft (120)); row.removeFromLeft (8);
    inputBtn.setBounds (row.removeFromLeft (80)); row.removeFromLeft (12);
    refDown.setBounds (row.removeFromLeft (30)); row.removeFromLeft (104);
    refUp.setBounds (row.removeFromLeft (30)); row.removeFromLeft (12);
    liveBtn.setBounds (row);
}

//==============================================================================
LiveTuner::LiveTuner (NAMHomeProcessor& p) : proc (p)
{
    setTooltip ("Live tuner: follows you while you play. Click for the full tuner.");
    setMouseCursor (MouseCursor::PointingHandCursor);
}

LiveTuner::~LiveTuner() { stopTimer(); }

void LiveTuner::setActive (bool on)
{
    setVisible (on);
    if (on) { proc.engine.tunerOn = true; startTimerHz (15); }
    else stopTimer();
}

void LiveTuner::timerCallback()
{
    if (! proc.engine.tunerOn.load()) proc.engine.tunerOn = true;
    reading = detector.analyse (proc.engine, proc.getSampleRateNow(), (float) proc.settings().getIntValue ("tunerA4", 440));
    needle += ((reading.valid ? reading.cents : 0.0f) - needle) * 0.4f;
    repaint();
}

void LiveTuner::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::raised);
    g.fillRoundedRectangle (r, 8.0f);
    const bool inTune = reading.valid && std::abs (reading.cents) < 3.0f;
    auto noteArea = r.removeFromLeft (34);
    g.setColour (reading.valid ? (inTune ? theme::good : theme::text) : theme::faint);
    g.setFont (theme::font (15.0f, 2));
    g.drawText (reading.valid ? reading.note : String ("-"), noteArea, Justification::centred);
    const bool hz = proc.settings().getBoolValue ("tunerHz", false);
    auto txt = r.removeFromRight (54);
    g.setColour (theme::muted);
    g.setFont (theme::font (11.0f, 1));
    if (reading.valid)
        g.drawText (hz ? String (reading.freq, 1) : (reading.cents >= 0 ? "+" : "") + String (roundToInt (reading.cents)) + "c", txt, Justification::centred);
    drawNeedle (g, r.reduced (4, 9), needle, reading.valid, true);
}
