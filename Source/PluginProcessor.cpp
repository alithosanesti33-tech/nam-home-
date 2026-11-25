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

    buildDefaultRig();
    startTimerHz (15);
}

NAMHomeProcessor::~NAMHomeProcessor() { stopTimer(); tone3000 = nullptr; }

void NAMHomeProcessor::buildDefaultRig()
{
    engine.clearAll();
    engine.addBlock (Block::Type::Gate, 0, 0);
    engine.addBlock (Block::Type::NamAmp, 0, 1);
    engine.addBlock (Block::Type::Cab, 0, 2);
    engine.addBlock (Block::Type::Delay, 0, 4)->enabled = false;
    auto rv = engine.addBlock (Block::Type::Reverb, 0, 5);
    rv->param ("mix")->set (18);
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
    engine.process (buffer, getTotalNumInputChannels(), midi);
}

void NAMHomeProcessor::timerCallback()
{
    engine.pollMessageThread();
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
        for (int c = col + 1; c < Engine::cols && cab == nullptr; ++c)
            if (engine.get (row, c) == nullptr)
                cab = std::dynamic_pointer_cast<CabBlock> (engine.addBlock (Block::Type::Cab, row, c));
    if (cab == nullptr) return;

    for (int k = 0; k < CabBlock::numIrs; ++k) cab->clearIr (k);
    for (int k = 0; k < juce::jmin (CabBlock::numIrs, irs.size()); ++k) cab->loadIr (k, irs[k]);
    cab->enabled = true;
    if (onNotice) onNotice ("Loaded the IR that came with this capture into the cab block.");
}

juce::AudioProcessorEditor* NAMHomeProcessor::createEditor() { return new NAMHomeEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NAMHomeProcessor(); }
