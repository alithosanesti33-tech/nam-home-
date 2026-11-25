#include "GridView.h"
#include "../PluginProcessor.h"

using namespace juce;

GridView::GridView (NAMHomeProcessor& p) : proc (p) {}

bool GridView::rowShown (int r) const
{
    if (r == 0 || dragging || openedRows.count (r) > 0) return true;
    int f, l;
    return proc.engine.rowSpan (r, f, l) || proc.engine.rowInput[r].load() > 0;
}

int GridView::numShownRows() const
{
    int n = 0;
    for (int r = 0; r < Engine::rows; ++r) if (rowShown (r)) ++n;
    return n;
}

int GridView::displayIndex (int r) const
{
    int d = 0;
    for (int k = 0; k < r; ++k) if (rowShown (k)) ++d;
    return d;
}

Rectangle<float> GridView::addPathButton() const
{
    if (numShownRows() >= Engine::rows || dragging) return {};
    const int last = [this] { int l = 0; for (int r = 0; r < Engine::rows; ++r) if (rowShown (r)) l = r; return l; }();
    const auto cb = cellBounds (last, 0);
    return { cb.getX(), cb.getBottom() + jmin (24.0f, gap * 1.2f), 128.0f, 28.0f };
}

void GridView::resized()
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float sideRoom = 100.0f;
    tile = jlimit (58.0f, 118.0f, (w - 2 * sideRoom) / (Engine::cols + (Engine::cols - 1) * 0.2f));
    gap = tile * 0.2f;
    const int shown = numShownRows();
    rowPitch = jmin (tile + gap * 1.6f, (h - 60.0f) / Engine::rows);
    tile = jmin (tile, rowPitch - 18.0f);
    const float gridW = Engine::cols * tile + (Engine::cols - 1) * gap;
    x0 = (w - gridW) * 0.5f;
    const float used = shown * rowPitch - (rowPitch - tile) + (shown < Engine::rows ? 52.0f : 0.0f);
    y0 = jmax (8.0f, (h - used) * 0.5f);
}

Rectangle<float> GridView::cellBounds (int r, int c) const
{
    if (! rowShown (r)) return {};
    return { x0 + c * (tile + gap), y0 + displayIndex (r) * rowPitch, tile, tile };
}

Rectangle<float> GridView::inputChip (int r) const
{
    const auto cb = cellBounds (r, 0);
    return { cb.getX() - 92.0f, cb.getCentreY() - 13.0f, 58.0f, 26.0f };
}

void GridView::showInputMenu (int row)
{
    PopupMenu m;
    m.addSectionHeader ("Row " + String (row + 1) + " input");
    const int cur = proc.engine.rowInput[row].load();
    if (row > 0) m.addItem (1, "From main path (split and merge)", true, cur == 0);
    m.addItem (2, "Interface input 1 (separate rig)", true, cur == 1);
    m.addItem (3, "Interface input 2 (separate rig)", true, cur == 2);
    int f, l;
    const bool empty = ! proc.engine.rowSpan (row, f, l);
    if (row > 0) { m.addSeparator(); m.addItem (4, "Remove this path", empty); }
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (inputChip (row).toNearestInt())), [this, row] (int r)
    {
        if (r == 0) return;
        if (r == 4) { openedRows.erase (row); proc.engine.rowInput[row] = 0; resized(); repaint(); if (onChanged) onChanged(); return; }
        proc.engine.rowInput[row] = r - 1;
        proc.dirty = true;
        repaint();
        if (onChanged) onChanged();
    });
}

Point<int> GridView::cellAt (Point<int> p) const
{
    for (int r = 0; r < Engine::rows; ++r)
        if (rowShown (r))
            for (int c = 0; c < Engine::cols; ++c)
                if (cellBounds (r, c).expanded (gap * 0.5f, 6).contains (p.toFloat())) return { c, r };
    return { -1, -1 };
}

float GridView::splitX (int row) const
{
    int f, l;
    proc.engine.rowSpan (row, f, l);
    return cellBounds (0, f).getX() - gap * 0.5f;
}

float GridView::mergeX (int row) const
{
    int f, l;
    proc.engine.rowSpan (row, f, l);
    return cellBounds (0, l).getRight() + gap * 0.5f;
}

int GridView::mergeNodeAt (Point<int> p) const
{
    const float cy = cellBounds (0, 0).getCentreY();
    for (int r = 1; r < Engine::rows; ++r)
    {
        int f, l;
        if (! proc.engine.rowSpan (r, f, l)) continue;
        const bool own = proc.engine.rowIsIndependent (r);
        ignoreUnused (cy);
        const float rowY = cellBounds (r, 0).getCentreY();
        const Point<float> node = own ? Point<float> (cellBounds (r, Engine::cols - 1).getRight() + 22.0f, rowY)
                                      : Point<float> (mergeX (r), rowY - 14.0f);
        if (node.getDistanceFrom (p.toFloat()) < 11.0f) return r;
    }
    return -1;
}

void GridView::drawTile (Graphics& g, Block& b, Rectangle<float> r, bool isSelected)
{
    const auto col = b.getColour();
    const bool on = b.enabled.load();
    Graphics::ScopedSaveState ss (g);
    if (! on) g.setOpacity (0.38f);

    // body
    g.setColour (theme::raised);
    g.fillRoundedRectangle (r, 12.0f);
    // colour strip
    Path top;
    const float stripH = jmax (18.0f, tile * 0.22f);
    top.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), stripH, 12.0f, 12.0f, true, true, false, false);
    g.setColour (col);
    g.fillPath (top);
    g.setColour (Colour (0xff121212));
    g.setFont (theme::font (jmin (12.0f, tile * 0.125f), 2));
    g.drawText (b.getTypeName().toUpperCase(), r.withHeight (stripH).reduced (8, 0), Justification::centredLeft);

    // text
    auto body = r.withTrimmedTop (stripH + 5).reduced (8, 2);
    g.setColour (theme::text);
    g.setFont (theme::font (jmin (13.5f, tile * 0.14f), 1));
    const auto title = b.getTitle();
    g.drawFittedText (title, body.removeFromTop (body.getHeight() * 0.62f).toNearestInt(), Justification::topLeft, 3, 0.85f);
    g.setColour (theme::muted);
    g.setFont (theme::font (jmin (11.5f, tile * 0.12f)));
    g.drawFittedText (b.getSubtitle(), body.toNearestInt(), Justification::topLeft, 1);

    if (auto* nb = dynamic_cast<NamBlock*> (&b))
        if (nb->amp && nb->isLoaded())
        {
            // gain strip: blue (clean) -> yellow (crunch) -> red (high gain), from the file's tone type
            auto strip = Rectangle<float> (r.getX() + 8, r.getBottom() - 9, r.getWidth() - 16, 4);
            g.setColour (gainLevelColour (gainLevelFromToneType (nb->getToneType())));
            g.fillRoundedRectangle (strip, 2.0f);
        }

    if (! on)
    {
        g.setOpacity (1.0f);
        g.setColour (theme::faint);
        g.setFont (theme::font (10.5f, 2));
        g.drawText ("OFF", r.reduced (8, 6), Justification::bottomRight);
    }
    if (isSelected)
    {
        g.setOpacity (1.0f);
        g.setColour (Colours::white);
        g.drawRoundedRectangle (r.expanded (2.5f), 14.0f, 2.0f);
    }
}

void GridView::paint (Graphics& g)
{
    auto& e = proc.engine;
    const float cy0 = cellBounds (0, 0).getCentreY();
    const float left = x0 - 34, right = x0 + Engine::cols * (tile + gap) - gap + 46;

    // main path
    g.setColour (theme::path);
    g.drawLine (left, cy0, right, cy0, 2.5f);
    g.setColour (theme::faint);
    g.setFont (theme::font (11.0f, 2));
    g.drawText ("OUT", Rectangle<float> (right + 4, cy0 - 9, 36, 18), Justification::centredLeft);
    g.fillEllipse (left - 3, cy0 - 3, 6, 6);
    g.fillEllipse (right - 3, cy0 - 3, 6, 6);

    // parallel rows: split down, run along, merge back up; rows with their own input run end to end
    for (int r = 1; r < Engine::rows; ++r)
    {
        int f, l;
        if (! e.rowSpan (r, f, l)) continue;
        const float cy = cellBounds (r, 0).getCentreY();
        Path p;
        if (e.rowIsIndependent (r))
        {
            const float nodeX = cellBounds (r, Engine::cols - 1).getRight() + 22.0f;
            p.startNewSubPath (left, cy);
            p.lineTo (nodeX, cy);
            p.lineTo (right - 10, cy); p.quadraticTo (right, cy, right, cy - 10);
            p.lineTo (right, cy0);
        }
        else
        {
            const float sx = splitX (r), mx = mergeX (r);
            p.startNewSubPath (sx, cy0);
            p.lineTo (sx, cy - 10); p.quadraticTo (sx, cy, sx + 10, cy);
            p.lineTo (mx - 10, cy); p.quadraticTo (mx, cy, mx, cy - 10);
            p.lineTo (mx, cy0);
        }
        g.setColour (theme::path);
        g.strokePath (p, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    for (int r = 1; r < Engine::rows; ++r)
    {
        int f, l;
        if (! e.rowSpan (r, f, l)) continue;
        Point<float> node;
        if (e.rowIsIndependent (r)) node = { cellBounds (r, Engine::cols - 1).getRight() + 22.0f, cellBounds (r, 0).getCentreY() };
        else
        {
            node = { mergeX (r), cellBounds (r, 0).getCentreY() - 14.0f };   // on this row's own return line
            g.setColour (theme::path);
            g.fillEllipse (splitX (r) - 4, cy0 - 4, 8, 8);
            g.fillEllipse (mergeX (r) - 4, cy0 - 4, 8, 8);
        }
        g.setColour (theme::bg);
        g.fillEllipse (Rectangle<float> (14, 14).withCentre (node));
        g.setColour (theme::accent);
        g.drawEllipse (Rectangle<float> (12, 12).withCentre (node), 2.0f);
    }

    // input chips at the start of each row
    for (int r = 0; r < Engine::rows; ++r)
    {
        if (! rowShown (r)) continue;
        const int in = e.rowInput[r].load();
        auto chip = inputChip (r);
        g.setColour (in > 0 ? theme::raised : theme::bg);
        g.fillRoundedRectangle (chip, 8.0f);
        g.setColour (in > 0 ? theme::line.brighter (0.2f) : theme::line);
        g.drawRoundedRectangle (chip.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (in > 0 ? theme::text : theme::faint);
        g.setFont (theme::font (11.5f, 2));
        g.drawText (in > 0 ? "IN " + String (in) : String ("SPLIT"), chip, Justification::centred);
    }

    // add a parallel path (rows stay hidden until you ask for one, like a single-chain view)
    if (auto ab = addPathButton(); ! ab.isEmpty())
    {
        const bool hot = ab.contains (getMouseXYRelative().toFloat());
        g.setColour (hot ? theme::hover : theme::bg);
        g.fillRoundedRectangle (ab, 8.0f);
        g.setColour (hot ? theme::muted : theme::line.brighter (0.15f));
        g.drawRoundedRectangle (ab.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (hot ? theme::text : theme::faint);
        g.setFont (theme::font (12.0f, 2));
        g.drawText ("+  Add path", ab, Justification::centred);
    }

    // cells
    for (int r = 0; r < Engine::rows; ++r)
        for (int c = 0; c < Engine::cols; ++c)
        {
            if (! rowShown (r)) continue;
            auto cb = cellBounds (r, c);
            auto b = e.get (r, c);
            const bool isSel = selected == Point<int> (c, r);
            if (b != nullptr) { drawTile (g, *b, cb, isSel); }
            else
            {
                const bool hot = hoverCell == Point<int> (c, r) || isSel;
                g.setColour (theme::bg);
                g.fillRoundedRectangle (cb, 12.0f);
                Path p;
                p.addRoundedRectangle (cb.reduced (1), 12.0f);
                float dash[] = { 5.0f, 5.0f };
                PathStrokeType (1.2f).createDashedStroke (p, p, dash, 2);
                g.setColour (hot ? theme::muted : (r == 0 ? theme::line.brighter (0.15f) : theme::line));
                g.fillPath (p);
                if (hot || r == 0)
                {
                    g.setColour (hot ? theme::text : theme::faint);
                    const auto cc = cb.getCentre();
                    g.drawLine (cc.x - 6, cc.y, cc.x + 6, cc.y, 1.6f);
                    g.drawLine (cc.x, cc.y - 6, cc.x, cc.y + 6, 1.6f);
                }
                if (isSel)
                {
                    g.setColour (Colours::white.withAlpha (0.8f));
                    g.drawRoundedRectangle (cb.expanded (2.5f), 14.0f, 1.5f);
                }
            }
            if (dropCell == Point<int> (c, r))
            {
                g.setColour (theme::accent);
                g.drawRoundedRectangle (cb.expanded (3.0f), 14.0f, 2.5f);
            }
        }
}

void GridView::mouseMove (const MouseEvent& e)
{
    const auto c = cellAt (e.getPosition());
    setMouseCursor (mergeNodeAt (e.getPosition()) > 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    if (c != hoverCell) { hoverCell = c; repaint(); }
    if (auto ab = addPathButton(); ! ab.isEmpty()) repaint (ab.expanded (2).toNearestInt());
}

void GridView::mouseDown (const MouseEvent& e)
{
    dragStarted = false;
    if (auto ab = addPathButton(); ab.contains (e.position))
    {
        for (int r = 1; r < Engine::rows; ++r)
            if (! rowShown (r)) { openedRows.insert (r); break; }
        pressCell = { -1, -1 };
        resized(); repaint();
        return;
    }
    for (int r = 0; r < Engine::rows; ++r)
        if (rowShown (r) && inputChip (r).contains (e.position)) { pressCell = { -1, -1 }; showInputMenu (r); return; }
    const int node = mergeNodeAt (e.getPosition());
    if (node > 0)
    {
        if (onMergeClicked)
        {
            const float cy0 = cellBounds (0, 0).getCentreY();
            ignoreUnused (cy0);
            const bool own = proc.engine.rowIsIndependent (node);
            const float nx = own ? cellBounds (node, Engine::cols - 1).getRight() + 22.0f : mergeX (node);
            const float ny = cellBounds (node, 0).getCentreY() - (own ? 0.0f : 14.0f);
            onMergeClicked (node, Rectangle<int> ((int) nx - 8, (int) ny - 8, 16, 16));
        }
        pressCell = { -1, -1 };
        return;
    }
    pressCell = cellAt (e.getPosition());
    if (pressCell.x < 0) return;
    selected = pressCell;
    repaint();
    if (e.mods.isPopupMenu()) { showContextMenu (pressCell); return; }
    if (onSelect) onSelect (pressCell);
}

void GridView::mouseDrag (const MouseEvent& e)
{
    if (pressCell.x < 0 || dragStarted || e.getDistanceFromDragStart() < 6) return;
    if (proc.engine.get (pressCell.y, pressCell.x) == nullptr) return;
    if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        auto area = cellBounds (pressCell.y, pressCell.x).toNearestInt();
        auto snapshot = createComponentSnapshot (area).rescaled (area.getWidth(), area.getHeight());
        c->startDragging ("cell:" + String (pressCell.y) + ":" + String (pressCell.x), this, ScaledImage (snapshot), true);
    }
}

void GridView::mouseUp (const MouseEvent& e)
{
    if (dragStarted || e.mods.isPopupMenu() || pressCell.x < 0) return;
    if (proc.engine.get (pressCell.y, pressCell.x) == nullptr && cellAt (e.getPosition()) == pressCell)
        showAddMenu (pressCell);
}

void GridView::mouseDoubleClick (const MouseEvent& e)
{
    const auto c = cellAt (e.getPosition());
    if (c.x < 0) return;
    if (auto b = proc.engine.get (c.y, c.x))
    {
        b->enabled = ! b->enabled.load();
        proc.dirty = true;
        repaint();
        if (onChanged) onChanged();
    }
}

void GridView::showAddMenu (Point<int> cell)
{
    PopupMenu m;
    StringArray cats { "Amps", "Pedals", "Cabs", "EQ", "Effects", "Synthesizers", "Saxophone", "Utility" };
    std::map<String, PopupMenu> subs;
    for (auto t : Block::allTypes())
    {
        const auto cat = Block::categoryName (t);
        if (t == Block::Type::Sax)
        {
            for (int v = 0; v < 4; ++v) subs[cat].addItem (1000 + v, juce::String (ndsp::saxModel (v).name) + " sax");   // Soprano first
            continue;
        }
        subs[cat].addItem ((int) t + 1, Block::typeName (t));
    }
    m.addSectionHeader ("Add a block");
    for (auto& c : cats)
        if (subs.count (c)) m.addSubMenu (c, subs[c]);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cellBounds (cell.y, cell.x).toNearestInt())),
                     [this, cell] (int r) { if (r >= 1000) addBlockAt (Block::Type::Sax, cell, r - 1000); else if (r > 0) addBlockAt ((Block::Type) (r - 1), cell); });
}

void GridView::addBlockAt (Block::Type t, Point<int> cell, int variant)
{
    auto b = proc.engine.addBlock (t, cell.y, cell.x);
    if (auto* sax = dynamic_cast<SaxBlock*> (b.get())) sax->setModel (juce::jmax (0, variant));
    if (t == Block::Type::Delay || t == Block::Type::Reverb || t == Block::Type::TriChorus
        || t == Block::Type::Chorus || t == Block::Type::Flanger || t == Block::Type::Doubler)
        if (cell.y > 0)
            if (auto* kd = b->param ("killDry")) { kd->set (1.0f); if (auto* mx = b->param ("mix")) mx->set (100.0f); }   // parallel rows: wet only
    proc.dirty = true;
    selected = cell;
    repaint();
    if (onSelect) onSelect (cell);
    if (onChanged) onChanged();
}

void GridView::removeAt (Point<int> cell)
{
    proc.engine.set (cell.y, cell.x, nullptr);
    proc.dirty = true;
    resized();
    repaint();
    if (onSelect) onSelect (cell);
    if (onChanged) onChanged();
}

void GridView::showContextMenu (Point<int> cell)
{
    auto b = proc.engine.get (cell.y, cell.x);
    if (b == nullptr) { showAddMenu (cell); return; }
    PopupMenu m;
    m.addSectionHeader (b->getTitle());
    m.addItem (1, b->enabled.load() ? "Switch off (bypass)" : "Switch on");
    const auto midi = proc.engine.describeMapping (b->id, -1);
    m.addItem (2, midi.isEmpty() ? "MIDI learn on/off switch" : "MIDI learn on/off again (now " + midi + ")");
    if (midi.isNotEmpty()) m.addItem (3, "Clear on/off MIDI");
    m.addSeparator();
    PopupMenu replace;
    for (auto t : Block::allTypes()) replace.addItem (100 + (int) t, Block::typeName (t));
    m.addSubMenu ("Replace with", replace);
    m.addItem (4, "Remove", true, false);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cellBounds (cell.y, cell.x).toNearestInt())),
                     [this, cell, b] (int r)
    {
        if (r == 1) { b->enabled = ! b->enabled.load(); proc.dirty = true; }
        if (r == 2) { proc.engine.armLearn (b->id, -1); if (onMessage) onMessage ("Press the footswitch or move the control on your MIDI device now...", false); }
        if (r == 3) proc.engine.clearMappingsFor (b->id, -1);
        if (r == 4) { removeAt (cell); return; }
        if (r >= 100) { addBlockAt ((Block::Type) (r - 100), cell); return; }
        repaint();
        if (onChanged) onChanged();
    });
}

void GridView::itemDragMove (const SourceDetails& d)
{
    dropCell = cellAt (d.localPosition.toInt());
    repaint();
}

void GridView::itemDropped (const SourceDetails& d)
{
    const auto cell = cellAt (d.localPosition.toInt());
    dropCell = { -1, -1 };
    dragging = false;
    if (cell.y > 0) openedRows.insert (cell.y);
    if (cell.x < 0) { repaint(); return; }
    const auto desc = d.description.toString();

    if (desc.startsWith ("cell:"))
    {
        const auto parts = StringArray::fromTokens (desc.fromFirstOccurrenceOf ("cell:", false, false), ":", {});
        const int r = parts[0].getIntValue(), c = parts[1].getIntValue();
        if (r != cell.y || c != cell.x) { proc.engine.move (r, c, cell.y, cell.x); proc.dirty = true; }
        selected = cell;
        if (onSelect) onSelect (cell);
    }
    else if (desc.startsWith ("type:"))
    {
        const auto parts = StringArray::fromTokens (desc.fromFirstOccurrenceOf ("type:", false, false), ":", {});
        addBlockAt ((Block::Type) parts[0].getIntValue(), cell, parts.size() > 1 ? parts[1].getIntValue() : -1);
    }
    else if (desc.startsWith ("file:"))
    {
        const auto err = proc.loadFileIntoBlock (File (desc.fromFirstOccurrenceOf ("file:", false, false)), cell.y, cell.x);
        if (err.isNotEmpty() && onMessage) onMessage (err, true);
        selected = cell;
        if (onSelect) onSelect (cell);
    }
    resized();
    repaint();
    if (onChanged) onChanged();
}

void GridView::filesDropped (const StringArray& files, int x, int y)
{
    auto cell = cellAt ({ x, y });
    dropCell = { -1, -1 };
    dragging = false;
    if (cell.y > 0) openedRows.insert (cell.y);
    Array<File> fs;
    for (auto& f : files) fs.add (File (f));
    const int added = proc.library.importFiles (fs);
    if (cell.x >= 0)
    {
        for (auto& f : fs)
        {
            if (! f.hasFileExtension ("nam;wav")) continue;
            auto inLib = proc.library.getFolder().getChildFile (f.getFileName());
            const auto err = proc.loadFileIntoBlock (inLib.existsAsFile() ? inLib : f, cell.y, cell.x);
            if (err.isNotEmpty() && onMessage) onMessage (err, true);
            break;
        }
        selected = cell;
        if (onSelect) onSelect (cell);
    }
    else if (onMessage) onMessage (added > 0 ? String (added) + " added to your library" : "Nothing new to add", false);
    resized();
    repaint();
    if (onChanged) onChanged();
}
