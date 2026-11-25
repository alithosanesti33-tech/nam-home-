#include "BlockEditors.h"
#include "../PluginProcessor.h"

using namespace juce;

//==============================================================================
BlockEditor::BlockEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b) : block (std::move (b)), proc (p)
{
    lastParam = (int) block->params.size() - 1;
    rebuild();
}

bool BlockEditor::skipParam (int i) const
{
    return i < firstParam || i > lastParam;
}

void BlockEditor::rebuild()
{
    knobs.clear(); combos.clear(); switches.clear(); typeBar.reset();
    auto& b = *block;
    const auto col = b.getColour();
    auto changed = [this] { resized(); repaint(); };

    for (int i = 0; i < (int) b.params.size(); ++i)
    {
        if (skipParam (i)) continue;
        auto& prm = *b.params[(size_t) i];
        if (prm.id == "type" && prm.kind == Param::Kind::Choice)
        {
            typeBar = std::make_unique<ParamSegments> (proc, b, i, col);
            typeBar->onChange = changed;
            addAndMakeVisible (*typeBar);
        }
        else if (prm.kind == Param::Kind::Choice)
        {
            auto* c = combos.add (new ParamCombo (proc, b, i));
            c->onChange = changed;
            c->getProperties().set ("index", i);
            addAndMakeVisible (c);
        }
        else if (prm.kind == Param::Kind::Toggle)
        {
            auto* s = switches.add (new ParamSwitch (proc, b, i));
            s->onChange = changed;
            s->getProperties().set ("index", i);
            addAndMakeVisible (s);
        }
        else
        {
            auto* k = knobs.add (new ParamKnob (proc, b, i, prm.realControl ? col : theme::shaping));
            k->getProperties().set ("index", i);
            addAndMakeVisible (k);
        }
    }
}

void BlockEditor::paint (Graphics&) {}

void BlockEditor::resized()
{
    auto r = getLocalBounds().reduced (18, 12);
    auto& b = *block;
    auto visible = [&b] (Component* c)
    {
        const int i = (int) c->getProperties()["index"];
        auto& prm = *b.params[(size_t) i];
        return prm.visible == nullptr || prm.visible (b);
    };

    if (typeBar != nullptr)
    {
        const int h = typeBar->preferredHeight (r.getWidth());
        typeBar->setBounds (r.removeFromTop (h));
        r.removeFromTop (14);
    }
    r = layoutSpecial (r);

    // right column: choices and switches
    int nRight = 0;
    for (auto* c : combos) if (visible (c)) ++nRight;
    for (auto* s : switches) if (visible (s)) ++nRight;
    if (nRight > 0)
    {
        auto col = r.removeFromRight (230);
        r.removeFromRight (16);
        for (auto* c : combos)
        {
            c->setVisible (visible (c));
            if (c->isVisible()) { c->setBounds (col.removeFromTop (54)); col.removeFromTop (4); }
        }
        for (auto* s : switches)
        {
            s->setVisible (visible (s));
            if (s->isVisible()) { s->setBounds (col.removeFromTop (32)); col.removeFromTop (4); }
        }
    }
    else
    {
        for (auto* c : combos) c->setVisible (false);
        for (auto* s : switches) s->setVisible (false);
    }

    // knobs flow, shrinking if there are many
    Array<ParamKnob*> vk;
    for (auto* k : knobs) { k->setVisible (visible (k)); if (k->isVisible()) vk.add (k); }
    int kw = 88, kh = 104;
    auto fits = [&] (int w, int h) { const int perRow = jmax (1, r.getWidth() / w); const int rowsNeeded = (vk.size() + perRow - 1) / perRow; return rowsNeeded * h <= r.getHeight(); };
    if (! fits (kw, kh)) { kw = 72; kh = 90; }
    if (! fits (kw, kh)) { kw = 62; kh = 80; }
    const int perRow = jmax (1, r.getWidth() / kw);
    for (int i = 0; i < vk.size(); ++i)
        vk[i]->setBounds (r.getX() + (i % perRow) * kw, r.getY() + (i / perRow) * kh, kw - 6, kh - 4);
}

void BlockEditor::sync()
{
    for (auto* k : knobs) k->sync();
    for (auto* c : combos) c->sync();
    for (auto* s : switches) s->sync();
    if (typeBar != nullptr) typeBar->repaint();
}

//==============================================================================
NamEditor::NamEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b)
    : BlockEditor (p, b), nam (dynamic_cast<NamBlock&> (*b))
{
    addAndMakeVisible (load);
    addAndMakeVisible (clear);
    load.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose a NAM capture", proc.library.getFolder(), "*.nam");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            int r, c;
            if (proc.engine.findCell (nam.id, r, c))
            {
                const auto err = proc.loadFileIntoBlock (f, r, c);
                if (err.isNotEmpty() && onError) onError (err);
            }
            repaint();
            if (onStructureChanged) onStructureChanged();
        });
    };
    clear.onClick = [this] { nam.unload(); proc.dirty = true; repaint(); if (onStructureChanged) onStructureChanged(); };
    lastPath = nam.getPath();
}

Rectangle<int> NamEditor::layoutSpecial (Rectangle<int> r)
{
    info = r.removeFromLeft (330);
    load.setBounds (info.getX(), info.getBottom() - 32, 130, 32);
    clear.setBounds (info.getX() + 138, info.getBottom() - 32, 96, 32);
    r.removeFromLeft (24);
    auto hdr = r.removeFromTop (22);
    ignoreUnused (hdr);
    return r;
}

void NamEditor::paint (Graphics& g)
{
    auto r = info.withTrimmedBottom (40);
    if (! nam.isLoaded())
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (14.0f));
        g.drawFittedText ("No capture loaded.\nDrag one from the library onto this block, or use Load capture.", r, Justification::topLeft, 4);
    }
    else
    {
        g.setColour (theme::text);
        g.setFont (theme::font (19.0f, 2));
        g.drawFittedText (nam.getCaptureName(), r.removeFromTop (28), Justification::centredLeft, 1);
        g.setColour (theme::muted);
        g.setFont (theme::font (13.5f));
        g.drawText ("Captured by " + (nam.getCreator().isNotEmpty() ? nam.getCreator() : String ("unknown (not in file)")), r.removeFromTop (22), Justification::centredLeft);
        if (nam.getGear().isNotEmpty()) g.drawText ("Gear: " + nam.getGear(), r.removeFromTop (20), Justification::centredLeft);
        g.drawText ("Gain: " + gainLevelName (gainLevelFromToneType (nam.getToneType())), r.removeFromTop (20), Justification::centredLeft);
        g.drawText ("File: " + File (nam.getPath()).getFileName(), r.removeFromTop (20), Justification::centredLeft);
        r.removeFromTop (8);

        auto badge = [&g] (Rectangle<int>& row, const String& t, Colour bgc, Colour fg)
        {
            const int w = (int) GlyphArrangement::getStringWidth (theme::font (12.0f, 1), t) + 18;
            auto b = row.removeFromLeft (w);
            row.removeFromLeft (6);
            g.setColour (bgc); g.fillRoundedRectangle (b.toFloat(), 6.0f);
            g.setColour (fg); g.setFont (theme::font (12.0f, 1)); g.drawText (t, b, Justification::centred);
        };
        auto row = r.removeFromTop (24);
        badge (row, "Standard capture", theme::raised, Colour (0xffc9ccd1));
        if (nam.includesCab()) badge (row, "Includes cab", Colour (0xff2a2140), Colour (0xffc4b3f0));
        if (nam.sampleRateMismatch())
        {
            r.removeFromTop (8);
            g.setColour (theme::warn);
            g.setFont (theme::font (13.0f, 1));
            g.drawFittedText ("Trained at " + String (nam.getExpectedRate() / 1000.0, 1) + " kHz. Set your interface to match in Settings.", r.removeFromTop (36), Justification::topLeft, 2);
        }
    }

    // label above the knobs
    if (! knobs.isEmpty())
    {
        auto k = knobs.getFirst()->getBounds();
        g.setColour (theme::faint);
        g.setFont (theme::font (11.5f, 2));
        g.drawText ("ADDED SHAPING  ·  the capture itself is never changed", k.getX(), k.getY() - 24, 420, 18, Justification::centredLeft);
    }
}

void NamEditor::sync()
{
    BlockEditor::sync();
    if (nam.getPath() != lastPath) { lastPath = nam.getPath(); repaint(); }
}

//==============================================================================
CabEditor::CabEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b)
    : BlockEditor (p, b), cab (dynamic_cast<CabBlock&> (*b))
{
    // the cab has its own layout; hide the generic controls
    knobs.clear(); combos.clear(); switches.clear();

    for (int k = 0; k < CabBlock::numIrs; ++k)
    {
        auto& row = rows[(size_t) k];
        const int base = k * 5;
        auto& lvl = *cab.params[(size_t) base];
        row.level = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::TextBoxRight);
        row.level->setTextBoxStyle (Slider::TextBoxRight, false, 70, 22);
        row.level->setColour (Slider::trackColourId, Block::colour (Block::Type::Cab));
        row.level->setRange (lvl.range.start, lvl.range.end, 0.1);
        row.level->setValue (lvl.get(), dontSendNotification);
        row.level->setDoubleClickReturnValue (true, lvl.def);
        row.level->textFromValueFunction = [] (double v) { return (v > 0.05 ? "+" : "") + String (v, 1) + " dB"; };
        row.level->updateText();
        row.level->onValueChange = [this, base, k] { cab.params[(size_t) base]->set ((float) rows[(size_t) k].level->getValue()); proc.dirty = true; };

        auto& al = *cab.params[(size_t) base + 3];
        row.align = std::make_unique<Slider> (Slider::IncDecButtons, Slider::TextBoxLeft);
        row.align->setTextBoxStyle (Slider::TextBoxLeft, false, 50, 26);
        row.align->setRange (al.range.start, al.range.end, 1);
        row.align->setValue (al.get(), dontSendNotification);
        row.align->setTextValueSuffix (" smp");
        row.align->setTooltip ("Delay this IR by a few samples to line it up with the others");
        row.align->onValueChange = [this, base, k] { cab.params[(size_t) base + 3]->set ((float) rows[(size_t) k].align->getValue()); proc.dirty = true; };

        row.pan = std::make_unique<ParamKnob> (proc, cab, base + 1, Block::colour (Block::Type::Cab));

        auto toggle = [this] (int idx, const String& text, Colour on)
        {
            auto t = std::make_unique<TextButton> (text);
            t->setClickingTogglesState (true);
            t->setColour (TextButton::buttonOnColourId, on);
            t->setToggleState (cab.params[(size_t) idx]->on(), dontSendNotification);
            auto* raw = t.get();
            t->onClick = [this, idx, raw] { cab.params[(size_t) idx]->set (raw->getToggleState() ? 1.0f : 0.0f); proc.dirty = true; };
            return t;
        };
        row.polarity = toggle (base + 2, String (CharPointer_UTF8 ("\xc3\x98")), Block::colour (Block::Type::Cab));
        row.polarity->setTooltip ("Invert polarity");
        row.mute = toggle (base + 4, "Mute", theme::muted);

        row.mic = std::make_unique<TextEditor>();
        row.mic->setTextToShowWhenEmpty ("Mic tag, e.g. SM57", theme::faint);
        row.mic->setFont (theme::font (13.0f));
        row.mic->setIndents (8, 6);
        row.mic->onReturnKey = row.mic->onFocusLost = [this, k]
        {
            const auto path = cab.getIrPath (k);
            if (path.isNotEmpty()) proc.library.setMicTag (path, rows[(size_t) k].mic->getText().trim());
        };

        row.load = std::make_unique<TextButton> ("Load");
        row.load->onClick = [this, k]
        {
            chooser = std::make_unique<FileChooser> ("Choose a cabinet IR", proc.library.getFolder(), "*.wav");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, k] (const FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                const auto err = cab.loadIr (k, f);
                if (err.isNotEmpty() && onError) onError (err);
                proc.dirty = true;
                sync(); repaint();
                if (onStructureChanged) onStructureChanged();
            });
        };
        row.clear = std::make_unique<TextButton> ("Remove");
        row.clear->onClick = [this, k] { cab.clearIr (k); proc.dirty = true; repaint(); if (onStructureChanged) onStructureChanged(); };

        for (auto* c : std::initializer_list<Component*> { row.level.get(), row.align.get(), row.pan.get(), row.polarity.get(), row.mute.get(), row.mic.get(), row.load.get(), row.clear.get() })
            addAndMakeVisible (c);
    }
    lowCut = std::make_unique<ParamKnob> (proc, cab, CabBlock::numIrs * 5, Block::colour (Block::Type::Cab));
    highCut = std::make_unique<ParamKnob> (proc, cab, CabBlock::numIrs * 5 + 1, Block::colour (Block::Type::Cab));
    addAndMakeVisible (*lowCut);
    addAndMakeVisible (*highCut);
    sync();
}

Rectangle<int> CabEditor::layoutSpecial (Rectangle<int> r)
{
    auto right = r.removeFromRight (200);
    lowCut->setBounds (right.getX(), right.getY() + 20, 92, 104);
    highCut->setBounds (right.getX() + 100, right.getY() + 20, 92, 104);
    r.removeFromRight (16);

    r.removeFromTop (20);
    const int rowH = jmin (76, (r.getHeight() - 24) / CabBlock::numIrs);
    for (int k = 0; k < CabBlock::numIrs; ++k)
    {
        auto& row = rows[(size_t) k];
        row.area = r.removeFromTop (rowH - 6);
        r.removeFromTop (6);
        auto a = row.area.reduced (12, 6);
        auto left = a.removeFromLeft (300);
        auto lower = left.removeFromBottom (28);
        row.mic->setBounds (lower.removeFromLeft (150));
        lower.removeFromLeft (6);
        row.load->setBounds (lower.removeFromLeft (62));
        lower.removeFromLeft (6);
        row.clear->setBounds (lower.removeFromLeft (78));
        a.removeFromLeft (16);
        const int cy = a.getCentreY();
        row.level->setBounds (a.getX(), cy - 14, jmin (260, a.getWidth() - 330), 28);
        int x = row.level->getRight() + 12;
        row.pan->setBounds (x, a.getY() - 4, 64, a.getHeight() + 8);
        x += 72;
        row.align->setBounds (x, cy - 15, 120, 30);
        x += 130;
        row.polarity->setBounds (x, cy - 15, 40, 30);
        x += 46;
        row.mute->setBounds (x, cy - 15, 64, 30);
    }
    return {};
}

void CabEditor::paint (Graphics& g)
{
    if (rows[0].area.isEmpty()) return;
    g.setColour (theme::faint);
    g.setFont (theme::font (11.5f, 2));
    g.drawText ("IR AND MIC TAG", rows[0].area.getX() + 12, rows[0].area.getY() - 20, 200, 16, Justification::centredLeft);
    g.drawText ("LEVEL", rows[0].level->getX(), rows[0].area.getY() - 20, 100, 16, Justification::centredLeft);
    g.drawText ("ALIGN", rows[0].align->getX(), rows[0].area.getY() - 20, 100, 16, Justification::centredLeft);
    for (int k = 0; k < CabBlock::numIrs; ++k)
    {
        auto& row = rows[(size_t) k];
        g.setColour (theme::card);
        g.fillRoundedRectangle (row.area.toFloat(), 10.0f);
        const bool loaded = cab.isIrLoaded (k);
        const auto* it = proc.library.findByPath (cab.getIrPath (k));
        auto t = row.area.reduced (12, 6).removeFromLeft (300).removeFromTop (20);
        g.setColour (loaded ? theme::text : theme::faint);
        g.setFont (theme::font (14.0f, loaded ? 1 : 0));
        g.drawFittedText (loaded ? (it != nullptr ? it->name : File (cab.getIrPath (k)).getFileNameWithoutExtension()) : String ("Empty IR slot " + String (k + 1)), t, Justification::centredLeft, 1);
        if (loaded && it != nullptr)
        {
            g.setColour (theme::muted);
            g.setFont (theme::font (12.0f));
            const auto credit = it->creditLine();
            g.drawFittedText (credit, t.translated (0, 18), Justification::centredLeft, 1);
        }
    }
    g.setColour (theme::faint);
    g.setFont (theme::font (12.0f));
    g.drawText ("Mic tags are labels for sorting and search. They don't change the sound.", rows.back().area.getX(), rows.back().area.getBottom() + 4, 600, 18, Justification::centredLeft);
}

void CabEditor::sync()
{
    for (int k = 0; k < CabBlock::numIrs; ++k)
    {
        auto& row = rows[(size_t) k];
        const int base = k * 5;
        if (! row.level->isMouseButtonDown()) row.level->setValue (cab.params[(size_t) base]->get(), dontSendNotification);
        row.pan->sync();
        row.polarity->setToggleState (cab.params[(size_t) base + 2]->on(), dontSendNotification);
        row.mute->setToggleState (cab.params[(size_t) base + 4]->on(), dontSendNotification);
        const bool loaded = cab.isIrLoaded (k);
        for (auto* c : std::initializer_list<Component*> { row.level.get(), row.align.get(), row.pan.get(), row.polarity.get(), row.mute.get(), row.mic.get(), row.clear.get() })
            c->setEnabled (loaded);
        if (! row.mic->hasKeyboardFocus (true))
        {
            const auto* it = proc.library.findByPath (cab.getIrPath (k));
            const auto tag = it != nullptr ? it->micTag : String();
            if (row.mic->getText() != tag) row.mic->setText (tag, false);
        }
    }
    lowCut->sync(); highCut->sync();
}

//==============================================================================
PeqEditor::PeqEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b)
    : BlockEditor (p, b), peq (dynamic_cast<ParametricEQBlock&> (*b)) {}

Rectangle<int> PeqEditor::layoutSpecial (Rectangle<int> r)
{
    graph = r.removeFromLeft (jmin (520, r.getWidth() / 2)).reduced (0, 2);
    r.removeFromLeft (20);
    return r;
}

float PeqEditor::xForFreq (float f) const { return (float) graph.getX() + graph.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
float PeqEditor::freqForX (float x) const { return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - graph.getX()) / (float) graph.getWidth())); }
float PeqEditor::yForDb (float db) const { return (float) graph.getCentreY() - db / 18.0f * graph.getHeight() * 0.5f; }
float PeqEditor::dbForY (float y) const { return ((float) graph.getCentreY() - y) / (graph.getHeight() * 0.5f) * 18.0f; }

Point<float> PeqEditor::bandPoint (int b) const
{
    if (b == -2) return { xForFreq (peq.params[1]->get()), yForDb (0) };                  // low cut
    if (b == -3) return { xForFreq (peq.params[3]->get()), yForDb (0) };                  // high cut
    return { xForFreq (peq.params[(size_t) (4 + b * 3)]->get()), yForDb (peq.params[(size_t) (5 + b * 3)]->get()) };
}

void PeqEditor::paint (Graphics& g)
{
    if (graph.isEmpty()) return;
    g.setColour (theme::card);
    g.fillRoundedRectangle (graph.toFloat(), 10.0f);
    g.setColour (theme::line);
    for (float f : { 100.0f, 1000.0f, 10000.0f }) g.drawVerticalLine ((int) xForFreq (f), (float) graph.getY(), (float) graph.getBottom());
    for (float d : { -12.0f, 0.0f, 12.0f }) g.drawHorizontalLine ((int) yForDb (d), (float) graph.getX(), (float) graph.getRight());
    g.setColour (theme::faint);
    g.setFont (theme::font (11.0f));
    g.drawText ("100", (int) xForFreq (100) + 4, graph.getBottom() - 16, 40, 14, Justification::left);
    g.drawText ("1k", (int) xForFreq (1000) + 4, graph.getBottom() - 16, 40, 14, Justification::left);
    g.drawText ("10k", (int) xForFreq (10000) + 4, graph.getBottom() - 16, 40, 14, Justification::left);

    Path curve;
    for (int x = graph.getX(); x <= graph.getRight(); x += 2)
    {
        const float y = jlimit ((float) graph.getY(), (float) graph.getBottom(), yForDb ((float) peq.responseDb (freqForX ((float) x))));
        if (x == graph.getX()) curve.startNewSubPath ((float) x, y); else curve.lineTo ((float) x, y);
    }
    Path fill (curve);
    fill.lineTo ((float) graph.getRight(), yForDb (0)); fill.lineTo ((float) graph.getX(), yForDb (0)); fill.closeSubPath();
    g.setColour (Block::colour (Block::Type::ParametricEQ).withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (Block::colour (Block::Type::ParametricEQ));
    g.strokePath (curve, PathStrokeType (2.0f));

    auto node = [&] (Point<float> pt, const String& label, bool active)
    {
        g.setColour (active ? Block::colour (Block::Type::ParametricEQ) : theme::path);
        g.fillEllipse (Rectangle<float> (18, 18).withCentre (pt));
        g.setColour (Colour (0xff0d1f0e));
        g.setFont (theme::font (10.5f, 2));
        g.drawText (label, Rectangle<float> (18, 18).withCentre (pt), Justification::centred);
    };
    node (bandPoint (-2), "L", peq.params[0]->on());
    node (bandPoint (-3), "H", peq.params[2]->on());
    for (int b = 0; b < ParametricEQBlock::numBands; ++b) node (bandPoint (b), String (b + 1), true);
}

void PeqEditor::mouseDown (const MouseEvent& e)
{
    dragBand = -1;
    float best = 14.0f;
    for (int b : { -2, -3, 0, 1, 2, 3, 4 })
    {
        const float d = bandPoint (b).getDistanceFrom (e.position);
        if (d < best) { best = d; dragBand = b; }
    }
}

void PeqEditor::mouseDrag (const MouseEvent& e)
{
    if (dragBand == -1 + 0 && dragBand != 0) {}
    const float f = jlimit (20.0f, 20000.0f, freqForX (e.position.x));
    if (dragBand == -2) peq.params[1]->set (f);
    else if (dragBand == -3) peq.params[3]->set (f);
    else if (dragBand >= 0)
    {
        peq.params[(size_t) (4 + dragBand * 3)]->set (f);
        peq.params[(size_t) (5 + dragBand * 3)]->set (jlimit (-15.0f, 15.0f, dbForY (e.position.y)));
    }
    else return;
    proc.dirty = true;
    BlockEditor::sync();
    repaint (graph);
}

void PeqEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    for (int b = 0; b < ParametricEQBlock::numBands; ++b)
        if (bandPoint (b).getDistanceFrom (e.position) < 16.0f)
        {
            auto& q = *peq.params[(size_t) (6 + b * 3)];
            q.set (q.get() * (w.deltaY > 0 ? 1.12f : 0.89f));
            proc.dirty = true;
            BlockEditor::sync();
            repaint (graph);
            return;
        }
}

void PeqEditor::sync()
{
    BlockEditor::sync();
    String snap;
    for (auto& prm : peq.params) snap << prm->get() << ",";
    if (snap != lastSnapshot) { lastSnapshot = snap; repaint (graph); }
}

//==============================================================================
void CompEditor::paint (Graphics& g)
{
    if (meter.isEmpty()) return;
    auto* comp = dynamic_cast<CompressorBlock*> (block.get());
    g.setColour (theme::card);
    g.fillRoundedRectangle (meter.toFloat(), 5.0f);
    const float gr = comp != nullptr ? jlimit (0.0f, 24.0f, -comp->reductionDb.load()) : 0.0f;
    auto bar = meter.toFloat().reduced (3);
    g.setColour (theme::amber);
    g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * gr / 24.0f), 3.0f);
    g.setColour (theme::faint);
    g.setFont (theme::font (10.5f, 2));
    g.drawText ("GR", meter.getX() - 4, meter.getBottom() + 2, meter.getWidth() + 8, 14, Justification::centred);
}

//==============================================================================
std::unique_ptr<BlockEditor> createEditorFor (NAMHomeProcessor& p, std::shared_ptr<Block> b)
{
    switch (b->getType())
    {
        case Block::Type::NamAmp:
        case Block::Type::NamPedal:     return std::make_unique<NamEditor> (p, b);
        case Block::Type::Cab:          return std::make_unique<CabEditor> (p, b);
        case Block::Type::ParametricEQ: return std::make_unique<PeqEditor> (p, b);
        case Block::Type::Compressor:   return std::make_unique<CompEditor> (p, b);
        default:                        return std::make_unique<BlockEditor> (p, b);
    }
}
