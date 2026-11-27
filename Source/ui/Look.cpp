#include "Look.h"
#include "BinaryData.h"

using namespace juce;

namespace theme
{
    float& fontScale() { static float s = 1.0f; return s; }

    static Typeface::Ptr face (int weight)
    {
        static Typeface::Ptr regular = Typeface::createSystemTypefaceFor (BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize);
        static Typeface::Ptr medium  = Typeface::createSystemTypefaceFor (BinaryData::InterMedium_otf, BinaryData::InterMedium_otfSize);
        static Typeface::Ptr semi    = Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
        return weight >= 2 ? semi : weight == 1 ? medium : regular;
    }

    Font font (float size, int weight)
    {
        return Font (FontOptions (face (weight)).withHeight (size * fontScale()));
    }
}

NamLookAndFeel::NamLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, theme::bg);
    setColour (DocumentWindow::backgroundColourId, theme::bg);
    setColour (TextButton::buttonColourId, theme::raised);
    setColour (TextButton::buttonOnColourId, theme::accent);
    setColour (TextButton::textColourOffId, theme::text);
    setColour (TextButton::textColourOnId, Colour (0xff061a18));
    setColour (Label::textColourId, theme::text);
    setColour (TextEditor::backgroundColourId, theme::raised);
    setColour (TextEditor::textColourId, theme::text);
    setColour (TextEditor::highlightColourId, theme::accent.withAlpha (0.35f));
    setColour (CaretComponent::caretColourId, theme::accent);
    setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxTextColourId, theme::muted);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.35f));
    setColour (ComboBox::backgroundColourId, theme::raised);
    setColour (ComboBox::textColourId, theme::text);
    setColour (ComboBox::arrowColourId, theme::muted);
    setColour (ComboBox::outlineColourId, Colours::transparentBlack);
    setColour (PopupMenu::backgroundColourId, theme::card);
    setColour (PopupMenu::textColourId, theme::text);
    setColour (PopupMenu::highlightedBackgroundColourId, theme::hover);
    setColour (PopupMenu::highlightedTextColourId, theme::text);
    setColour (ScrollBar::thumbColourId, theme::line);
    setColour (ToggleButton::textColourId, theme::text);
    setColour (ToggleButton::tickColourId, theme::accent);
    setColour (TooltipWindow::backgroundColourId, theme::card);
    setColour (TooltipWindow::textColourId, theme::text);
    setColour (AlertWindow::backgroundColourId, theme::card);
    setColour (AlertWindow::textColourId, theme::text);
    setColour (ListBox::textColourId, theme::text);
    setColour (TreeView::backgroundColourId, theme::card);
}

Typeface::Ptr NamLookAndFeel::getTypefaceForFont (const Font& f)
{
    const bool bold = f.isBold();
    return theme::font (12.0f, bold ? 2 : 0).getTypefacePtr();
}

void NamLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto c = s.findColour (Slider::rotarySliderFillColourId);
    auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3);
    const float d = jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const auto centre = r.getCentre();
    const float rad = d * 0.5f;
    const float angle = start + pos * (end - start);
    const float thick = jmax (2.5f, d * 0.07f);

    // track + value arc
    Path track, value;
    track.addCentredArc (centre.x, centre.y, rad - thick, rad - thick, 0, start, end, true);
    value.addCentredArc (centre.x, centre.y, rad - thick, rad - thick, 0, start, angle, true);
    g.setColour (theme::line);
    g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (s.isEnabled() ? c : c.withAlpha (0.35f));
    g.strokePath (value, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

    // knob body with a subtle top highlight
    auto body = r.reduced (thick * 2.6f);
    g.setGradientFill (ColourGradient (Colour (0xff222226), body.getCentreX(), body.getY(), Colour (0xff0a0a0c), body.getCentreX(), body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (Colour (0xff3a3a40));
    g.drawEllipse (body, 1.0f);
    Path pointer;
    const float pr = body.getWidth() * 0.5f;
    pointer.addRoundedRectangle (-1.25f, -pr + 3.0f, 2.5f, pr * 0.45f, 1.25f);
    g.setColour (theme::text);
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (centre));
    if (s.isMouseOverOrDragging())
    {
        g.setColour (c.withAlpha (0.12f));
        g.fillEllipse (r.expanded (1));
    }
}

void NamLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider& s)
{
    const bool vertical = style == Slider::LinearVertical;
    const auto c = s.findColour (Slider::trackColourId);
    if (vertical)
    {
        const auto track = Rectangle<float> ((float) x + w * 0.5f - 2.5f, (float) y, 5.0f, (float) h);
        g.setColour (theme::line);
        g.fillRoundedRectangle (track, 2.5f);
        const float zero = (float) y + h * 0.5f;
        g.setColour (c);
        g.fillRoundedRectangle (track.withTop (jmin (zero, pos)).withBottom (jmax (zero, pos)), 2.5f);
        auto thumb = Rectangle<float> ((float) x + w * 0.5f - 11, pos - 5, 22, 10);
        g.setColour (theme::text);
        g.fillRoundedRectangle (thumb, 3.0f);
        return;
    }
    const auto track = Rectangle<float> ((float) x, (float) y + h * 0.5f - 2.5f, (float) w, 5.0f);
    g.setColour (theme::line);
    g.fillRoundedRectangle (track, 2.5f);
    g.setColour (c);
    g.fillRoundedRectangle (track.withRight (pos), 2.5f);
    g.setColour (theme::text);
    g.fillEllipse (pos - 7, (float) y + h * 0.5f - 7, 14, 14);
}

void NamLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& bgc, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = jmin (8.0f, r.getHeight() * 0.5f);
    auto c = b.getToggleState() ? b.findColour (TextButton::buttonOnColourId) : bgc;
    if (! b.isEnabled()) c = c.withAlpha (0.4f);
    else if (down) c = c.brighter (0.05f);
    else if (over) c = c.brighter (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, radius);
}

void NamLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? TextButton::textColourOnId : TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 2), Justification::centred, 1);
}

void NamLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    // modern switch on the right, label on the left
    auto r = b.getLocalBounds().toFloat();
    auto sw = r.removeFromRight (40).withSizeKeepingCentre (36, 20);
    const bool on = b.getToggleState();
    g.setColour (on ? theme::accent : (over ? theme::hover : theme::line));
    g.fillRoundedRectangle (sw, 10.0f);
    const float knobX = on ? sw.getRight() - 18 : sw.getX() + 2;
    g.setColour (on ? Colours::white : theme::muted);
    g.fillEllipse (knobX, sw.getY() + 2, 16, 16);
    g.setColour (theme::text);
    g.setFont (theme::font (13.5f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedRight (8), Justification::centredLeft, 2);
}

void NamLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isMouseOver (true) ? theme::hover : theme::raised);
    g.fillRoundedRectangle (r, 7.0f);
    Path arrow;
    const float cx = (float) w - 14, cy = h * 0.5f;
    arrow.startNewSubPath (cx - 4, cy - 2);
    arrow.lineTo (cx, cy + 2);
    arrow.lineTo (cx + 4, cy - 2);
    g.setColour (theme::muted);
    g.strokePath (arrow, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void NamLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void NamLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (theme::card);
    g.setColour (theme::line);
    g.drawRect (0, 0, w, h, 1);
}

void NamLookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                        bool isTicked, bool hasSubMenu, const String& text, const String& shortcut,
                                        const Drawable*, const Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (theme::line);
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (theme::hover);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
    }
    g.setColour (textColour != nullptr ? *textColour : (isActive ? theme::text : theme::faint));
    g.setFont (getPopupMenuFont());
    auto tr = r.reduced (12, 0);
    if (isTicked)
    {
        Path tick;
        const float cy = (float) tr.getCentreY();
        tick.startNewSubPath ((float) tr.getX(), cy);
        tick.lineTo ((float) tr.getX() + 4, cy + 4);
        tick.lineTo ((float) tr.getX() + 11, cy - 4);
        g.setColour (theme::accent);
        g.strokePath (tick, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (isActive ? theme::text : theme::faint);
    }
    tr.removeFromLeft (20);
    g.drawFittedText (text, tr, Justification::centredLeft, 1);
    if (shortcut.isNotEmpty())
    {
        g.setColour (theme::faint);
        g.drawText (shortcut, tr, Justification::centredRight);
    }
    if (hasSubMenu)
    {
        Path arrow;
        const float cx = (float) tr.getRight() - 4, cy = (float) tr.getCentreY();
        arrow.startNewSubPath (cx - 3, cy - 4); arrow.lineTo (cx + 1, cy); arrow.lineTo (cx - 3, cy + 4);
        g.setColour (theme::muted);
        g.strokePath (arrow, PathStrokeType (1.6f));
    }
}

void NamLookAndFeel::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
{
    g.setFont (theme::font (11.5f, 2));
    g.setColour (theme::faint);
    g.drawFittedText (name.toUpperCase(), area.reduced (16, 0), Justification::bottomLeft, 1);
}

void NamLookAndFeel::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? Rectangle<int> (x, thumbStart, w, thumbSize) : Rectangle<int> (thumbStart, y, thumbSize, h);
    g.setColour ((over || down) ? theme::path : theme::line);
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), 3.0f);
}

void NamLookAndFeel::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& te)
{
    if (te.hasKeyboardFocus (true))
    {
        g.setColour (theme::accent.withAlpha (0.7f));
        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 7.0f, 1.2f);
    }
}

void NamLookAndFeel::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor& te)
{
    g.setColour (te.findColour (TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 7.0f);
}

Label* NamLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (theme::font (12.0f));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::textColourId, theme::muted);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    return l;
}

void NamLookAndFeel::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.setColour (theme::card);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 6.0f, 1.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (12.5f));
    g.drawFittedText (text, Rectangle<int> (w, h).reduced (8, 4), Justification::centredLeft, 4);
}

Rectangle<int> NamLookAndFeel::getTooltipBounds (const String& text, Point<int> pos, Rectangle<int> parent)
{
    const int w = jmin (320, (int) GlyphArrangement::getStringWidth (theme::font (12.5f), text) + 20);
    const int h = 28;
    return Rectangle<int> (pos.x + 12, pos.y + 18, w, h).constrainedWithin (parent);
}

void drawLogo (Graphics& g, Rectangle<float> a)
{
    const float s = jmin (a.getWidth(), a.getHeight());
    auto r = a.withSizeKeepingCentre (s, s);
    const auto c = r.getCentre();
    g.setColour (theme::accent);
    g.drawEllipse (r.reduced (s * 0.06f), s * 0.07f);
    g.setColour (Colour (0xff1d4f4a));
    g.drawEllipse (r.reduced (s * 0.2f), s * 0.035f);
    Path p;
    const float u = s / 1024.0f * 1.45f;   // NH letterform scaled from the icon artwork
    auto P = [&] (float x, float y) { return Point<float> (c.x + (x - 512.0f) * u, c.y + (y - 512.0f) * u); };
    p.startNewSubPath (P (366, 648)); p.lineTo (P (366, 376)); p.lineTo (P (512, 648)); p.lineTo (P (512, 376));
    p.startNewSubPath (P (512, 512)); p.lineTo (P (658, 512));
    p.startNewSubPath (P (658, 376)); p.lineTo (P (658, 648));
    g.setColour (theme::text);
    g.strokePath (p, PathStrokeType (jmax (1.5f, s * 0.075f), PathStrokeType::curved, PathStrokeType::rounded));
}
