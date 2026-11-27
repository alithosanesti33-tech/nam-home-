#include "LiveMode.h"
#include "../PluginProcessor.h"

using namespace juce;

//==============================================================================
GigCheckPanel::GigCheckPanel (NAMHomeProcessor& p, AudioDeviceManager* dm)
{
    for (auto& g : p.gigCheck (dm)) items.push_back ({ g.ok, g.text });
}

bool GigCheckPanel::allGood() const
{
    for (auto& i : items) if (! i.ok) return false;
    return true;
}

int GigCheckPanel::preferredHeight (int) const { return 120 + 34 * (int) items.size() + 40; }

void GigCheckPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::card);
    g.fillRoundedRectangle (r, 18.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 18.0f, 1.0f);
    auto a = getLocalBounds().reduced (26, 20);
    const bool ok = allGood();
    g.setColour (ok ? theme::good : theme::amber);
    g.setFont (theme::font (30.0f, 2));
    g.drawText (ok ? "READY TO PLAY" : "NEEDS ATTENTION", a.removeFromTop (44), Justification::centredLeft);
    g.setColour (theme::muted);
    g.setFont (theme::font (13.0f));
    g.drawText ("Gig check", a.removeFromTop (22), Justification::centredLeft);
    a.removeFromTop (14);
    for (auto& i : items)
    {
        auto row = a.removeFromTop (34);
        auto dot = row.removeFromLeft (26).toFloat().withSizeKeepingCentre (16, 16);
        g.setColour (i.ok ? theme::good : theme::amber);
        g.fillEllipse (dot);
        g.setColour (Colour (0xff111111));
        g.setFont (theme::font (11.0f, 2));
        g.drawText (i.ok ? "OK" : "!", dot.toNearestInt(), Justification::centred);
        g.setColour (i.ok ? theme::text : theme::amber);
        g.setFont (theme::font (13.5f, i.ok ? 0 : 1));
        g.drawFittedText (i.text, row.withTrimmedLeft (6), Justification::centredLeft, 2, 0.9f);
    }
    g.setColour (theme::faint);
    g.setFont (theme::font (12.0f));
    g.drawText ("Click anywhere to close", a.removeFromBottom (24), Justification::centredLeft);
}

//==============================================================================
LiveView::LiveView (NAMHomeProcessor& p, AudioDeviceManager* dm) : proc (p), deviceManager (dm), tuner (p)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (tuner);
    tuner.setActive (true);
    startTimerHz (20);
}

LiveView::~LiveView() { tuner.setActive (false); }

void LiveView::resized()
{
    auto r = getLocalBounds().reduced (24, 18);
    auto top = r.removeFromTop (46);
    checkR = top.removeFromLeft (190);
    exitR = top.removeFromRight (230);
    r.removeFromTop (8);
    bannerR = r.removeFromTop (50);
    r.removeFromTop (6);
    const float hs = jlimit (0.7f, 1.4f, getHeight() / 860.0f);
    auto preset = r.removeFromTop ((int) (150 * hs));
    prevR = preset.removeFromLeft ((int) (130 * hs)).reduced (0, 12);
    nextR = preset.removeFromRight ((int) (130 * hs)).reduced (0, 12);
    nameR = preset.reduced (20, 0);
    r.removeFromTop (10);
    auto info = r.removeFromTop ((int) (76 * hs));
    tempoR = info.removeFromLeft (230);
    info.removeFromLeft (12);
    tapR = info.removeFromLeft (150);
    info.removeFromLeft (24);
    tuner.setBounds (info.removeFromRight (jmin (440, info.getWidth())).reduced (0, (int) (8 * hs)));
    r.removeFromTop (18);
    const int sceneH = jmin ((int) (130 * hs), r.getHeight() / 2 - 10);
    auto scenes = r.removeFromTop (sceneH);
    const int sw = (scenes.getWidth() - 7 * 10) / 8;
    for (int i = 0; i < 8; ++i) { sceneR[i] = scenes.removeFromLeft (sw); scenes.removeFromLeft (10); }
    r.removeFromTop (18);
    auto strip = r.removeFromTop (jmin ((int) (150 * hs), r.getHeight()));
    const int bw = (strip.getWidth() - 6 * 10) / 7;
    for (int i = 0; i < 6; ++i) { stripR[i] = strip.removeFromLeft (bw); strip.removeFromLeft (10); }
    panicR = strip;
    if (check != nullptr)
    {
        const int w = jmin (720, getWidth() - 80);
        check->setBounds (getLocalBounds().withSizeKeepingCentre (w, jmin (getHeight() - 60, check->preferredHeight (w))));
    }
}

static void bigButton (Graphics& g, Rectangle<int> r, const String& text, const String& sub, bool lit, Colour litColour, bool down, float fontSize)
{
    auto f = r.toFloat();
    g.setColour (lit ? litColour : (down ? theme::hover : theme::raised));
    g.fillRoundedRectangle (f, 14.0f);
    g.setColour (lit ? litColour.brighter (0.3f) : theme::line);
    g.drawRoundedRectangle (f.reduced (0.5f), 14.0f, 1.0f);
    g.setColour (lit ? Colour (0xff101010) : theme::text);
    g.setFont (theme::font (fontSize, 2));
    auto t = r.reduced (8, 6);
    if (sub.isNotEmpty())
    {
        g.drawFittedText (text, t.removeFromTop (t.getHeight() * 6 / 10), Justification::centredBottom, 2, 0.8f);
        g.setColour (lit ? Colour (0xff202020) : theme::muted);
        g.setFont (theme::font (fontSize * 0.45f, 1));
        g.drawFittedText (sub, t, Justification::centredTop, 1, 0.8f);
    }
    else g.drawFittedText (text, t, Justification::centred, 2, 0.8f);
}

void LiveView::paint (Graphics& g)
{
    g.fillAll (theme::bg);
    const float hs = jlimit (0.7f, 1.4f, getHeight() / 860.0f);

    // top: gig check, LIVE, hold to exit
    bigButton (g, checkR, "Gig check", {}, false, theme::good, pressed == 100, 15.0f);
    g.setColour (theme::accent);
    g.setFont (theme::font (15.0f, 2));
    g.drawText ("LIVE", getLocalBounds().withHeight (46 + 18).withTrimmedTop (18), Justification::centredTop);
    {
        auto f = exitR.toFloat();
        g.setColour (theme::raised);
        g.fillRoundedRectangle (f, 14.0f);
        if (exitHeld > 0)
        {
            const float t = (float) jlimit (0.0, 1.0, (Time::getMillisecondCounterHiRes() - exitHeld) / 1200.0);
            g.setColour (theme::accent.withAlpha (0.5f));
            g.fillRoundedRectangle (f.withWidth (f.getWidth() * t), 14.0f);
        }
        g.setColour (theme::muted);
        g.setFont (theme::font (14.0f, 1));
        g.drawText ("Hold to exit live mode", exitR, Justification::centred);
    }

    // emergency bypass banner
    if (proc.panicActive())
    {
        g.setColour (theme::warn);
        g.fillRoundedRectangle (bannerR.toFloat(), 12.0f);
        g.setColour (Colours::white);
        g.setFont (theme::font (18.0f, 2));
        g.drawText (proc.engine.panic.load() == 2 ? "OUTPUT MUTED  -  press Emergency again to come back"
                                                  : "EMERGENCY BYPASS: dry guitar  -  press Emergency again to come back", bannerR, Justification::centred);
    }

    // preset
    bigButton (g, prevR, "<", {}, false, theme::accent, pressed == 101, 40.0f * hs);
    bigButton (g, nextR, ">", {}, false, theme::accent, pressed == 102, 40.0f * hs);
    {
        g.setColour (theme::text);
        g.setFont (theme::font (54.0f * hs, 2));
        const auto name = proc.currentPreset.isEmpty() ? String ("Untitled rig") : proc.currentPreset;
        auto nr = nameR;
        g.drawFittedText (name, nr.removeFromTop (nr.getHeight() * 2 / 3), Justification::centredBottom, 1, 0.6f);
        g.setColour (theme::muted);
        g.setFont (theme::font (17.0f * hs, 1));
        const auto order = proc.presetOrder();
        const int idx = order.indexOf (proc.currentPreset);
        String sub;
        if (! proc.setlist.isEmpty()) sub = proc.setlistName + "  -  song " + String (idx + 1) + " of " + String (order.size());
        else if (idx >= 0) sub = "Preset " + String (idx + 1) + " of " + String (order.size());
        if (proc.dirty) sub << "  -  edited";
        g.drawText (sub, nr, Justification::centredTop);
    }

    // tempo and tap
    {
        auto f = tempoR.toFloat();
        g.setColour (theme::raised);
        g.fillRoundedRectangle (f, 14.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (34.0f * hs, 2));
        g.drawText (String (roundToInt (proc.engine.getBpm())), tempoR.withTrimmedRight (tempoR.getWidth() * 40 / 100), Justification::centredRight);
        g.setColour (theme::faint);
        g.setFont (theme::font (15.0f, 1));
        g.drawText ("BPM", tempoR.withTrimmedLeft (tempoR.getWidth() * 64 / 100), Justification::centredLeft);
    }
    bigButton (g, tapR, "Tap", {}, false, theme::accent, pressed == 103, 22.0f * hs);

    // scenes
    const int cur = proc.engine.currentScene.load();
    for (int i = 0; i < 8; ++i)
    {
        const auto label = proc.engine.sceneLabel (i);
        const auto letter = label.substring (0, 1), name = label.substring (1).trim();
        const bool used = proc.engine.sceneIsUsed (i) || i == cur;
        bigButton (g, sceneR[i], letter, name, i == cur, theme::accent, pressed == i, (used ? 40.0f : 30.0f) * hs);
        if (! used && i != cur) { g.setColour (theme::bg.withAlpha (0.45f)); g.fillRoundedRectangle (sceneR[i].toFloat(), 14.0f); }
    }

    // the performance strip and the emergency button
    const auto acts = proc.stripActions();
    for (int i = 0; i < NAMHomeProcessor::stripSlots; ++i)
    {
        const auto a = acts[i];
        bigButton (g, stripR[i], NAMHomeProcessor::actionLabel (a), a == "none" ? String ("right-click to set") : String(),
                   proc.actionLit (a), a == "panic" ? theme::warn : theme::amber, pressed == 200 + i, 22.0f * hs);
    }
    {
        const int pm = proc.panicMode();
        bigButton (g, panicR, "Emergency", pm == 0 ? "dry guitar" : pm == 1 ? "mute" : "safe preset", proc.panicActive(), theme::warn, pressed == 300, 22.0f * hs);
        if (! proc.panicActive()) { g.setColour (theme::warn); g.drawRoundedRectangle (panicR.toFloat().reduced (1.5f), 14.0f, 2.5f); }
    }
}

void LiveView::mouseDown (const MouseEvent& e)
{
    const auto pt = e.getPosition();
    if (exitR.contains (pt)) { exitHeld = Time::getMillisecondCounterHiRes(); repaint(); return; }
    for (int i = 0; i < NAMHomeProcessor::stripSlots; ++i)
        if (stripR[i].contains (pt))
        {
            if (e.mods.isPopupMenu()) { stripMenu (i); return; }
            pressed = 200 + i;
            proc.performAction (proc.stripActions()[i]);
            repaint(); return;
        }
    if (panicR.contains (pt))
    {
        if (e.mods.isPopupMenu()) return;
        pressed = 300; proc.triggerPanic(); repaint(); return;
    }
    for (int i = 0; i < 8; ++i)
        if (sceneR[i].contains (pt)) { pressed = i; proc.engine.selectScene (i); repaint(); return; }
    if (prevR.contains (pt)) { pressed = 101; proc.stepPresetAny (-1); repaint(); return; }
    if (nextR.contains (pt)) { pressed = 102; proc.stepPresetAny (1); repaint(); return; }
    if (tapR.contains (pt)) { pressed = 103; proc.engine.tapTempo(); repaint(); return; }
    if (checkR.contains (pt))
    {
        pressed = 100;
        check = std::make_unique<GigCheckPanel> (proc, deviceManager);
        check->onClose = [this] { MessageManager::callAsync ([sp = SafePointer<LiveView> (this)] { if (sp != nullptr) { sp->check = nullptr; sp->repaint(); } }); };
        addAndMakeVisible (*check);
        resized();
        return;
    }
}

void LiveView::mouseUp (const MouseEvent&)
{
    exitHeld = 0;
    pressed = -1;
    repaint();
}

void LiveView::timerCallback()
{
    if (exitHeld > 0 && Time::getMillisecondCounterHiRes() - exitHeld > 1200.0)
    {
        exitHeld = 0;
        if (onExit) onExit();
        return;
    }
    repaint();
}

void LiveView::stripMenu (int slot)
{
    PopupMenu m, blocks;
    const auto cur = proc.stripActions()[slot];
    int id = 1;
    const auto all = NAMHomeProcessor::allActions();
    for (auto& a : all)
    {
        if (a.startsWith ("type:")) blocks.addItem (id, NAMHomeProcessor::actionLabel (a) + " on/off", true, a == cur);
        else m.addItem (id, NAMHomeProcessor::actionLabel (a), true, a == cur);
        ++id;
    }
    m.addSubMenu ("Turn a block on/off", blocks);
    m.showMenuAsync (PopupMenu::Options(), [this, slot, all] (int r)
    {
        if (r >= 1 && r <= all.size()) { proc.setStripAction (slot, all[r - 1]); repaint(); }
    });
}

//==============================================================================
ControllerPage::ControllerPage (NAMHomeProcessor& p) : proc (p)
{
    panicTitle.setText ("Emergency button (live mode)", dontSendNotification);
    panicTitle.setFont (theme::font (15.0f, 2));
    panicTitle.setColour (Label::textColourId, theme::text);
    help.setText ("What the red Emergency button in live mode does. Press it again to get your rig back.", dontSendNotification);
    help.setFont (theme::font (12.5f));
    help.setColour (Label::textColourId, theme::muted);
    panicBox.addItemList ({ "Dry guitar straight through", "Mute the output", "Load a safe preset" }, 1);
    panicBox.setSelectedItemIndex (proc.panicMode(), dontSendNotification);
    panicBox.onChange = [this] { proc.settings().setValue ("panicMode", panicBox.getSelectedItemIndex()); safeBox.setVisible (panicBox.getSelectedItemIndex() == 2); };
    safeBox.setTextWhenNothingSelected ("Choose the safe preset");
    const auto names = proc.presets.list();
    safeBox.addItemList (names, 1);
    const int i = names.indexOf (proc.settings().getValue ("safePreset"));
    if (i >= 0) safeBox.setSelectedItemIndex (i, dontSendNotification);
    safeBox.onChange = [this] { proc.settings().setValue ("safePreset", safeBox.getText()); };
    safeBox.setVisible (proc.panicMode() == 2);
    for (auto* c : std::initializer_list<Component*> { &panicTitle, &help, &panicBox, &safeBox }) addAndMakeVisible (c);
}

void ControllerPage::resized()
{
    auto r = getLocalBounds();
    panicTitle.setBounds (r.removeFromTop (26));
    help.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    auto row = r.removeFromTop (30);
    panicBox.setBounds (row.removeFromLeft (250)); row.removeFromLeft (8);
    safeBox.setBounds (row.removeFromLeft (240));
}
