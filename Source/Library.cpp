#include "Library.h"
#include "Measure.h"
#include "json.hpp"
#include <fstream>

//==============================================================================
juce::String LibraryItem::creditLine() const
{
    const auto who = creator.isNotEmpty() ? creator : juce::String ("unknown (not in file)");
    return (kind == Kind::IR ? "IR by " : "Captured by ") + who;
}

GainLevel gainLevelFromToneType (const juce::String& t)
{
    const auto s = t.toLowerCase().replaceCharacter ('-', '_').replace ("high_gain", "hi_gain").replace ("highgain", "hi_gain");
    if (s == "clean")                           return GainLevel::Clean;
    if (s == "crunch")                          return GainLevel::Crunch;
    if (s == "overdrive")                       return GainLevel::Overdrive;
    if (s == "hi_gain" || s == "high_gain")     return GainLevel::HighGain;
    if (s == "fuzz")                            return GainLevel::Fuzz;
    return GainLevel::Unknown;
}

juce::Colour gainLevelColour (GainLevel g)
{
    switch (g)
    {
        case GainLevel::Clean:     return juce::Colour (0xff3b82f6); // blue
        case GainLevel::Crunch:    return juce::Colour (0xffeab308); // yellow
        case GainLevel::Overdrive: return juce::Colour (0xfff97316); // orange
        case GainLevel::HighGain:  return juce::Colour (0xffef4444); // red
        case GainLevel::Fuzz:      return juce::Colour (0xffb91c1c); // deep red
        case GainLevel::Unknown:   break;
    }
    return juce::Colour (0xff3a3a3a);
}

juce::String gainLevelName (GainLevel g)
{
    switch (g)
    {
        case GainLevel::Clean:     return "Clean";
        case GainLevel::Crunch:    return "Crunch";
        case GainLevel::Overdrive: return "Overdrive";
        case GainLevel::HighGain:  return "High gain";
        case GainLevel::Fuzz:      return "Fuzz";
        case GainLevel::Unknown:   break;
    }
    return "Gain not set in file";
}

//==============================================================================
Library::Library() : juce::Thread ("Library measuring")
{
    formats.registerBasicFormats();
    folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("NAM Home");
    folder.createDirectory();

    juce::PropertiesFile::Options o;
    o.applicationName = "NAM Home";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = "NAM Home";
    settings = std::make_unique<juce::PropertiesFile> (o);

    loadSettings();
    loadTags();
    rescan();
    startTimer (3000);
}

Library::~Library()
{
    stopThread (5000);
    stopTimer();
    saveSettings();
    saveTags();
}

void Library::loadSettings()
{
    watchDownloads = settings->getBoolValue ("watchDownloads", true);
    watchStartMs = (juce::int64) settings->getDoubleValue ("watchStartMs", 0.0);
    if (watchStartMs == 0)
        watchStartMs = juce::Time::currentTimeMillis();
    handledDownloads.addLines (settings->getValue ("handledDownloads"));
    handledDownloads.removeEmptyStrings();
    saveSettings();
}

void Library::saveSettings()
{
    settings->setValue ("watchDownloads", watchDownloads);
    settings->setValue ("watchStartMs", (double) watchStartMs);
    // keep the list from growing forever
    while (handledDownloads.size() > 2000)
        handledDownloads.remove (0);
    settings->setValue ("handledDownloads", handledDownloads.joinIntoString ("\n"));
    settings->saveIfNeeded();
}

void Library::setWatchDownloads (bool shouldWatch)
{
    if (shouldWatch && ! watchDownloads)
        watchStartMs = juce::Time::currentTimeMillis(); // don't sweep up old downloads
    watchDownloads = shouldWatch;
    saveSettings();
}

//==============================================================================
void Library::loadTags()
{
    auto f = folder.getChildFile ("library.json");
    tags = f.existsAsFile() ? juce::JSON::parse (f) : juce::var();
    if (! tags.isObject())
        tags = juce::var (new juce::DynamicObject());
}

void Library::saveTags()
{
    folder.getChildFile ("library.json").replaceWithText (juce::JSON::toString (tags));
}

static juce::DynamicObject* entryFor (juce::var& tags, const juce::String& path)
{
    auto* root = tags.getDynamicObject();
    if (! root->hasProperty (path) || ! root->getProperty (path).isObject())
        root->setProperty (path, juce::var (new juce::DynamicObject()));
    return root->getProperty (path).getDynamicObject();
}

void Library::setMicTag (const juce::String& path, const juce::String& tag)
{
    entryFor (tags, path)->setProperty ("mic", tag);
    saveTags();
    rescan();
}

void Library::setCreator (const juce::String& path, const juce::String& creator)
{
    entryFor (tags, path)->setProperty ("creator", creator);
    saveTags();
    rescan();
}

void Library::clearNewFlags()
{
    newPaths.clear();
    for (auto& i : items) i.isNew = false;
    if (onChanged) onChanged();
}

//==============================================================================
// Reads only the "metadata" block of a .nam file; the (large) weights are skipped.
static nlohmann::json readNamMetadata (const juce::File& f)
{
    std::ifstream in (f.getFullPathName().toStdString());
    if (! in) return {};

    nlohmann::json::parser_callback_t cb = [] (int depth, nlohmann::json::parse_event_t ev, nlohmann::json& parsed)
    {
        if (depth == 1 && ev == nlohmann::json::parse_event_t::key)
            return parsed == "metadata" || parsed == "version" || parsed == "architecture";
        return true;
    };

    try { return nlohmann::json::parse (in, cb, false); }
    catch (...) { return {}; }
}

static juce::String js (const nlohmann::json& m, const char* key)
{
    if (m.is_object() && m.contains (key) && m[key].is_string())
        return juce::String (m[key].get<std::string>());
    return {};
}

LibraryItem Library::readItem (const juce::File& f) const
{
    LibraryItem it;
    it.file = f;
    it.name = f.getFileNameWithoutExtension();
    if (f.getParentDirectory() != folder)
        it.pack = f.getParentDirectory().getFileName();

    const auto path = f.getFullPathName();
    auto* root = tags.getDynamicObject();
    juce::var entry = root->getProperty (path);

    if (f.hasFileExtension ("wav"))
    {
        it.kind = LibraryItem::Kind::IR;
    }
    else
    {
        // cache metadata by modification time so rescans stay fast
        const auto mtime = (double) f.getLastModificationTime().toMilliseconds();
        juce::var meta;
        if (entry.isObject() && (double) entry.getProperty ("mtime", 0.0) == mtime)
            meta = entry.getProperty ("meta", {});

        if (! meta.isObject())
        {
            auto j = readNamMetadata (f);
            nlohmann::json m = (j.is_object() && j.contains ("metadata")) ? j["metadata"] : nlohmann::json();
            auto* o = new juce::DynamicObject();
            for (auto* k : { "name", "modeled_by", "gear_make", "gear_model", "gear_type", "tone_type" })
                o->setProperty (k, js (m, k));
            meta = juce::var (o);
            auto* e = entryFor (const_cast<Library*> (this)->tags, path);
            e->setProperty ("meta", meta);
            e->setProperty ("mtime", mtime);
            entry = root->getProperty (path);
        }

        auto get = [&meta] (const char* k) { return meta.getProperty (k, {}).toString(); };
        if (get ("name").isNotEmpty()) it.name = get ("name");
        it.creator   = get ("modeled_by");
        it.gearMake  = get ("gear_make");
        it.gearModel = get ("gear_model");
        it.gearType  = get ("gear_type");
        it.toneType  = get ("tone_type");
        // "pedal" alone is a pedal; "pedal_amp", "amp_cab" etc. include an amp, so they go in the amp slot
        it.kind = it.gearType.equalsIgnoreCase ("pedal") ? LibraryItem::Kind::Pedal : LibraryItem::Kind::Amp;
    }

    if (entry.isObject())
    {
        it.micTag = entry.getProperty ("mic", {}).toString();
        const auto userCreator = entry.getProperty ("creator", {}).toString();
        if (userCreator.isNotEmpty()) it.creator = userCreator;
    }
    if (it.kind == LibraryItem::Kind::IR && it.creator.isEmpty() && it.pack.isNotEmpty())
        it.creator = it.pack + " (pack folder)";

    if (entry.isObject())
    {
        const auto mt = (double) f.getLastModificationTime().toMilliseconds();
        if ((double) entry.getProperty ("measuredAt", 0.0) == mt)
        {
            it.gain = (float) (double) entry.getProperty ("gainScore", -1.0);
            it.bright = (float) (double) entry.getProperty ("brightScore", -1.0);
            measure::remember (path, it.kind == LibraryItem::Kind::IR ? it.bright : it.gain);
        }
    }
    it.isNew = newPaths.contains (path);
    return it;
}

// sax recordings, loops and drum sounds live in the library folder too, but they aren't IRs
static bool isMediaFile (const juce::File& f, const juce::File& root)
{
    const auto rel = f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    for (auto* top : { "Instruments/", "Loops/", "Drums/", "Setlists/", "Presets/" })
        if (rel.startsWithIgnoreCase (top)) return true;
    return false;
}

void Library::rescan()
{
    items.clear();
    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.nam;*.wav", juce::File::findFiles))
    {
        const auto f = entry.getFile();
        if (f.getFileName().startsWithChar ('.') || f.getFullPathName().contains ("__MACOSX") || isMediaFile (f, folder))
            continue;
        items.push_back (readItem (f));
    }
    std::sort (items.begin(), items.end(), [] (const LibraryItem& a, const LibraryItem& b)
               { return a.name.compareNatural (b.name) < 0; });
    saveTags();
    if (onChanged) onChanged();
    measureInBackground();
}

const LibraryItem* Library::findByPath (const juce::String& path) const
{
    for (auto& i : items)
        if (i.file.getFullPathName() == path)
            return &i;
    return nullptr;
}

//==============================================================================
bool Library::isShortWav (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->sampleRate <= 0) return false;
    return (double) r->lengthInSamples / r->sampleRate <= maxIrSeconds;
}

int Library::importOne (const juce::File& f, const juce::File& destFolder)
{
    if (f.hasFileExtension ("wav") && ! isShortWav (f)) return 0;
    if (! f.hasFileExtension ("nam;wav")) return 0;

    destFolder.createDirectory();
    auto dest = destFolder.getChildFile (f.getFileName());
    if (dest.existsAsFile())
    {
        if (dest.getSize() == f.getSize()) return 0; // already have it
        dest = dest.getNonexistentSibling();
    }
    if (! f.copyFileTo (dest)) return 0;
    newPaths.add (dest.getFullPathName());
    return 1;
}

int Library::importZip (const juce::File& zipFile)
{
    juce::ZipFile zip (zipFile);
    const auto dest = folder.getChildFile (zipFile.getFileNameWithoutExtension());
    int count = 0;

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        auto* e = zip.getEntry (i);
        const auto name = e->filename;
        const auto base = name.fromLastOccurrenceOf ("/", false, false);
        if (name.contains ("__MACOSX") || base.startsWithChar ('.')) continue;
        if (! (base.endsWithIgnoreCase (".nam") || base.endsWithIgnoreCase (".wav"))) continue;

        dest.createDirectory();
        auto out = dest.getChildFile (base);
        if (out.existsAsFile()) continue;

        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        if (in == nullptr) continue;
        {
            juce::FileOutputStream os (out);
            if (! os.openedOk()) continue;
            os.writeFromInputStream (*in, -1);
        }
        if (base.endsWithIgnoreCase (".wav") && ! isShortWav (out)) { out.deleteFile(); continue; }
        newPaths.add (out.getFullPathName());
        ++count;
    }
    if (count == 0) dest.deleteRecursively (false);
    return count;
}

int Library::importFiles (const juce::Array<juce::File>& files)
{
    int count = 0;
    for (auto& f : files)
        count += f.hasFileExtension ("zip") ? importZip (f) : importOne (f, folder);
    if (count > 0) rescan();
    return count;
}

void Library::timerCallback()
{
    if (pluginMode)
    {
        // every ~6 s: has the app (or Finder) added or removed anything in the library folder?
        if (++signatureTick < 2) return;
        signatureTick = 0;
        const auto sig = computeSignature();
        if (folderSignature.isNotEmpty() && sig != folderSignature) { rescan(); if (onChanged) onChanged(); }
        folderSignature = sig;
        return;
    }
    if (watchDownloads)
        checkDownloads();
}

juce::String Library::computeSignature() const
{
    juce::int64 total = 0, newest = 0;
    int count = 0;
    for (const auto& e : juce::RangedDirectoryIterator (folder, true, "*.nam;*.wav", juce::File::findFiles))
    {
        if (isMediaFile (e.getFile(), folder)) continue;
        ++count;
        total += e.getFileSize();
        newest = juce::jmax (newest, e.getModificationTime().toMilliseconds());
    }
    return juce::String (count) + ":" + juce::String (total) + ":" + juce::String (newest);
}

void Library::checkDownloads()
{
    const auto downloads = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    if (! downloads.isDirectory()) return;

    int added = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (downloads, false, "*.nam;*.wav;*.zip", juce::File::findFiles))
    {
        const auto f = entry.getFile();
        const auto mtime = f.getLastModificationTime().toMilliseconds();
        const auto created = f.getCreationTime().toMilliseconds();
        if (juce::jmax (mtime, created) < watchStartMs) continue;

        const auto key = f.getFullPathName() + "|" + juce::String (f.getSize()) + "|" + juce::String (mtime);
        if (handledDownloads.contains (key)) continue;

        // wait until the size has stopped changing (download finished)
        const auto size = f.getSize();
        auto it = pendingSizes.find (f.getFullPathName());
        if (it == pendingSizes.end() || it->second != size)
        {
            pendingSizes[f.getFullPathName()] = size;
            continue;
        }
        pendingSizes.erase (it);

        added += f.hasFileExtension ("zip") ? importZip (f) : importOne (f, folder);
        handledDownloads.add (key);
        saveSettings();
    }

    if (added > 0)
    {
        rescan();
        if (onNotice)
            onNotice (juce::String (added) + (added == 1 ? " new file" : " new files") + " added from Downloads");
    }
}

juce::String LibraryItem::ampGroup() const
{
    // the amp a capture belongs to: its own make/model info, else the pack folder, else the file name
    auto clean = [] (juce::String x) { return x.replaceCharacters ("_-", "  ").trim(); };
    juce::String g = (gearMake + " " + gearModel).trim();
    if (g.isEmpty() && pack.isNotEmpty()) g = clean (pack);
    if (g.isEmpty())
    {
        auto words = juce::StringArray::fromTokens (clean (file.getFileNameWithoutExtension()), " ", {});
        words.removeEmptyStrings();
        while (words.size() > 2) words.remove (words.size() - 1);
        g = words.joinIntoString (" ");
    }
    return g.isEmpty() ? juce::String ("Other") : g;
}

void Library::measureInBackground()
{
    if (isThreadRunning()) return;
    toMeasure.clear();
    for (auto& it : items)
        if ((it.kind == LibraryItem::Kind::IR ? it.bright : it.gain) < 0) toMeasure.push_back ({ it.file, it.kind == LibraryItem::Kind::IR });
    if (! toMeasure.empty()) startThread (juce::Thread::Priority::low);
}

void Library::run()
{
    // measure each capture / IR once; results are kept in library.json with the file's date
    const auto todo = toMeasure;
    int done = 0;
    for (auto& [f, isIr] : todo)
    {
        if (threadShouldExit()) return;
        const float v = isIr ? measure::irBrightness (f) : measure::namGain (f);
        const auto path = f.getFullPathName();
        const double mt = (double) f.getLastModificationTime().toMilliseconds();
        juce::MessageManager::callAsync ([this, path, v, isIr, mt]
        {
            auto* e = entryFor (tags, path);
            e->setProperty (isIr ? "brightScore" : "gainScore", v);
            e->setProperty ("measuredAt", mt);
            measure::remember (path, v);
            for (auto& it : items) if (it.file.getFullPathName() == path) (isIr ? it.bright : it.gain) = v;
        });
        if (++done % 8 == 0) juce::MessageManager::callAsync ([this] { saveTags(); if (onChanged) onChanged(); });
    }
    juce::MessageManager::callAsync ([this] { saveTags(); if (onChanged) onChanged(); });
}
