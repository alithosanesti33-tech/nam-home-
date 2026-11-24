#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using namespace juce;

static Font uiFont (float size, bool bold = false)
{
    return Font (FontOptions (size, bold ? Font::bold : Font::plain));
}

//==============================================================================
NamLookAndFeel::NamLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, theme::bg);
    setColour (TextButton::buttonColourId, theme::raised);
    setColour (TextButton::buttonOnColourId, theme::text);
    setColour (TextButton::textColourOffId, theme::text);
    setColour (TextButton::textColourOnId, theme::bg);
    setColour (Label::textColourId, theme::text);
    setColour (TextEditor::backgroundColourId, theme::raised);
    setColour (TextEditor::textColourId, theme::text);
    setColour (TextEditor::outlineColourId, theme::line);
    setColour (TextEditor::focusedOutlineColourId, theme::muted);
    setColour (ListBox::backgroundColourId, theme::panel);
    setColour (Slider::textBoxTextColourId, theme::muted);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (PopupMenu::backgroundColourId, theme::panel);
    setColour (PopupMenu::textColourId, theme::text);
    setColour (PopupMenu::highlightedBackgroundColourId, theme::raised);
    setColour (ScrollBar::thumbColourId, theme::line);
}

void NamLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto c = s.findColour (Slider::rotarySliderFillColourId);
    auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4);
    const float d = jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const auto centre = r.getCentre();
    const float rad = d * 0.5f;
    const float angle = start + pos * (end - start);

    g.setColour (theme::raised);
    g.fillEllipse (r.reduced (5));
    Path track, value;
    track.addCentredArc (centre.x, centre.y, rad - 2, rad - 2, 0, start, end, true);
    value.addCentredArc (centre.x, centre.y, rad - 2, rad - 2, 0, start, angle, true);
    g.setColour (theme::line);
    g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (c);
    g.strokePath (value, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    Path pointer;
    pointer.addRoundedRectangle (-1.5f, -(rad - 8), 3.0f, rad * 0.5f, 1.5f);
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (centre));
}

void NamLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& bgc, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bgc;
    if (b.getToggleState()) c = b.findColour (TextButton::buttonOnColourId);
    if (over) c = c.brighter (0.08f);
    if (down) c = c.darker (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 6.0f);
}

void NamLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s)
{
    const auto track = Rectangle<float> ((float) x, (float) y + h * 0.5f - 3, (float) w, 6.0f);
    g.setColour (theme::line);
    g.fillRoundedRectangle (track, 3.0f);
    g.setColour (s.findColour (Slider::trackColourId));
    g.fillRoundedRectangle (track.withRight (pos), 3.0f);
    g.setColour (theme::text);
    g.fillEllipse (pos - 6, (float) y + h * 0.5f - 6, 12, 12);
}

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& s, const String& paramId, const String& label, Colour c)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 16);
    slider.setColour (Slider::rotarySliderFillColourId, c);
    slider.setDoubleClickReturnValue (true, s.getParameter (paramId)->convertFrom0to1 (s.getParameter (paramId)->getDefaultValue()));
    name.setText (label, dontSendNotification);
    name.setJustificationType (Justification::centred);
    name.setFont (uiFont (12.0f));
    addAndMakeVisible (slider);
    addAndMakeVisible (name);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromBottom (16));
    slider.setBounds (r);
}

ParamToggle::ParamToggle (AudioProcessorValueTreeState& s, const String& paramId, const String& label, Colour on)
    : TextButton (label)
{
    setClickingTogglesState (true);
    setColour (TextButton::buttonOnColourId, on);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, paramId, *this);
}

//==============================================================================
static Colour kindColour (LibraryItem::Kind k)
{
    return k == LibraryItem::Kind::IR ? theme::cab : k == LibraryItem::Kind::Pedal ? theme::pedal : theme::amp;
}

static const char* chipNames[] = { "All", "Adjustable amps", "NAM captures", "Pedals", "Cabs / IRs", "Effects" };

LibraryPanel::LibraryPanel (NAMHomeEditor& e, Library& lib) : editor (e), library (lib)
{
    search.setTextToShowWhenEmpty ("Search captures and IRs", theme::muted);
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);

    for (int i = 0; i < 6; ++i)
    {
        auto* b = chips.add (new TextButton (chipNames[i]));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (101);
        b->setToggleState (i == 0, dontSendNotification);
        b->onClick = [this, i] { filter = i; refresh(); };
        addAndMakeVisible (b);
    }

    list.setModel (this);
    list.setRowHeight (44);
    list.setColour (ListBox::backgroundColourId, theme::panel);
    addAndMakeVisible (list);

    importBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Import captures and IRs", File(), "*.nam;*.wav;*.zip");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc)
                              {
                                  const int n = library.importFiles (fc.getResults());
                                  showNotice (n > 0 ? String (n) + " added to your library" : "Nothing new to add");
                              });
    };
    folderBtn.onClick = [this] { library.getFolder().revealToUser(); };
    addAndMakeVisible (importBtn);
    addAndMakeVisible (folderBtn);
    refresh();
}

void LibraryPanel::showNotice (const String& s)
{
    notice = s;
    repaint();
}

void LibraryPanel::refresh()
{
    shown.clear();
    const auto q = search.getText().trim();
    for (auto& it : library.getItems())
    {
        const bool isNam = it.kind != LibraryItem::Kind::IR;
        bool ok = true;
        switch (filter)
        {
            case 1: ok = it.kind == LibraryItem::Kind::Amp && it.adjustable; break;
            case 2: ok = isNam && ! it.adjustable; break;
            case 3: ok = it.kind == LibraryItem::Kind::Pedal; break;
            case 4: ok = it.kind == LibraryItem::Kind::IR; break;
            case 5: ok = false; break; // built-in effects arrive in Stage 3
            default: break;
        }
        if (ok && q.isNotEmpty())
            ok = (it.name + " " + it.creator + " " + it.gearMake + " " + it.gearModel + " " + it.micTag + " " + it.pack).containsIgnoreCase (q);
        if (ok) shown.push_back (&it);
    }
    list.updateContent();
    list.repaint();
    repaint();
}

void LibraryPanel::paint (Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (theme::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText ("Library", 12, 8, 200, 22, Justification::centredLeft);
    g.setColour (theme::muted);
    g.setFont (uiFont (12.0f));
    g.drawText (String ((int) library.getItems().size()) + " files", getWidth() - 112, 8, 100, 22, Justification::centredRight);

    if (shown.empty())
    {
        g.setColour (theme::muted);
        g.setFont (uiFont (13.0f));
        const auto msg = filter == 5 ? String ("Built-in effects arrive in Stage 3.")
                       : filter == 1 ? String ("No adjustable amp models yet. Support for them comes at the end of Stage 1.")
                       : String ("Add .nam captures and .wav IRs: download them (they're added automatically), drop them on this window, or use Import files.");
        g.drawFittedText (msg, list.getBounds().reduced (12), Justification::centredTop, 4);
    }

    if (notice.isNotEmpty())
    {
        auto r = Rectangle<int> (10, importBtn.getY() - 34, getWidth() - 20, 28);
        g.setColour (Colour (0xff10302c));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (Colour (0xff9fe1d6));
        g.setFont (uiFont (12.0f));
        g.drawText (notice, r.reduced (8, 0), Justification::centredLeft);
    }
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (28);
    search.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);

    // chips wrap onto rows
    int x = r.getX(), y = r.getY();
    for (auto* b : chips)
    {
        const int w = (int) GlyphArrangement::getStringWidth (uiFont (12.0f), b->getButtonText()) + 20;
        if (x + w > r.getRight()) { x = r.getX(); y += 30; }
        b->setBounds (x, y, w, 26);
        x += w + 6;
    }
    r.setTop (y + 34);

    auto bottom = r.removeFromBottom (30);
    importBtn.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 3));
    bottom.removeFromLeft (6);
    folderBtn.setBounds (bottom);
    r.removeFromBottom (notice.isNotEmpty() ? 42 : 8);
    list.setBounds (r);
}

void LibraryPanel::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& it = *shown[(size_t) row];
    if (selected) { g.setColour (theme::raised); g.fillRoundedRectangle (2, 2, (float) w - 4, (float) h - 4, 6.0f); }

    g.setColour (kindColour (it.kind));
    g.fillRoundedRectangle (10, (float) h * 0.5f - 6, 12, 12, 2.0f);

    g.setColour (theme::text);
    g.setFont (uiFont (13.0f));
    g.drawText (it.name, 32, 4, w - 90, 18, Justification::centredLeft);

    auto sub = it.creditLine();
    if (it.micTag.isNotEmpty()) sub << "  ·  " << it.micTag;
    g.setColour (theme::muted);
    g.setFont (uiFont (11.5f));
    g.drawText (sub, 32, 22, w - 90, 16, Justification::centredLeft);

    if (it.isNew)
    {
        g.setColour (Colour (0xff10302c));
        g.fillRoundedRectangle ((float) w - 50, (float) h * 0.5f - 9, 40, 18, 5.0f);
        g.setColour (Colour (0xff7fded3));
        g.setFont (uiFont (11.0f));
        g.drawText ("New", w - 50, h / 2 - 9, 40, 18, Justification::centred);
    }
}

void LibraryPanel::listBoxItemDoubleClicked (int row, const MouseEvent&)
{
    if (row >= 0 && row < (int) shown.size())
        editor.loadItem (shown[(size_t) row]->file);
}

var LibraryPanel::getDragSourceDescription (const SparseSet<int>& rows)
{
    if (rows.size() > 0 && rows[0] < (int) shown.size())
        return shown[(size_t) rows[0]]->file.getFullPathName();
    return {};
}

//==============================================================================
ChainView::ChainView (NAMHomeEditor& e) : editor (e) {}

Rectangle<float> ChainView::tileBounds (int slot) const
{
    const float size = 120.0f, gap = 70.0f;
    const float total = size * 3 + gap * 2;
    const float x0 = (getWidth() - total) * 0.5f;
    return { x0 + slot * (size + gap), getHeight() * 0.5f - size * 0.5f, size, size };
}

int ChainView::slotAt (Point<int> p) const
{
    for (int s = 0; s < 3; ++s)
        if (tileBounds (s).contains (p.toFloat())) return s;
    return -1;
}

void ChainView::paint (Graphics& g)
{
    auto& proc = editor.proc;
    const float cy = getHeight() * 0.5f;

    g.setColour (Colour (0xff4a4a4a));
    g.drawLine (40, cy, (float) getWidth() - 40, cy, 2.0f);
    g.setColour (theme::muted);
    g.setFont (uiFont (12.0f, true));
    g.drawText ("IN", 8, (int) cy - 24, 40, 16, Justification::centred);
    g.drawText ("OUT", getWidth() - 48, (int) cy - 24, 40, 16, Justification::centred);

    auto* params = proc.params.getRawParameterValue ("pedalBypass");
    const bool bypassed[3] = { params->load() > 0.5f,
                               proc.params.getRawParameterValue ("ampBypass")->load() > 0.5f,
                               proc.params.getRawParameterValue ("cabBypass")->load() > 0.5f };

    for (int s = 0; s < 3; ++s)
    {
        auto r = tileBounds (s);
        const Colour c = s == PedalSlot ? theme::pedal : s == AmpSlot ? theme::amp : theme::cab;
        const String title = s == PedalSlot ? "Pedal" : s == AmpSlot ? "Amp" : "Cab";

        String name, sub;
        bool loaded = false;
        GainLevel gain = GainLevel::Unknown;
        if (s == PedalSlot || s == AmpSlot)
        {
            auto& slot = s == PedalSlot ? proc.getPedal() : proc.getAmp();
            loaded = slot.isLoaded();
            if (loaded)
            {
                const auto path = slot.getPath();
                const auto* it = proc.library.findByPath (path);
                name = it != nullptr ? it->name : File (path).getFileNameWithoutExtension();
                if (it != nullptr) gain = gainLevelFromToneType (it->toneType);
            }
        }
        else
        {
            int n = 0;
            for (int k = 0; k < NAMHomeProcessor::numIrs; ++k)
                if (proc.getIr (k).loaded.load()) ++n;
            loaded = n > 0;
            if (loaded) name = n == 1 ? String ("1 IR") : "IR blend x" + String (n);
        }

        Graphics::ScopedSaveState ss (g);
        if (bypassed[s] && loaded) g.setOpacity (0.4f);

        if (! loaded)
        {
            g.setColour (theme::bg);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (hover == s ? theme::text : Colour (0xff444444));
            float dash[] = { 5.0f, 4.0f };
            Path p;
            p.addRoundedRectangle (r.reduced (1), 10.0f);
            PathStrokeType (1.5f).createDashedStroke (p, p, dash, 2);
            g.fillPath (p);
            g.setColour (theme::muted);
            g.setFont (uiFont (12.0f));
            g.drawFittedText (title + "\n+\nDrop or double-click", r.reduced (8).toNearestInt(), Justification::centred, 3);
        }
        else
        {
            g.setColour (theme::raised);
            g.fillRoundedRectangle (r, 10.0f);
            Path top;
            top.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), 26.0f, 10.0f, 10.0f, true, true, false, false);
            g.setColour (c);
            g.fillPath (top);
            g.setColour (Colour (0xff141414));
            g.setFont (uiFont (12.5f, true));
            g.drawText (title, r.withHeight (26).reduced (10, 0), Justification::centredLeft);
            g.setColour (theme::text);
            g.setFont (uiFont (13.0f));
            g.drawFittedText (name, r.withTrimmedTop (32).withTrimmedBottom (14).reduced (9, 2).toNearestInt(), Justification::topLeft, 3);

            if (s == AmpSlot)
            {
                // gain strip: blue (clean) -> yellow (crunch) -> red (high gain), from the file's tone type
                auto strip = Rectangle<float> (r.getX() + 9, r.getBottom() - 12, r.getWidth() - 18, 5);
                g.setColour (gainLevelColour (gain));
                g.fillRoundedRectangle (strip, 2.5f);
            }
        }
        if (selected == s)
        {
            g.setColour (Colours::white);
            g.drawRoundedRectangle (r.expanded (2), 11.0f, 2.0f);
        }
        else if (hover == s)
        {
            g.setColour (c);
            g.drawRoundedRectangle (r.expanded (2), 11.0f, 2.0f);
        }
    }
}

void ChainView::mouseDown (const MouseEvent& e)
{
    const int s = slotAt (e.getPosition());
    if (s >= 0) editor.select (s);
}

void ChainView::mouseDoubleClick (const MouseEvent& e)
{
    const int s = slotAt (e.getPosition());
    if (s >= 0) editor.chooseFileFor (s);
}

static bool kindFitsSlot (const LibraryItem* it, const File& f, int slot)
{
    if (slot == ChainView::CabSlot) return f.hasFileExtension ("wav");
    if (! f.hasFileExtension ("nam")) return false;
    if (it == nullptr) return true;
    return slot == ChainView::PedalSlot ? true : true; // a capture can go in either NAM slot
}

bool ChainView::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.isString();
}

void ChainView::itemDragEnter (const SourceDetails& d)
{
    hover = slotAt (d.localPosition);
    repaint();
}

void ChainView::itemDragExit (const SourceDetails&)
{
    hover = -1;
    repaint();
}

void ChainView::itemDropped (const SourceDetails& d)
{
    const int s = slotAt (d.localPosition);
    hover = -1;
    const File f (d.description.toString());
    if (s >= 0 && kindFitsSlot (editor.proc.library.findByPath (f.getFullPathName()), f, s))
    {
        if (s == CabSlot) editor.loadItem (f, -1);
        else if (s == PedalSlot) { const auto err = editor.proc.loadPedal (f); if (err.isNotEmpty()) editor.showError (err); editor.select (s); }
        else { const auto err = editor.proc.loadAmp (f); if (err.isNotEmpty()) editor.showError (err); editor.select (s); }
    }
    else if (s >= 0)
        editor.showError (s == CabSlot ? "Cab slots take .wav IRs." : "This slot takes .nam captures.");
    editor.refreshAll();
}

//==============================================================================
static void drawEditorHeader (Graphics& g, Rectangle<int> r, Colour c, const String& title, const String& badge, Colour badgeBg, Colour badgeText)
{
    g.setColour (c);
    g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 5, 12, 12, 2.0f);
    g.setColour (theme::text);
    g.setFont (uiFont (15.0f, true));
    const int w = (int) GlyphArrangement::getStringWidth (uiFont (15.0f, true), title) + 4;
    g.drawText (title, r.getX() + 20, r.getY(), w, 22, Justification::centredLeft);
    if (badge.isNotEmpty())
    {
        const int bw = (int) GlyphArrangement::getStringWidth (uiFont (11.5f), badge) + 16;
        auto b = Rectangle<int> (r.getX() + 28 + w, r.getY() + 2, bw, 18);
        g.setColour (badgeBg);
        g.fillRoundedRectangle (b.toFloat(), 5.0f);
        g.setColour (badgeText);
        g.setFont (uiFont (11.5f));
        g.drawText (badge, b, Justification::centred);
    }
}

static void drawCaptureInfo (Graphics& g, Rectangle<int> r, NAMHomeProcessor& proc, NamSlot& slot)
{
    g.setColour (theme::muted);
    g.setFont (uiFont (12.5f));
    if (! slot.isLoaded())
    {
        g.drawFittedText ("No capture loaded. Drag one from the library onto the tile, double-click a library item, or use Load capture.", r, Justification::topLeft, 4);
        return;
    }
    const auto* it = proc.library.findByPath (slot.getPath());
    const File f (slot.getPath());
    String s;
    g.setColour (theme::text);
    g.setFont (uiFont (15.0f));
    g.drawText (it != nullptr ? it->name : f.getFileNameWithoutExtension(), r.removeFromTop (22), Justification::centredLeft);
    g.setColour (theme::muted);
    g.setFont (uiFont (12.5f));
    if (it != nullptr)
    {
        s << it->creditLine() << "\n";
        if (it->gearMake.isNotEmpty() || it->gearModel.isNotEmpty()) s << "Gear: " << it->gearMake << " " << it->gearModel << "\n";
        s << "Gain: " << gainLevelName (gainLevelFromToneType (it->toneType)) << "\n";
    }
    s << "File: " << f.getFileName();
    if (slot.sampleRateMismatch())
        s << "\nTrained at " << String (slot.getExpectedRate() / 1000.0, 1) << " kHz: set your interface to match in Settings.";
    g.drawFittedText (s, r, Justification::topLeft, 6);
}

PedalEditor::PedalEditor (NAMHomeEditor& e)
    : editor (e),
      in (e.proc.params, "pedalIn", "Input", theme::shaping),
      out (e.proc.params, "pedalOut", "Output", theme::shaping),
      bypass (e.proc.params, "pedalBypass", "Bypass", theme::muted)
{
    for (auto* c : std::initializer_list<Component*> { &in, &out, &bypass, &load, &clear }) addAndMakeVisible (c);
    load.onClick = [this] { editor.chooseFileFor (ChainView::PedalSlot); };
    clear.onClick = [this] { editor.proc.clearPedal(); editor.refreshAll(); };
}

void PedalEditor::paint (Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    drawEditorHeader (g, { 14, 12, 600, 22 }, theme::pedal, "NAM pedal", "Standard capture", theme::raised, Colour (0xffc9ccd1));
    drawCaptureInfo (g, { 14, 44, 300, 140 }, editor.proc, editor.proc.getPedal());
    g.setColour (theme::muted);
    g.setFont (uiFont (12.0f));
    g.drawFittedText ("A capture is one fixed pedal setting. Input changes how hard you hit it; output sets its level. For more drive, load a capture made at a higher setting.",
                      Rectangle<int> (340, getHeight() - 44, getWidth() - 354, 36), Justification::topLeft, 2);
}

void PedalEditor::resized()
{
    in.setBounds (340, 44, 90, 100);
    out.setBounds (440, 44, 90, 100);
    bypass.setBounds (getWidth() - 100, 10, 86, 26);
    load.setBounds (14, getHeight() - 40, 120, 28);
    clear.setBounds (142, getHeight() - 40, 90, 28);
}

AmpEditor::AmpEditor (NAMHomeEditor& e)
    : editor (e),
      drive (e.proc.params, "ampDrive", "Input drive", theme::shaping),
      tight (e.proc.params, "ampTight", "Tight (pre)", theme::shaping),
      preMid (e.proc.params, "ampPreMid", "Mid (pre)", theme::shaping),
      low (e.proc.params, "ampLow", "Low (post)", theme::shaping),
      mid (e.proc.params, "ampMid", "Mid (post)", theme::shaping),
      high (e.proc.params, "ampHigh", "High (post)", theme::shaping),
      out (e.proc.params, "ampOut", "Output", theme::shaping),
      bypass (e.proc.params, "ampBypass", "Bypass", theme::muted),
      normalize (e.proc.params, "ampNormalize", "Even out loudness", theme::amp)
{
    for (auto* c : std::initializer_list<Component*> { &drive, &tight, &preMid, &low, &mid, &high, &out, &bypass, &normalize, &load, &clear })
        addAndMakeVisible (c);
    load.onClick = [this] { editor.chooseFileFor (ChainView::AmpSlot); };
    clear.onClick = [this] { editor.proc.clearAmp(); editor.refreshAll(); };
}

void AmpEditor::paint (Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    drawEditorHeader (g, { 14, 12, 600, 22 }, theme::amp, "NAM amp", "Standard capture", theme::raised, Colour (0xffc9ccd1));
    drawCaptureInfo (g, { 14, 44, 300, 140 }, editor.proc, editor.proc.getAmp());

    g.setColour (theme::muted);
    g.setFont (uiFont (12.0f));
    g.drawText ("Added shaping", 340, 40, 300, 16, Justification::centredLeft);
    g.drawFittedText ("The amp's own knobs are fixed where it was captured. Grey knobs shape the sound around the capture; the capture itself is never changed.",
                      Rectangle<int> (340, getHeight() - 44, getWidth() - 354, 36), Justification::topLeft, 2);
}

void AmpEditor::resized()
{
    int x = 340;
    for (auto* k : { &drive, &tight, &preMid, &low, &mid, &high, &out })
    {
        k->setBounds (x, 60, 86, 100);
        x += 92;
    }
    bypass.setBounds (getWidth() - 100, 10, 86, 26);
    normalize.setBounds (getWidth() - 250, 10, 140, 26);
    load.setBounds (14, getHeight() - 40, 120, 28);
    clear.setBounds (142, getHeight() - 40, 90, 28);
}

CabEditor::CabEditor (NAMHomeEditor& e)
    : editor (e),
      lowCut (e.proc.params, "cabLowCut", "Low cut", theme::cab),
      highCut (e.proc.params, "cabHighCut", "High cut", theme::cab),
      bypass (e.proc.params, "cabBypass", "Bypass", theme::muted)
{
    auto& s = e.proc.params;
    for (int k = 0; k < NAMHomeProcessor::numIrs; ++k)
    {
        auto& r = rows[(size_t) k];
        const auto id = "ir" + String (k + 1);
        r.level = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::TextBoxRight);
        r.level->setTextBoxStyle (Slider::TextBoxRight, false, 64, 20);
        r.level->setColour (Slider::trackColourId, theme::cab);
        r.levelAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id + "Level", *r.level);
        r.align = std::make_unique<Slider> (Slider::IncDecButtons, Slider::TextBoxLeft);
        r.align->setTextBoxStyle (Slider::TextBoxLeft, false, 44, 22);
        r.align->setTextValueSuffix (" smp");
        r.alignAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id + "Align", *r.align);
        r.pan = std::make_unique<Knob> (s, id + "Pan", "Pan", theme::cab);
        r.polarity = std::make_unique<ParamToggle> (s, id + "Polarity", String (CharPointer_UTF8 ("\xc3\x98")), theme::cab);
        r.polarity->setTooltip ("Invert polarity");
        r.mute = std::make_unique<ParamToggle> (s, id + "Mute", "Mute", theme::muted);
        r.mic = std::make_unique<TextEditor>();
        r.mic->setTextToShowWhenEmpty ("Mic tag (e.g. SM57)", theme::muted);
        r.mic->onReturnKey = r.mic->onFocusLost = [this, k]
        {
            const auto path = editor.proc.getIr (k).path;
            if (path.isNotEmpty()) editor.proc.library.setMicTag (path, rows[(size_t) k].mic->getText().trim());
        };
        r.load = std::make_unique<TextButton> ("Load");
        r.load->onClick = [this, k] { editor.chooseFileFor (ChainView::CabSlot, k); };
        r.clear = std::make_unique<TextButton> ("Remove");
        r.clear->onClick = [this, k] { editor.proc.clearIr (k); editor.refreshAll(); };
        for (auto* c : std::initializer_list<Component*> { r.level.get(), r.align.get(), r.pan.get(), r.polarity.get(), r.mute.get(), r.mic.get(), r.load.get(), r.clear.get() })
            addAndMakeVisible (c);
    }
    addAndMakeVisible (lowCut);
    addAndMakeVisible (highCut);
    addAndMakeVisible (bypass);
}

void CabEditor::refreshTags()
{
    for (int k = 0; k < NAMHomeProcessor::numIrs; ++k)
    {
        auto& r = rows[(size_t) k];
        if (r.mic->hasKeyboardFocus (true)) continue;
        const auto* it = editor.proc.library.findByPath (editor.proc.getIr (k).path);
        r.mic->setText (it != nullptr ? it->micTag : String(), false);
        r.mic->setEnabled (editor.proc.getIr (k).loaded.load());
    }
}

void CabEditor::paint (Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    drawEditorHeader (g, { 14, 12, 600, 22 }, theme::cab, "Cab · IR blend", {}, {}, {});

    g.setColour (theme::muted);
    g.setFont (uiFont (11.5f));
    const int hy = 40;
    g.drawText ("IR and mic tag", 14, hy, 200, 14, Justification::centredLeft);
    g.drawText ("Level", 330, hy, 100, 14, Justification::centredLeft);
    g.drawText ("Align", 640, hy, 100, 14, Justification::centredLeft);

    for (int k = 0; k < NAMHomeProcessor::numIrs; ++k)
    {
        auto& r = rows[(size_t) k];
        g.setColour (theme::raised);
        g.fillRoundedRectangle (r.area.toFloat(), 7.0f);
        const auto& ir = editor.proc.getIr (k);
        const auto* it = editor.proc.library.findByPath (ir.path);
        g.setColour (ir.loaded.load() ? theme::text : theme::muted);
        g.setFont (uiFont (13.0f));
        const auto name = ir.loaded.load() ? (it != nullptr ? it->name : File (ir.path).getFileNameWithoutExtension()) : String ("Empty: drop an IR on the Cab tile or press Load");
        g.drawText (name, r.area.getX() + 10, r.area.getY() + 4, 300, 18, Justification::centredLeft);
        if (ir.loaded.load() && it != nullptr)
        {
            g.setColour (theme::muted);
            g.setFont (uiFont (11.5f));
            g.drawText (it->creditLine(), r.area.getX() + 10, r.area.getY() + 22, 300, 14, Justification::centredLeft);
        }
    }
    g.setColour (theme::muted);
    g.setFont (uiFont (12.0f));
    g.drawFittedText ("Mic tags are labels you set to sort and search. They don't change the sound.",
                      Rectangle<int> (14, getHeight() - 26, getWidth() - 28, 18), Justification::centredLeft, 1);
}

void CabEditor::resized()
{
    bypass.setBounds (getWidth() - 100, 10, 86, 26);
    const int rowH = 64, top = 58, right = getWidth() - 200;
    for (int k = 0; k < NAMHomeProcessor::numIrs; ++k)
    {
        auto& r = rows[(size_t) k];
        r.area = { 14, top + k * (rowH + 6), right - 14, rowH };
        const int y = r.area.getY();
        r.mic->setBounds (r.area.getX() + 10, y + 38, 170, 22);
        r.load->setBounds (r.area.getX() + 186, y + 38, 54, 22);
        r.clear->setBounds (r.area.getX() + 244, y + 38, 66, 22);
        r.level->setBounds (330, y + 18, 220, 28);
        r.pan->setBounds (556, y + 2, 60, 60);
        r.align->setBounds (630, y + 18, 110, 28);
        r.polarity->setBounds (752, y + 18, 36, 28);
        r.mute->setBounds (796, y + 18, 56, 28);
    }
    lowCut.setBounds (getWidth() - 186, 60, 86, 100);
    highCut.setBounds (getWidth() - 96, 60, 86, 100);
}

//==============================================================================
NAMHomeEditor::NAMHomeEditor (NAMHomeProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      libraryPanel (*this, p.library), chain (*this),
      pedalEditor (*this), ampEditor (*this), cabEditor (*this)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (libraryBtn);
    addAndMakeVisible (settingsBtn);
    addAndMakeVisible (libraryPanel);
    addAndMakeVisible (chain);
    addChildComponent (pedalEditor);
    addChildComponent (ampEditor);
    addChildComponent (cabEditor);

    libraryBtn.setClickingTogglesState (true);
    libraryBtn.setToggleState (true, dontSendNotification);
    libraryBtn.onClick = [this] { libraryOpen = libraryBtn.getToggleState(); libraryPanel.setVisible (libraryOpen); resized(); };
    settingsBtn.onClick = [this] { showSettingsMenu(); };

    proc.library.onChanged = [this] { libraryPanel.refresh(); cabEditor.refreshTags(); chain.repaint(); };
    proc.library.onNotice = [this] (const String& s) { libraryPanel.showNotice (s); libraryPanel.resized(); };

   #if JucePlugin_Build_Standalone
    // A guitar rig needs its input: the standalone wrapper mutes it by default.
    if (auto* h = StandalonePluginHolder::getInstance())
        h->getMuteInputValue().setValue (false);
   #endif

    select (ChainView::AmpSlot);
    setSize (1280, 800);
    startTimerHz (20);
}

NAMHomeEditor::~NAMHomeEditor()
{
    proc.library.onChanged = nullptr;
    proc.library.onNotice = nullptr;
    setLookAndFeel (nullptr);
}

void NAMHomeEditor::paint (Graphics& g)
{
    g.fillAll (theme::bg);

    // top bar
    auto bar = Rectangle<int> (10, 10, getWidth() - 20, 46);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bar.toFloat(), 8.0f);
    g.setColour (theme::amp);
    g.setFont (uiFont (17.0f, true));
    g.drawText ("NAM Home", bar.getX() + 110, bar.getY(), 140, bar.getHeight(), Justification::centredLeft);

    auto meter = [&] (int x, const String& label, float level)
    {
        g.setColour (theme::muted);
        g.setFont (uiFont (11.5f, true));
        g.drawText (label, x, bar.getY(), 34, bar.getHeight(), Justification::centredRight);
        auto r = Rectangle<float> ((float) x + 40, (float) bar.getCentreY() - 4, 110, 8);
        g.setColour (theme::raised);
        g.fillRoundedRectangle (r, 3.0f);
        const float db = Decibels::gainToDecibels (level, -60.0f);
        const float w = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -1.0f ? theme::warn : db > -12.0f ? Colour (0xffeab308) : theme::eq);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * w), 3.0f);
    };
    const int right = bar.getRight() - 100;
    meter (right - 330, "IN", inLevel);
    meter (right - 170, "OUT", outLevel);

    g.setColour (theme::muted);
    g.setFont (uiFont (12.0f));
    g.drawText ("CPU " + String (roundToInt (cpu * 100.0)) + "%", right - 460, bar.getY(), 110, bar.getHeight(), Justification::centredRight);
    g.drawText (String (proc.getSampleRateNow() / 1000.0, 1) + " kHz", right - 560, bar.getY(), 90, bar.getHeight(), Justification::centredRight);

    String status = errorText;
    Colour statusColour = theme::warn;
    if (status.isEmpty() && proc.anyRateMismatch())
        status = "Capture expects a different sample rate: open Settings > Audio interface";
    if (status.isNotEmpty())
    {
        g.setColour (statusColour);
        g.drawFittedText (status, bar.getX() + 260, bar.getY(), right - 580 - (bar.getX() + 260), bar.getHeight(), Justification::centredLeft, 2);
    }
}

void NAMHomeEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto bar = r.removeFromTop (46);
    libraryBtn.setBounds (bar.getX() + 10, bar.getY() + 9, 90, 28);
    settingsBtn.setBounds (bar.getRight() - 94, bar.getY() + 9, 84, 28);
    r.removeFromTop (10);

    if (libraryOpen)
    {
        libraryPanel.setBounds (r.removeFromLeft (310));
        r.removeFromLeft (10);
    }
    chain.setBounds (r.removeFromTop (220));
    r.removeFromTop (10);
    for (auto* e : std::initializer_list<Component*> { &pedalEditor, &ampEditor, &cabEditor })
        e->setBounds (r);
}

void NAMHomeEditor::select (int slot)
{
    chain.selected = slot;
    pedalEditor.setVisible (slot == ChainView::PedalSlot);
    ampEditor.setVisible (slot == ChainView::AmpSlot);
    cabEditor.setVisible (slot == ChainView::CabSlot);
    refreshAll();
}

void NAMHomeEditor::refreshAll()
{
    cabEditor.refreshTags();
    chain.repaint();
    pedalEditor.repaint();
    ampEditor.repaint();
    cabEditor.repaint();
    repaint();
}

void NAMHomeEditor::showError (const String& s)
{
    errorText = s;
    errorTicks = 120; // ~6 seconds
    repaint();
}

void NAMHomeEditor::loadItem (const File& f, int irRow)
{
    String err;
    if (f.hasFileExtension ("wav"))
    {
        int row = irRow;
        if (row < 0)
        {
            row = 0;
            for (int k = 0; k < NAMHomeProcessor::numIrs; ++k)
                if (! proc.getIr (k).loaded.load()) { row = k; break; }
        }
        err = proc.loadIr (row, f);
        select (ChainView::CabSlot);
    }
    else if (f.hasFileExtension ("nam"))
    {
        const auto* it = proc.library.findByPath (f.getFullPathName());
        const bool isPedal = it != nullptr && it->kind == LibraryItem::Kind::Pedal;
        err = isPedal ? proc.loadPedal (f) : proc.loadAmp (f);
        select (isPedal ? ChainView::PedalSlot : ChainView::AmpSlot);
    }
    if (err.isNotEmpty()) showError (err);
    refreshAll();
}

void NAMHomeEditor::chooseFileFor (int slot, int irRow)
{
    const bool cab = slot == ChainView::CabSlot;
    chooser = std::make_unique<FileChooser> (cab ? "Choose a cabinet IR" : "Choose a NAM capture",
                                             proc.library.getFolder(), cab ? "*.wav" : "*.nam");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this, slot, irRow] (const FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              String err;
                              if (slot == ChainView::CabSlot) err = proc.loadIr (irRow >= 0 ? irRow : 0, f);
                              else if (slot == ChainView::PedalSlot) err = proc.loadPedal (f);
                              else err = proc.loadAmp (f);
                              if (err.isNotEmpty()) showError (err);
                              select (slot);
                          });
}

void NAMHomeEditor::filesDropped (const StringArray& files, int, int)
{
    Array<File> fs;
    for (auto& s : files) fs.add (File (s));
    const int n = proc.library.importFiles (fs);
    libraryPanel.showNotice (n > 0 ? String (n) + " added to your library" : "Nothing new to add (only .nam, short .wav and .zip files)");
    libraryPanel.resized();
}

void NAMHomeEditor::showSettingsMenu()
{
    PopupMenu m;
    m.addItem (1, "Audio interface settings...");
    m.addSeparator();
    m.addItem (2, "Watch Downloads for new captures", true, proc.library.isWatchingDownloads());
    m.addItem (3, "Mono output", true, proc.params.getRawParameterValue ("monoOut")->load() > 0.5f);
    m.addSeparator();
    m.addItem (4, "Rescan library");
    m.addItem (5, "Show library folder");
    m.addItem (6, "Clear \"New\" labels");

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (settingsBtn), [this] (int r)
    {
        switch (r)
        {
            case 1:
               #if JucePlugin_Build_Standalone
                if (auto* h = StandalonePluginHolder::getInstance()) h->showAudioSettingsDialog();
               #endif
                break;
            case 2: proc.library.setWatchDownloads (! proc.library.isWatchingDownloads()); break;
            case 3:
            {
                auto* prm = proc.params.getParameter ("monoOut");
                prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
                break;
            }
            case 4: proc.library.rescan(); break;
            case 5: proc.library.getFolder().revealToUser(); break;
            case 6: proc.library.clearNewFlags(); break;
            default: break;
        }
    });
}

void NAMHomeEditor::timerCallback()
{
    inLevel = proc.inputPeak.load();
    outLevel = jmax (proc.outputPeakL.load(), proc.outputPeakR.load());
    cpu = cpu * 0.8 + proc.getCpuLoad() * 0.2;
    if (errorTicks > 0 && --errorTicks == 0) errorText = {};
    repaint (0, 0, getWidth(), 66);
}
