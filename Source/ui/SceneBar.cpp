#include "SceneBar.h"
#include "Look.h"
#include "../PluginProcessor.h"

using namespace juce;

SceneBar::SceneBar (NAMHomeProcessor& p) : proc (p)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (15);
}

void SceneBar::timerCallback()
{
    const int s = proc.engine.currentScene.load(), v = proc.engine.sceneVersion.load();
    if (s != shownScene || v != shownVersion)
    {
        const bool changed = shownScene >= 0 && s != shownScene;
        shownScene = s; shownVersion = v;
        repaint();
        if (changed && onChanged) onChanged();   // knobs moved: refresh the open editor
    }
}

Rectangle<float> SceneBar::padBounds (int i) const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (70);
    const float gap = 8.0f, w = (r.getWidth() - gap * (Engine::numScenes - 1)) / Engine::numScenes;
    return { r.getX() + (float) i * (w + gap), r.getY() + 4.0f, w, r.getHeight() - 8.0f };
}

int SceneBar::padAt (Point<int> p) const
{
    for (int i = 0; i < Engine::numScenes; ++i)
        if (padBounds (i).contains (p.toFloat())) return i;
    return -1;
}

void SceneBar::paint (Graphics& g)
{
    g.setColour (theme::faint);
    g.setFont (theme::font (11.5f, 2));
    g.drawText ("SCENES", 4, 0, 66, getHeight(), Justification::centredLeft);
    auto& e = proc.engine;
    for (int i = 0; i < Engine::numScenes; ++i)
    {
        const auto b = padBounds (i);
        const bool cur = i == e.currentScene.load();
        const bool used = e.scenes[(size_t) i].valid || cur;
        g.setColour (cur ? theme::raised : (i == hover ? theme::hover : theme::card));
        g.fillRoundedRectangle (b, 9.0f);
        g.setColour (cur ? theme::accent : theme::line);
        g.drawRoundedRectangle (b.reduced (0.5f), 9.0f, cur ? 1.6f : 1.0f);
        // footswitch LED
        g.setColour (cur ? theme::accent : (used ? theme::muted.withAlpha (0.5f) : theme::line));
        g.fillEllipse (b.getX() + 12.0f, b.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setColour (cur ? theme::text : (used ? theme::muted : theme::faint));
        g.setFont (theme::font (13.0f, cur ? 2 : 1));
        g.drawFittedText (e.sceneLabel (i), b.withTrimmedLeft (26).withTrimmedRight (8).toNearestInt(), Justification::centredLeft, 1);
        const auto midi = e.describeMapping (Engine::sceneMidiId, i);
        if (midi.isNotEmpty() && b.getWidth() > 110)
        {
            g.setColour (theme::faint);
            g.setFont (theme::font (10.5f));
            g.drawText ("MIDI", b.withTrimmedRight (8).toNearestInt(), Justification::centredRight);
        }
    }
}

void SceneBar::mouseMove (const MouseEvent& m) { const int h = padAt (m.getPosition()); if (h != hover) { hover = h; repaint(); } }
void SceneBar::mouseExit (const MouseEvent&) { hover = -1; repaint(); }

void SceneBar::mouseUp (const MouseEvent& m)
{
    const int i = padAt (m.getPosition());
    if (i < 0) return;
    if (m.mods.isPopupMenu()) { showMenu (i); return; }
    proc.engine.selectScene (i);
    proc.dirty = true;
    repaint();
    if (onChanged) onChanged();
}

void SceneBar::showMenu (int i)
{
    auto& e = proc.engine;
    PopupMenu m, copyTo;
    m.addSectionHeader ("Scene " + e.sceneLabel (i));
    m.addItem (1, "Rename...");
    for (int k = 0; k < Engine::numScenes; ++k)
        if (k != i) copyTo.addItem (100 + k, e.sceneLabel (k));
    m.addSubMenu ("Copy this scene to", copyTo);
    m.addItem (30, "Copy this scene into a new rig (as scene A)");
    m.addSeparator();
    const auto midi = e.describeMapping (Engine::sceneMidiId, i);
    m.addItem (2, midi.isEmpty() ? "MIDI learn a footswitch for this scene" : "MIDI learn again (now " + midi + ")");
    if (midi.isNotEmpty()) m.addItem (3, "Clear MIDI");
    m.addSeparator();
    PopupMenu lvl;
    lvl.addItem (20, "Match all scenes to scene A (play each one for a few seconds first)");
    lvl.addItem (21, "Match the whole preset to my reference level");
    lvl.addItem (22, "Use this preset's level as my reference");
    lvl.addSeparator();
    const float t = e.sceneTrim (i);
    lvl.addItem (24, "Undo the last level match", proc.canUndoLevelMatch());
    lvl.addItem (25, "Measure again (after changing sounds)");
    lvl.addItem (23, "Reset this scene's level (now " + (t >= 0 ? juce::String ("+") : juce::String()) + juce::String (t, 1) + " dB)", std::abs (t) > 0.05f);
    m.addSubMenu ("Level matching", lvl);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (padBounds (i).toNearestInt())),
                     [this, i] (int r)
    {
        auto& en = proc.engine;
        auto say = [this] (const juce::String& t) { if (proc.onNotice) proc.onNotice (t); };
        if (r == 30 && onSceneToNewRig) { onSceneToNewRig (i); return; }
        if (r == 20) say (proc.matchScenesToA());
        if (r == 21) say (proc.matchPresetLevel());
        if (r == 22)
        {
            const float l = proc.presetLevelNow();
            if (l < -90.0f) say ("Play this preset for a few seconds first.");
            else { proc.setReferenceLevel (l); say ("Reference level set. Use \"Match the whole preset\" on your other presets."); }
        }
        if (r == 23) { proc.rememberLevelsForUndo(); en.setSceneTrim (i, 0.0f); proc.dirty = true; }
        if (r == 24) say (proc.undoLevelMatch() ? "Levels put back the way they were." : "Nothing to undo.");
        if (r == 25) { for (int m = 0; m <= Engine::presetMeter; ++m) en.resetLoudness (m); say ("Measuring again: play each scene for a few seconds."); }
        if (r == 1)
        {
            auto* aw = new AlertWindow ("Rename scene", {}, MessageBoxIconType::NoIcon);
            aw->addTextEditor ("name", en.scenes[(size_t) i].name, "Name");
            aw->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
            aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            aw->enterModalState (true, ModalCallbackFunction::create ([this, i, aw] (int res)
            {
                if (res == 1)
                {
                    proc.engine.scenes[(size_t) i].name = aw->getTextEditorContents ("name").trim().substring (0, 16);
                    ++proc.engine.sceneVersion;
                    proc.dirty = true;
                }
            }), true);
        }
        if (r == 2)
        {
            en.armLearn (Engine::sceneMidiId, i);
            if (onMessage) onMessage ("Press the footswitch for scene " + en.sceneLabel (i) + " now...", false);
        }
        if (r == 3) { en.clearMappingsFor (Engine::sceneMidiId, i); proc.dirty = true; }
        if (r >= 100) { en.copyScene (i, r - 100); proc.dirty = true; if (onChanged) onChanged(); }
        repaint();
    });
}
