#include "GridView.h"
#include "../PluginProcessor.h"

using namespace juce;

GridView::GridView (NAMHomeProcessor& p) : proc (p) {}

bool GridView::rowShown (int r) const
{
    // only the rows you have: extra paths appear when you press + (not while dragging)
    if (r == 0 || openedRows.count (r) > 0) return true;
    int f, l;
    return proc.engine.rowSpan (r, f, l) || proc.engine.rowInput[r].load() > 0;
}

int GridView::rowStart (int r) const
{
    // the main line and continued lines start at the left; split paths keep their place under the main line
    if (r == 0 || proc.engine.rowContinues (r) || proc.engine.rowIsIndependent (r)) return 0;
    int f, l;
    return proc.engine.rowSpan (r, f, l) ? f : 0;
}

int GridView::visibleCols() const
{
    int n = 1;
    for (int r = 0; r < Engine::rows; ++r)
    {
        if (! rowShown (r)) continue;
        int f, l;
        const int used = proc.engine.rowSpan (r, f, l) ? l + 1 : rowStart (r);
        n = jmax (n, jmin (Engine::cols, used + 1));   // + the add slot after the last block
    }
    if (dragging) n = jmin (Engine::cols, n + 1);
    return n;
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
    // the chain sits dead centre and grows outward as blocks are added
    const int vc = visibleCols();
    const float gridW = vc * tile + (vc - 1) * gap;
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
    if (row > 0) m.addItem (5, "Continue the chain from the row above (one long chain)", row == 1 || proc.engine.rowInput[row - 1].load() == 3, cur == 3);
    m.addItem (2, "Interface input 1 (separate rig)", true, cur == 1);
    m.addItem (3, "Interface input 2 (separate rig)", true, cur == 2);
    int f, l;
    const bool empty = ! proc.engine.rowSpan (row, f, l);
    if (row > 0) { m.addSeparator(); m.addItem (4, "Remove this path", empty); }
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (inputChip (row).toNearestInt())), [this, row] (int r)
    {
        if (r == 0) return;
        if (r == 4) { openedRows.erase (row); proc.engine.rowInput[row] = 0; resized(); repaint(); if (onChanged) onChanged(); return; }
        if (r == 5)
        {
            // a continued row starts at the left, like the main line
            auto blocks = proc.engine.rowBlocks (row);
            proc.engine.rowInput[row] = 3;
            proc.engine.setRow (row, 0, blocks);
        }
        else proc.engine.rowInput[row] = r - 1;
        proc.dirty = true;
        resized(); repaint();
        if (onChanged) onChanged();
    });
}

Point<int> GridView::cellAt (Point<int> p) const
{
    for (int r = 0; r < Engine::rows; ++r)
        if (rowShown (r))
            for (int c = 0; c < visibleCols(); ++c)
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
    // icon only (the block's name shows at the top left of its settings when you click it)
    const auto col = b.getColour();
    const bool on = b.enabled.load();
    Graphics::ScopedSaveState ss (g);
    g.setColour (theme::raised);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (col.withAlpha (on ? 0.55f : 0.2f));
    g.drawRoundedRectangle (r.reduced (0.75f), 12.0f, 1.5f);
    if (! on) g.setOpacity (0.35f);
    drawIcon (g, b, r.reduced (r.getWidth() * 0.2f), col);
    if (isSelected)
    {
        g.setOpacity (1.0f);
        g.setColour (Colours::white);
        g.drawRoundedRectangle (r.expanded (2.5f), 14.0f, 2.0f);
    }
}

void GridView::drawIcon (Graphics& g, Block& b, Rectangle<float> r, Colour c)
{
    using T = Block::Type;
    const auto t = b.getType();
    const bool dark = c.getPerceivedBrightness() < 0.25f;      // black pedals (RAT, wah) get a light outline
    const auto line = dark ? Colour (0xffb8bcc4) : c.darker (0.6f);
    const float s = jmin (r.getWidth(), r.getHeight());
    auto box = r.withSizeKeepingCentre (s, s);
    const float lw = jmax (1.5f, s * 0.045f);
    auto fillStroke = [&] (const Path& p) { g.setColour (c); g.fillPath (p); g.setColour (line); g.strokePath (p, PathStrokeType (lw)); };
    auto stroke = [&] (const Path& p, float w) { g.setColour (dark ? line : c); g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded)); };

    switch (t)
    {
        case T::NamAmp:
        {
            // amp head: body, grille, row of knobs
            auto body = box.withSizeKeepingCentre (s, s * 0.62f);
            Path p; p.addRoundedRectangle (body, s * 0.08f); fillStroke (p);
            auto panel = body.removeFromTop (body.getHeight() * 0.36f).reduced (s * 0.07f, s * 0.05f);
            g.setColour (c.darker (0.7f)); g.fillRoundedRectangle (panel, 2.0f);
            for (int k = 0; k < 5; ++k) { g.setColour (c.brighter (0.6f)); g.fillEllipse (panel.getX() + panel.getWidth() * (0.1f + 0.2f * (float) k) - 2.5f, panel.getCentreY() - 2.5f, 5, 5); }
            g.setColour (c.darker (0.5f));
            for (int k = 1; k < 5; ++k) g.drawHorizontalLine ((int) (body.getY() + body.getHeight() * (float) k / 5.0f), body.getX() + s * 0.08f, body.getRight() - s * 0.08f);
            break;
        }
        case T::Cab:
        {
            auto body = box.reduced (s * 0.06f);
            Path p; p.addRoundedRectangle (body, s * 0.06f); fillStroke (p);
            g.setColour (c.darker (0.7f)); g.fillEllipse (body.reduced (body.getWidth() * 0.16f));
            g.setColour (c.brighter (0.4f)); g.fillEllipse (body.withSizeKeepingCentre (body.getWidth() * 0.22f, body.getWidth() * 0.22f));
            break;
        }
        case T::Wah: case T::Volume:
        {
            // expression pedal: tilted treadle
            Path p;
            p.startNewSubPath (box.getX() + s * 0.12f, box.getBottom() - s * 0.15f);
            p.lineTo (box.getRight() - s * 0.12f, box.getBottom() - s * 0.15f);
            p.lineTo (box.getRight() - s * 0.2f, box.getY() + s * 0.28f);
            p.lineTo (box.getX() + s * 0.2f, box.getY() + s * 0.5f);
            p.closeSubPath(); fillStroke (p);
            break;
        }
        case T::GraphicEQ: case T::ParametricEQ:
        {
            for (int k = 0; k < 3; ++k)
            {
                const float x = box.getX() + s * (0.22f + 0.28f * (float) k);
                Path l; l.startNewSubPath (x, box.getY() + s * 0.12f); l.lineTo (x, box.getBottom() - s * 0.12f); stroke (l, lw);
                const float y = box.getY() + s * (k == 1 ? 0.3f : (k == 0 ? 0.6f : 0.45f));
                g.setColour (c); g.fillRoundedRectangle (x - s * 0.09f, y - s * 0.05f, s * 0.18f, s * 0.1f, 2.0f);
            }
            break;
        }
        case T::Delay:
        {
            for (int k = 0; k < 3; ++k)
            {
                const float d = s * (0.32f - 0.08f * (float) k);
                g.setColour (c.withAlpha (1.0f - 0.28f * (float) k));
                g.fillEllipse (box.getX() + s * (0.08f + 0.32f * (float) k), box.getCentreY() - d * 0.5f, d, d);
            }
            break;
        }
        case T::Reverb:
        {
            for (int k = 0; k < 3; ++k)
            {
                Path a; const float rr = s * (0.16f + 0.15f * (float) k);
                a.addCentredArc (box.getX() + s * 0.2f, box.getCentreY(), rr, rr, 0.0f, 0.5f, MathConstants<float>::pi - 0.5f, true);
                g.setColour (c.withAlpha (1.0f - 0.25f * (float) k)); g.strokePath (a, PathStrokeType (lw * 1.3f, PathStrokeType::curved, PathStrokeType::rounded));
            }
            g.setColour (c); g.fillEllipse (box.getX() + s * 0.14f, box.getCentreY() - s * 0.06f, s * 0.12f, s * 0.12f);
            break;
        }
        case T::Freeze:
        {
            for (int k = 0; k < 3; ++k)
            {
                const float a = MathConstants<float>::pi * (float) k / 3.0f;
                Path l; l.startNewSubPath (box.getCentreX() - std::cos (a) * s * 0.42f, box.getCentreY() - std::sin (a) * s * 0.42f);
                l.lineTo (box.getCentreX() + std::cos (a) * s * 0.42f, box.getCentreY() + std::sin (a) * s * 0.42f);
                stroke (l, lw * 1.2f);
            }
            break;
        }
        case T::PhatTrack:
        {
            auto kb = box.withSizeKeepingCentre (s, s * 0.6f);
            Path p; p.addRoundedRectangle (kb, 3.0f); fillStroke (p);
            g.setColour (c.darker (0.8f));
            for (int k = 1; k < 7; ++k) g.drawVerticalLine ((int) (kb.getX() + kb.getWidth() * (float) k / 7.0f), kb.getY(), kb.getBottom());
            for (int k : { 1, 2, 4, 5, 6 }) g.fillRect (kb.getX() + kb.getWidth() * ((float) k / 7.0f) - s * 0.04f, kb.getY(), s * 0.08f, kb.getHeight() * 0.55f);
            break;
        }
        case T::Sax:
        {
            // a saxophone's curve: neck, body, upturned bell
            Path p;
            p.startNewSubPath (box.getX() + s * 0.35f, box.getY() + s * 0.05f);
            p.lineTo (box.getX() + s * 0.45f, box.getY() + s * 0.12f);
            p.lineTo (box.getX() + s * 0.42f, box.getY() + s * 0.72f);
            p.quadraticTo (box.getX() + s * 0.45f, box.getBottom() - s * 0.02f, box.getX() + s * 0.62f, box.getBottom() - s * 0.08f);
            p.quadraticTo (box.getRight() - s * 0.12f, box.getBottom() - s * 0.2f, box.getRight() - s * 0.1f, box.getY() + s * 0.48f);
            stroke (p, s * 0.13f);
            g.setColour (dark ? line : c); g.fillEllipse (box.getRight() - s * 0.24f, box.getY() + s * 0.36f, s * 0.26f, s * 0.14f);
            break;
        }
        case T::Looper:
        {
            // a circle with an arrow: a loop
            Path a; a.addCentredArc (box.getCentreX(), box.getCentreY(), s * 0.34f, s * 0.34f, 0.0f, 0.5f, 5.8f, true);
            stroke (a, lw * 1.5f);
            const float ex = box.getCentreX() + std::sin (0.5f) * s * 0.34f, ey = box.getCentreY() - std::cos (0.5f) * s * 0.34f;
            Path h; h.addTriangle (ex - s * 0.13f, ey - s * 0.1f, ex + s * 0.06f, ey + s * 0.12f, ex - s * 0.17f, ey + s * 0.12f);
            g.setColour (dark ? line : c); g.fillPath (h);
            g.fillEllipse (box.getCentreX() - s * 0.08f, box.getCentreY() - s * 0.08f, s * 0.16f, s * 0.16f);
            break;
        }
        case T::Drums:
        {
            // a snare drum with two sticks
            auto shell = box.withSizeKeepingCentre (s * 0.8f, s * 0.42f).translated (0, s * 0.14f);
            Path p; p.addRectangle (shell.withTrimmedTop (shell.getHeight() * 0.25f)); fillStroke (p);
            Path top; top.addEllipse (shell.withHeight (shell.getHeight() * 0.5f)); fillStroke (top);
            Path st;
            st.startNewSubPath (box.getX() + s * 0.2f, box.getY() + s * 0.02f); st.lineTo (box.getCentreX() + s * 0.05f, shell.getY() + s * 0.06f);
            st.startNewSubPath (box.getRight() - s * 0.2f, box.getY() + s * 0.02f); st.lineTo (box.getCentreX() - s * 0.05f, shell.getY() + s * 0.06f);
            stroke (st, lw * 1.3f);
            break;
        }
        case T::Gate: case T::Utility:
        {
            auto bx = box.withSizeKeepingCentre (s * 0.8f, s * 0.5f);
            Path p; p.addRoundedRectangle (bx, 4.0f); fillStroke (p);
            g.setColour (c.darker (0.7f)); g.fillRect (bx.reduced (s * 0.1f, s * 0.16f));
            break;
        }
        case T::Chorus: case T::Phaser: case T::Flanger: case T::TriChorus: case T::Tremolo: case T::Vibrato:
        case T::Rotary: case T::Vibe: case T::Doubler: case T::Harmonizer:
        {
            // wave: smooth for chorus-type, squared for tremolo, a spinning rotor for rotary
            if (t == T::Rotary)
            {
                Path a; a.addCentredArc (box.getCentreX(), box.getCentreY(), s * 0.36f, s * 0.36f, 0.0f, 0.6f, 5.6f, true);
                stroke (a, lw * 1.4f);
                g.setColour (dark ? line : c); g.fillEllipse (box.getCentreX() - s * 0.1f, box.getCentreY() - s * 0.1f, s * 0.2f, s * 0.2f);
                break;
            }
            Path w;
            const int n = 40;
            for (int k = 0; k <= n; ++k)
            {
                const float x = (float) k / (float) n;
                float y = std::sin (x * MathConstants<float>::twoPi * 1.5f);
                if (t == T::Tremolo) y = std::tanh (y * 4.0f);
                const auto pt = Point<float> (box.getX() + x * s, box.getCentreY() - y * s * 0.28f);
                if (k == 0) w.startNewSubPath (pt); else w.lineTo (pt);
            }
            stroke (w, lw * 1.4f);
            if (t == T::Harmonizer || t == T::Doubler)
            {
                Path w2 = w; w2.applyTransform (AffineTransform::translation (0, s * 0.18f));
                g.setColour ((dark ? line : c).withAlpha (0.5f)); g.strokePath (w2, PathStrokeType (lw * 1.2f));
            }
            break;
        }
        default:
        {
            // stompbox: everything else that sits on a pedalboard (drives, fuzz, comp, whammy, octave...)
            auto body = box.withSizeKeepingCentre (s * 0.66f, s);
            Path p; p.addRoundedRectangle (body, s * 0.08f); fillStroke (p);
            const auto kc = dark ? line : c.darker (0.7f);
            g.setColour (kc);
            g.fillEllipse (body.getX() + body.getWidth() * 0.16f, body.getY() + s * 0.12f, s * 0.16f, s * 0.16f);
            g.fillEllipse (body.getRight() - body.getWidth() * 0.16f - s * 0.16f, body.getY() + s * 0.12f, s * 0.16f, s * 0.16f);
            g.fillEllipse (body.getCentreX() - s * 0.1f, body.getBottom() - s * 0.3f, s * 0.2f, s * 0.2f);
            break;
        }
    }
}

void GridView::paint (Graphics& g)
{
    auto& e = proc.engine;
    const float cy0 = cellBounds (0, 0).getCentreY();
    const int vc = visibleCols();
    const float left = x0 - 34, right = x0 + vc * (tile + gap) - gap + 46;
    int outRow = 0;
    for (int r = 1; r < Engine::rows; ++r) if (e.rowContinues (r) && rowShown (r)) outRow = r;

    // main path
    g.setColour (theme::path);
    g.drawLine (left, cy0, right, cy0, 2.5f);
    g.setColour (theme::faint);
    g.setFont (theme::font (11.0f, 2));
    const float cyOut = cellBounds (outRow, 0).getCentreY();
    g.drawText ("OUT", Rectangle<float> (right + 4, cyOut - 9, 36, 18), Justification::centredLeft);
    g.fillEllipse (left - 3, cy0 - 3, 6, 6);
    g.fillEllipse (right - 3, cyOut - 3, 6, 6);

    // parallel rows: split down, run along, merge back up; rows with their own input run end to end
    for (int r = 1; r < Engine::rows; ++r)
    {
        int f, l;
        if (e.rowContinues (r) && rowShown (r))
        {
            // one long chain: the end of the line above wraps round to the start of this one
            const float cyPrev = cellBounds (r - 1, 0).getCentreY(), cy = cellBounds (r, 0).getCentreY(), mid = 0.5f * (cyPrev + cy);
            Path p;
            p.startNewSubPath (right - 12, cyPrev);
            p.quadraticTo (right, cyPrev, right, cyPrev + 12); p.lineTo (right, mid - 10); p.quadraticTo (right, mid, right - 10, mid);
            p.lineTo (left + 10, mid); p.quadraticTo (left, mid, left, mid + 10); p.lineTo (left, cy - 10); p.quadraticTo (left, cy, left + 10, cy);
            p.lineTo (right, cy);
            g.setColour (theme::path);
            g.strokePath (p, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
            continue;
        }
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
        if (! e.rowSpan (r, f, l) || e.rowContinues (r)) continue;
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
        g.drawText (in == 3 ? String ("CHAIN") : in > 0 ? "IN " + String (in) : String ("SPLIT"), chip, Justification::centred);
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

    // cells: blocks sit on their line; while dragging, the blocks after the drop point slide over
    for (int r = 0; r < Engine::rows; ++r)
    {
        if (! rowShown (r)) continue;
        const int start = rowStart (r);
        auto blocks = e.rowBlocks (r);
        const bool insertingHere = dropRow == r && dropIndex >= 0;
        if (insertingHere && dragFrom.y == r)
        {
            auto src = e.get (dragFrom.y, dragFrom.x);
            blocks.erase (std::remove (blocks.begin(), blocks.end(), src), blocks.end());
        }
        // where each block sits: packed from the row start (split paths keep their columns when not dragging)
        if (! insertingHere)
        {
            for (int c = 0; c < Engine::cols; ++c)
                if (auto b = e.get (r, c))
                {
                    const bool isSel = selected == Point<int> (c, r);
                    Graphics::ScopedSaveState ss (g);
                    if (dragging && dragFrom == Point<int> (c, r)) g.setOpacity (0.35f);
                    drawTile (g, *b, cellBounds (r, c), isSel);
                    if (dropOnto == Point<int> (c, r))
                    {
                        g.setColour (theme::accent);
                        g.drawRoundedRectangle (cellBounds (r, c).expanded (3.0f), 14.0f, 2.5f);
                    }
                }
        }
        else
        {
            for (int k = 0; k < (int) blocks.size(); ++k)
            {
                const int c = start + (k >= dropIndex ? k + 1 : k);
                if (c < Engine::cols) drawTile (g, *blocks[(size_t) k], cellBounds (r, c), false);
            }
            const auto slot = cellBounds (r, jmin (Engine::cols - 1, start + dropIndex));
            g.setColour (theme::accent.withAlpha (0.12f));
            g.fillRoundedRectangle (slot, 12.0f);
            g.setColour (theme::accent);
            g.drawRoundedRectangle (slot.reduced (1.0f), 12.0f, 2.0f);
        }

        // the add slot after the last block (an empty line shows just this, in the centre)
        int f, l;
        const int next = e.rowSpan (r, f, l) ? l + 1 : start;
        if (! dragging && next < Engine::cols)
        {
            auto cb = cellBounds (r, next);
            const bool hot = hoverCell == Point<int> (next, r) || selected == Point<int> (next, r);
            Path p;
            p.addRoundedRectangle (cb.reduced (1), 12.0f);
            float dash[] = { 5.0f, 5.0f };
            PathStrokeType (1.2f).createDashedStroke (p, p, dash, 2);
            g.setColour (hot ? theme::muted : theme::line.brighter (0.15f));
            g.fillPath (p);
            g.setColour (hot ? theme::text : theme::faint);
            const auto cc = cb.getCentre();
            g.drawLine (cc.x - 6, cc.y, cc.x + 6, cc.y, 1.6f);
            g.drawLine (cc.x, cc.y - 6, cc.x, cc.y + 6, 1.6f);
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
        dragFrom = pressCell;
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
    StringArray cats { "Amps", "Cabs", "Drive", "Dynamics & EQ", "Wah, filters & talk box", "Pitch", "Modulation", "Delay",
                       "Reverb & ambient", "Guitar sims", "Synthesizers", "Saxophone", "Looper & drums", "Effects" };
    std::map<String, PopupMenu> subs;
    for (auto t : Block::allTypes())
    {
        const auto cat = Block::libraryGroup (t);
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
    else if (variant >= 0) if (auto* vp = b->variantParam()) vp->set ((float) variant);   // e.g. "Green OD" from the library
    if (t == Block::Type::Delay || t == Block::Type::Reverb || t == Block::Type::TriChorus
        || t == Block::Type::Chorus || t == Block::Type::Flanger || t == Block::Type::Doubler)
        if (cell.y > 0)
            if (auto* kd = b->param ("killDry")) { kd->set (1.0f); if (auto* mx = b->param ("mix")) mx->set (100.0f); }   // parallel rows: wet only
    proc.dirty = true;
    selected = cell;
    resized();
    repaint();
    if (onSelect) onSelect (cell);
    if (onChanged) onChanged();
}

void GridView::removeAt (Point<int> cell)
{
    // the line closes up behind a removed block
    auto blocks = proc.engine.rowBlocks (cell.y);
    auto gone = proc.engine.get (cell.y, cell.x);
    const int start = rowStart (cell.y);
    blocks.erase (std::remove (blocks.begin(), blocks.end(), gone), blocks.end());
    proc.engine.setRow (cell.y, start, blocks);
    proc.dirty = true;
    resized();
    repaint();
    if (onSelect) onSelect (cell);
    if (onChanged) onChanged();
}

int GridView::slotIndexFor (int row, float x) const
{
    const int start = rowStart (row);
    int n = (int) proc.engine.rowBlocks (row).size();
    if (dragFrom.y == row && proc.engine.get (dragFrom.y, dragFrom.x) != nullptr) --n;
    const float firstX = x0 + (float) start * (tile + gap);
    const int idx = (int) std::floor ((x - firstX + gap * 0.5f) / (tile + gap) + 0.5f);
    return jlimit (0, jmax (0, n), idx);
}

void GridView::updateDrop (Point<int> p, bool files)
{
    dropRow = dropIndex = -1; dropOnto = { -1, -1 };
    for (int r = 0; r < Engine::rows; ++r)
    {
        if (! rowShown (r)) continue;
        const auto band = cellBounds (r, 0);
        if (p.y < band.getY() - gap * 0.6f || p.y > band.getBottom() + gap * 0.6f) continue;
        // a capture or IR dropped right onto a block loads into it; anything else slots in between
        if (files)
            for (int c = 0; c < Engine::cols; ++c)
                if (proc.engine.get (r, c) != nullptr && cellBounds (r, c).reduced (tile * 0.2f, 0).contains (p.toFloat())) { dropOnto = { c, r }; return; }
        dropRow = r;
        dropIndex = slotIndexFor (r, (float) p.x);
        return;
    }
}

bool GridView::insertBlock (int row, int index, std::shared_ptr<Block> b, Point<int> from)
{
    auto& e = proc.engine;
    auto list = e.rowBlocks (row);
    const int start = rowStart (row);
    const bool sameRow = from.y == row;
    if (sameRow) list.erase (std::remove (list.begin(), list.end(), b), list.end());
    if ((int) list.size() + 1 > Engine::cols)
    {
        if (onMessage) onMessage ("This line is full (10 blocks). Remove one, or add a path with + and set it to continue the chain.", true);
        return false;
    }
    index = jlimit (0, (int) list.size(), index);
    list.insert (list.begin() + index, b);
    if (from.y >= 0 && ! sameRow)
    {
        auto src = e.rowBlocks (from.y);
        const int srcStart = rowStart (from.y);
        src.erase (std::remove (src.begin(), src.end(), b), src.end());
        e.setRow (from.y, srcStart, src);
    }
    const int s0 = jmin (start, Engine::cols - (int) list.size());
    e.setRow (row, s0, list);
    selected = { s0 + index, row };
    proc.dirty = true;
    return true;
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
    dragging = true;
    const auto desc = d.description.toString();
    if (! desc.startsWith ("cell:")) dragFrom = { -1, -1 };
    updateDrop (d.localPosition.toInt(), desc.startsWith ("file:"));
    resized();
    repaint();
}

void GridView::itemDropped (const SourceDetails& d)
{
    const auto desc = d.description.toString();
    updateDrop (d.localPosition.toInt(), desc.startsWith ("file:"));
    const int row = dropRow, index = dropIndex;
    const auto onto = dropOnto, from = dragFrom;
    clearDrop();
    auto& e = proc.engine;

    if (desc.startsWith ("cell:"))
    {
        const auto parts = StringArray::fromTokens (desc.fromFirstOccurrenceOf ("cell:", false, false), ":", {});
        const Point<int> src (parts[1].getIntValue(), parts[0].getIntValue());
        if (auto b = e.get (src.y, src.x); b != nullptr && row >= 0) insertBlock (row, index, b, src);
        ignoreUnused (from);
    }
    else if (desc.startsWith ("type:") && row >= 0)
    {
        const auto parts = StringArray::fromTokens (desc.fromFirstOccurrenceOf ("type:", false, false), ":", {});
        const auto t = (Block::Type) parts[0].getIntValue();
        const int variant = parts.size() > 1 ? parts[1].getIntValue() : -1;
        std::shared_ptr<Block> b (Block::create (t).release());
        if (auto* sax = dynamic_cast<SaxBlock*> (b.get())) sax->setModel (jmax (0, variant));
        else if (variant >= 0) if (auto* vp = b->variantParam()) vp->set ((float) variant);
        if (row > 0 && ! e.rowContinues (row) && ! e.rowIsIndependent (row))
            if (auto* kd = b->param ("killDry")) { kd->set (1.0f); if (auto* mx = b->param ("mix")) mx->set (100.0f); }
        insertBlock (row, index, b);
    }
    else if (desc.startsWith ("file:"))
    {
        const File f (desc.fromFirstOccurrenceOf ("file:", false, false));
        Point<int> cell = onto;
        if (cell.x < 0 && row >= 0)
        {
            // open a slot on the line, then load the capture / IR into it
            if (insertBlock (row, index, nullptr)) cell = selected;
        }
        if (cell.x >= 0)
        {
            const auto err = proc.loadFileIntoBlock (f, cell.y, cell.x);
            if (err.isNotEmpty())
            {
                if (onMessage) onMessage (err, true);
                if (e.get (cell.y, cell.x) == nullptr) { auto l = e.rowBlocks (cell.y); e.setRow (cell.y, rowStart (cell.y), l); }
            }
            selected = cell;
        }
    }
    if (onSelect && selected.x >= 0) onSelect (selected);
    resized();
    repaint();
    if (onChanged) onChanged();
}

void GridView::filesDropped (const StringArray& files, int x, int y)
{
    updateDrop ({ x, y }, true);
    const int row = dropRow, index = dropIndex;
    const auto onto = dropOnto;
    clearDrop();
    Array<File> fs;
    for (auto& f : files) fs.add (File (f));
    const int added = proc.library.importFiles (fs);
    Point<int> cell = onto;
    for (auto& f : fs)
    {
        if (! f.hasFileExtension ("nam;wav")) continue;
        if (cell.x < 0 && row >= 0 && insertBlock (row, index, nullptr)) cell = selected;
        if (cell.x < 0) break;
        auto inLib = proc.library.getFolder().getChildFile (f.getFileName());
        const auto err = proc.loadFileIntoBlock (inLib.existsAsFile() ? inLib : f, cell.y, cell.x);
        if (err.isNotEmpty() && onMessage) onMessage (err, true);
        selected = cell;
        if (onSelect) onSelect (cell);
        break;
    }
    if (cell.x < 0 && onMessage) onMessage (added > 0 ? String (added) + " added to your library" : "Nothing new to add", false);
    resized();
    repaint();
    if (onChanged) onChanged();
}
