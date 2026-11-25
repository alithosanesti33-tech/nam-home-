#include "PluginProcessor.h"
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
    loadOutputSettings();
    ndsp::SaxSampleBank::get().setRoot (library.getFolder());   // recordings live next to your captures (on your SSD)
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

juce::String NAMHomeProcessor::matchPresetLevel()
{
    const float l = engine.presetLoudness.load();
    if (l < -90.0f) return "Play this preset for a few seconds first, so NAM Home can hear how loud it is.";
    const float measuredWithoutTrim = l - engine.presetTrimDb.load();
    engine.presetTrimDb = juce::jlimit (-24.0f, 24.0f, referenceLevelDb() - measuredWithoutTrim);
    engine.presetLoudness = -100.0f;
    dirty = true;
    return "Preset level matched (" + juce::String (engine.presetTrimDb.load(), 1) + " dB). Save the preset to keep it.";
}

juce::String NAMHomeProcessor::matchScenesToA()
{
    const float a = engine.sceneLoudness[0].load();
    if (a < -90.0f) return "Play scene A for a few seconds first.";
    int done = 0;
    for (int s = 1; s < Engine::numScenes; ++s)
    {
        const float l = engine.sceneLoudness[s].load();
        if (l < -90.0f) continue;
        const float rawA = a - engine.sceneTrim (0), raw = l - engine.sceneTrim (s);
        engine.setSceneTrim (s, engine.sceneTrim (0) + rawA - raw);
        engine.sceneLoudness[s] = -100.0f;
        ++done;
    }
    dirty = true;
    return done == 0 ? juce::String ("Play each scene for a few seconds first, then match.")
                     : "Matched " + juce::String (done) + " scene" + (done > 1 ? "s" : "") + " to scene A. Save the preset to keep it.";
}

void NAMHomeProcessor::buildDefaultRig()
{
    // a new rig is one empty line, like the Quad Cortex: drag blocks onto it
    engine.clearAll();
    currentPreset = {};
    dirty = false;
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
                if (auto bpm = pos->getBpm()) engine.setBpm ((float) *bpm);
    engine.process (buffer, getTotalNumInputChannels(), midi);
}

void NAMHomeProcessor::timerCallback()
{
    engine.pollMessageThread();
    if (const int st = engine.setlistStepFromMidi.exchange (0); st != 0) stepSetlist (st);
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
