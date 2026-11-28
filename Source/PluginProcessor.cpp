#include "PluginProcessor.h"
#include "cab/CabLibrary.h"
#include "Rhythm.h"
#include "PluginEditor.h"

//==============================================================================
PresetManager::PresetManager (const juce::File& libraryFolder)
    : folder (libraryFolder.getChildFile ("Presets"))
{
    folder.createDirectory();
}

juce::String PresetManager::cleanName (const juce::String& s)
{
    return juce::File::createLegalFileName (s.trim()).substring (0, 60);
}

juce::StringArray PresetManager::list() const
{
    juce::StringArray names;
    for (const auto& e : juce::RangedDirectoryIterator (folder, false, "*.namrig", juce::File::findFiles))
        names.add (e.getFile().getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool PresetManager::save (const juce::String& name, const juce::var& rig)
{
    if (cleanName (name).isEmpty()) return false;
    return fileFor (name).replaceWithText (juce::JSON::toString (rig));
}

juce::var PresetManager::load (const juce::String& name) const
{
    const auto f = fileFor (name);
    return f.existsAsFile() ? juce::JSON::parse (f) : juce::var();
}

bool PresetManager::remove (const juce::String& name) { return fileFor (name).moveToTrash(); }

bool PresetManager::rename (const juce::String& from, const juce::String& to)
{
    if (cleanName (to).isEmpty() || exists (to)) return false;
    return fileFor (from).moveFileTo (fileFor (to));
}

//==============================================================================
NAMHomeProcessor::NAMHomeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      presets (library.getFolder())
{
    juce::PropertiesFile::Options o;
    o.applicationName = "NAM Home";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = "NAM Home";
    appSettings = std::make_unique<juce::PropertiesFile> (o);
    tone3000 = std::make_unique<Tone3000> (library, *appSettings);
    library.setPluginMode (isPlugin());
    if (! isPlugin())
    {
        // if the flag is still there, NAM Home didn't close normally last time
        auto flag = autosaveFile().getSiblingFile ("running.flag");
        crashedLastTime = flag.existsAsFile() && autosaveFile().existsAsFile();
        flag.getParentDirectory().createDirectory();
        flag.replaceWithText ("running");
    }
    loadSetlist (appSettings->getValue ("setlist"));
    engine.panicKind = appSettings->getIntValue ("panicMidiKind", -1);
    engine.panicNum = appSettings->getIntValue ("panicMidiNum", 0);
    engine.panicLatch = appSettings->getBoolValue ("panicMidiLatch", false);
    loadOutputSettings();
    ndsp::SaxSampleBank::get().setRoot (library.getFolder());
    rhythm::setLibraryFolder (library.getFolder());   // loops, drums and recordings live on the SSD
    cab::Library::get().setRoot (library.getFolder());   // Cab Studio IRs: <library>/Cabs (on the SSD)
    cab::Library::get().rescan();
    {
        // tell the installer where the library is, so downloads (sax recordings) land on the same drive
        auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("Application Support").getChildFile ("NAM Home").getChildFile ("library-path.txt");
        f.getParentDirectory().createDirectory();
        if (f.loadFileAsString().trim() != library.getFolder().getFullPathName()) f.replaceWithText (library.getFolder().getFullPathName());
    }

    buildDefaultRig();
    if (crashedLastTime)
    {
        // NAM Home closed unexpectedly: put back the rig you were playing
        const auto v = juce::JSON::parse (autosaveFile());
        if (v.isObject()) { rigFromVar (v); recoveredRig = true; dirty = true; }
    }
    startTimerHz (15);
}

NAMHomeProcessor::~NAMHomeProcessor()
{
    stopTimer();
    tone3000 = nullptr;
    // a clean quit: no crash recovery needed next time
    if (! isPlugin()) autosaveFile().getSiblingFile ("running.flag").deleteFile();
}

juce::StringArray NAMHomeProcessor::outputProfileNames()
{
    return { "Flat (studio monitors / FRFR)", "Headphones", "PA / front of house", "Small speaker / laptop", "Into a real power amp + cab" };
}

void NAMHomeProcessor::applyOutputProfile (int index)
{
    // low cut, bass, low mid, mid, presence, air, high cut
    static const float profiles[][7] = {
        { 20, 0, 0, 0, 0, 0, 20000 },
        { 30, -1.5f, 0, 0, -1.0f, -2.0f, 14000 },     // headphones: a touch less fizz and boom
        { 80, -2.0f, -1.0f, 0.5f, 0, -1.5f, 12000 },  // PA: tighter lows, sits in a mix
        { 120, 2.0f, -1.0f, 1.0f, 0, -1.0f, 16000 },  // small speakers: no subs they can't play
        { 20, 0, 0, 0, 0, 0, 20000 }                  // power amp + cab: turn your cab block off; flat here
    };
    index = juce::jlimit (0, 4, index);
    for (int k = 0; k < 7; ++k) engine.outEq[k] = profiles[index][k];
    ++engine.outEqVersion;
    appSettings->setValue ("outProfile", index + 1);
    for (int k = 0; k < 7; ++k) appSettings->setValue ("outEq" + juce::String (k), profiles[index][k]);
}

void NAMHomeProcessor::setOutputEq (int band, float v)
{
    engine.outEq[band] = v;
    ++engine.outEqVersion;
    appSettings->setValue ("outEq" + juce::String (band), v);
    appSettings->setValue ("outProfile", 99);
}

void NAMHomeProcessor::loadOutputSettings()
{
    const float def[] = { 20, 0, 0, 0, 0, 0, 20000 };
    for (int k = 0; k < 7; ++k) engine.outEq[k] = (float) appSettings->getDoubleValue ("outEq" + juce::String (k), def[k]);
    ++engine.outEqVersion;
    engine.spilloverOn = appSettings->getBoolValue ("spillover", true);
    engine.cutTailsOnScene = appSettings->getBoolValue ("cutSceneTails", false);
}

juce::File NAMHomeProcessor::autosaveFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support").getChildFile ("NAM Home").getChildFile ("autosave.namrig");
}

void NAMHomeProcessor::autosaveTick()
{
    if (isPlugin()) return;   // inside Logic, the song saves NAM Home's state
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastAutosave < 5000.0) return;
    lastAutosave = now;
    const auto text = juce::JSON::toString (rigToVar());
    if (text == lastAutosaved) return;
    lastAutosaved = text;
    autosaveFile().getParentDirectory().createDirectory();
    autosaveFile().replaceWithText (text);
}

void NAMHomeProcessor::snapshotIfChanged()
{
    const auto now = juce::JSON::toString (rigToVar(), true);
    if (lastSnapshot.isEmpty()) { lastSnapshot = now; return; }
    if (now == lastSnapshot) return;
    undoStack.push_back (lastSnapshot);
    if (undoStack.size() > 60) undoStack.erase (undoStack.begin());
    redoStack.clear();
    lastSnapshot = now;
}

bool NAMHomeProcessor::undo()
{
    snapshotIfChanged();
    if (undoStack.empty()) return false;
    redoStack.push_back (lastSnapshot);
    lastSnapshot = undoStack.back(); undoStack.pop_back();
    rigFromVar (juce::JSON::parse (lastSnapshot));
    dirty = true;
    return true;
}

bool NAMHomeProcessor::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (lastSnapshot);
    lastSnapshot = redoStack.back(); redoStack.pop_back();
    rigFromVar (juce::JSON::parse (lastSnapshot));
    dirty = true;
    return true;
}

juce::StringArray NAMHomeProcessor::setlistNames() const
{
    juce::StringArray n;
    for (const auto& e : juce::RangedDirectoryIterator (setlistFolder(), false, "*.setlist")) n.add (e.getFile().getFileNameWithoutExtension());
    n.sort (true);
    return n;
}

void NAMHomeProcessor::loadSetlist (const juce::String& name)
{
    setlist.clear(); setlistName = name; setlistPos = -1;
    if (name.isEmpty()) { appSettings->setValue ("setlist", ""); return; }
    const auto v = juce::JSON::parse (setlistFolder().getChildFile (name + ".setlist"));
    if (auto* a = v.getProperty ("songs", {}).getArray()) for (auto& x : *a) setlist.add (x.toString());
    setlistPos = setlist.indexOf (currentPreset);
    appSettings->setValue ("setlist", name);
}

void NAMHomeProcessor::saveSetlist() const
{
    if (setlistName.isEmpty()) return;
    setlistFolder().createDirectory();
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> songs; for (auto& x : setlist) songs.add (x);
    o->setProperty ("songs", songs);
    setlistFolder().getChildFile (setlistName + ".setlist").replaceWithText (juce::JSON::toString (juce::var (o)));
}

bool NAMHomeProcessor::stepSetlist (int delta)
{
    if (setlist.isEmpty()) return false;
    const int cur = setlist.indexOf (currentPreset);
    setlistPos = juce::jlimit (0, setlist.size() - 1, (cur < 0 ? (delta > 0 ? -1 : 0) : cur) + delta);
    return loadPreset (setlist[setlistPos]);
}

void NAMHomeProcessor::rememberLevelsForUndo()
{
    levelUndo.clear();
    for (int sc = 0; sc < Engine::numScenes; ++sc) levelUndo.push_back (engine.sceneTrim (sc));
    levelUndo.push_back (engine.presetTrimDb.load());
}

bool NAMHomeProcessor::undoLevelMatch()
{
    if ((int) levelUndo.size() != Engine::numScenes + 1) return false;
    for (int sc = 0; sc < Engine::numScenes; ++sc) engine.setSceneTrim (sc, levelUndo[(size_t) sc]);
    engine.presetTrimDb = levelUndo.back();
    levelUndo.clear();
    dirty = true;
    return true;
}

static juce::String signedDb (float db) { return (db >= 0 ? "+" : "") + juce::String (db, 1) + " dB"; }

juce::String NAMHomeProcessor::matchPresetLevel()
{
    const float l = engine.measuredLoudness (Engine::presetMeter);
    if (l < -90.0f) return "Play this preset for a few seconds first, so NAM Home can hear how loud it is.";
    rememberLevelsForUndo();
    engine.presetTrimDb = juce::jlimit (-12.0f, 12.0f, referenceLevelDb() - l);
    dirty = true;
    return "Preset level matched (" + signedDb (engine.presetTrimDb.load()) + "). Save the preset to keep it.";
}

float NAMHomeProcessor::presetLevelNow() const
{
    const float l = engine.measuredLoudness (Engine::presetMeter);
    return l < -90.0f ? l : l + engine.presetTrimDb.load();
}

juce::String NAMHomeProcessor::matchScenesToA()
{
    const float a = engine.measuredLoudness (0);
    if (a < -90.0f) return "Play scene A for a few seconds first.";
    rememberLevelsForUndo();
    juce::StringArray done, skipped;
    bool clamped = false;
    for (int s = 1; s < Engine::numScenes; ++s)
    {
        if (! engine.sceneIsUsed (s)) continue;
        const float l = engine.measuredLoudness (s);   // measured before the scene's trim
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + s));
        if (l < -90.0f) { skipped.add (letter); continue; }
        float t = engine.sceneTrim (0) + a - l;
        if (std::abs (t) > 12.0f) { t = juce::jlimit (-12.0f, 12.0f, t); clamped = true; }
        engine.setSceneTrim (s, t);
        done.add (letter + " " + signedDb (t));
    }
    dirty = true;
    if (done.isEmpty()) return "Play each scene for a few seconds first, then match.";
    juce::String msg = "Matched to A: " + done.joinIntoString (", ") + ".";
    if (skipped.size() > 0) msg << " Not played yet: " << skipped.joinIntoString (", ") << ".";
    if (clamped) msg << " (Limited to 12 dB.)";
    return msg + " Save the preset to keep it.";
}

void NAMHomeProcessor::buildDefaultRig()
{
    // a new rig is one empty line, like the Quad Cortex: drag blocks onto it
    engine.clearAll();
    currentPreset = {};
    dirty = false;
}

juce::String NAMHomeProcessor::sceneToNewRig (int scene)
{
    snapshotIfChanged();   // Cmd+Z brings the whole rig back
    const auto from = currentPreset.isEmpty() ? juce::String ("My rig") : currentPreset;
    const auto label = engine.sceneLabel (scene);
    engine.sceneToNewRig (scene);
    engine.presetTrimDb = 0.0f;
    currentPreset = {};
    dirty = true;
    if (onRigChanged) onRigChanged();
    return from + " - " + label.replace ("  ", " ");
}

void NAMHomeProcessor::newRig()
{
    buildDefaultRig();
    if (onRigChanged) onRigChanged();
}

bool NAMHomeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto in = l.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono() || in.isDisabled();
}

void NAMHomeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);
}

void NAMHomeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // inside Logic, delays and tempo-synced effects follow the song tempo
    if (isPlugin())
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto bpm = pos->getBpm()) engine.setBpm ((float) *bpm);
                auto& ctx = Block::context();
                const bool playing = pos->getIsPlaying();
                if (playing && ! ctx.running.load()) ++ctx.starts;
                ctx.running = playing;
                const auto ppq = pos->getPpqPosition();
                ctx.hostBeats = (playing && ppq) ? *ppq : -1.0;
            }
    engine.process (buffer, getTotalNumInputChannels(), midi);
}

void NAMHomeProcessor::timerCallback()
{
    engine.pollMessageThread();
    if (const int st = engine.setlistStepFromMidi.exchange (0); st != 0) stepPresetAny (st);   // no setlist: presets A-Z
    if (engine.panicLearned.exchange (false))
    {
        appSettings->setValue ("panicMidiKind", engine.panicKind.load());
        appSettings->setValue ("panicMidiNum", engine.panicNum.load());
        appSettings->setValue ("panicMidiLatch", engine.panicLatch.load());
        appSettings->saveIfNeeded();
        if (onNotice) onNotice ("Emergency button is now " + panicMidiText() + ".");
    }
    if (engine.panicFromMidi.exchange (false)) triggerPanic();
    if (const int want = engine.panicSetFromMidi.exchange (-1); want >= 0)
        if ((want == 1) != panicActive()) triggerPanic();
    if (! releases.empty())
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto it = releases.begin(); it != releases.end();)
        {
            if (now < it->at) { ++it; continue; }
            if (auto b = engine.findById (it->blockId)) if (it->param < (int) b->params.size()) b->params[(size_t) it->param]->set (0.0f);
            it = releases.erase (it);
        }
    }
    autosaveTick();
    const int pc = engine.pendingProgram.exchange (-1);
    if (pc >= 0)
    {
        const auto names = presets.list();
        if (pc < names.size()) loadPreset (names[pc]);
    }
}

//==============================================================================
juce::var NAMHomeProcessor::rigToVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("app", "NAM Home");
    o->setProperty ("version", 1);
    o->setProperty ("engine", engine.toVar());
    o->setProperty ("inputGain", engine.inputGainDb.load());
    o->setProperty ("outputGain", engine.outputGainDb.load());
    return juce::var (o);
}

void NAMHomeProcessor::rigFromVar (const juce::var& v)
{
    if (! v.isObject()) return;
    engine.fromVar (v.getProperty ("engine", {}));
    engine.inputGainDb = (float) v.getProperty ("inputGain", 0.0f);
    engine.outputGainDb = (float) v.getProperty ("outputGain", 0.0f);
}

bool NAMHomeProcessor::loadPreset (const juce::String& name)
{
    const auto v = presets.load (name);
    if (! v.isObject()) return false;
    rigFromVar (v);
    currentPreset = name;
    dirty = false;
    settings().setValue ("lastPreset", name);
    if (onRigChanged) onRigChanged();
    return true;
}

bool NAMHomeProcessor::savePreset (const juce::String& name)
{
    if (! presets.save (name, rigToVar())) return false;
    currentPreset = PresetManager::cleanName (name);
    dirty = false;
    settings().setValue ("lastPreset", currentPreset);
    return true;
}

void NAMHomeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto v = rigToVar();
    v.getDynamicObject()->setProperty ("preset", currentPreset);
    v.getDynamicObject()->setProperty ("dirty", dirty);
    v.getDynamicObject()->setProperty ("mono", engine.monoOut.load());
    const auto s = juce::JSON::toString (v);
    dest.replaceAll (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

void NAMHomeProcessor::setStateInformation (const void* data, int size)
{
    const auto v = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, size));
    if (! v.isObject() || ! v.hasProperty ("engine")) return;   // ignores the old Stage 1 format
    rigFromVar (v);
    currentPreset = v.getProperty ("preset", {}).toString();
    dirty = (bool) v.getProperty ("dirty", false);
    engine.monoOut = (bool) v.getProperty ("mono", false);
    if (onRigChanged) onRigChanged();
}

//==============================================================================
juce::String NAMHomeProcessor::loadFileIntoBlock (const juce::File& f, int row, int col)
{
    auto existing = engine.get (row, col);
    juce::String err;

    if (f.hasFileExtension ("nam"))
    {
        auto nb = std::dynamic_pointer_cast<NamBlock> (existing);
        if (nb == nullptr)
        {
            const auto* it = library.findByPath (f.getFullPathName());
            const bool pedal = it != nullptr && it->kind == LibraryItem::Kind::Pedal;
            nb = std::dynamic_pointer_cast<NamBlock> (engine.addBlock (pedal ? Block::Type::NamPedal : Block::Type::NamAmp, row, col));
        }
        err = nb->load (f);
        if (err.isEmpty() && nb->amp) afterAmpLoaded (row, col);
    }
    else if (f.hasFileExtension ("wav"))
    {
        auto cab = std::dynamic_pointer_cast<CabBlock> (existing);
        if (cab == nullptr) cab = std::dynamic_pointer_cast<CabBlock> (engine.addBlock (Block::Type::Cab, row, col));
        int slot = 0;
        for (int k = 0; k < CabBlock::numIrs; ++k) if (! cab->isIrLoaded (k)) { slot = k; break; }
        err = cab->loadIr (slot, f);
    }
    else err = "NAM Home takes .nam captures and .wav IRs.";

    dirty = true;
    return err;
}

void NAMHomeProcessor::afterAmpLoaded (int row, int col)
{
    auto amp = std::dynamic_pointer_cast<NamBlock> (engine.get (row, col));
    if (amp == nullptr) return;

    if (amp->includesCab())
    {
        if (! settings().getBoolValue ("autoBypassCab", true)) return;
        int n = 0;
        for (int c = col + 1; c < Engine::cols; ++c)
            if (auto cab = std::dynamic_pointer_cast<CabBlock> (engine.get (row, c))) { cab->enabled = false; ++n; }
        if (n > 0 && onNotice) onNotice ("This capture includes its cab, so the cab block after it was switched off.");
        // Cab Studio would add a second cab: switch it to Enhance only (its tone controls, no cab)
        std::vector<std::shared_ptr<Block>> studios { engine.rigCab (false) };
        for (int c = col + 1; c < Engine::cols; ++c) studios.push_back (engine.get (row, c));
        for (auto& rc : studios)
            if (rc != nullptr && rc->getType() == Block::Type::CabStudio && rc->enabled.load())
                if (auto* mode = rc->param ("mode"); mode != nullptr && mode->getInt() == 0)
                {
                    mode->set (1);
                    if (onNotice) onNotice ("This capture includes its cab, so Cab Studio switched to Enhance only.");
                }
        return;
    }

    if (! settings().getBoolValue ("autoPairIr", true)) return;
    const auto folder = juce::File (amp->getPath()).getParentDirectory();
    if (folder == library.getFolder()) return;   // only files that came together in one download/pack
    juce::Array<juce::File> irs;
    for (const auto& e : juce::RangedDirectoryIterator (folder, false, "*.wav", juce::File::findFiles))
        if (! e.getFile().getFileName().startsWithChar ('.')) irs.add (e.getFile());
    if (irs.isEmpty()) return;
    irs.sort();

    std::shared_ptr<CabBlock> cab;
    for (int c = col + 1; c < Engine::cols && cab == nullptr; ++c)
        cab = std::dynamic_pointer_cast<CabBlock> (engine.get (row, c));
    if (cab == nullptr)
        if (col + 1 < Engine::cols)
        {
            // the cab goes right after the amp; anything after slides over
            std::shared_ptr<Block> nb (Block::create (Block::Type::Cab).release());
            nb->prepareBlock (getSampleRate() > 0 ? getSampleRate() : 48000.0, juce::jmax (64, getBlockSize()));
            if (engine.insertInRow (row, col + 1, nb)) cab = std::dynamic_pointer_cast<CabBlock> (nb);
        }
    if (cab == nullptr) return;

    for (int k = 0; k < CabBlock::numIrs; ++k) cab->clearIr (k);
    for (int k = 0; k < juce::jmin (CabBlock::numIrs, irs.size()); ++k) cab->loadIr (k, irs[k]);
    cab->enabled = true;
    if (onNotice) onNotice ("Loaded the IR that came with this capture into the cab block.");
}

juce::AudioProcessorEditor* NAMHomeProcessor::createEditor() { return new NAMHomeEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NAMHomeProcessor(); }

//==============================================================================
// live playing
juce::StringArray NAMHomeProcessor::presetOrder() const
{
    return setlist.isEmpty() ? presets.list() : setlist;
}

bool NAMHomeProcessor::stepPresetAny (int delta)
{
    if (! setlist.isEmpty()) return stepSetlist (delta);
    const auto names = presets.list();
    if (names.isEmpty()) return false;
    int i = names.indexOf (currentPreset);
    i = (i < 0) ? (delta > 0 ? 0 : names.size() - 1) : (i + delta + names.size()) % names.size();
    return loadPreset (names[i]);
}

void NAMHomeProcessor::triggerPanic()
{
    const int mode = panicMode();
    if (engine.panic.load() != 0) { engine.panic = 0; if (onNotice) onNotice ("Emergency bypass off: your rig is back."); return; }
    if (mode == 2)
    {
        const auto safe = appSettings->getValue ("safePreset");
        if (safe.isNotEmpty() && presets.exists (safe)) { loadPreset (safe); if (onNotice) onNotice ("Safe preset loaded: " + safe); return; }
        engine.panic = 1;   // no safe preset chosen: dry is the safest thing
        if (onNotice) onNotice ("No safe preset chosen, so your dry guitar goes straight out. Press again to come back.");
        return;
    }
    engine.panic = mode == 1 ? 2 : 1;
    if (onNotice) onNotice (mode == 1 ? "Output muted. Press again to come back." : "Emergency bypass: dry guitar straight out. Press again to come back.");
}

juce::StringArray NAMHomeProcessor::stripActions() const
{
    juce::StringArray a;
    a.addTokens (appSettings->getValue ("liveStrip", "tuner|scene+|type:Delay|type:Drive|tap|looper"), "|", {});
    while (a.size() < stripSlots) a.add ("none");
    a.removeRange (stripSlots, a.size());
    return a;
}

void NAMHomeProcessor::setStripAction (int slot, const juce::String& action)
{
    auto a = stripActions();
    if (slot < 0 || slot >= stripSlots) return;
    a.set (slot, action);
    appSettings->setValue ("liveStrip", a.joinIntoString ("|"));
}

juce::StringArray NAMHomeProcessor::allActions()
{
    juce::StringArray a { "tuner", "tap", "scene-", "scene+", "preset-", "preset+", "looper", "freeze", "granular", "talk", "panic", "none" };
    for (auto t : Block::allTypes())
        if (t != Block::Type::NamAmp && t != Block::Type::Cab) a.add ("type:" + Block::typeName (t));
    return a;
}

juce::String NAMHomeProcessor::actionLabel (const juce::String& a)
{
    if (a == "tuner") return "Tuner";
    if (a == "tap") return "Tap";
    if (a == "scene-") return "Scene -";
    if (a == "scene+") return "Scene +";
    if (a == "preset-") return "Preset -";
    if (a == "preset+") return "Preset +";
    if (a == "looper") return "Looper";
    if (a == "freeze") return "Freeze hold";
    if (a == "granular") return "Granular hold";
    if (a == "talk") return "Talk box";
    if (a == "panic") return "Emergency";
    if (a.startsWith ("type:")) return a.fromFirstOccurrenceOf (":", false, false);
    return "(empty)";
}

namespace
{
    std::shared_ptr<Block> firstOfType (Engine& e, const juce::String& typeName)
    {
        for (int r = 0; r < Engine::rows; ++r)
            for (int c = 0; c < Engine::cols; ++c)
                if (auto b = e.get (r, c)) if (b->getTypeName() == typeName) return b;
        return nullptr;
    }
    int paramIndex (const Block& b, const juce::String& id)
    {
        for (size_t i = 0; i < b.params.size(); ++i) if (b.params[i]->id == id) return (int) i;
        return -1;
    }
}

bool NAMHomeProcessor::actionLit (const juce::String& a) const
{
    auto& e = const_cast<Engine&> (engine);
    if (a == "tuner") return engine.tunerOn.load();
    if (a == "panic") return panicActive();
    auto param = [&e] (const char* type, const char* id)
    {
        if (auto b = firstOfType (e, type)) { const int i = paramIndex (*b, id); return i >= 0 && b->params[(size_t) i]->on(); }
        return false;
    };
    if (a == "freeze") return param ("Freeze", "hold");
    if (a == "granular") return param ("Granular", "hold");
    if (a == "talk") return param ("Talk box", "talk");
    if (a.startsWith ("type:")) if (auto b = firstOfType (e, a.fromFirstOccurrenceOf (":", false, false))) return b->enabled.load();
    return false;
}

void NAMHomeProcessor::performAction (const juce::String& a)
{
    auto toggleParam = [this] (const char* type, const char* id)
    {
        if (auto b = firstOfType (engine, type)) { const int i = paramIndex (*b, id); if (i >= 0) b->params[(size_t) i]->set (b->params[(size_t) i]->on() ? 0.0f : 1.0f); }
        else if (onNotice) onNotice (juce::String ("There's no ") + type + " block in this rig.");
    };
    auto stepScene = [this] (int d)
    {
        const int cur = engine.currentScene.load();
        for (int k = 1; k <= Engine::numScenes; ++k)
        {
            const int s = (cur + d * k + Engine::numScenes * 4) % Engine::numScenes;
            if (engine.sceneIsUsed (s) || k == Engine::numScenes) { engine.selectScene (s); return; }
        }
    };
    if (a == "tuner")
    {
        if (onToggleTuner) onToggleTuner();
        else { engine.tunerOn = ! engine.tunerOn.load(); engine.tunerMute = true; }
    }
    else if (a == "tap") engine.tapTempo();
    else if (a == "scene+") stepScene (1);
    else if (a == "scene-") stepScene (-1);
    else if (a == "preset+") stepPresetAny (1);
    else if (a == "preset-") stepPresetAny (-1);
    else if (a == "panic") triggerPanic();
    else if (a == "freeze") toggleParam ("Freeze", "hold");
    else if (a == "granular") toggleParam ("Granular", "hold");
    else if (a == "talk") toggleParam ("Talk box", "talk");
    else if (a == "looper")
    {
        // a footswitch press on the looper's main button (record / play / overdub)
        if (auto b = firstOfType (engine, "Looper")) { b->params[0]->set (1.0f); releases.push_back ({ b->id, 0, juce::Time::getMillisecondCounterHiRes() + 120.0 }); }
        else if (onNotice) onNotice ("There's no Looper block in this rig.");
    }
    else if (a.startsWith ("type:"))
    {
        const auto tn = a.fromFirstOccurrenceOf (":", false, false);
        if (auto b = firstOfType (engine, tn)) { b->enabled = ! b->enabled.load(); dirty = true; }
        else if (onNotice) onNotice ("There's no " + tn + " block in this rig.");
    }
}

bool NAMHomeProcessor::revertPreset()
{
    if (currentPreset.isEmpty() || ! presets.exists (currentPreset)) return false;
    snapshotIfChanged();           // so Cmd+Z can bring your edits back
    const int scene = engine.currentScene.load();
    if (! loadPreset (currentPreset)) return false;
    if (scene != engine.currentScene.load() && engine.sceneIsUsed (scene)) engine.selectScene (scene);
    return true;
}

namespace
{
    void collectPaths (const juce::var& v, juce::StringArray& out)
    {
        if (auto* o = v.getDynamicObject())
        {
            for (auto& p : o->getProperties())
            {
                if (p.name.toString() == "path" && p.value.isString() && p.value.toString().containsChar ('/')) out.addIfNotAlreadyThere (p.value.toString());
                else collectPaths (p.value, out);
            }
        }
        else if (auto* a = v.getArray()) for (auto& x : *a) collectPaths (x, out);
    }
}

std::vector<NAMHomeProcessor::GigItem> NAMHomeProcessor::gigCheck (juce::AudioDeviceManager* dm)
{
    std::vector<GigItem> out;
    // presets: every song in the setlist (or the current preset) opens, and every capture / IR it uses is there
    juce::StringArray songs = setlist.isEmpty() ? juce::StringArray (currentPreset) : setlist;
    songs.removeEmptyStrings();
    int bad = 0, missingFiles = 0;
    juce::StringArray missingNames;
    for (auto& name : songs)
    {
        const auto v = presets.load (name);
        if (! v.isObject()) { ++bad; out.push_back ({ false, "Preset \"" + name + "\" is missing or won't open." }); continue; }
        juce::StringArray paths;
        collectPaths (v, paths);
        for (auto& p : paths)
            if (! juce::File (p).existsAsFile()) { ++missingFiles; missingNames.addIfNotAlreadyThere (juce::File (p).getFileName() + " (in " + name + ")"); }
    }
    if (songs.isEmpty()) out.push_back ({ false, "No preset loaded and no setlist. Save your rig as a preset first." });
    else if (bad == 0) out.push_back ({ true, setlist.isEmpty() ? "Current preset opens." : "All " + juce::String (songs.size()) + " setlist presets open." });
    if (missingFiles == 0 && ! songs.isEmpty()) out.push_back ({ true, "All captures and IRs are there." });
    else if (missingFiles > 0)
    {
        juce::String list = missingNames.size() > 4 ? missingNames.joinIntoString (", ", 0, 4) + "..." : missingNames.joinIntoString (", ");
        out.push_back ({ false, juce::String (missingFiles) + " capture / IR file" + (missingFiles > 1 ? "s are" : " is") + " missing (is the SSD plugged in?): " + list });
    }

    // MIDI controller
    const auto devs = juce::MidiInput::getAvailableDevices();
    if (isPlugin()) out.push_back ({ true, "MIDI comes through Logic (check the track's MIDI input there)." });
    else if (devs.isEmpty()) out.push_back ({ false, "No MIDI controller connected." });
    else
    {
        juce::StringArray on;
        for (auto& d : devs) if (dm == nullptr || dm->isMidiInputDeviceEnabled (d.identifier)) on.add (d.name);
        if (on.isEmpty()) out.push_back ({ false, "A MIDI device is connected but switched off in Settings > MIDI." });
        else out.push_back ({ true, "MIDI: " + on.joinIntoString (", ") });
    }

    // audio interface
    if (isPlugin()) out.push_back ({ true, "Audio interface is chosen in Logic." });
    else if (dm == nullptr || dm->getCurrentAudioDevice() == nullptr) out.push_back ({ false, "No audio interface is running. Pick one in Settings > Audio." });
    else
    {
        auto* d = dm->getCurrentAudioDevice();
        const auto nm = d->getName();
        const bool builtIn = nm.containsIgnoreCase ("MacBook") || nm.containsIgnoreCase ("Built-in") || nm.containsIgnoreCase ("Microphone");
        const int ins = d->getActiveInputChannels().countNumberOfSetBits();
        if (ins == 0) out.push_back ({ false, nm + " has no input switched on. Turn on input 1 in Settings > Audio." });
        else if (builtIn) out.push_back ({ false, "The audio device is " + nm + ", not your interface." });
        else out.push_back ({ true, "Interface: " + nm + ", " + juce::String (d->getCurrentSampleRate() / 1000.0, 1) + " kHz, buffer " + juce::String (d->getCurrentBufferSizeSamples()) });
    }

    // levels since the last check
    if (engine.inputPeakHold.load() > 0.98f) out.push_back ({ false, "Your input is clipping. Turn the gain down on your interface." });
    else out.push_back ({ true, "Input level is fine." });
    if (engine.limiterHoldDb.load() > 3.0f) out.push_back ({ false, "The output is hitting the safety limiter (" + juce::String (engine.limiterHoldDb.load(), 1) + " dB). Turn the output or the loudest preset down." });
    else out.push_back ({ true, "Output isn't clipping." });
    engine.inputPeakHold = 0; engine.limiterHoldDb = 0;

    // CPU
    const double cpu = engine.getCpuLoad();
    if (cpu > 0.7) out.push_back ({ false, "CPU is at " + juce::String (juce::roundToInt (cpu * 100)) + "%. A larger buffer or fewer heavy blocks will make dropouts less likely." });
    else out.push_back ({ true, "CPU is at " + juce::String (juce::roundToInt (cpu * 100)) + "%." });

    // the safe preset
    if (panicMode() == 2 && ! presets.exists (appSettings->getValue ("safePreset")))
        out.push_back ({ false, "Emergency bypass is set to a safe preset, but none is chosen (Settings > Live)." });
    return out;
}


juce::String NAMHomeProcessor::panicMidiText() const
{
    const int k = engine.panicKind.load(), n = engine.panicNum.load();
    if (k < 0) return "not set";
    juce::String s = k == Engine::kindCC ? "CC " + juce::String (n) : k == Engine::kindNote ? "Note " + juce::MidiMessage::getMidiNoteName (n, true, true, 4) : "Program " + juce::String (n + 1);
    return s + (engine.panicLatch.load() ? " (latching)" : "");
}

void NAMHomeProcessor::clearPanicMidi()
{
    engine.panicKind = -1;
    appSettings->setValue ("panicMidiKind", -1);
    appSettings->saveIfNeeded();
}
