#include "Panels.h"
#include "../Measure.h"
#include "../PluginProcessor.h"

using namespace juce;

static const char* chipNames[] = { "All", "Adjustable amps", "NAM captures", "Pedals", "Cabs / IRs", "Effects", "Synthesizers", "Saxophone" };

//==============================================================================
LibraryPanel::LibraryPanel (NAMHomeProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty ("Search captures, IRs and effects", theme::faint);
    search.setFont (theme::font (13.5f));
    search.setIndents (10, 8);
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);

    for (int i = 0; i < 8; ++i)
    {
        auto* b = chips.add (new TextButton (chipNames[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (101);
        b->setToggleState (i == 0, dontSendNotification);
        b->setColour (TextButton::buttonOnColourId, theme::text);
        b->setColour (TextButton::textColourOnId, theme::bg);
        b->onClick = [this, i] { filter = i; refresh(); };
        addAndMakeVisible (b);
    }

    list.setModel (this);
    list.setRowHeight (46);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);

    importBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Import captures and IRs", File(), "*.nam;*.wav;*.zip");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc)
                              {
                                  const int n = proc.library.importFiles (fc.getResults());
                                  if (onMessage) onMessage (n > 0 ? String (n) + " added to your library" : "Nothing new to add", false);
                              });
    };
    folderBtn.onClick = [this] { proc.library.getFolder().revealToUser(); };
    addAndMakeVisible (importBtn);
    addAndMakeVisible (folderBtn);
    refresh();
}

void LibraryPanel::refresh()
{
    shown.clear();
    const auto q = search.getText().trim();
    auto matches = [&q] (const String& s) { return q.isEmpty() || s.containsIgnoreCase (q); };

    auto addSection = [this] (const String& label) { Entry e; e.header = true; e.label = label; shown.push_back (e); };
    auto addItems = [&] (const String& label, std::function<bool (const LibraryItem&)> pred)
    {
        std::vector<Entry> tmp;
        for (auto& it : proc.library.getItems())
            if (pred (it) && matches (it.name + " " + it.creator + " " + it.gearMake + " " + it.gearModel + " " + it.micTag + " " + it.pack))
            {
                Entry e; e.item = &it; tmp.push_back (e);
            }
        if (tmp.empty()) return;
        addSection (label + "  ·  " + String ((int) tmp.size()));
        shown.insert (shown.end(), tmp.begin(), tmp.end());
    };

    // captures: amp folder > gain-stage folder > the captures themselves (like Neural DSP's menus)
    const bool searching = q.isNotEmpty();
    auto addCaptureTree = [&] (const String& label, std::function<bool (const LibraryItem&)> pred)
    {
        std::map<String, std::map<int, std::vector<const LibraryItem*>>> tree;
        int total = 0;
        for (auto& it : proc.library.getItems())
            if (pred (it) && matches (it.name + " " + it.creator + " " + it.gearMake + " " + it.gearModel + " " + it.pack + " " + it.ampGroup()))
            {
                tree[it.ampGroup()][measure::gainStage (it.gain)].push_back (&it);
                ++total;
            }
        if (total == 0) return;
        addSection (label + "  ·  " + String (total));
        for (auto& [group, stages] : tree)
        {
            int n = 0; for (auto& [st, v] : stages) n += (int) v.size();
            Entry f; f.folder = true; f.depth = 0; f.key = label + "/" + group; f.label = group; f.count = n;
            shown.push_back (f);
            if (! searching && openFolders.count (f.key) == 0) continue;
            // the model's short name for the stage folders: "SLO-100 Clean"
            auto words = StringArray::fromTokens (group, " ", {});
            const String model = words.size() > 1 ? words.joinIntoString (" ", 1) : group;
            for (auto& [st, v] : stages)
            {
                Entry sf; sf.folder = true; sf.depth = 1; sf.key = f.key + "/" + String (st); sf.count = (int) v.size();
                sf.label = st < 0 ? model + "  (measuring...)" : model + " " + measure::gainStageName (st);
                sf.swatch = st < 0 ? theme::faint : measure::gainColour ((float) st * 0.2f + 0.1f);
                shown.push_back (sf);
                if (! searching && openFolders.count (sf.key) == 0) continue;
                for (auto* it : v) { Entry e; e.item = it; e.depth = 2; shown.push_back (e); }
            }
        }
    };

    const bool all = filter == 0;
    if (all || filter == 1) addItems ("Adjustable amps", [] (const LibraryItem& i) { return i.kind == LibraryItem::Kind::Amp && i.adjustable; });
    if (all || filter == 2) addCaptureTree ("NAM amp captures", [] (const LibraryItem& i) { return i.kind == LibraryItem::Kind::Amp && ! i.adjustable; });
    if (all || filter == 2 || filter == 3) addCaptureTree ("NAM pedal captures", [] (const LibraryItem& i) { return i.kind == LibraryItem::Kind::Pedal; });
    if (all || filter == 4) addItems ("Cabinet IRs", [] (const LibraryItem& i) { return i.kind == LibraryItem::Kind::IR; });
    auto addBuiltIns = [&] (const String& label, std::function<bool (Block::Type)> pred)
    {
        // every model gets its own entry ("Green OD", "Tape delay", "Hall reverb"...)
        static const std::map<Block::Type, String> suffix {
            { Block::Type::Drive, "" }, { Block::Type::Fuzz, "" }, { Block::Type::Delay, " delay" }, { Block::Type::Reverb, " reverb" },
            { Block::Type::Chorus, " chorus" }, { Block::Type::Phaser, " phaser" }, { Block::Type::Flanger, " flanger" },
            { Block::Type::Compressor, " compressor" }, { Block::Type::Tremolo, " tremolo" } };
        std::vector<Entry> fx;
        for (auto t : Block::allTypes())
        {
            if (t == Block::Type::NamAmp || t == Block::Type::NamPedal || t == Block::Type::Cab || ! pred (t)) continue;
            auto sfx = suffix.find (t);
            if (sfx != suffix.end())
            {
                auto tmp = Block::create (t);
                if (auto* vp = tmp->variantParam())
                {
                    for (int v = 0; v < vp->choices.size(); ++v)
                    {
                        String name = vp->choices[v] + sfx->second;
                        if (t == Block::Type::Compressor && v == 0) name = "Studio compressor";
                        if (! matches (name + " " + Block::typeName (t) + " " + Block::categoryName (t))) continue;
                        Entry e; e.type = t; e.variant = v; e.label = name; e.swatch = Block::colourFor (t, v); fx.push_back (e);
                    }
                    continue;
                }
            }
            if (! matches (Block::typeName (t) + " " + Block::categoryName (t))) continue;
            Entry e; e.type = t; e.label = Block::typeName (t); e.swatch = Block::colourFor (t, -1); fx.push_back (e);
        }
        if (fx.empty()) return;
        addSection (label);
        shown.insert (shown.end(), fx.begin(), fx.end());
    };
    if (filter == 3) addBuiltIns ("Built-in pedals", [] (Block::Type t) { return Block::categoryName (t) == "Pedals"; });
    if (all || filter == 6) addBuiltIns ("Synthesizers", [] (Block::Type t) { return Block::categoryName (t) == "Synthesizers"; });
    if (all || filter == 7)
    {
        // each horn is its own entry; Soprano first
        std::vector<Entry> sx;
        for (int v = 0; v < 4; ++v)
        {
            const juce::String name = juce::String (ndsp::saxModel (v).name) + " sax";
            if (! matches (name + " saxophone")) continue;
            Entry e; e.type = Block::Type::Sax; e.variant = v; e.label = name; sx.push_back (e);
        }
        if (! sx.empty()) { addSection ("Saxophone"); shown.insert (shown.end(), sx.begin(), sx.end()); }
    }
    if (all || filter == 5)
        for (auto* grp : { "Drive", "Dynamics & EQ", "Wah, filters & talk box", "Pitch", "Modulation", "Delay", "Reverb & ambient", "Guitar sims" })
            addBuiltIns (grp, [grp] (Block::Type t) { return Block::libraryGroup (t) == grp; });
    if (all || filter == 5) addBuiltIns ("Looper & drums", [] (Block::Type t) { return Block::categoryName (t) == "Looper & drums"; });
    list.updateContent();
    list.repaint();
    repaint();
}

void LibraryPanel::paint (Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (16.0f, 2));
    g.drawText ("Library", 16, 12, 200, 24, Justification::centredLeft);
    g.setColour (theme::faint);
    g.setFont (theme::font (12.0f));
    g.drawText (String ((int) proc.library.getItems().size()) + " files", getWidth() - 116, 12, 100, 24, Justification::centredRight);
    if (shown.empty())
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (13.5f));
        const auto msg = filter == 1 ? String ("No adjustable amp models yet. Support for adjustable formats is coming next.")
                                     : String ("Add .nam captures and .wav IRs: download them (they're picked up automatically), drop them on the window, use Import files, or Browse TONE3000.");
        g.drawFittedText (msg, list.getBounds().reduced (14, 8), Justification::centredTop, 5);
    }
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (34);
    search.setBounds (r.removeFromTop (34));
    r.removeFromTop (10);
    int x = r.getX(), y = r.getY();
    for (auto* b : chips)
    {
        const int w = (int) GlyphArrangement::getStringWidth (theme::font (13.0f, 1), b->getButtonText()) + 22;
        if (x + w > r.getRight()) { x = r.getX(); y += 32; }
        b->setBounds (x, y, w, 26);
        x += w + 6;
    }
    r.setTop (y + 36);
    auto bottom = r.removeFromBottom (34);
    importBtn.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 4));
    bottom.removeFromLeft (8);
    folderBtn.setBounds (bottom);
    r.removeFromBottom (8);
    list.setBounds (r);
}

void LibraryPanel::listBoxItemClicked (int row, const MouseEvent&)
{
    if (row < 0 || row >= (int) shown.size() || ! shown[(size_t) row].folder) return;
    const auto key = shown[(size_t) row].key;
    if (openFolders.count (key)) openFolders.erase (key); else openFolders.insert (key);
    refresh();
}

void LibraryPanel::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = shown[(size_t) row];
    if (e.header)
    {
        g.setColour (theme::faint);
        g.setFont (theme::font (11.5f, 2));
        g.drawText (e.label.toUpperCase(), 6, h - 22, w - 12, 18, Justification::centredLeft);
        return;
    }
    if (selected)
    {
        g.setColour (theme::hover);
        g.fillRoundedRectangle (2, 2, (float) w - 4, (float) h - 4, 8.0f);
    }
    const int ind = e.depth * 16;
    if (e.folder)
    {
        const bool open = openFolders.count (e.key) > 0 || search.getText().trim().isNotEmpty();
        Path chev;
        const float cx = 18.0f + (float) ind, cy = (float) h * 0.5f;
        if (open) { chev.startNewSubPath (cx - 4, cy - 2); chev.lineTo (cx, cy + 2); chev.lineTo (cx + 4, cy - 2); }
        else      { chev.startNewSubPath (cx - 2, cy - 4); chev.lineTo (cx + 2, cy); chev.lineTo (cx - 2, cy + 4); }
        g.setColour (theme::muted);
        g.strokePath (chev, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        int x = 30 + ind;
        if (e.depth == 1) { g.setColour (e.swatch); g.fillRoundedRectangle ((float) x, cy - 5, 10, 10, 3.0f); x += 16; }
        g.setColour (theme::text);
        g.setFont (theme::font (e.depth == 0 ? 14.0f : 13.0f, e.depth == 0 ? 2 : 1));
        g.drawText (e.label, x, 0, w - x - 44, h, Justification::centredLeft);
        g.setColour (theme::faint);
        g.setFont (theme::font (11.5f));
        g.drawText (String (e.count), w - 44, 0, 34, h, Justification::centredRight);
        return;
    }
    if (e.item != nullptr)
    {
        const auto& it = *e.item;
        const auto c = it.kind == LibraryItem::Kind::IR ? measure::irColour (it.bright) : measure::gainColour (it.gain);
        g.setColour (c);
        g.fillRoundedRectangle (12.0f + (float) ind, (float) h * 0.5f - 6, 12, 12, 3.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (13.5f, 1));
        g.drawText (it.name, 34 + ind, 5, w - 96 - ind, 19, Justification::centredLeft);
        auto sub = it.creditLine();
        if (it.micTag.isNotEmpty()) sub << "  ·  " << it.micTag;
        g.setColour (theme::muted);
        g.setFont (theme::font (12.0f));
        g.drawText (sub, 34 + ind, 24, w - 96 - ind, 17, Justification::centredLeft);
        if (it.isNew)
        {
            g.setColour (Colour (0xff0f2e2a));
            g.fillRoundedRectangle ((float) w - 54, (float) h * 0.5f - 10, 42, 20, 6.0f);
            g.setColour (Colour (0xff7fded3));
            g.setFont (theme::font (11.5f, 2));
            g.drawText ("New", w - 54, h / 2 - 10, 42, 20, Justification::centred);
        }
    }
    else
    {
        g.setColour (e.swatch.isTransparent() ? Block::colour (e.type) : e.swatch);
        g.fillRoundedRectangle (12, (float) h * 0.5f - 6, 12, 12, 3.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (13.5f, 1));
        g.drawText (e.label, 34, 5, w - 60, 19, Justification::centredLeft);
        g.setColour (theme::muted);
        g.setFont (theme::font (12.0f));
        const auto sub = Block::categoryName (e.type) == "Synthesizers" ? String ("Guitar-to-synth, played by your guitar")
                       : Block::categoryName (e.type) == "Saxophone" ? String ("Played by your guitar, every control MIDI-ready")
                                                                        : "Built-in " + Block::categoryName (e.type).toLowerCase().trimCharactersAtEnd ("s") + ", every control adjustable";
        g.drawText (sub, 34, 24, w - 60, 17, Justification::centredLeft);
    }
}

void LibraryPanel::listBoxItemDoubleClicked (int row, const MouseEvent&)
{
    if (row < 0 || row >= (int) shown.size() || shown[(size_t) row].header || shown[(size_t) row].folder) return;
    const auto& e = shown[(size_t) row];
    if (e.item != nullptr) { if (onLoadFile) onLoadFile (e.item->file); }
    else if (onAddEffect) onAddEffect (e.type, e.variant);
}

var LibraryPanel::getDragSourceDescription (const SparseSet<int>& rows)
{
    if (rows.size() == 0) return {};
    const int r = rows[0];
    if (r < 0 || r >= (int) shown.size() || shown[(size_t) r].header || shown[(size_t) r].folder) return {};
    const auto& e = shown[(size_t) r];
    if (e.item != nullptr) return var ("file:" + e.item->file.getFullPathName());
    return var ("type:" + String ((int) e.type) + (e.variant >= 0 ? ":" + String (e.variant) : String()));
}

//==============================================================================
static Label* styleLabel (Label& l, const String& text, float size = 13.0f, Colour c = theme::muted)
{
    l.setText (text, dontSendNotification);
    l.setFont (theme::font (size));
    l.setColour (Label::textColourId, c);
    l.setJustificationType (Justification::topLeft);
    l.setMinimumHorizontalScale (1.0f);
    return &l;
}

SettingsSheet::SettingsSheet (NAMHomeProcessor& p, AudioDeviceManager* dm) : proc (p), deviceManager (dm)
{
    const char* pages[] = { "Audio", "MIDI", "Library", "TONE3000", "Display", "Output", "Setlists", "Live" };
    for (int i = 0; i < 8; ++i)
    {
        auto* b = nav.add (new TextButton (pages[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (202);
        b->setColour (TextButton::buttonColourId, Colours::transparentBlack);
        b->setColour (TextButton::buttonOnColourId, theme::hover);
        b->setColour (TextButton::textColourOnId, theme::text);
        b->onClick = [this, i] { showPage (i); };
        addAndMakeVisible (b);
    }
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);

    // ---- Audio
    if (deviceManager != nullptr)
    {
        selector = std::make_unique<AudioDeviceSelectorComponent> (*deviceManager, 1, 2, 1, 2, false, false, true, false);
        audioPage.addAndMakeVisible (*selector);
    }
    auto& e = proc.engine;
    inGain = std::make_unique<ValueKnob> ("Input", -24.0f, 24.0f, 0.0f, "dB", [&e] { return e.inputGainDb.load(); }, [this, &e] (float v) { e.inputGainDb = v; proc.dirty = true; }, theme::accent);
    outGain = std::make_unique<ValueKnob> ("Output", -40.0f, 12.0f, 0.0f, "dB", [&e] { return e.outputGainDb.load(); }, [this, &e] (float v) { e.outputGainDb = v; proc.dirty = true; }, theme::accent);
    metroLevel = std::make_unique<ValueKnob> ("Click level", -40.0f, 0.0f, -14.0f, "dB", [&e] { return e.metronomeLevelDb.load(); }, [&e] (float v) { e.metronomeLevelDb = v; }, theme::accent);
    monoOut.setToggleState (e.monoOut.load(), dontSendNotification);
    monoOut.onClick = [this] { proc.engine.monoOut = monoOut.getToggleState(); };
    for (int b = 2; b <= 7; ++b) beatsBox.addItem (String (b) + " beats per bar", b);
    beatsBox.setSelectedId (e.beatsPerBar.load(), dontSendNotification);
    beatsBox.onChange = [this] { proc.engine.beatsPerBar = beatsBox.getSelectedId(); };
    for (auto* c : std::initializer_list<Component*> { inGain.get(), outGain.get(), metroLevel.get(), &monoOut, &beatsBox })
        audioPage.addAndMakeVisible (c);

    // ---- MIDI
    midiPage.addAndMakeVisible (styleLabel (midiInfo,
        "MIDI devices are switched on automatically when they connect.\n"
        "To control something, right-click any knob, switch, choice or block and choose MIDI learn, then move your pedal or press a footswitch. "
        "CC, Note and Program Change messages all work; footswitches are detected as momentary or latching automatically.\n"
        "Program changes you haven't learned to a control switch presets in alphabetical order (program 1 = first preset).\n\n"
        "BOSS EV-1-WL: set the pedal to send one CC with the toe switch on and another with it off (in the BOSS app). "
        "Learn the wah position to the first, the volume position to the second, and learn the toe switch to the wah's on/off."));
    midiPage.addAndMakeVisible (styleLabel (lastCcLabel, "Last MIDI received: none yet", 13.0f, theme::text));
    bluetoothBtn.onClick = []
    {
        if (BluetoothMidiDevicePairingDialogue::isAvailable()) BluetoothMidiDevicePairingDialogue::open();
    };
    bluetoothBtn.setEnabled (BluetoothMidiDevicePairingDialogue::isAvailable());
    tapLearn.onClick = [this] { proc.engine.armLearn (-1, 0); if (proc.onNotice) proc.onNotice ("Press the footswitch you want to use for tap tempo..."); };
    midiPage.addAndMakeVisible (bluetoothBtn);
    midiPage.addAndMakeVisible (tapLearn);
    buildMidiList();

    // ---- Library
    auto& s = proc.settings();
    watchDl.setToggleState (proc.library.isWatchingDownloads(), dontSendNotification);
    watchDl.onClick = [this] { proc.library.setWatchDownloads (watchDl.getToggleState()); };
    pairIr.setToggleState (s.getBoolValue ("autoPairIr", true), dontSendNotification);
    pairIr.onClick = [this] { proc.settings().setValue ("autoPairIr", pairIr.getToggleState()); };
    bypassCab.setToggleState (s.getBoolValue ("autoBypassCab", true), dontSendNotification);
    bypassCab.onClick = [this] { proc.settings().setValue ("autoBypassCab", bypassCab.getToggleState()); };
    showLib.onClick = [this] { proc.library.getFolder().revealToUser(); };
    rescan.onClick = [this] { proc.library.rescan(); };
    showPresets.onClick = [this] { proc.presets.getFolder().revealToUser(); };
    for (auto* c : std::initializer_list<Component*> { &watchDl, &pairIr, &bypassCab, &showLib, &rescan, &showPresets })
        libraryPage.addAndMakeVisible (c);

    // ---- TONE3000
    t3kPage.addAndMakeVisible (styleLabel (t3kHelp,
        "Browse TONE3000 from the Library panel and the tones you pick download straight into your library.\n\n"
        "One-time setup:\n"
        "1. Open your TONE3000 settings and create an API key.\n"
        "2. Add this redirect address to it:  namhome://tone3000\n"
        "3. Paste the publishable key (client ID) below and press Save key.\n\n"
        "Powered by the TONE3000 API. Captures stay credited to their creators."));
    t3kKey.setTextToShowWhenEmpty ("Publishable key", theme::faint);
    t3kKey.setFont (theme::font (13.5f));
    t3kKey.setIndents (10, 8);
    t3kKey.setText (proc.tone3000->getClientId(), false);
    t3kSave.onClick = [this] { proc.tone3000->setClientId (t3kKey.getText()); if (proc.onNotice) proc.onNotice ("TONE3000 key saved."); timerCallback(); };
    t3kSignOut.onClick = [this] { proc.tone3000->signOut(); timerCallback(); };
    t3kOpen.onClick = [] { URL ("https://www.tone3000.com/settings").launchInDefaultBrowser(); };
    for (auto* c : std::initializer_list<Component*> { &t3kKey, &t3kSave, &t3kSignOut, &t3kOpen, &t3kStatus })
        t3kPage.addAndMakeVisible (c);

    // ---- Display
    auto setupSlider = [] (Slider& sl, double lo, double hi, double v)
    {
        sl.setSliderStyle (Slider::LinearHorizontal);
        sl.setTextBoxStyle (Slider::TextBoxRight, false, 64, 24);
        sl.setRange (lo, hi, 5);
        sl.setValue (v, dontSendNotification);
        sl.setColour (Slider::trackColourId, theme::accent);
        sl.textFromValueFunction = [] (double x) { return String (roundToInt (x)) + "%"; };
        sl.updateText();
    };
    setupSlider (zoom, 75, 150, s.getDoubleValue ("zoom", 100.0));
    setupSlider (fontSize, 85, 130, s.getDoubleValue ("fontSize", 100.0));
    zoom.onDragEnd = [this] { proc.settings().setValue ("zoom", zoom.getValue()); if (onDisplayChanged) onDisplayChanged(); };
    fontSize.onValueChange = [this] { proc.settings().setValue ("fontSize", fontSize.getValue()); if (onDisplayChanged) onDisplayChanged(); };
    displayPage.addAndMakeVisible (styleLabel (zoomLabel, "Zoom (whole window)", 13.5f, theme::text));
    displayPage.addAndMakeVisible (styleLabel (fontLabel, "Text size", 13.5f, theme::text));
    fullscreen.onClick = [this] { if (onToggleFullscreen) onToggleFullscreen(); };
    for (auto* c : std::initializer_list<Component*> { &zoom, &fontSize, &fullscreen })
        displayPage.addAndMakeVisible (c);

    // ---- Output: a final EQ for whatever you're playing through, and spillover
    outputPage.addAndMakeVisible (styleLabel (outHelp,
        "This EQ shapes everything NAM Home sends out, after the whole chain and every preset. "
        "Use it to fit your presets to headphones, studio monitors or a PA without touching the presets themselves."));
    {
        const char* names[] = { "Low cut", "Bass", "Low mid", "Mid", "Presence", "Air", "High cut" };
        const float lo[] = { 20, -12, -12, -12, -12, -12, 3000 }, hi[] = { 300, 12, 12, 12, 12, 12, 20000 }, def[] = { 20, 0, 0, 0, 0, 0, 20000 };
        for (int k = 0; k < 7; ++k)
        {
            auto* kn = eqKnobs.add (new ValueKnob (names[k], lo[k], hi[k], def[k], (k == 0 || k == 6) ? "Hz" : "dB",
                [this, k] { return proc.engine.outEq[k].load(); },
                [this, k] (float v) { proc.setOutputEq (k, v); outProfile.setSelectedId (99, dontSendNotification); },
                Colour (0xff2bff6a)));
            if (k == 0 || k == 6) kn->slider.setSkewFactorFromMidPoint (k == 0 ? 80.0 : 8000.0);
            outputPage.addAndMakeVisible (kn);
        }
    }
    {
        int id = 1;
        for (auto& n : NAMHomeProcessor::outputProfileNames()) outProfile.addItem (n, id++);
        outProfile.addItem ("Custom", 99);
        outProfile.setSelectedId (proc.settings().getIntValue ("outProfile", 1), dontSendNotification);
        outProfile.onChange = [this]
        {
            const int id = outProfile.getSelectedId();
            if (id == 99) return;
            proc.applyOutputProfile (id - 1);
            for (auto* k : eqKnobs) k->sync();
        };
        outputPage.addAndMakeVisible (outProfile);
    }
    spill.setToggleState (proc.engine.spilloverOn.load(), dontSendNotification);
    spill.onClick = [this] { proc.engine.spilloverOn = spill.getToggleState(); proc.settings().setValue ("spillover", spill.getToggleState()); };
    outputPage.addAndMakeVisible (spill);
    cutScene.setToggleState (proc.engine.cutTailsOnScene.load(), dontSendNotification);
    cutScene.onClick = [this] { proc.engine.cutTailsOnScene = cutScene.getToggleState(); proc.settings().setValue ("cutSceneTails", cutScene.getToggleState()); };
    outputPage.addAndMakeVisible (cutScene);

    // ---- Setlists
    struct SongModel : public ListBoxModel
    {
        NAMHomeProcessor& p; explicit SongModel (NAMHomeProcessor& pp) : p (pp) {}
        int getNumRows() override { return p.setlist.size(); }
        void paintListBoxItem (int row, Graphics& g, int w, int h, bool sel) override
        {
            if (sel) { g.setColour (theme::hover); g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4, (float) h - 2, 6.0f); }
            const bool current = p.setlist[row] == p.currentPreset;
            g.setColour (theme::muted); g.setFont (theme::font (12.0f, 1));
            g.drawText (String (row + 1), 8, 0, 26, h, Justification::centredRight);
            g.setColour (current ? theme::accent : theme::text); g.setFont (theme::font (14.0f, current ? 2 : 0));
            g.drawText (p.setlist[row], 44, 0, w - 52, h, Justification::centredLeft);
        }
        void listBoxItemDoubleClicked (int row, const MouseEvent&) override { p.setlistPos = row; p.loadPreset (p.setlist[row]); }
    };
    songModel = std::make_unique<SongModel> (proc);
    songList.setModel (songModel.get());
    songList.setRowHeight (30);
    songList.setColour (ListBox::backgroundColourId, theme::panel);
    setlistPage.addAndMakeVisible (styleLabel (setlistHelp,
        "Put presets in song order. Cmd + Left / Right (or a footswitch learned to \"Next song\" / \"Previous song\" below) steps through them. "
        "Setlists are saved in your library folder, on your SSD."));
    setlistBox.onChange = [this]
    {
        proc.loadSetlist (setlistBox.getSelectedId() <= 1 ? String() : setlistBox.getText());
        songList.updateContent(); songList.repaint();
    };
    newSetlist.onClick = [this]
    {
        auto* aw = new AlertWindow ("New setlist", {}, MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", "", "Name");
        aw->addButton ("Create", 1, KeyPress (KeyPress::returnKey));
        aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        aw->enterModalState (true, ModalCallbackFunction::create ([this, aw] (int res)
        {
            const auto n = File::createLegalFileName (aw->getTextEditorContents ("name").trim());
            if (res != 1 || n.isEmpty()) return;
            proc.setlistName = n; proc.setlist.clear(); proc.saveSetlist();
            proc.loadSetlist (n);
            refreshSetlists();
        }), true);
    };
    auto edit = [this] (std::function<void (int)> f)
    {
        if (proc.setlistName.isEmpty()) { if (proc.onNotice) proc.onNotice ("Make or pick a setlist first."); return; }
        f (songList.getSelectedRow());
        proc.saveSetlist();
        songList.updateContent(); songList.repaint();
    };
    addSong.onClick = [this, edit] { edit ([this] (int) { if (proc.currentPreset.isNotEmpty()) proc.setlist.add (proc.currentPreset); else if (proc.onNotice) proc.onNotice ("Save the current rig as a preset first."); }); };
    removeSong.onClick = [this, edit] { edit ([this] (int r) { if (r >= 0) proc.setlist.remove (r); }); };
    songUp.onClick = [this, edit] { edit ([this] (int r) { if (r > 0) { proc.setlist.move (r, r - 1); songList.selectRow (r - 1); } }); };
    songDown.onClick = [this, edit] { edit ([this] (int r) { if (r >= 0 && r < proc.setlist.size() - 1) { proc.setlist.move (r, r + 1); songList.selectRow (r + 1); } }); };
    loadSong.onClick = [this] { const int r = songList.getSelectedRow(); if (r >= 0) { proc.setlistPos = r; proc.loadPreset (proc.setlist[r]); songList.repaint(); } };
    learnPrev.onClick = [this] { proc.engine.armLearn (Engine::setlistMidiId, 0); if (proc.onNotice) proc.onNotice ("Press the footswitch for the previous song..."); };
    learnNext.onClick = [this] { proc.engine.armLearn (Engine::setlistMidiId, 1); if (proc.onNotice) proc.onNotice ("Press the footswitch for the next song..."); };
    showSetlists.onClick = [this] { proc.setlistFolder().createDirectory(); proc.setlistFolder().revealToUser(); };
    for (auto* c : std::initializer_list<Component*> { &setlistBox, &newSetlist, &learnPrev, &learnNext, &addSong, &removeSong, &songUp, &songDown, &loadSong, &showSetlists, &songList })
        setlistPage.addAndMakeVisible (c);
    refreshSetlists();

    controllerPage = std::make_unique<ControllerPage> (proc);
    addChildComponent (*controllerPage);
    for (auto* pg : std::initializer_list<Component*> { &audioPage, &midiPage, &libraryPage, &t3kPage, &displayPage, &outputPage, &setlistPage })
        addChildComponent (pg);
    showPage (0);
    startTimerHz (5);
}

SettingsSheet::~SettingsSheet() { stopTimer(); songList.setModel (nullptr); }

void SettingsSheet::refreshSetlists()
{
    setlistBox.clear (dontSendNotification);
    setlistBox.addItem ("No setlist (presets in A-Z order)", 1);
    int id = 2, sel = 1;
    for (auto& n : proc.setlistNames()) { if (n == proc.setlistName) sel = id; setlistBox.addItem (n, id++); }
    setlistBox.setSelectedId (sel, dontSendNotification);
    songList.updateContent(); songList.repaint();
}

void SettingsSheet::buildMidiList()
{
    midiToggles.clear();
    const auto devices = MidiInput::getAvailableDevices();
    for (auto& d : devices)
    {
        auto* t = midiToggles.add (new ToggleButton (d.name));
        t->setToggleState (deviceManager != nullptr && deviceManager->isMidiInputDeviceEnabled (d.identifier), dontSendNotification);
        const auto id = d.identifier;
        t->onClick = [this, t, id] { if (deviceManager != nullptr) deviceManager->setMidiInputDeviceEnabled (id, t->getToggleState()); };
        midiPage.addAndMakeVisible (t);
    }
    lastMidiDevices.clear();
    for (auto& d : devices) lastMidiDevices.add (d.identifier);
    resized();
}

void SettingsSheet::timerCallback()
{
    StringArray now;
    for (auto& d : MidiInput::getAvailableDevices()) now.add (d.identifier);
    if (now != lastMidiDevices) buildMidiList();
    lastCcLabel.setText ("Last MIDI received: " + proc.engine.lastMidiText(), dontSendNotification);
    t3kStatus.setText (proc.tone3000->getClientId().isEmpty() ? "Not set up yet"
                       : proc.tone3000->isSignedIn() ? "Key saved · signed in" : "Key saved · you'll sign in the first time you browse", dontSendNotification);
    t3kStatus.setFont (theme::font (13.0f, 1));
    t3kStatus.setColour (Label::textColourId, proc.tone3000->isSignedIn() ? theme::accent : theme::muted);
}

void SettingsSheet::showPage (int pg)
{
    page = pg;
    nav[pg]->setToggleState (true, dontSendNotification);
    audioPage.setVisible (pg == 0);
    midiPage.setVisible (pg == 1);
    libraryPage.setVisible (pg == 2);
    t3kPage.setVisible (pg == 3);
    displayPage.setVisible (pg == 4);
    outputPage.setVisible (pg == 5);
    setlistPage.setVisible (pg == 6);
    if (controllerPage != nullptr) controllerPage->setVisible (pg == 7);
    repaint();
}

Rectangle<int> SettingsSheet::card() const
{
    return getLocalBounds().withSizeKeepingCentre (jmin (900, getWidth() - 60), jmin (620, getHeight() - 60));
}

void SettingsSheet::mouseDown (const MouseEvent& e)
{
    if (! card().contains (e.getPosition()) && onClose) onClose();
}

void SettingsSheet::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.6f));
    auto c = card().toFloat();
    g.setColour (theme::card);
    g.fillRoundedRectangle (c, 18.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (c.reduced (0.5f), 18.0f, 1.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (c.withWidth (200), 18.0f);
    g.fillRect (c.withWidth (20).withX (c.getX() + 180));
    g.setColour (theme::text);
    g.setFont (theme::font (20.0f, 2));
    g.drawText ("Settings", (int) c.getX() + 24, (int) c.getY() + 22, 160, 28, Justification::centredLeft);
    const char* titles[] = { "Audio", "MIDI", "Library", "TONE3000", "Display", "Output", "Setlists", "Live" };
    g.drawText (titles[page], (int) c.getX() + 228, (int) c.getY() + 22, 400, 28, Justification::centredLeft);
}

void SettingsSheet::resized()
{
    auto c = card();
    closeBtn.setBounds (c.getRight() - 48, c.getY() + 18, 32, 32);
    auto navArea = c.withWidth (200).reduced (14, 0).withTrimmedTop (70);
    for (auto* b : nav) { b->setBounds (navArea.removeFromTop (38)); navArea.removeFromTop (4); }
    auto content = c.withTrimmedLeft (220).withTrimmedTop (66).reduced (8, 16).withTrimmedRight (16);
    for (auto* pg : std::initializer_list<Component*> { &audioPage, &midiPage, &libraryPage, &t3kPage, &displayPage, &outputPage, &setlistPage })
        pg->setBounds (content);
    if (controllerPage != nullptr) controllerPage->setBounds (content);

    // output
    {
        auto o = outputPage.getLocalBounds();
        outHelp.setBounds (o.removeFromTop (60));
        o.removeFromTop (6);
        outProfile.setBounds (o.removeFromTop (32).withWidth (300));
        o.removeFromTop (14);
        auto kr = o.removeFromTop (108);
        for (auto* k : eqKnobs) k->setBounds (kr.removeFromLeft (jmin (92, kr.getWidth() / 7 + 1)));
        o.removeFromTop (16);
        spill.setBounds (o.removeFromTop (34).withWidth (600));
        cutScene.setBounds (o.removeFromTop (34).withWidth (600));
    }
    // setlists
    {
        auto st = setlistPage.getLocalBounds();
        setlistHelp.setBounds (st.removeFromTop (56));
        auto top = st.removeFromTop (32);
        setlistBox.setBounds (top.removeFromLeft (300)); top.removeFromLeft (10);
        newSetlist.setBounds (top.removeFromLeft (120)); top.removeFromLeft (10);
        showSetlists.setBounds (top.removeFromLeft (170));
        st.removeFromTop (12);
        auto side = st.removeFromRight (170);
        songList.setBounds (st.withTrimmedRight (12));
        for (auto* b : { &addSong, &loadSong, &songUp, &songDown, &removeSong }) { b->setBounds (side.removeFromTop (32)); side.removeFromTop (8); }
        side.removeFromTop (14);
        for (auto* b : { &learnPrev, &learnNext }) { b->setBounds (side.removeFromTop (32)); side.removeFromTop (8); }
    }

    // audio
    auto a = audioPage.getLocalBounds();
    auto bottom = a.removeFromBottom (110);
    if (selector != nullptr) selector->setBounds (a);
    inGain->setBounds (bottom.removeFromLeft (96).withHeight (104));
    outGain->setBounds (bottom.removeFromLeft (96).withHeight (104));
    metroLevel->setBounds (bottom.removeFromLeft (96).withHeight (104));
    bottom.removeFromLeft (16);
    monoOut.setBounds (bottom.removeFromTop (34).withWidth (240));
    bottom.removeFromTop (10);
    beatsBox.setBounds (bottom.removeFromTop (32).withWidth (200));

    // midi
    auto m = midiPage.getLocalBounds();
    midiInfo.setBounds (m.removeFromTop (150));
    lastCcLabel.setBounds (m.removeFromTop (24));
    m.removeFromTop (8);
    auto btns = m.removeFromTop (34);
    bluetoothBtn.setBounds (btns.removeFromLeft (250));
    btns.removeFromLeft (10);
    tapLearn.setBounds (btns.removeFromLeft (200));
    m.removeFromTop (14);
    for (auto* t : midiToggles) { t->setBounds (m.removeFromTop (32).withWidth (420)); m.removeFromTop (4); }

    // library
    auto l = libraryPage.getLocalBounds();
    for (auto* t : { &watchDl, &pairIr, &bypassCab }) { t->setBounds (l.removeFromTop (34).withWidth (560)); l.removeFromTop (8); }
    l.removeFromTop (12);
    auto lb = l.removeFromTop (34);
    showLib.setBounds (lb.removeFromLeft (180)); lb.removeFromLeft (10);
    rescan.setBounds (lb.removeFromLeft (150)); lb.removeFromLeft (10);
    showPresets.setBounds (lb.removeFromLeft (190));

    // tone3000
    auto t = t3kPage.getLocalBounds();
    t3kHelp.setBounds (t.removeFromTop (200));
    auto keyRow = t.removeFromTop (36);
    t3kKey.setBounds (keyRow.removeFromLeft (360));
    keyRow.removeFromLeft (10);
    t3kSave.setBounds (keyRow.removeFromLeft (110));
    t.removeFromTop (12);
    t3kStatus.setBounds (t.removeFromTop (24));
    t.removeFromTop (12);
    auto tb = t.removeFromTop (34);
    t3kOpen.setBounds (tb.removeFromLeft (230)); tb.removeFromLeft (10);
    t3kSignOut.setBounds (tb.removeFromLeft (110));

    // display
    auto d = displayPage.getLocalBounds();
    zoomLabel.setBounds (d.removeFromTop (22));
    zoom.setBounds (d.removeFromTop (36).withWidth (420));
    d.removeFromTop (18);
    fontLabel.setBounds (d.removeFromTop (22));
    fontSize.setBounds (d.removeFromTop (36).withWidth (420));
    d.removeFromTop (24);
    fullscreen.setBounds (d.removeFromTop (34).withWidth (160));
}

//==============================================================================
MergePanel::MergePanel (NAMHomeProcessor& p, int r) : row (r)
{
    auto& m = p.engine.merges[r];
    auto set = [&p] (std::atomic<float>& a) { return [&a, &p] (float v) { a = v; p.dirty = true; }; };
    mainLevel = std::make_unique<ValueKnob> ("Main level", -40.0f, 6.0f, 0.0f, "dB", [&m] { return m.mainLevel.load(); }, set (m.mainLevel), theme::accent);
    mainPan = std::make_unique<ValueKnob> ("Main pan", -1.0f, 1.0f, 0.0f, "pan", [&m] { return m.mainPan.load(); }, set (m.mainPan), theme::accent);
    branchLevel = std::make_unique<ValueKnob> ("Branch level", -40.0f, 6.0f, 0.0f, "dB", [&m] { return m.branchLevel.load(); }, set (m.branchLevel), theme::accent);
    branchPan = std::make_unique<ValueKnob> ("Branch pan", -1.0f, 1.0f, 0.0f, "pan", [&m] { return m.branchPan.load(); }, set (m.branchPan), theme::accent);
    independent = p.engine.rowIsIndependent (r);
    if (independent) { addAndMakeVisible (*branchLevel); addAndMakeVisible (*branchPan); setSize (220, 150); }
    else { for (auto* k : { mainLevel.get(), mainPan.get(), branchLevel.get(), branchPan.get() }) addAndMakeVisible (k); setSize (420, 150); }
}

void MergePanel::paint (Graphics& g)
{
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, 2));
    g.drawText (independent ? "Row " + String (row + 1) + " rig in the mix" : "Merge for row " + String (row + 1), 12, 6, 300, 20, Justification::centredLeft);
}

void MergePanel::resized()
{
    auto r = getLocalBounds().reduced (8).withTrimmedTop (24);
    if (independent) { const int w = r.getWidth() / 2; branchLevel->setBounds (r.removeFromLeft (w)); branchPan->setBounds (r); return; }
    const int w = r.getWidth() / 4;
    for (auto* k : { mainLevel.get(), mainPan.get(), branchLevel.get(), branchPan.get() }) k->setBounds (r.removeFromLeft (w));
}

//==============================================================================
TransposePanel::TransposePanel (NAMHomeProcessor& p, std::function<void()> changed) : proc (p), onChange (std::move (changed))
{
    auto& e = proc.engine;
    semis = std::make_unique<ValueKnob> ("Semitones", -12.0f, 12.0f, 0.0f, "st", [&e] { return (float) e.transposeSemis.load(); },
                                         [this, &e] (float v) { e.transposeSemis = roundToInt (v); proc.dirty = true; if (onChange) onChange(); }, theme::accent);
    cents = std::make_unique<ValueKnob> ("Cents", -50.0f, 50.0f, 0.0f, "c", [&e] { return e.transposeCents.load(); },
                                         [this, &e] (float v) { e.transposeCents = (float) roundToInt (v); proc.dirty = true; if (onChange) onChange(); }, theme::accent);
    addAndMakeVisible (*semis);
    addAndMakeVisible (*cents);
    const std::pair<const char*, int> tunings[] = { { "Standard", 0 }, { "Eb (-1)", -1 }, { "D (-2)", -2 }, { "C# (-3)", -3 }, { "C (-4)", -4 }, { "B (-5)", -5 } };
    for (auto& t : tunings)
    {
        auto* b = presets.add (new juce::TextButton (t.first));
        const int st = t.second;
        b->onClick = [this, st]
        {
            proc.engine.transposeSemis = st;
            proc.engine.transposeCents = 0;
            semis->sync(); cents->sync();
            proc.dirty = true;
            if (onChange) onChange();
        };
        addAndMakeVisible (b);
    }
    setSize (380, 250);
}

void TransposePanel::paint (juce::Graphics& g)
{
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, 2));
    g.drawText ("Transpose (virtual tuning)", 12, 6, 300, 20, juce::Justification::centredLeft);
    g.setColour (theme::faint);
    g.setFont (theme::font (11.5f));
    g.drawFittedText ("Shifts your guitar before the amps and pedals. Adds only a few ms of delay for typical drop tunings; small shifts sound most natural.",
                      juce::Rectangle<int> (12, getHeight() - 40, getWidth() - 24, 34), juce::Justification::topLeft, 2);
}

void TransposePanel::resized()
{
    auto r = getLocalBounds().reduced (10).withTrimmedTop (24).withTrimmedBottom (40);
    auto knobs = r.removeFromLeft (200);
    semis->setBounds (knobs.removeFromLeft (100).withHeight (110));
    cents->setBounds (knobs.withHeight (110));
    r.removeFromLeft (10);
    for (auto* b : presets) { b->setBounds (r.removeFromTop (26)); r.removeFromTop (4); }
}
