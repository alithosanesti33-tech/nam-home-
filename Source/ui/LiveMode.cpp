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
    const auto maps = proc.engine.listMappings();
    for (int i = 0; i < NAMHomeProcessor::stripSlots; ++i)
    {
        String midi;
        for (auto& m : maps) if (m.global && m.m.blockId == Engine::stripMidiId && m.m.param == i) midi = m.message;
        const auto a = acts[i];
        bigButton (g, stripR[i], NAMHomeProcessor::actionLabel (a), midi.isNotEmpty() ? midi : (a == "none" ? String ("right-click to set") : String()),
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
        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            m.addItem (1, "MIDI learn the emergency switch");
            m.showMenuAsync (PopupMenu::Options(), [this] (int r)
            {
                if (r == 1) { proc.engine.armLearn (Engine::panicMidiId, 0, true); if (onMessage) onMessage ("Press the switch on your controller now...", false); }
            });
            return;
        }
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
    m.addSeparator();
    m.addItem (900, "MIDI learn this button");
    m.addItem (901, "Clear its MIDI");
    m.showMenuAsync (PopupMenu::Options(), [this, slot, all] (int r)
    {
        if (r >= 1 && r <= all.size()) { proc.setStripAction (slot, all[r - 1]); repaint(); }
        if (r == 900) { proc.engine.armLearn (Engine::stripMidiId, slot, true); if (onMessage) onMessage ("Press the switch on your controller now...", false); }
        if (r == 901)
            for (auto& mp : proc.engine.listMappings())
                if (mp.global && mp.m.blockId == Engine::stripMidiId && mp.m.param == slot) proc.engine.removeMapping (true, mp.index);
    });
}

//==============================================================================
MidiSetupOverlay::MidiSetupOverlay (NAMHomeProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, true);
    done.onClick = [this] { if (onDone) onDone(); };
    done.setColour (TextButton::buttonColourId, Colour (0xff111111));
    addAndMakeVisible (done);
    startTimerHz (10);
}

void MidiSetupOverlay::resized()
{
    done.setBounds (banner.withTrimmedLeft (banner.getWidth() - 100).reduced (10, 8));
}

void MidiSetupOverlay::paint (Graphics& g)
{
    g.setColour (theme::amber);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 16.0f, 3.0f);
    g.fillRoundedRectangle (banner.toFloat(), 12.0f);
    g.setColour (Colour (0xff111111));
    g.setFont (theme::font (14.5f, 2));
    const auto txt = proc.engine.isLearning() ? String ("Now press the switch or move the pedal on your controller...")
                                              : String ("MIDI setup: click any knob, switch or choice, then press a switch on your controller. Each block's on/off is the MIDI icon in its editor.");
    g.drawFittedText (txt, banner.reduced (16, 4).withTrimmedRight (100), Justification::centredLeft, 2, 0.85f);

    // show what's already assigned on the controls you can see
    std::function<void (Component&)> walk = [&] (Component& c)
    {
        for (auto* ch : c.getChildren())
        {
            if (! ch->isShowing() || ch == this) continue;
            Block* b = nullptr; int idx = -1;
            if (auto* k = dynamic_cast<ParamKnob*> (ch)) { b = &k->getBlock(); idx = k->getIndex(); }
            else if (auto* s = dynamic_cast<ParamSegments*> (ch)) { b = &s->getBlock(); idx = s->getIndex(); }
            else if (auto* cb = dynamic_cast<ParamCombo*> (ch)) { b = &cb->getBlock(); idx = cb->getIndex(); }
            else if (auto* sw = dynamic_cast<ParamSwitch*> (ch)) { b = &sw->getBlock(); idx = sw->getIndex(); }
            if (b != nullptr)
            {
                const auto d = proc.engine.describeMapping (b->id, idx);
                if (d.isNotEmpty())
                {
                    auto rr = getLocalArea (ch, ch->getLocalBounds());
                    auto pill = rr.withHeight (18).withSizeKeepingCentre (jmin (rr.getWidth(), 110), 18).withY (rr.getY());
                    g.setColour (theme::amber);
                    g.fillRoundedRectangle (pill.toFloat(), 9.0f);
                    g.setColour (Colour (0xff111111));
                    g.setFont (theme::font (11.0f, 2));
                    g.drawFittedText (d, pill, Justification::centred, 1, 0.7f);
                }
                continue;
            }
            walk (*ch);
        }
    };
    if (auto* parent = getParentComponent()) walk (*parent);
}

//==============================================================================
ControllerPage::ControllerPage (NAMHomeProcessor& p) : proc (p)
{
    auto title = [this] (Label& l, const String& t)
    {
        l.setText (t, dontSendNotification);
        l.setFont (theme::font (15.0f, 2));
        l.setColour (Label::textColourId, theme::text);
        addAndMakeVisible (l);
    };
    title (layoutTitle, "Foot controller layout");
    title (panicTitle, "Emergency bypass");
    title (mapTitle, "MIDI map");

    auto& st = proc.settings();
    layout.addItemList ({ "Preset mode", "Scene mode", "Stomp mode", "Hybrid (scenes + stomps)" }, 1);
    layout.setSelectedItemIndex (st.getIntValue ("fcLayout", 1), dontSendNotification);
    for (int i = 2; i <= 12; ++i) switches.addItem (String (i) + " switches", i);
    switches.setSelectedId (st.getIntValue ("fcSwitches", 4), dontSendNotification);
    sends.addItemList ({ "They send Program Change", "They send CC", "They send Notes" }, 1);
    sends.setSelectedItemIndex (st.getIntValue ("fcSends", 0), dontSendNotification);
    for (int i = 0; i <= 127; ++i) firstNum.addItem ("starting at " + String (i), i + 1);
    firstNum.setSelectedId (st.getIntValue ("fcFirst", 1) + 1, dontSendNotification);
    latching.setToggleState (st.getBoolValue ("fcLatch", false), dontSendNotification);
    for (auto* c : std::initializer_list<Component*> { &layout, &switches, &sends, &firstNum, &latching, &generate, &result,
                                                       &panicBox, &safeBox, &learnPanic, &setupMode, &list, &rename, &remove })
        addAndMakeVisible (c);
    result.setFont (theme::font (12.5f));
    result.setColour (Label::textColourId, theme::muted);
    result.setText ("Pick how your switches should work and press the button: NAM Home makes the assignments for every preset.", dontSendNotification);
    generate.onClick = [this]
    {
        auto& s = proc.settings();
        s.setValue ("fcLayout", layout.getSelectedItemIndex());
        s.setValue ("fcSwitches", switches.getSelectedId());
        s.setValue ("fcSends", sends.getSelectedItemIndex());
        s.setValue ("fcFirst", firstNum.getSelectedId() - 1);
        s.setValue ("fcLatch", latching.getToggleState());
        const auto msg = proc.generateLayout (layout.getSelectedItemIndex(), switches.getSelectedId(), sends.getSelectedItemIndex(),
                                              firstNum.getSelectedId() - 1, latching.getToggleState());
        result.setText (msg, dontSendNotification);
        refresh();
    };

    panicBox.addItemList ({ "Dry guitar straight through", "Mute the output", "Load a safe preset" }, 1);
    panicBox.setSelectedItemIndex (proc.panicMode(), dontSendNotification);
    panicBox.onChange = [this] { proc.settings().setValue ("panicMode", panicBox.getSelectedItemIndex()); safeBox.setVisible (panicBox.getSelectedItemIndex() == 2); };
    safeBox.setTextWhenNothingSelected ("Choose the safe preset");
    {
        const auto names = proc.presets.list();
        safeBox.addItemList (names, 1);
        const int i = names.indexOf (proc.settings().getValue ("safePreset"));
        if (i >= 0) safeBox.setSelectedItemIndex (i, dontSendNotification);
    }
    safeBox.onChange = [this] { proc.settings().setValue ("safePreset", safeBox.getText()); };
    safeBox.setVisible (proc.panicMode() == 2);
    learnPanic.onClick = [this] { proc.engine.armLearn (Engine::panicMidiId, 0, true); if (proc.onNotice) proc.onNotice ("Press the switch on your controller now..."); };

    setupMode.onClick = [this] { if (onMidiSetup) onMidiSetup(); };
    list.setModel (this);
    list.setRowHeight (30);
    list.setColour (ListBox::backgroundColourId, theme::panel);
    rename.onClick = [this]
    {
        const int r = list.getSelectedRow();
        if (r < 0 || r >= (int) rows.size()) return;
        const auto row = rows[(size_t) r];
        auto* aw = new AlertWindow ("Name this switch", row.message + "  ->  " + row.target, MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", row.name);
        aw->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
        aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        aw->enterModalState (true, ModalCallbackFunction::create ([this, aw, row] (int ok)
        {
            if (ok != 1) return;
            proc.engine.renameMapping (row.global, row.index, aw->getTextEditorContents ("name").trim());
            if (! row.global) proc.dirty = true;
            refresh();
        }), true);
    };
    remove.onClick = [this]
    {
        const int r = list.getSelectedRow();
        if (r < 0 || r >= (int) rows.size()) return;
        proc.engine.removeMapping (rows[(size_t) r].global, rows[(size_t) r].index);
        if (! rows[(size_t) r].global) proc.dirty = true;
        refresh();
    };
    refresh();
    startTimerHz (10);
}

ControllerPage::~ControllerPage() { list.setModel (nullptr); }

void ControllerPage::refresh()
{
    rows.clear();
    for (auto& m : proc.engine.listMappings())
        rows.push_back ({ m.global, m.index, m.m.name, m.message, m.target, m.duplicate, m.m.kind, m.m.number });
    list.updateContent();
    list.repaint();
}

void ControllerPage::timerCallback()
{
    const int hc = proc.engine.hitCount.load();
    if (hc != lastHit)
    {
        lastHit = hc;
        hitKind = proc.engine.hitKind.load(); hitNum = proc.engine.hitNum.load();
        hitAt = Time::getMillisecondCounterHiRes();
        list.repaint();
    }
    if ((int) proc.engine.listMappings().size() != (int) rows.size()) refresh();
    if (hitAt > 0 && Time::getMillisecondCounterHiRes() - hitAt > 700) { hitAt = 0; list.repaint(); }
}

void ControllerPage::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& r = rows[(size_t) row];
    const bool hit = hitAt > 0 && r.kind == hitKind && r.number == hitNum;
    g.setColour (hit ? theme::good.withAlpha (0.35f) : selected ? theme::hover : Colours::transparentBlack);
    g.fillRect (0, 0, w, h);
    g.setFont (theme::font (13.0f, 1));
    g.setColour (theme::text);
    g.drawText (r.name.isNotEmpty() ? r.name : r.target, 10, 0, w * 30 / 100, h, Justification::centredLeft);
    g.setColour (r.duplicate ? theme::amber : theme::muted);
    g.drawText (r.message + (r.duplicate ? "  (used twice)" : ""), w * 31 / 100, 0, w * 27 / 100, h, Justification::centredLeft);
    g.setColour (theme::muted);
    g.setFont (theme::font (12.5f));
    g.drawText (r.target + (r.global ? "   (all presets)" : "   (this preset)"), w * 58 / 100, 0, w * 42 / 100 - 8, h, Justification::centredLeft);
}

void ControllerPage::listBoxItemDoubleClicked (int row, const MouseEvent&) { list.selectRow (row); rename.triggerClick(); }

void ControllerPage::paint (Graphics& g)
{
    g.setColour (theme::faint);
    g.setFont (theme::font (12.0f));
    g.drawText ("A row lights up green when its switch arrives, so you can test each one. Double-click a row to name it.",
                list.getX(), list.getBottom() + 4, list.getWidth(), 18, Justification::centredLeft);
}

void ControllerPage::resized()
{
    auto r = getLocalBounds();
    layoutTitle.setBounds (r.removeFromTop (24));
    auto row = r.removeFromTop (30);
    layout.setBounds (row.removeFromLeft (180)); row.removeFromLeft (8);
    switches.setBounds (row.removeFromLeft (120)); row.removeFromLeft (8);
    sends.setBounds (row.removeFromLeft (190)); row.removeFromLeft (8);
    firstNum.setBounds (row.removeFromLeft (jmin (130, row.getWidth())));
    r.removeFromTop (6);
    row = r.removeFromTop (30);
    generate.setBounds (row.removeFromRight (190));
    latching.setBounds (row);
    result.setBounds (r.removeFromTop (38));
    r.removeFromTop (8);
    row = r.removeFromTop (26);
    learnPanic.setBounds (row.removeFromRight (240));
    panicTitle.setBounds (row);
    row = r.removeFromTop (30);
    panicBox.setBounds (row.removeFromLeft (250)); row.removeFromLeft (8);
    safeBox.setBounds (row.removeFromLeft (240));
    r.removeFromTop (14);
    row = r.removeFromTop (28);
    setupMode.setBounds (row.removeFromRight (170));
    mapTitle.setBounds (row);
    r.removeFromTop (4);
    auto bottom = r.removeFromBottom (30);
    remove.setBounds (bottom.removeFromRight (100)); bottom.removeFromRight (8);
    rename.setBounds (bottom.removeFromRight (100));
    r.removeFromBottom (26);
    list.setBounds (r);
}
