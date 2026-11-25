#include "Widgets.h"
#include "../PluginProcessor.h"

using namespace juce;

//==============================================================================
ParamKnob::ParamKnob (NAMHomeProcessor& p, Block& b, int i, Colour c)
    : proc (p), block (b), index (i), prm (*b.params[(size_t) i])
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 90, 18);
    slider.setColour (Slider::rotarySliderFillColourId, c);
    slider.setNormalisableRange ({ prm.range.start, prm.range.end,
                                   [this] (float, float, float n) { return prm.range.convertFrom0to1 (n); },
                                   [this] (float, float, float v) { return prm.range.convertTo0to1 (v); },
                                   [this] (float, float, float v) { return prm.range.snapToLegalValue (v); } });
    slider.setValue (prm.get(), dontSendNotification);
    slider.setDoubleClickReturnValue (true, prm.def);
    slider.setMouseDragSensitivity (220);
    slider.textFromValueFunction = [this] (double v)
    {
        const float old = prm.value.load();
        prm.value = (float) v;
        const auto t = prm.text();
        prm.value = old;
        return t;
    };
    slider.updateText();
    slider.onValueChange = [this]
    {
        prm.set ((float) slider.getValue());
        proc.dirty = true;
        if (onChange) onChange();
    };
    slider.setPopupMenuEnabled (false);
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
    setTooltip (prm.realControl ? prm.name : prm.name + " (added shaping, not the amp's own control)");
    midiText = proc.engine.describeMapping (block.id, index);
}

void ParamKnob::sync()
{
    if (! slider.isMouseButtonDown() && std::abs ((float) slider.getValue() - prm.get()) > 1.0e-6f)
        slider.setValue (prm.get(), dontSendNotification);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    slider.setBounds (r);
}

void ParamKnob::paint (Graphics& g)
{
    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f, 1));
    g.drawFittedText (prm.name, getLocalBounds().removeFromTop (16), Justification::centred, 1);
    if (midiText.isNotEmpty())
    {
        g.setColour (theme::accent);
        g.setFont (theme::font (10.0f, 2));
        g.drawText ("MIDI", getLocalBounds().removeFromTop (16).removeFromRight (30), Justification::centredRight);
    }
}

void showMidiLearnMenu (Component& target, NAMHomeProcessor& proc, Block& block, int index, std::function<void()> resetToDefault)
{
    PopupMenu m;
    auto& prm = *block.params[(size_t) index];
    m.addSectionHeader (prm.name);
    const auto current = proc.engine.describeMapping (block.id, index);
    m.addItem (1, current.isEmpty() ? "MIDI learn (move a pedal, knob or press a footswitch)" : "MIDI learn again (now " + current + ")");
    m.addItem (2, "Clear MIDI", current.isNotEmpty());
    m.addItem (3, "Reset to default", resetToDefault != nullptr);
    if (auto* mp = proc.engine.findMapping (block.id, index); mp != nullptr && mp->toggle == 0 && prm.kind == Param::Kind::Float)
    {
        // expression pedal range: set the knob where you want it, then mark it as heel or toe
        PopupMenu r;
        r.addItem (10, "Pedal up (heel) = current setting  (now " + juce::String (juce::roundToInt (mp->lo * 100)) + "%)");
        r.addItem (11, "Pedal down (toe) = current setting  (now " + juce::String (juce::roundToInt (mp->hi * 100)) + "%)");
        r.addSeparator();
        r.addItem (12, "Curve: straight", true, mp->curve == 0);
        r.addItem (13, "Curve: slow start (fine control near the heel)", true, mp->curve == 1);
        r.addItem (14, "Curve: fast start", true, mp->curve == 2);
        r.addSeparator();
        r.addItem (15, "Reset to full range");
        m.addSubMenu ("Pedal range", r);
    }
    Component::SafePointer<Component> safe (&target);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&target), [&proc, &block, index, resetToDefault, safe] (int r)
    {
        if (r == 1) { proc.engine.armLearn (block.id, index); if (proc.onNotice) proc.onNotice ("Move or press the control on your MIDI device now..."); }
        if (r == 2) proc.engine.clearMappingsFor (block.id, index);
        if (r == 3 && resetToDefault) resetToDefault();
        if (r == 10 || r == 11) { proc.engine.setMappingEnd (block.id, index, r == 11, block.params[(size_t) index]->getNorm()); proc.dirty = true; }
        if (r >= 12 && r <= 14) { proc.engine.setMappingCurve (block.id, index, r - 12); proc.dirty = true; }
        if (r == 15) { proc.engine.resetMappingRange (block.id, index); proc.dirty = true; }
        if (safe != nullptr) safe->repaint();
    });
}

void ParamKnob::mouseDown (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    showMidiLearnMenu (*this, proc, block, index, [this] { slider.setValue (prm.def); });
    midiText = proc.engine.describeMapping (block.id, index);
}

//==============================================================================
ParamSegments::ParamSegments (NAMHomeProcessor& p, Block& b, int i, Colour c)
    : proc (p), block (b), index (i), prm (*b.params[(size_t) i]), colour (c) {}

Array<Rectangle<int>> ParamSegments::layout (int width) const
{
    Array<Rectangle<int>> out;
    int x = 0, y = 0;
    for (auto& s : prm.choices)
    {
        const int w = (int) GlyphArrangement::getStringWidth (theme::font (13.0f, 1), s) + 26;
        if (x + w > width && x > 0) { x = 0; y += 34; }
        out.add ({ x, y, w, 30 });
        x += w + 6;
    }
    return out;
}

int ParamSegments::preferredHeight (int width) const
{
    const auto l = layout (width);
    return l.isEmpty() ? 30 : l.getLast().getBottom();
}

void ParamSegments::paint (Graphics& g)
{
    const auto l = layout (getWidth());
    const int cur = prm.getInt();
    for (int i = 0; i < l.size(); ++i)
    {
        const bool on = i == cur;
        g.setColour (on ? colour : (i == hoverIdx ? theme::hover : theme::raised));
        g.fillRoundedRectangle (l[i].toFloat(), 8.0f);
        g.setColour (on ? Colour (0xff111111) : theme::text);
        g.setFont (theme::font (13.0f, on ? 2 : 1));
        g.drawText (prm.choices[i], l[i], Justification::centred);
    }
}

void ParamSegments::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMidiLearnMenu (*this, proc, block, index, [this] { prm.set (prm.def); repaint(); if (onChange) onChange(); }); return; }
    const auto l = layout (getWidth());
    for (int i = 0; i < l.size(); ++i)
        if (l[i].contains (e.getPosition()))
        {
            prm.set ((float) i);
            proc.dirty = true;
            repaint();
            if (onChange) onChange();
        }
}

//==============================================================================
ParamCombo::ParamCombo (NAMHomeProcessor& p, Block& b, int i) : proc (p), block (b), index (i), prm (*b.params[(size_t) i])
{
    name.addMouseListener (this, false);
    name.setText (prm.name, dontSendNotification);
    name.setFont (theme::font (12.0f, 1));
    name.setColour (Label::textColourId, theme::muted);
    name.setJustificationType (Justification::centred);
    box.addItemList (prm.choices, 1);
    box.setSelectedItemIndex (prm.getInt(), dontSendNotification);
    box.onChange = [this]
    {
        prm.set ((float) box.getSelectedItemIndex());
        proc.dirty = true;
        if (onChange) onChange();
    };
    addAndMakeVisible (name);
    addAndMakeVisible (box);
}

void ParamCombo::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (18));
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), 30));
}

void ParamCombo::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showMidiLearnMenu (*this, proc, block, index, [this] { prm.set (prm.def); sync(); if (onChange) onChange(); });
}

void ParamCombo::sync()
{
    if (box.getSelectedItemIndex() != prm.getInt()) box.setSelectedItemIndex (prm.getInt(), dontSendNotification);
}

//==============================================================================
void ParamSwitch::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMidiLearnMenu (*this, proc, block, index, [this] { prm.set (prm.def); sync(); if (onChange) onChange(); }); return; }
    ToggleButton::mouseDown (e);
}

ParamSwitch::ParamSwitch (NAMHomeProcessor& p, Block& b, int i) : ToggleButton (b.params[(size_t) i]->name), proc (p), block (b), index (i), prm (*b.params[(size_t) i])
{
    setToggleState (prm.on(), dontSendNotification);
    onClick = [this]
    {
        prm.set (getToggleState() ? 1.0f : 0.0f);
        proc.dirty = true;
        if (onChange) onChange();
    };
}

//==============================================================================
ValueKnob::ValueKnob (const String& label, float lo, float hi, float def, const String& unit,
                      std::function<float()> get, std::function<void (float)> set, Colour c)
    : name (label), getter (std::move (get))
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 90, 18);
    slider.setColour (Slider::rotarySliderFillColourId, c);
    slider.setRange (lo, hi, unit == "pan" ? 0.01 : (unit == "st" || unit == "c") ? 1.0 : 0.1);
    slider.setDoubleClickReturnValue (true, def);
    slider.setValue (getter(), dontSendNotification);
    slider.textFromValueFunction = [unit] (double v)
    {
        if (unit == "pan")
        {
            const int pc = roundToInt (v * 100);
            return pc == 0 ? String ("C") : pc < 0 ? "L" + String (-pc) : "R" + String (pc);
        }
        if (unit == "%") return String (roundToInt (v)) + "%";
        if (unit == "st") return (v > 0 ? "+" : "") + String (roundToInt (v)) + " st";
        if (unit == "c") return (v > 0 ? "+" : "") + String (roundToInt (v)) + " cents";
        return (v > 0.05 ? "+" : "") + String (v, 1) + " " + unit;
    };
    slider.updateText();
    slider.onValueChange = [this, set] { set ((float) slider.getValue()); };
    addAndMakeVisible (slider);
}

void ValueKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    slider.setBounds (r);
}

void ValueKnob::paint (Graphics& g)
{
    g.setColour (theme::muted);
    g.setFont (theme::font (12.0f, 1));
    g.drawFittedText (name, getLocalBounds().removeFromTop (16), Justification::centred, 1);
}

//==============================================================================
void Meter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::raised);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    const float db = Decibels::gainToDecibels (level, -60.0f);
    const float w = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (w <= 0) return;
    g.setColour (db > -1.0f ? theme::warn : db > -9.0f ? theme::amber : theme::good);
    g.fillRoundedRectangle (r.withWidth (jmax (r.getHeight(), r.getWidth() * w)), r.getHeight() * 0.5f);
}

//==============================================================================
void Toasts::show (const String& text, bool warning)
{
    items.add ({ text, warning, Time::getMillisecondCounterHiRes() + (warning ? 7000.0 : 4000.0) });
    while (items.size() > 3) items.remove (0);
    startTimerHz (20);
    repaint();
}

void Toasts::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    for (int i = items.size(); --i >= 0;)
        if (items[i].until < now) items.remove (i);
    if (items.isEmpty()) stopTimer();
    repaint();
}

void Toasts::paint (Graphics& g)
{
    const double now = Time::getMillisecondCounterHiRes();
    int y = getHeight() - 16;
    for (int i = items.size(); --i >= 0;)
    {
        auto& it = items.getReference (i);
        const float alpha = (float) jlimit (0.0, 1.0, (it.until - now) / 400.0);
        const auto f = theme::font (13.5f, 1);
        const int w = jmin (getWidth() - 40, (int) GlyphArrangement::getStringWidth (f, it.text) + 44);
        auto r = Rectangle<int> ((getWidth() - w) / 2, y - 40, w, 40);
        g.setColour (Colour (0xff1b1b1e).withAlpha (0.97f * alpha));
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour ((it.warning ? theme::warn : theme::accent).withAlpha (alpha));
        g.fillEllipse ((float) r.getX() + 16, (float) r.getCentreY() - 4, 8, 8);
        g.setColour (theme::text.withAlpha (alpha));
        g.setFont (f);
        g.drawFittedText (it.text, r.withTrimmedLeft (32).withTrimmedRight (12), Justification::centredLeft, 2);
        y -= 48;
    }
}

//==============================================================================
void IconButton::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    if (over || down || lit)
    {
        g.setColour (lit ? theme::accent.withAlpha (0.2f) : theme::hover);
        g.fillRoundedRectangle (r, 8.0f);
    }
    const auto c = lit ? theme::accent : (isEnabled() ? theme::text : theme::faint);
    g.setColour (c);
    auto a = r.withSizeKeepingCentre (18, 18);
    Path p;
    const PathStrokeType stroke (1.7f, PathStrokeType::curved, PathStrokeType::rounded);
    switch (icon)
    {
        case Icon::Gear:
        {
            const auto cc = a.getCentre();
            for (int k = 0; k < 8; ++k)
            {
                const float ang = k * MathConstants<float>::twoPi / 8.0f;
                p.startNewSubPath (cc + Point<float> (std::sin (ang), -std::cos (ang)) * 6.5f);
                p.lineTo (cc + Point<float> (std::sin (ang), -std::cos (ang)) * 9.0f);
            }
            g.strokePath (p, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
            g.drawEllipse (a.reduced (3.0f), 1.7f);
            g.fillEllipse (a.withSizeKeepingCentre (4, 4));
            return;
        }
        case Icon::Fullscreen:
            p.startNewSubPath (a.getX(), a.getY() + 6); p.lineTo (a.getX(), a.getY()); p.lineTo (a.getX() + 6, a.getY());
            p.startNewSubPath (a.getRight() - 6, a.getY()); p.lineTo (a.getRight(), a.getY()); p.lineTo (a.getRight(), a.getY() + 6);
            p.startNewSubPath (a.getRight(), a.getBottom() - 6); p.lineTo (a.getRight(), a.getBottom()); p.lineTo (a.getRight() - 6, a.getBottom());
            p.startNewSubPath (a.getX() + 6, a.getBottom()); p.lineTo (a.getX(), a.getBottom()); p.lineTo (a.getX(), a.getBottom() - 6);
            break;
        case Icon::Library:
            p.addRoundedRectangle (a.getX(), a.getY() + 1, 18, 16, 3);
            p.startNewSubPath (a.getX() + 6, a.getY() + 1); p.lineTo (a.getX() + 6, a.getBottom() - 1);
            break;
        case Icon::Metronome:
            p.startNewSubPath (a.getX() + 3, a.getBottom()); p.lineTo (a.getX() + 7, a.getY()); p.lineTo (a.getRight() - 7, a.getY());
            p.lineTo (a.getRight() - 3, a.getBottom()); p.closeSubPath();
            p.startNewSubPath (a.getCentreX(), a.getBottom() - 4); p.lineTo (a.getRight() - 1, a.getY() + 3);
            break;
        case Icon::Close:
            p.startNewSubPath (a.getX() + 3, a.getY() + 3); p.lineTo (a.getRight() - 3, a.getBottom() - 3);
            p.startNewSubPath (a.getRight() - 3, a.getY() + 3); p.lineTo (a.getX() + 3, a.getBottom() - 3);
            break;
        case Icon::Chevron:
            p.startNewSubPath (a.getX() + 3, a.getY() + 6); p.lineTo (a.getCentreX(), a.getBottom() - 5); p.lineTo (a.getRight() - 3, a.getY() + 6);
            break;
        case Icon::Plus:
            p.startNewSubPath (a.getCentreX(), a.getY() + 2); p.lineTo (a.getCentreX(), a.getBottom() - 2);
            p.startNewSubPath (a.getX() + 2, a.getCentreY()); p.lineTo (a.getRight() - 2, a.getCentreY());
            break;
        case Icon::Save:
            p.addRoundedRectangle (a.reduced (1), 3);
            p.startNewSubPath (a.getX() + 5, a.getY() + 1); p.lineTo (a.getX() + 5, a.getY() + 6); p.lineTo (a.getRight() - 5, a.getY() + 6); p.lineTo (a.getRight() - 5, a.getY() + 1);
            break;
        case Icon::Prev:
            p.startNewSubPath (a.getCentreX() + 3, a.getY() + 3); p.lineTo (a.getCentreX() - 3, a.getCentreY()); p.lineTo (a.getCentreX() + 3, a.getBottom() - 3);
            break;
        case Icon::Next:
            p.startNewSubPath (a.getCentreX() - 3, a.getY() + 3); p.lineTo (a.getCentreX() + 3, a.getCentreY()); p.lineTo (a.getCentreX() - 3, a.getBottom() - 3);
            break;
        case Icon::Midi:
            g.drawEllipse (a.reduced (1), 1.7f);
            for (int k = 0; k < 5; ++k)
            {
                const float ang = MathConstants<float>::pi * (0.75f + k * 0.125f);
                g.fillEllipse (Rectangle<float> (3, 3).withCentre (a.getCentre() + Point<float> (std::sin (ang), std::cos (ang)) * 4.5f));
            }
            return;
        case Icon::Tuner:
            p.startNewSubPath (a.getX() + 2, a.getBottom() - 3);
            p.addCentredArc (a.getCentreX(), a.getBottom() - 3, 8, 8, 0, -1.3f, 1.3f, false);
            p.startNewSubPath (a.getCentreX(), a.getBottom() - 3); p.lineTo (a.getCentreX() + 3, a.getY() + 3);
            break;
        case Icon::Spectrum:
        {
            // a few bars of a spectrum
            const float hs[] = { 0.45f, 0.85f, 0.6f, 1.0f, 0.4f };
            for (int k = 0; k < 5; ++k)
            {
                const float x = a.getX() + 1.5f + (float) k * (a.getWidth() - 3.0f) / 4.0f;
                p.startNewSubPath (x, a.getBottom() - 1.0f); p.lineTo (x, a.getBottom() - 1.0f - hs[k] * (a.getHeight() - 3.0f));
            }
            break;
        }
        case Icon::Power:
            p.addCentredArc (a.getCentreX(), a.getCentreY() + 1, 7, 7, 0, 0.6f, MathConstants<float>::twoPi - 0.6f, true);
            p.startNewSubPath (a.getCentreX(), a.getY()); p.lineTo (a.getCentreX(), a.getCentreY());
            break;
    }
    g.strokePath (p, stroke);
}
