#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using namespace juce;

//==============================================================================
void TempoDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (isMouseOver() ? theme::hover : theme::raised);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, 2));
    g.drawText (String (roundToInt (engine.getBpm())), r.withTrimmedRight (r.getWidth() * 0.42f), Justification::centredRight);
    g.setColour (theme::faint);
    g.setFont (theme::font (11.5f, 1));
    g.drawText ("BPM", r.withTrimmedLeft (r.getWidth() * 0.62f), Justification::centredLeft);
}

void TempoDisplay::mouseDoubleClick (const MouseEvent&)
{
    auto* aw = new AlertWindow ("Tempo", "Type a tempo in BPM", MessageBoxIconType::NoIcon);
    aw->addTextEditor ("bpm", String (roundToInt (engine.getBpm())));
    aw->addButton ("Set", 1, KeyPress (KeyPress::returnKey));
    aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    aw->enterModalState (true, ModalCallbackFunction::create ([this, aw] (int r)
    {
        if (r == 1) { const float v = aw->getTextEditorContents ("bpm").getFloatValue(); if (v > 0) engine.setBpm (v); repaint(); }
    }), true);
}

//==============================================================================
Drawer::Drawer (NAMHomeProcessor& p) : proc (p)
{
    power.onClick = [this]
    {
        if (auto b = current()) { b->enabled = ! b->enabled.load(); proc.dirty = true; repaint(); if (onChanged) onChanged(); }
    };
    midi.onClick = [this]
    {
        if (auto b = current()) { proc.engine.armLearn (b->id, -1); if (onMessage) onMessage ("Press the footswitch on your MIDI device now...", false); }
    };
    close.onClick = [this] { if (onClose) onClose(); };
    starts.onClick = [this]
    {
        auto b = current();
        if (b == nullptr) return;
        PopupMenu m;
        auto list = b->startingPoints();
        for (int i = 0; i < (int) list.size(); ++i) m.addItem (i + 1, list[(size_t) i].name);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&starts), [this, b, list] (int r)
        {
            if (r <= 0 || current() != b) return;
            b->applyStartingPoint (list[(size_t) r - 1]);
            proc.dirty = true;
            sync(); repaint();
            if (onChanged) onChanged();
            if (onMessage) onMessage (list[(size_t) r - 1].name + ": " + list[(size_t) r - 1].note, false);
        });
    };
    for (auto* c : std::initializer_list<Component*> { &power, &midi, &close, &starts }) addChildComponent (c);
    for (auto* c : std::initializer_list<Component*> { &power, &midi, &close }) c->setVisible (true);
}

void Drawer::show (std::shared_ptr<Block> b)
{
    editor = createEditorFor (proc, b);
    editor->onStructureChanged = [this] { repaint(); if (onChanged) onChanged(); };
    editor->onError = [this] (const String& s) { if (onMessage) onMessage (s, true); };
    addAndMakeVisible (*editor);
    starts.setVisible (! b->startingPoints().empty());
    resized();
    repaint();
}

void Drawer::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
    auto b = current();
    if (b == nullptr) return;
    auto hdr = getLocalBounds().removeFromTop (54).reduced (18, 0);
    g.setColour (b->getColour());
    g.fillRoundedRectangle ((float) hdr.getX(), (float) hdr.getCentreY() - 7, 14, 14, 4.0f);
    // the block's name, small, top left (like Cortex Control)
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, 2));
    const auto title = b->getTitle();
    const int tw = (int) GlyphArrangement::getStringWidth (theme::font (13.0f, 2), title) + 8;
    g.drawText (title, hdr.getX() + 24, hdr.getY(), tw, hdr.getHeight(), Justification::centredLeft);
    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f));
    g.drawText (b->getTypeName() + (b->enabled.load() ? String() : String ("  ·  Off")), hdr.getX() + 32 + tw, hdr.getY(), 300, hdr.getHeight(), Justification::centredLeft);
    g.setColour (theme::line);
    g.drawHorizontalLine (54, 12.0f, (float) getWidth() - 12.0f);
}

void Drawer::mouseMove (const MouseEvent& e) { setMouseCursor (e.y < 10 ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor); }
void Drawer::mouseDown (const MouseEvent& e) { resizing = e.y < 10; }
void Drawer::mouseDrag (const MouseEvent& e) { if (resizing && onResizeDrag) onResizeDrag (e.getDistanceFromDragStartY(), false); }
void Drawer::mouseUp (const MouseEvent& e) { if (resizing && onResizeDrag) onResizeDrag (e.getDistanceFromDragStartY(), true); resizing = false; }

void Drawer::resized()
{
    auto hdr = getLocalBounds().removeFromTop (54).reduced (14, 11);
    close.setBounds (hdr.removeFromRight (32));
    hdr.removeFromRight (6);
    midi.setBounds (hdr.removeFromRight (32));
    hdr.removeFromRight (6);
    power.setBounds (hdr.removeFromRight (32));
    hdr.removeFromRight (10);
    starts.setBounds (hdr.removeFromRight (140));
    if (editor != nullptr) editor->setBounds (getLocalBounds().withTrimmedTop (56).reduced (2, 4));
}

void Drawer::sync()
{
    if (auto b = current())
    {
        power.lit = b->enabled.load();
        power.repaint();
        midi.lit = proc.engine.describeMapping (b->id, -1).isNotEmpty();
        midi.repaint();
        // live status in the title (looper state, drum groove...)
        const auto t = b->getTitle();
        if (t != lastTitle) { lastTitle = t; repaint (0, 0, getWidth(), 54); }
    }
    if (editor != nullptr) editor->sync();
}

//==============================================================================
NAMHomeEditor::NAMHomeEditor (NAMHomeProcessor& p)
    : AudioProcessorEditor (&p), proc (p), tempo (p.engine), library (p), grid (p), sceneBar (p), drawer (p)
{
    LookAndFeel::setDefaultLookAndFeel (&lnf);
    setLookAndFeel (&lnf);
    applyDisplaySettings();

    for (auto* c : std::initializer_list<Component*> { &libraryBtn, &prevBtn, &nextBtn, &presetBtn, &saveBtn, &tempo, &tapBtn, &metroBtn, &transposeBtn,
                                                       &browseT3k, &inMeter, &outMeter, &tunerBtn, &fullBtn, &settingsBtn, &library, &grid })
        addAndMakeVisible (c);
    addChildComponent (drawer);
    addAndMakeVisible (toasts);

    libraryBtn.lit = true;
    libraryBtn.onClick = [this] { libraryOpen = ! libraryOpen; libraryBtn.lit = libraryOpen; library.setVisible (libraryOpen); resized(); };
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    presetBtn.onClick = [this] { showPresetMenu(); };
    presetBtn.setColour (TextButton::buttonColourId, theme::raised);
    saveBtn.onClick = [this] { if (proc.currentPreset.isEmpty()) saveAs (false); else { proc.savePreset (proc.currentPreset); message ("Saved \"" + proc.currentPreset + "\""); } };
    tapBtn.onClick = [this] { proc.engine.tapTempo(); tempo.repaint(); };
    metroBtn.onClick = [this] { proc.engine.metronomeOn = ! proc.engine.metronomeOn.load(); metroBtn.lit = proc.engine.metronomeOn.load(); metroBtn.repaint(); };
    browseT3k.setColour (TextButton::buttonColourId, theme::raised);
    browseT3k.setTooltip ("Pick tones on TONE3000 and download them into your library");
    browseT3k.onClick = [this]
    {
        if (proc.isPlugin())
        {
            // the TONE3000 sign-in comes back to the NAM Home app, so browsing happens there;
            // the library folder is shared, so anything downloaded shows up here within seconds
            message ("Browse TONE3000 from the NAM Home app. Anything you download there shows up here automatically.");
            return;
        }
        PopupMenu m;
        m.addItem (1, "Amp and pedal captures (NAM)");
        m.addItem (2, "Cabinet IRs");
        m.addItem (3, "Everything");
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (browseT3k), [this] (int r)
        {
            if (r == 0) return;
            const auto err = proc.tone3000->browse (r == 1 ? "nam" : r == 2 ? "ir" : "");
            if (err.isNotEmpty()) { message (err, true); if (proc.tone3000->getClientId().isEmpty()) { openSettings(); if (settingsSheet) settingsSheet->showPage (3); } }
            else message ("TONE3000 opened in your browser. Pick a tone and it will download here.");
        });
    };
    fullBtn.onClick = [this] { toggleFullscreen(); };
    transposeBtn.setColour (TextButton::buttonColourId, theme::raised);
    transposeBtn.setTooltip ("Transpose: tune your guitar down or up by semitones and cents");
    transposeBtn.onClick = [this]
    {
        CallOutBox::launchAsynchronously (std::make_unique<TransposePanel> (proc, [this] { updateTransposeText(); }),
                                          transposeBtn.getScreenBounds(), nullptr);
    };
    tunerBtn.onClick = [this] { openTuner(); };
    // spectrum analyzer: off by default, remembers whether you left it open
    addAndMakeVisible (analyzerBtn);
    addChildComponent (analyzer);
    analyzer.sampleRate = [this] { return (float) proc.getSampleRateNow(); };
    analyzer.selectedBlockId = [this] { auto b = proc.engine.get (grid.selected.y, grid.selected.x); return b != nullptr ? b->id : -1; };
    analyzerOpen = proc.settings().getBoolValue ("analyzer", false);
    analyzerShown = analyzerOpen ? 1.0f : 0.0f;
    analyzerBtn.lit = analyzerOpen;
    analyzer.setActive (analyzerOpen);
    analyzerBtn.onClick = [this]
    {
        analyzerOpen = ! analyzerOpen;
        analyzerBtn.lit = analyzerOpen; analyzerBtn.repaint();
        analyzer.setActive (analyzerOpen);
        proc.settings().setValue ("analyzer", analyzerOpen);
    };
    liveTuner = std::make_unique<LiveTuner> (proc);
    liveTuner->onClick = [this] { openTuner(); };
    addChildComponent (*liveTuner);
    liveTuner->setActive (proc.settings().getBoolValue ("liveTuner", false));
    settingsBtn.onClick = [this] { openSettings(); };

    library.onLoadFile = [this] (const File& f) { loadFromLibrary (f); };
    library.onAddEffect = [this] (Block::Type t, int v) { addEffectFromLibrary (t, v); };
    library.onMessage = [this] (const String& s, bool w) { message (s, w); };

    grid.onSelect = [this] (Point<int> c) { selectCell (c); };
    grid.onMessage = [this] (const String& s, bool w) { message (s, w); };
    grid.onChanged = [this] { refreshAll(); };
    grid.onMergeClicked = [this] (int row, Rectangle<int> node)
    {
        CallOutBox::launchAsynchronously (std::make_unique<MergePanel> (proc, row), grid.localAreaToGlobal (node), nullptr);
    };

    addAndMakeVisible (sceneBar);
    sceneBar.onChanged = [this] { grid.repaint(); if (drawerOpen) drawer.sync(); };
    sceneBar.onMessage = [this] (const String& s, bool w) { message (s, w); };
    sceneBar.onSceneToNewRig = [this] (int scene)
    {
        auto go = [this, scene]
        {
            const auto name = proc.sceneToNewRig (scene);
            closeDrawer();
            refreshAll();
            message ("New rig from that scene, as scene A. Name it to save it (Cmd+Z goes back).");
            saveAsWithName (name);
        };
        if (proc.dirty && proc.currentPreset.isNotEmpty())
            AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Unsaved changes",
                "\"" + proc.currentPreset + "\" has changes you haven't saved. Make the new rig anyway? (Cmd+Z brings them back.)",
                "Make new rig", "Cancel", this, ModalCallbackFunction::create ([go] (int ok) { if (ok != 0) go(); }));
        else
            go();
    };

    drawer.onClose = [this] { closeDrawer(); };
    drawerHeight = proc.settings().getIntValue ("drawerH", 340);
    drawer.onResizeDrag = [this] (int dy, bool finished)
    {
        if (drawerLive < 0) drawerDragStart = drawer.getHeight();
        const int h = drawerDragStart - dy;
        if (! finished) { drawerLive = h; resized(); return; }
        drawerLive = -1;
        if (h < 150) { closeDrawer(); return; }   // pulled down far enough: it closes
        drawerHeight = h;
        proc.settings().setValue ("drawerH", drawerHeight);
        resized();
    };
    drawer.onChanged = [this] { grid.repaint(); };
    drawer.onMessage = [this] (const String& s, bool w) { message (s, w); };

    proc.onNotice = [this] (const String& s) { message (s); };
    proc.onRigChanged = [this] { closeDrawer(); refreshAll(); };
    proc.library.onChanged = [this] { library.refresh(); grid.repaint(); };
    proc.library.onNotice = [this] (const String& s) { message (s); };
    proc.tone3000->onMessage = [this] (const String& s, bool w) { message (s, w); };
    proc.tone3000->onDownloaded = [this] { library.refresh(); };

   #if JucePlugin_Build_Standalone
    if (auto* h = StandalonePluginHolder::getInstance())
        h->getMuteInputValue().setValue (false);   // a guitar rig needs its input
   #endif

    setResizable (true, true);
    setResizeLimits (1000, 640, 4000, 3000);
    setSize (proc.settings().getIntValue ("winW", 1440), proc.settings().getIntValue ("winH", 900));
    setWantsKeyboardFocus (true);
    refreshAll();
    startTimerHz (30);
}

NAMHomeEditor::~NAMHomeEditor()
{
    if (keyTop != nullptr) keyTop->removeKeyListener (&keyRelay);
    proc.settings().setValue ("winW", getWidth());
    proc.settings().setValue ("winH", getHeight());
    proc.onNotice = nullptr; proc.onRigChanged = nullptr;
    proc.library.onChanged = nullptr; proc.library.onNotice = nullptr;
    if (proc.tone3000 != nullptr) { proc.tone3000->onMessage = nullptr; proc.tone3000->onDownloaded = nullptr; }
    setLookAndFeel (nullptr);
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void NAMHomeEditor::applyDisplaySettings()
{
    theme::fontScale() = (float) proc.settings().getDoubleValue ("fontSize", 100.0) / 100.0f;
    if (! proc.isPlugin())   // inside Logic a global zoom would resize every plugin window
        Desktop::getInstance().setGlobalScaleFactor ((float) proc.settings().getDoubleValue ("zoom", 100.0) / 100.0f);
    library.refresh();
    resized();
    repaint();
}

void NAMHomeEditor::toggleFullscreen()
{
    if (auto* w = findParentComponentOfClass<ResizableWindow>())
        w->setFullScreen (! w->isFullScreen());
}

String NAMHomeEditor::presetTitle() const
{
    return (proc.currentPreset.isEmpty() ? String ("Untitled rig") : proc.currentPreset) + (proc.dirty ? "  \xe2\x80\xa2" : "");
}

void NAMHomeEditor::paint (Graphics& g)
{
    g.fillAll (theme::bg);
    auto bar = topBar.toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bar, 14.0f);
    drawLogo (g, Rectangle<float> (bar.getX() + 58, bar.getCentreY() - 15, 30, 30));
    g.setColour (theme::text);
    g.setFont (theme::font (16.5f, 2));
    g.drawText ("NAM Home", (int) bar.getX() + 96, (int) bar.getY(), 110, (int) bar.getHeight(), Justification::centredLeft);

    g.setColour (theme::faint);
    g.setFont (theme::font (11.0f, 2));
    g.drawText ("IN", inMeter.getX() - 26, inMeter.getY() - 4, 22, inMeter.getHeight() + 8, Justification::centredRight);
    g.drawText ("OUT", outMeter.getX() - 34, outMeter.getY() - 4, 30, outMeter.getHeight() + 8, Justification::centredRight);
    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f, 1));
    const auto status = "CPU " + String (roundToInt (cpu * 100.0)) + "%   " + String (proc.getSampleRateNow() / 1000.0, 1) + " kHz";
    g.drawText (status, inMeter.getX() - 200, (int) bar.getY(), 168, (int) bar.getHeight(), Justification::centredRight);

    if (proc.engine.isLearning())
    {
        g.setColour (theme::amber);
        g.setFont (theme::font (12.5f, 2));
        g.drawText ("MIDI LEARN: move a control...", grid.getX() + 12, grid.getY() + 4, 300, 20, Justification::centredLeft);
    }
}

void NAMHomeEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    topBar = r.removeFromTop (58);
    auto t = topBar.reduced (10, 11);
    libraryBtn.setBounds (t.removeFromLeft (36));
    t.removeFromLeft (196);
    prevBtn.setBounds (t.removeFromLeft (30));
    presetBtn.setBounds (t.removeFromLeft (jmin (300, jmax (180, t.getWidth() / 5))));
    nextBtn.setBounds (t.removeFromLeft (30));
    t.removeFromLeft (4);
    saveBtn.setBounds (t.removeFromLeft (36));
    t.removeFromLeft (18);
    tempo.setBounds (t.removeFromLeft (92));
    t.removeFromLeft (6);
    tapBtn.setBounds (t.removeFromLeft (54));
    t.removeFromLeft (4);
    metroBtn.setBounds (t.removeFromLeft (36));
    t.removeFromLeft (8);
    transposeBtn.setBounds (t.removeFromLeft (124));

    settingsBtn.setBounds (t.removeFromRight (36));
    t.removeFromRight (4);
    fullBtn.setVisible (! proc.isPlugin());   // Logic owns the plugin window
    if (! proc.isPlugin()) { fullBtn.setBounds (t.removeFromRight (36)); t.removeFromRight (4); }
    tunerBtn.setBounds (t.removeFromRight (36));
    t.removeFromRight (4);
    analyzerBtn.setBounds (t.removeFromRight (36));
    if (liveTuner != nullptr && liveTuner->isVisible()) { t.removeFromRight (8); liveTuner->setBounds (t.removeFromRight (170)); }
    t.removeFromRight (16);
    outMeter.setBounds (t.removeFromRight (90).withSizeKeepingCentre (90, 8));
    t.removeFromRight (40);
    inMeter.setBounds (t.removeFromRight (90).withSizeKeepingCentre (90, 8));
    t.removeFromRight (230);
    browseT3k.setBounds (t.removeFromRight (jmin (170, jmax (0, t.getWidth() - 10))));
    browseT3k.setVisible (browseT3k.getWidth() > 120);

    r.removeFromTop (12);
    if (libraryOpen)
    {
        library.setBounds (r.removeFromLeft (jlimit (260, 340, getWidth() / 5)));
        r.removeFromLeft (12);
    }
    if (drawerOpen)
    {
        const int maxH = (int) (getHeight() * 0.75f);
        const int h = drawerLive >= 0 ? jlimit (60, maxH, drawerLive) : jlimit (180, maxH, drawerHeight);
        drawer.setBounds (r.removeFromBottom (h));
        r.removeFromBottom (12);
    }
    sceneBar.setBounds (r.removeFromBottom (42));
    r.removeFromBottom (8);
    {
        // the analyzer slides up from the scene bar
        const int h = roundToInt (jmin (150.0f, r.getHeight() * 0.35f) * analyzerShown);
        analyzer.setVisible (h > 4);
        if (h > 4) { analyzer.setBounds (r.removeFromBottom (h)); r.removeFromBottom (8); }
    }
    grid.setBounds (r);
    toasts.setBounds (getLocalBounds().withTrimmedBottom (drawerOpen ? drawer.getHeight() + 30 : 20));
    if (settingsSheet != nullptr) settingsSheet->setBounds (getLocalBounds());
    if (tunerPanel != nullptr) tunerPanel->setBounds (getLocalBounds());
}

void NAMHomeEditor::openTuner()
{
    tunerPanel = std::make_unique<TunerPanel> (proc);
    tunerPanel->onClose = [this]
    {
        MessageManager::callAsync ([this]
        {
            tunerPanel = nullptr;
            liveTuner->setActive (proc.settings().getBoolValue ("liveTuner", false));
            resized();
        });
    };
    addAndMakeVisible (*tunerPanel);
    tunerPanel->setBounds (getLocalBounds());
}

void NAMHomeEditor::selectCell (Point<int> cell)
{
    auto b = proc.engine.get (cell.y, cell.x);
    if (b != nullptr) openDrawer (b);
    else closeDrawer();
    grid.repaint();
}

void NAMHomeEditor::openDrawer (std::shared_ptr<Block> b)
{
    if (drawer.current() != b) drawer.show (b);
    drawer.setVisible (true);
    if (! drawerOpen) { drawerOpen = true; resized(); }
}

void NAMHomeEditor::closeDrawer()
{
    if (! drawerOpen) return;
    drawerOpen = false;
    drawer.setVisible (false);
    drawer.clear();
    resized();
}

void NAMHomeEditor::updateTransposeText()
{
    const int st = proc.engine.transposeSemis.load();
    const int ct = roundToInt (proc.engine.transposeCents.load());
    String s = "Transpose";
    if (st != 0 || ct != 0)
        s = (st != 0 ? (st > 0 ? "+" : "") + String (st) + " st" : String()) + (st != 0 && ct != 0 ? " " : "") + (ct != 0 ? (ct > 0 ? "+" : "") + String (ct) + "c" : String());
    transposeBtn.setButtonText (s);
    transposeBtn.setToggleState (st != 0 || ct != 0, dontSendNotification);
    presetBtn.setButtonText (presetTitle());
}

void NAMHomeEditor::refreshAll()
{
    updateTransposeText();
    presetBtn.setButtonText (presetTitle());
    metroBtn.lit = proc.engine.metronomeOn.load();
    grid.resized();
    grid.repaint();
    tempo.repaint();
    // the drawer may show a block that was removed or replaced
    if (drawerOpen)
    {
        const auto cur = drawer.current();
        auto sel = proc.engine.get (grid.selected.y, grid.selected.x);
        if (sel == nullptr) closeDrawer();
        else if (sel != cur) drawer.show (sel);
    }
    repaint();
}

void NAMHomeEditor::timerCallback()
{
    inMeter.setLevel (proc.engine.inputPeak.load());
    outMeter.setLevel (jmax (proc.engine.outputPeakL.load(), proc.engine.outputPeakR.load()));
    cpu = cpu * 0.9 + proc.engine.getCpuLoad() * 0.1;
    if (drawerOpen) drawer.sync();
    {
        const float target = analyzerOpen ? 1.0f : 0.0f;
        if (std::abs (analyzerShown - target) > 0.001f)
        {
            analyzerShown += (target - analyzerShown) * 0.35f;
            if (std::abs (analyzerShown - target) < 0.02f) analyzerShown = target;
            resized();
        }
    }
    static int tick = 0;
    if (++tick % 10 == 0)
    {
        const auto title = presetTitle();
        if (presetBtn.getButtonText() != title) presetBtn.setButtonText (title);
        tempo.repaint();
        repaint (topBar);
        // undo history: take a snapshot when you've let go of the mouse
        if (! ModifierKeys::currentModifiers.isAnyMouseButtonDown()) proc.snapshotIfChanged();
        if (proc.recoveredRig) { proc.recoveredRig = false; message ("NAM Home closed unexpectedly last time, so your last rig was restored."); }
    }
    static bool wasLearning = false;
    const bool learning = proc.engine.isLearning();
    if (learning != wasLearning)
    {
        if (wasLearning && ! learning) message ("MIDI learned.");
        wasLearning = learning;
        repaint (grid.getBounds().withHeight (30));
    }
}

void NAMHomeEditor::parentHierarchyChanged()
{
    auto* top = getTopLevelComponent();
    if (top == keyTop.getComponent()) return;
    if (keyTop != nullptr) keyTop->removeKeyListener (&keyRelay);
    keyTop = top;
    if (top != nullptr && top != this) top->addKeyListener (&keyRelay);
}

bool NAMHomeEditor::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey)
    {
        if (tunerPanel != nullptr) { tunerPanel->setVisible (false); MessageManager::callAsync ([this] { tunerPanel = nullptr; liveTuner->setActive (proc.settings().getBoolValue ("liveTuner", false)); resized(); }); return true; }
        if (settingsSheet != nullptr) { settingsSheet = nullptr; return true; }
        if (proc.engine.isLearning()) { proc.engine.cancelLearn(); return true; }
        closeDrawer(); return true;
    }
    if (k == KeyPress ('s', ModifierKeys::commandModifier, 0)) { saveBtn.triggerClick(); return true; }
    if (k == KeyPress ('z', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0)
        || k == KeyPress ('y', ModifierKeys::commandModifier, 0))
    {
        if (proc.redo()) { closeDrawer(); refreshAll(); message ("Redo"); }
        return true;
    }
    if (k == KeyPress ('z', ModifierKeys::commandModifier, 0))
    {
        if (proc.undo()) { closeDrawer(); refreshAll(); message ("Undo"); }
        else message ("Nothing to undo.");
        return true;
    }
    if (k == KeyPress::leftKey && k.getModifiers().isCommandDown()) { if (proc.stepSetlist (-1)) refreshAll(); return true; }
    if (k == KeyPress::rightKey && k.getModifiers().isCommandDown()) { if (proc.stepSetlist (+1)) refreshAll(); return true; }
    if (k == KeyPress ('f', ModifierKeys::commandModifier | ModifierKeys::ctrlModifier, 0)) { toggleFullscreen(); return true; }
    auto sel = proc.engine.get (grid.selected.y, grid.selected.x);
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && sel != nullptr) { grid.removeAt (grid.selected); return true; }
    if (k == KeyPress ('b') && sel != nullptr) { sel->enabled = ! sel->enabled.load(); proc.dirty = true; refreshAll(); return true; }
    if (k == KeyPress ('t')) { tapBtn.triggerClick(); return true; }
    if (k == KeyPress ('u')) { openTuner(); return true; }
    return false;
}

//==============================================================================
void NAMHomeEditor::loadFromLibrary (const File& f)
{
    // into the selected cell if it fits, else the first matching block in the main row, else the first empty slot
    auto fits = [&f] (Block* b)
    {
        if (b == nullptr) return false;
        if (f.hasFileExtension ("wav")) return b->getType() == Block::Type::Cab;
        return b->getType() == Block::Type::NamAmp || b->getType() == Block::Type::NamPedal;
    };
    Point<int> target { -1, -1 };
    if (auto s = proc.engine.get (grid.selected.y, grid.selected.x); fits (s.get()) || s == nullptr)
        target = grid.selected;
    if (target.x < 0)
        for (int c = 0; c < Engine::cols && target.x < 0; ++c)
            if (fits (proc.engine.get (0, c).get())) target = { c, 0 };
    if (target.x < 0)
        for (int c = 0; c < Engine::cols && target.x < 0; ++c)
            if (proc.engine.get (0, c) == nullptr) target = { c, 0 };
    if (target.x < 0) { message ("No free slot in the main row.", true); return; }
    const auto err = proc.loadFileIntoBlock (f, target.y, target.x);
    if (err.isNotEmpty()) message (err, true);
    grid.selected = target;
    selectCell (target);
    refreshAll();
}

void NAMHomeEditor::addEffectFromLibrary (Block::Type t, int variant)
{
    Point<int> target { -1, -1 };
    if (proc.engine.get (grid.selected.y, grid.selected.x) == nullptr) target = grid.selected;
    for (int c = 0; c < Engine::cols && target.x < 0; ++c)
        if (proc.engine.get (0, c) == nullptr) target = { c, 0 };
    if (target.x < 0) { message ("No free slot in the main row.", true); return; }
    grid.addBlockAt (t, target, variant);
}

//==============================================================================
void NAMHomeEditor::showPresetMenu()
{
    PopupMenu m;
    const auto names = proc.presets.list();
    m.addSectionHeader ("Presets");
    for (int i = 0; i < names.size(); ++i)
        m.addItem (100 + i, names[i], true, names[i] == proc.currentPreset);
    if (names.isEmpty()) m.addItem (-1, "No saved presets yet", false);
    m.addSeparator();
    m.addItem (1, "Save", true, false);
    m.addItem (2, "Save as...");
    m.addItem (3, "Rename...", proc.currentPreset.isNotEmpty());
    m.addItem (4, "Delete", proc.currentPreset.isNotEmpty());
    m.addSeparator();
    m.addItem (5, "New rig");
    m.addItem (6, "Show presets folder");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (presetBtn).withMinimumWidth (presetBtn.getWidth()), [this, names] (int r)
    {
        if (r >= 100) { proc.loadPreset (names[r - 100]); message ("Loaded \"" + names[r - 100] + "\""); }
        else if (r == 1) saveBtn.triggerClick();
        else if (r == 2) saveAs (false);
        else if (r == 3) saveAs (true);
        else if (r == 4)
        {
            const auto name = proc.currentPreset;
            AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Delete preset", "Move \"" + name + "\" to the Trash?", "Delete", "Cancel", this,
                                          ModalCallbackFunction::create ([this, name] (int ok)
            {
                if (ok == 0) return;
                proc.presets.remove (name);
                proc.currentPreset = {};
                proc.dirty = true;
                refreshAll();
                message ("Deleted \"" + name + "\"");
            }));
        }
        else if (r == 5) proc.newRig();
        else if (r == 6) proc.presets.getFolder().revealToUser();
        refreshAll();
    });
}

void NAMHomeEditor::saveAs (bool rename) { saveAsImpl (rename, {}); }
void NAMHomeEditor::saveAsWithName (const String& suggested) { saveAsImpl (false, suggested); }

void NAMHomeEditor::saveAsImpl (bool rename, const String& suggested)
{
    auto* aw = new AlertWindow (rename ? "Rename preset" : "Save preset", "Name", MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", suggested.isNotEmpty() ? suggested : rename ? proc.currentPreset : (proc.currentPreset.isEmpty() ? String ("My rig") : proc.currentPreset + " copy"));
    aw->addButton (rename ? "Rename" : "Save", 1, KeyPress (KeyPress::returnKey));
    aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    aw->enterModalState (true, ModalCallbackFunction::create ([this, aw, rename] (int r)
    {
        if (r != 1) return;
        const auto name = PresetManager::cleanName (aw->getTextEditorContents ("name"));
        if (name.isEmpty()) return;
        if (rename)
        {
            if (proc.presets.rename (proc.currentPreset, name)) { proc.currentPreset = name; message ("Renamed to \"" + name + "\""); }
            else message ("A preset with that name already exists.", true);
        }
        else { proc.savePreset (name); message ("Saved \"" + name + "\""); }
        refreshAll();
    }), true);
}

void NAMHomeEditor::stepPreset (int delta)
{
    const auto names = proc.presets.list();
    if (names.isEmpty()) { message ("Save a preset first."); return; }
    int i = names.indexOf (proc.currentPreset);
    i = (i < 0) ? (delta > 0 ? 0 : names.size() - 1) : (i + delta + names.size()) % names.size();
    proc.loadPreset (names[i]);
    refreshAll();
}

void NAMHomeEditor::openSettings()
{
    AudioDeviceManager* dm = nullptr;
   #if JucePlugin_Build_Standalone
    if (auto* h = StandalonePluginHolder::getInstance()) dm = &h->deviceManager;
   #endif
    settingsSheet = std::make_unique<SettingsSheet> (proc, dm);
    settingsSheet->onClose = [this] { MessageManager::callAsync ([this] { settingsSheet = nullptr; }); };
    settingsSheet->onDisplayChanged = [this] { applyDisplaySettings(); };
    settingsSheet->onToggleFullscreen = [this] { toggleFullscreen(); };
    addAndMakeVisible (*settingsSheet);
    settingsSheet->setBounds (getLocalBounds());
}
