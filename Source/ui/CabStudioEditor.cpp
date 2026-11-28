#include "CabStudioEditor.h"
#include "Look.h"
#include "PluginProcessor.h"
#include "BinaryData.h"
#include <set>

using namespace juce;

namespace
{
#if JUCE_WEB_BROWSER
// the 3D stage's page and three.js come from inside the app (no network)
std::optional<WebBrowserComponent::Resource> stageResource (const String& url)
{
    auto name = url.upToFirstOccurrenceOf ("?", false, false).fromLastOccurrenceOf ("/", false, false);
    if (name.isEmpty()) name = "stage.html";
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (String (BinaryData::originalFilenames[i]) != name) continue;
        int size = 0;
        const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        if (data == nullptr) return std::nullopt;
        WebBrowserComponent::Resource r;
        r.data.resize ((size_t) size);
        std::memcpy (r.data.data(), data, (size_t) size);
        r.mimeType = name.endsWith (".html") ? "text/html" : name.endsWith (".js") ? "text/javascript" : "text/plain";
        return r;
    }
    return std::nullopt;
}

class Stage : public WebBrowserComponent
{
public:
    explicit Stage (std::function<void (const var&)> onEvent)
        : WebBrowserComponent (Options()
                                   .withBackend (Options::Backend::defaultBackend)
                                   .withNativeIntegrationEnabled()
                                   .withResourceProvider (stageResource)
                                   .withEventListener ("cabEvent", [this] (const var& v) { if (handler) handler (v); })
                                   .withKeepPageLoadedWhenBrowserIsHidden()),
          handler (std::move (onEvent))
    {
        goToURL (getResourceProviderRoot() + "stage.html");
    }
    std::function<void (const var&)> handler;
};
#endif

// a placeholder when the 3D view isn't available (builds without the web view)
struct NoStage : public Component
{
    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black);
        g.setColour (theme::faint);
        g.setFont (theme::font (13.0f));
        g.drawFittedText ("The 3D view isn't available in this build. The controls below still work.", getLocalBounds().reduced (20), Justification::centred, 3);
    }
};

String configText (const cab::Entry& e)
{
    StringArray s;
    s.add (e.back == 2 ? "Open back" : e.back == 1 ? "Closed back" : "Back not stated");
    s.add (String (e.speakers > 0 ? e.speakers : 1) + String::fromUTF8 (" \xc3\x97 ") + String (e.size) + "\"");
    if (e.tweed) s.add ("tweed");
    const auto spk = e.speakerTypes();
    if (spk.size() > 1) s.add (String (spk.size()) + " speakers to choose from");
    else if (e.speaker.isNotEmpty()) s.add (e.speaker);
    int close = 0; for (auto& i : e.irs) if (! i.room) ++close;
    s.add (String (close) + " IRs");
    return s.joinIntoString (String::fromUTF8 (" \xc2\xb7 "));
}
}

//==============================================================================
CabStudioEditor::CabStudioEditor (NAMHomeProcessor& p, std::shared_ptr<Block> b)
    : BlockEditor (p, b), cs (dynamic_cast<CabStudioBlock&> (*b))
{
    // the mode switch sits above the stage (the generic layout skips ungrouped controls)
    for (int i = 0; i < (int) cs.params.size(); ++i)
        if (cs.params[(size_t) i]->id == "mode")
        {
            modeBar = std::make_unique<ParamSegments> (proc, cs, i, cs.getColour());
            modeBar->onChange = [this] { resized(); repaint(); };
            addAndMakeVisible (*modeBar);
        }
    for (auto* c : combos) if (cs.params[(size_t) (int) c->getProperties()["index"]]->id == "mode") c->setVisible (false);

    search.setTextToShowWhenEmpty ("Search cabs", theme::faint);
    search.onTextChange = [this] { refreshList(); };
    addAndMakeVisible (search);
    list.setRowHeight (40);
    list.setColour (ListBox::backgroundColourId, theme::card);
    addAndMakeVisible (list);
    favBtn.setClickingTogglesState (true);
    favBtn.onClick = [this] { refreshList(); };
    importBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Import IRs (zips, folders or WAVs)", File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"), "*.zip;*.wav;*.aif;*.aiff");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectDirectories | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc)
                              {
                                  auto files = fc.getResults();
                                  if (files.isEmpty()) return;
                                  if (! cab::Library::get().root().getParentDirectory().exists())
                                  {
                                      if (onError) onError ("Your library drive isn't connected, so nothing was imported.");
                                      return;
                                  }
                                  cab::Library::get().importFiles (files);
                              });
    };
    inboxBtn.onClick = []
    {
        auto in = cab::Library::get().inbox();
        if (cab::Library::get().root().getParentDirectory().exists()) { in.createDirectory(); in.revealToUser(); }
    };
    for (auto* bt : { &importBtn, &inboxBtn, &favBtn }) addAndMakeVisible (*bt);

    speakerBox.setTextWhenNothingSelected ("Speaker");
    speakerBox.onChange = [this]
    {
        const int i = speakerBox.getSelectedItemIndex();
        if (i >= 0 && i < speakerList.size()) { cs.setSpeakerType (speakerList[i]); proc.dirty = true; }
    };
    addChildComponent (speakerBox);

#if JUCE_WEB_BROWSER
    stage = std::make_unique<Stage> ([this] (const var& v)
    {
        // events come in on the message thread
        Component::SafePointer<CabStudioEditor> safe (this);
        MessageManager::callAsync ([safe, v] { if (safe != nullptr) safe->onStageEvent (v); });
    });
#else
    stage = std::make_unique<NoStage>();
#endif
    addAndMakeVisible (*stage);
    cab::Library::get().rescan();   // picks up anything new in the Inbox
    refreshList();
    resized();
}

CabStudioEditor::~CabStudioEditor() { list.setModel (nullptr); }

//==============================================================================
void CabStudioEditor::refreshList()
{
    auto& lib = cab::Library::get();
    libVersion = lib.version();
    const auto q = search.getText().trim().toLowerCase();
    shown.clear();
    for (auto& e : lib.entries())
    {
        if (favBtn.getToggleState() && ! e.favourite) continue;
        const auto hay = (e.name + " " + e.pack + " " + e.config() + " " + e.speakerTypes().joinIntoString (" ") + (e.tweed ? " tweed" : "")).toLowerCase();
        if (q.isNotEmpty() && ! hay.contains (q)) continue;
        shown.push_back (e);
    }
    std::stable_sort (shown.begin(), shown.end(), [] (const cab::Entry& a, const cab::Entry& b) { return a.favourite > b.favourite; });
    list.updateContent();
    list.repaint();
    // speakers of the current cab
    cab::Entry cur;
    speakerList.clear();
    speakerBox.clear (dontSendNotification);
    if (lib.find (cs.getCab(), cur)) speakerList = cur.speakerTypes();
    if (speakerList.size() > 1)
    {
        for (int i = 0; i < speakerList.size(); ++i) speakerBox.addItem (speakerList[i], i + 1);
        int sel = speakerList.indexOf (cs.getSpeakerType());
        if (sel < 0) { sel = 0; cs.setSpeakerType (speakerList[0]); }
        speakerBox.setSelectedItemIndex (sel, dontSendNotification);
    }
    speakerBox.setVisible (speakerList.size() > 1);
    sendState (true);
}

void CabStudioEditor::paintListBoxItem (int row, Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = shown[(size_t) row];
    const bool cur = e.id == cs.getCab();
    if (cur) { g.setColour (cs.getColour().withAlpha (0.18f)); g.fillRoundedRectangle (2.0f, 2.0f, (float) w - 4, (float) h - 4, 6.0f); }
    // a little swatch: tweed, or black tolex
    g.setColour (e.tweed ? Colour (0xffc9a868) : Colour (0xff2a2a2e));
    g.fillRoundedRectangle (8.0f, 9.0f, 22.0f, 22.0f, 4.0f);
    g.setColour (Colour (0xff111113));
    g.fillEllipse (12.0f, 13.0f, 14.0f, 14.0f);
    g.setColour (cur ? theme::text : theme::text.withAlpha (0.9f));
    g.setFont (theme::font (13.0f, 1));
    g.drawText ((e.favourite ? String::fromUTF8 ("\xe2\x98\x85 ") : String()) + e.name, 38, 4, w - 44, 18, Justification::centredLeft, true);
    g.setColour (theme::faint);
    g.setFont (theme::font (11.0f));
    g.drawText (e.config() + (e.back == 2 ? " open" : e.back == 1 ? " closed" : "") + String::fromUTF8 (" \xc2\xb7 ") + e.pack, 38, 21, w - 44, 15, Justification::centredLeft, true);
}

void CabStudioEditor::listBoxItemClicked (int row, const MouseEvent& m)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto e = shown[(size_t) row];
    if (m.mods.isPopupMenu())
    {
        PopupMenu menu;
        menu.addItem (1, e.favourite ? "Remove from favourites" : "Add to favourites");
        menu.addItem (2, "Show in Finder");
        menu.showMenuAsync (PopupMenu::Options(), [this, e] (int r)
        {
            if (r == 1) { cab::Library::get().setFavourite (e.id, ! e.favourite); refreshList(); }
            if (r == 2) cab::Library::get().store().getChildFile (e.id).revealToUser();
        });
        return;
    }
    if (e.id != cs.getCab())
    {
        cs.setCab (e.id);
        cs.setSpeakerType ({});
        proc.dirty = true;
        refreshList();
        repaint();
    }
}

//==============================================================================
var CabStudioEditor::stateVar() const
{
    auto* root = new DynamicObject();
    cab::Entry e;
    if (cab::Library::get().find (cs.getCab(), e))
    {
        auto* c = new DynamicObject();
        c->setProperty ("id", e.id);
        c->setProperty ("name", e.name);
        c->setProperty ("info", configText (e));
        c->setProperty ("speakers", e.speakers > 0 ? e.speakers : 1);
        c->setProperty ("size", e.size);
        c->setProperty ("back", e.back);
        c->setProperty ("tweed", e.tweed);
        // the distances from the centre the pack measured (for the chosen speaker)
        Array<var> spots;
        const auto want = cs.getSpeakerType();
        std::set<int> seen;
        for (auto& i : e.irs)
        {
            if (i.room || i.r < 0) continue;
            if (want.isNotEmpty() && i.speakerType.isNotEmpty() && i.speakerType != want) continue;
            const int key = roundToInt (i.r * 10);
            if (seen.insert (key).second) spots.add (i.r);
        }
        c->setProperty ("spots", spots);
        root->setProperty ("cab", var (c));
    }
    Array<var> mics;
    const auto inf = cs.info();
    for (int k = 0; k < 2; ++k)
    {
        const String s = k == 0 ? "a" : "b";
        auto* m = new DynamicObject();
        m->setProperty ("on", cs.param (s + "On")->on() && cs.param ("mode")->getInt() == 0);
        m->setProperty ("model", cs.param (s + "Model")->getInt());
        m->setProperty ("spk", cs.param (s + "Spk")->getInt());
        m->setProperty ("pos", cs.param (s + "Pos")->get());
        m->setProperty ("clock", cs.clockAngle (k));
        m->setProperty ("dist", cs.param (s + "Dist")->get());
        m->setProperty ("angle", cs.param (s + "Angle")->get());
        m->setProperty ("measured", inf.measured[k]);
        mics.add (var (m));
    }
    root->setProperty ("mics", mics);
    root->setProperty ("selected", selectedMic);
    return var (root);
}

void CabStudioEditor::sendState (bool force)
{
#if JUCE_WEB_BROWSER
    auto* web = dynamic_cast<WebBrowserComponent*> (stage.get());
    if (web == nullptr) return;
    const auto v = stateVar();
    const auto json = JSON::toString (v, true);
    if (! force && json == lastState) return;
    lastState = json;
    web->emitEventIfBrowserIsVisible ("cabState", v);
#else
    ignoreUnused (force);
#endif
}

void CabStudioEditor::onStageEvent (const var& v)
{
    const auto type = v.getProperty ("type", {}).toString();
    if (type == "ready") { sendState (true); return; }
    if (type == "select") { selectedMic = (int) v.getProperty ("i", 0); return; }
    if (type == "mic")
    {
        const int k = jlimit (0, 1, (int) v.getProperty ("i", 0));
        const String s = k == 0 ? "a" : "b";
        cs.param (s + "Spk")->set ((float) (int) v.getProperty ("spk", 0));
        cs.param (s + "Pos")->set ((float) (double) v.getProperty ("pos", 1.0));
        cs.param (s + "Dist")->set ((float) (double) v.getProperty ("dist", 1.0));
        cs.param (s + "Angle")->set ((float) (double) v.getProperty ("angle", 0.0));
        cs.setClockAngle (k, (float) (double) v.getProperty ("clock", 0.0));
        selectedMic = k;
        proc.dirty = true;
        lastState = JSON::toString (stateVar(), true);   // the page already shows this
        for (auto* kn : knobs) kn->sync();
    }
}

//==============================================================================
void CabStudioEditor::sync()
{
    BlockEditor::sync();
    if (cab::Library::get().version() != libVersion) refreshList();
    sendState (false);
    repaint (sideArea);
}

Rectangle<int> CabStudioEditor::layoutSpecial (Rectangle<int> r)
{
    const bool enhance = cs.param ("mode")->getInt() == 1;
    // knobs along the bottom; library, stage and readouts above
    const int knobsH = jlimit (190, 250, r.getHeight() / 3);
    auto top = r.removeFromTop (jmax (120, r.getHeight() - knobsH));
    r.removeFromTop (8);

    libArea = top.removeFromLeft (jlimit (190, 260, top.getWidth() / 5));
    top.removeFromLeft (10);
    sideArea = top.removeFromRight (jlimit (180, 240, top.getWidth() / 5));
    top.removeFromRight (10);
    stageArea = top;

    auto lib = libArea;
    search.setBounds (lib.removeFromTop (28));
    lib.removeFromTop (6);
    auto btns = lib.removeFromBottom (28);
    lib.removeFromBottom (6);
    list.setBounds (lib);
    const int bw = (btns.getWidth() - 8) / 3;
    importBtn.setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (4);
    inboxBtn.setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (4);
    favBtn.setBounds (btns);

    auto side = sideArea;
    if (modeBar != nullptr) { modeBar->setBounds (side.removeFromTop (modeBar->preferredHeight (side.getWidth()))); side.removeFromTop (8); }
    if (speakerBox.isVisible()) { speakerBox.setBounds (side.removeFromTop (28)); side.removeFromTop (8); }
    infoArea = side;

    stage->setVisible (! enhance);
    stage->setBounds (stageArea);
    return r;
}

void CabStudioEditor::paint (Graphics& g)
{
    BlockEditor::paint (g);
    const auto inf = cs.info();
    auto a = infoArea;
    g.setFont (theme::font (11.0f, 2));
    if (cs.param ("mode")->getInt() == 1)
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (12.0f));
        g.drawFittedText ("Enhance only: for amp + cab captures. No second cab is added; just the cabinet controls below (cuts, resonance, character, room and EQ).", a.removeFromTop (100), Justification::topLeft, 7);
        g.setColour (Colours::black);
        g.fillRect (stageArea);
        g.setColour (theme::faint);
        g.drawFittedText ("The cab is already in your capture.", stageArea, Justification::centred, 2);
        return;
    }
    if (cs.getCab().isEmpty() || inf.cabMissing)
    {
        g.setColour (theme::muted);
        g.setFont (theme::font (12.0f));
        g.drawFittedText (inf.cabMissing ? "This rig's cab isn't in your library (is the SSD connected?)." : "Pick a cab from the list.", a.removeFromTop (60), Justification::topLeft, 4);
    }
    else
    {
        static const Colour micCol[2] { Colour (0xff00f0d0), Colour (0xffff2fa0) };
        for (int k = 0; k < 2; ++k)
        {
            const String s = k == 0 ? "a" : "b";
            if (! cs.param (s + "On")->on()) continue;
            auto box = a.removeFromTop (74);
            a.removeFromTop (6);
            g.setColour (theme::raised);
            g.fillRoundedRectangle (box.toFloat(), 8.0f);
            g.setColour (micCol[k]);
            g.fillRoundedRectangle (box.removeFromLeft (4).toFloat(), 2.0f);
            box.reduce (10, 6);
            g.setFont (theme::font (12.0f, 2));
            g.drawText (String (k == 0 ? "Mic A" : "Mic B") + String::fromUTF8 (" \xc2\xb7 ") + cs.param (s + "Model")->text(), box.removeFromTop (16), Justification::centredLeft);
            g.setColour (inf.measured[k] ? theme::good : theme::amber);
            g.setFont (theme::font (10.5f, 2));
            g.drawText (inf.measured[k] ? "MEASURED" : "MODELLED", box.removeFromTop (14), Justification::centredLeft);
            g.setColour (theme::muted);
            g.setFont (theme::font (11.0f));
            g.drawFittedText (inf.source[k], box, Justification::topLeft, 2);
        }
        if (cs.param ("aOn")->on() && cs.param ("bOn")->on())
        {
            auto box = a.removeFromTop (44);
            g.setColour (theme::muted);
            g.setFont (theme::font (11.0f, 1));
            const float ms = (float) inf.alignSamples / 48.0f;
            g.drawText ("Phase match" + String (cs.param ("autoAlign")->on() ? " (auto)" : ""), box.removeFromTop (16), Justification::centredLeft);
            auto bar = box.removeFromTop (8).toFloat();
            g.setColour (theme::raised);
            g.fillRoundedRectangle (bar, 4.0f);
            g.setColour (inf.phaseMatch > 0.75f ? theme::good : inf.phaseMatch > 0.5f ? theme::amber : theme::warn);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * inf.phaseMatch), 4.0f);
            g.setColour (theme::faint);
            g.setFont (theme::font (10.5f));
            g.drawText ("B moved " + String (ms, 2) + " ms", box, Justification::centredLeft);
        }
    }
    const auto st = cab::Library::get().status();
    if (st.isNotEmpty())
    {
        g.setColour (theme::faint);
        g.setFont (theme::font (10.5f));
        g.drawFittedText (st, a.removeFromBottom (30), Justification::bottomLeft, 2);
    }
}
