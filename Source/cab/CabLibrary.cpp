#include "CabLibrary.h"

namespace cab
{
const char* micName (Mic m)
{
    switch (m)
    {
        case Mic::SM57:  return "SM57-style dynamic";
        case Mic::MD421: return "MD421-style dynamic";
        case Mic::R121:  return "R-121-style ribbon";
        case Mic::U87:   return "U87-style condenser";
        default:         return "Other mic";
    }
}
const char* micShort (Mic m)
{
    switch (m) { case Mic::SM57: return "SM57"; case Mic::MD421: return "MD421"; case Mic::R121: return "R-121"; case Mic::U87: return "U87"; default: return "Other"; }
}
juce::String micId (Mic m)
{
    switch (m) { case Mic::SM57: return "sm57"; case Mic::MD421: return "md421"; case Mic::R121: return "r121"; case Mic::U87: return "u87"; default: return "other"; }
}
Mic micFromId (const juce::String& id)
{
    for (auto m : { Mic::SM57, Mic::MD421, Mic::R121, Mic::U87 }) if (micId (m) == id) return m;
    return Mic::Other;
}

juce::String Entry::config() const
{
    const int n = speakers > 0 ? speakers : 1;
    return juce::String (n) + "x" + juce::String (size);
}
juce::StringArray Entry::speakerTypes() const
{
    juce::StringArray t;
    for (auto& i : irs) if (! i.room) t.addIfNotAlreadyThere (i.speakerType.isNotEmpty() ? i.speakerType : (speaker.isNotEmpty() ? speaker : juce::String ("Stock speaker")));
    return t;
}
bool Entry::hasMic (Mic mic) const { for (auto& i : irs) if (i.mic == mic && ! i.room) return true; return false; }
juce::String Entry::description() const
{
    juce::StringArray parts;
    parts.add (config());
    if (speaker.isNotEmpty()) parts.add (speaker);
    if (back == 1) parts.add ("closed back"); else if (back == 2) parts.add ("open back");
    int close = 0; for (auto& i : irs) if (! i.room) ++close;
    parts.add (juce::String (close) + " IR" + (close == 1 ? "" : "s"));
    return parts.joinIntoString (" · ");
}

//==============================================================================
// Reading facts from names. Packs name things in many ways ("SM57_CapEdge_1in", "57 cone 2in",
// "R121 off-axis 45", "Neumann U87 6 inch"...), so this looks for each fact on its own.
static juce::StringArray words (const juce::String& s)
{
    juce::StringArray w;
    juce::String cur;
    for (auto c : s)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (c) || c == '.') cur << juce::String::charToString (c);
        else { if (cur.isNotEmpty()) w.add (cur); cur.clear(); }
    }
    if (cur.isNotEmpty()) w.add (cur);
    return w;
}

void parseName (const juce::String& path, Ir& ir)
{
    // the extension is not part of the name ("cap 1in.wav" must read as 1 in)
    const auto p = path.endsWithIgnoreCase (".wav") || path.endsWithIgnoreCase (".aif") || path.endsWithIgnoreCase (".aiff") || path.endsWithIgnoreCase (".flac")
                       ? path.upToLastOccurrenceOf (".", false, false) : path;
    const auto w = words (p);
    auto has = [&w] (const char* t) { return w.contains (t); };
    // microphone
    if (p.contains ("sm57") || p.contains ("sm 57") || has ("57") || p.contains ("sm-57") || has ("or57") || has ("545") || has ("545sd") || has ("beta57")) ir.mic = Mic::SM57;
    else if (p.contains ("md421") || p.contains ("md 421") || has ("421") || p.contains ("md-421")) ir.mic = Mic::MD421;
    else if (p.contains ("r121") || p.contains ("r-121") || p.contains ("r 121") || has ("121") || p.contains ("royer")) ir.mic = Mic::R121;
    else if (p.contains ("u87") || p.contains ("u 87") || p.contains ("u-87") || has ("87") || p.contains ("neumann")) ir.mic = Mic::U87;
    else
    {
        ir.mic = Mic::Other;
        static const char* others[] = { "c414", "414", "e906", "906", "m160", "160", "sm7b", "sm7", "m201", "201", "e609", "609", "md441", "441", "tlm", "km184", "nt5", "ksm32", "sm58", "beta57", "e602", "u47", "r10", "r84", "c12", "m88" };
        for (auto* o : others) if (p.contains (o)) { ir.micLabel = juce::String (o).toUpperCase(); break; }
    }
    // room / ambient mics
    // (whole words: "Marshall" is not a hall)
    ir.room = has ("room") || has ("rm") || p.contains ("ambien") || has ("hall") || has ("far") || has ("amb");
    // position across the speaker
    const bool cap = p.contains ("cap"), edge = p.contains ("edge");
    const bool offCentre = p.contains ("off cent") || p.contains ("offcent") || p.contains ("off-cent") || p.contains ("off_cent");   // "1.5in Off Center" is a distance from the centre
    if (p.contains ("capedge") || p.contains ("cap edge") || p.contains ("cap-edge") || p.contains ("cap_edge") || (cap && edge)) ir.r = 2.0f;
    else if (((p.contains ("cent") || p.contains ("cntr")) && ! offCentre) || p.contains ("dustcap") || cap) ir.r = 0.5f;
    else if (p.contains ("coneedge") || p.contains ("cone edge") || p.contains ("outer") || p.contains ("surround")) ir.r = 5.0f;
    else if (p.contains ("cone")) ir.r = 3.5f;
    else if (edge) ir.r = 5.0f;
    // distance and angle: a number followed by a unit
    juce::Array<float> inches;
    for (int i = 0; i < w.size(); ++i)
    {
        const auto t = w[i];
        auto num = [] (const juce::String& s) { return s.retainCharacters ("0123456789.").getFloatValue(); };
        const auto next = i + 1 < w.size() ? w[i + 1] : juce::String();
        const bool digits = t.containsAnyOf ("0123456789");
        if (! digits) continue;
        if (t.endsWith ("in") || t.endsWith ("inch") || t.endsWith ("inches") || next == "in" || next == "inch" || next == "inches") inches.add (num (t));
        else if (t.endsWith ("cm") || next == "cm") ir.dist = num (t) / 2.54f;
        else if (t.endsWith ("mm") || next == "mm") ir.dist = num (t) / 25.4f;
        else if (t.endsWith ("deg") || t.endsWith ("degrees") || next == "deg" || next == "degrees") ir.angle = num (t);
    }
    // one inch value is the distance; two ("1.5in Off Center ... 0in", "A12Q 013 2.0in 1.0in") are
    // the position from the centre, then the distance
    if (inches.size() == 1) ir.dist = inches[0];
    else if (inches.size() >= 2) { if (ir.r < 0) ir.r = inches[0]; ir.dist = inches[1]; }
    for (auto& t : w) if (t.startsWith ("oa") && t.length() > 2 && t.substring (2).containsOnly ("0123456789")) ir.angle = (float) t.substring (2).getIntValue();
    if (p.contains ("\"")) { const auto q = p.upToFirstOccurrenceOf ("\"", false, false); const auto lw = words (q); if (! lw.isEmpty()) ir.dist = lw[lw.size() - 1].retainCharacters ("0123456789.").getFloatValue(); }
    if (ir.angle < 0 && (p.contains ("offaxis") || p.contains ("off axis") || p.contains ("off-axis") || has ("ofx") || has ("oa") || p.contains ("angled") || p.contains ("tilted"))) ir.angle = 45.0f;
    if (ir.angle < 0 && (p.contains ("onaxis") || p.contains ("on axis") || p.contains ("on-axis"))) ir.angle = 0.0f;
    if (p.contains ("close") && ir.dist < 0) ir.dist = 0.5f;
    // which speaker, when stated ("spk2", "speaker 3", "top left"...)
    for (int i = 0; i < w.size(); ++i)
        if ((w[i] == "spk" || w[i] == "speaker" || w[i] == "spkr") && i + 1 < w.size()) ir.speaker = w[i + 1].getIntValue() - 1;
        else if (w[i].startsWith ("spk") && w[i].length() == 4) ir.speaker = w[i].getLastCharacters (1).getIntValue() - 1;
}

void parseCab (const juce::String& path, Entry& e)
{
    const auto p = path.endsWithIgnoreCase (".wav") ? path.upToLastOccurrenceOf (".", false, false) : path;
    struct Sz { const char* t; int n, s; };
    static const Sz sizes[] = { { "4x12", 4, 12 }, { "412", 4, 12 }, { "2x12", 2, 12 }, { "212", 2, 12 }, { "1x12", 1, 12 }, { "112", 1, 12 },
                                { "4x10", 4, 10 }, { "410", 4, 10 }, { "2x10", 2, 10 }, { "1x10", 1, 10 }, { "1x15", 1, 15 }, { "1x8", 1, 8 }, { "1960", 4, 12 }, { "twin", 2, 12 }, { "ac30", 2, 12 }, { "ac 30", 2, 12 }, { "bassman", 4, 10 }, { "champ", 1, 8 }, { "princeton", 1, 10 }, { "deluxe", 1, 12 }, { "magnatone", 1, 12 }, { "bogner", 4, 12 }, { "rectifier", 4, 12 }, { "oversized", 4, 12 } };
    for (auto& s : sizes) if (p.contains (s.t)) { e.speakers = s.n; e.size = s.s; break; }
    struct Sp { const char* t; const char* name; };
    static const Sp spk[] = { { "v30", "V30-style" }, { "vintage 30", "V30-style" }, { "greenback", "Greenback-style" }, { "g12m", "Greenback-style" }, { "g12h", "G12H-style" },
                              { "t75", "G12T-75-style" }, { "g12-65", "G12-65-style" }, { "creamback", "Creamback-style" }, { "p12r", "Jensen P12R-style" }, { "p12q", "Jensen P12Q-style" }, { "p12n", "Jensen P12N-style" }, { "c12n", "Jensen C12N-style" }, { "c12k", "Jensen C12K-style" }, { "c12r", "Jensen C12R-style" },
                              { "a12q", "Weber A12Q-style" }, { "speed shop", "Weber-style" }, { "weber", "Weber-style" }, { "oxford", "Oxford-style" }, { "utah", "Utah-style" },
                              { "alnico blue", "Alnico blue-style" }, { "blue", "Alnico blue-style" }, { "alnico", "Alnico-style" }, { "jbl", "JBL D120-style" }, { "d120", "JBL D120-style" }, { "e120", "JBL E120-style" }, { "gold", "Alnico Gold-style" }, { "jensen", "Jensen-style" }, { "p12", "Jensen-style" }, { "evm", "EVM-12L" }, { "eminence", "Eminence" }, { "k100", "G12K-100-style" } };
    for (auto& s : spk) if (p.contains (s.t)) { e.speaker = s.name; break; }
    if (p.contains ("open")) e.back = 2; else if (p.contains ("closed") || p.contains ("sealed")) e.back = 1;
    // tweed-era Fenders: 1x12 Deluxe (5E3/5C3), Champ (1x8), Bassman (4x10), Pro (1x15), Twin
    if (p.contains ("tweed") || p.contains ("5e3") || p.contains ("5c3") || p.contains ("5d3") || p.contains ("5f1") || p.contains ("5f6") || p.contains ("deluxe 57") || p.contains ("57 deluxe") || p.contains ("58 deluxe") || p.contains ("1958 deluxe"))
    {
        e.tweed = true;
        if (e.back == 0) e.back = 2;
        if (e.speakers == 0)
        {
            if (p.contains ("5f6") || p.contains ("bassman")) { e.speakers = 4; e.size = 10; }
            else if (p.contains ("5f1") || p.contains ("champ")) { e.speakers = 1; e.size = 8; }
            else { e.speakers = 1; e.size = 12; }
        }
    }
}

//==============================================================================
Library& Library::get() { static Library l; return l; }

void Library::setRoot (const juce::File& libraryFolder)
{
    {
        std::lock_guard<std::mutex> l (m);
        rootDir = libraryFolder.getChildFile ("Cabs");
        favourites.clear();
        const auto v = juce::JSON::parse (rootDir.getChildFile ("cabs.json"));
        if (auto* fav = v.getProperty ("favourites", {}).getArray()) for (auto& f : *fav) favourites[f.toString()] = true;
    }
    rescan();
}

juce::File Library::root() const { std::lock_guard<std::mutex> l (m); return rootDir; }

void Library::rescan()
{
    again = true;
    if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
    else notify();
}

void Library::importFiles (const juce::Array<juce::File>& files)
{
    // on the same drive this is a quick copy; the importer then sorts them
    if (! root().getParentDirectory().exists()) return;   // SSD not mounted: never fall back to the internal disk
    inbox().createDirectory();
    for (auto& f : files) f.copyFileTo (inbox().getChildFile (f.getFileName()));
    for (auto& f : files) if (f.isDirectory()) f.copyDirectoryTo (inbox().getChildFile (f.getFileName()));
    rescan();
}

std::vector<Entry> Library::entries() const { std::lock_guard<std::mutex> l (m); return list; }
bool Library::find (const juce::String& id, Entry& out) const
{
    std::lock_guard<std::mutex> l (m);
    for (auto& e : list) if (e.id == id) { out = e; return true; }
    return false;
}
juce::String Library::status() const { std::lock_guard<std::mutex> l (m); return statusText; }

void Library::setFavourite (const juce::String& id, bool on)
{
    {
        std::lock_guard<std::mutex> l (m);
        if (on) favourites[id] = true; else favourites.erase (id);
        for (auto& e : list) if (e.id == id) e.favourite = on;
    }
    save();
    ++changes;
}

void Library::save() const
{
    juce::Array<juce::var> fav;
    juce::File dir;
    {
        std::lock_guard<std::mutex> l (m);
        for (auto& [k, v] : favourites) if (v) fav.add (k);
        dir = rootDir;
    }
    if (dir == juce::File() || ! dir.getParentDirectory().exists()) return;   // never create it on the wrong drive
    dir.createDirectory();
    auto* o = new juce::DynamicObject();
    o->setProperty ("favourites", fav);
    dir.getChildFile ("cabs.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

void Library::run()
{
    while (! threadShouldExit())
    {
        if (again.exchange (false))
        {
            importInbox();
            index();
            ++changes;
        }
        wait (-1);
    }
}

void Library::importInbox()
{
    const auto in = inbox();
    // only work where the library already lives (the SSD); don't create folders on the internal disk
    if (! root().getParentDirectory().exists()) return;
    in.createDirectory(); store().createDirectory();
    for (const auto& e : juce::RangedDirectoryIterator (in, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto f = e.getFile();
        if (f.getFileName().startsWithChar ('.')) continue;
        if (f.hasFileExtension ("zip"))
        {
            juce::ZipFile zip (f);
            auto dest = store().getChildFile (f.getFileNameWithoutExtension());
            if (zip.uncompressTo (dest, true).wasOk()) f.deleteFile();
        }
        else if (f.isDirectory()) { auto dest = store().getChildFile (f.getFileName()); if (! dest.exists()) f.moveFileTo (dest); else { f.copyDirectoryTo (dest); f.deleteRecursively(); } }
        else if (f.hasFileExtension ("wav;aif;aiff"))
        {
            auto dir = store().getChildFile ("My IRs"); dir.createDirectory();
            f.moveFileTo (dir.getChildFile (f.getFileName()));
        }
    }
    {
        std::lock_guard<std::mutex> l (m);
        statusText = {};
    }
}

void Library::index()
{
    std::map<juce::String, Entry> cabs;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    // a sample-rate folder ("44.1k", "48kHz", "96") holds duplicates: prefer 48 kHz when it's there
    auto rateFolder = [] (const juce::String& s) { const auto t = s.toLowerCase().removeCharacters (" "); return t == "44" || t == "48" || t == "96" || t.startsWith ("44.1") || t.startsWith ("44k") || t.startsWith ("48k") || t.startsWith ("96k") || t.endsWith ("khz"); };
    std::set<juce::String> seenNames;   // the same IR in Cabs/Library and in your main library shows once

    // base: the folder to read; prefix: how its files are named in the index ("" = Cabs/Library,
    // "@lib/" = your main NAM Home library, read where the files already are)
    auto scan = [&] (const juce::File& lib, const juce::String& prefix, bool mainLibrary)
    {
        if (! lib.isDirectory()) return;
        std::map<juce::String, bool> has48;
        for (const auto& e : juce::RangedDirectoryIterator (lib, true, "*", juce::File::findDirectories))
            if (rateFolder (e.getFile().getFileName()) && e.getFile().getFileName().contains ("48")) has48[e.getFile().getParentDirectory().getFullPathName()] = true;

        for (const auto& e : juce::RangedDirectoryIterator (lib, true, "*.wav;*.aif;*.aiff", juce::File::findFiles))
        {
            const auto f = e.getFile();
            if (f.getFileName().startsWithChar ('.') || f.getFullPathName().contains ("__MACOSX")) continue;
            auto rel = f.getRelativePathFrom (lib).replaceCharacter ('\\', '/');
            const auto parts = juce::StringArray::fromTokens (rel, "/", "");
            if (mainLibrary)
            {
                // only cab IRs: not Cab Studio's own folder, recordings, loops, drums or instruments
                static const char* skipTop[] = { "Cabs", "Instruments", "Loops", "Drums", "Setlists", "Presets", "Recordings", "Backing Tracks" };
                bool skipIt = false;
                if (parts.size() > 1) for (auto* t : skipTop) if (parts[0].equalsIgnoreCase (t)) skipIt = true;
                if (skipIt || f.getSize() > 2000000 || seenNames.count (f.getFileName().toLowerCase()) > 0) continue;
                std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (f) };
                if (r == nullptr || r->lengthInSamples > r->sampleRate * 1.2) continue;   // songs, takes and rooms aren't cab IRs
            }
            else seenNames.insert (f.getFileName().toLowerCase());
            // skip the non-48k copies when a 48k folder exists beside them
            bool skip = false;
            for (auto d = f.getParentDirectory(); d != lib && d.isAChildOf (lib); d = d.getParentDirectory())
                if (rateFolder (d.getFileName()) && ! d.getFileName().contains ("48") && has48.count (d.getParentDirectory().getFullPathName())) { skip = true; break; }
            if (skip) continue;
            // which cab: a top folder that already names a cab ("Tweed Deluxe 5E3 1x12") keeps everything
            // under it as one cab. Otherwise the cab is the deepest folder that isn't about the mic, its
            // position, the sample rate or just the speaker (those are choices inside one cab):
            // "Pack/American Twin/48k/SM57/cap.wav" is the cab "American Twin"; "Pack/V30/SM57/cap.wav"
            // is the cab "Pack" with a V30 speaker.
            juce::String cabFolder = parts.size() > 1 ? parts[0] : juce::String (mainLibrary ? "Loose IRs" : "My IRs");
            juce::String cabName = cabFolder;
            Entry topProbe; parseCab (parts[0].toLowerCase(), topProbe);
            for (int k = parts.size() - 2; k >= 1 && topProbe.speakers == 0; --k)
            {
                const auto low = parts[k].toLowerCase();
                Entry probe; parseCab (low, probe);
                Ir mp; parseName (low, mp);
                const bool micFolder = mp.mic != Mic::Other || mp.micLabel.isNotEmpty() || mp.r >= 0 || mp.dist >= 0 || mp.angle >= 0 || mp.room
                                       || low.contains ("mix") || low.contains ("blend") || low.contains ("multi") || low.contains ("single");
                const bool speakerOnly = probe.speaker.isNotEmpty() && probe.speakers == 0;
                if (micFolder || rateFolder (parts[k]) || speakerOnly) continue;
                cabFolder = parts.joinIntoString ("/", 0, k + 1); cabName = parts[k];
                break;
            }
            auto& c = cabs[prefix + cabFolder];
            if (c.id.isEmpty())
            {
                c.id = prefix + cabFolder; c.name = cabName.replaceCharacters ("_", " ").trim(); c.pack = parts.size() > 1 ? parts[0] : cabFolder;
                c.user = ! mainLibrary && (parts.size() <= 1 || parts[0] == "My IRs");
                parseCab (cabFolder.toLowerCase(), c);
            }
            Ir ir; ir.file = prefix + rel;
            parseName (rel.toLowerCase(), ir);
            { Entry sp; parseCab (rel.toLowerCase(), sp); ir.speakerType = sp.speaker; }
            if (c.speaker.isEmpty() || c.speakers == 0) parseCab (rel.toLowerCase(), c);
            // long files are rooms, whatever they're called
            if (! mainLibrary)
                if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (f) })
                    if (r->lengthInSamples > r->sampleRate * 1.2) ir.room = true;
            c.irs.push_back (ir);
        }
    };
    scan (store(), {}, false);
    scan (root().getParentDirectory(), "@lib/", true);

    std::vector<Entry> out;
    {
        std::lock_guard<std::mutex> l (m);
        for (auto& [k, c] : cabs) { c.favourite = favourites.count (c.id) > 0; out.push_back (c); }
        std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b) { return a.name.compareNatural (b.name) < 0; });
        list = std::move (out);
        statusText = list.empty() ? juce::String ("No cabs yet: drop IR packs into the Cabs/Inbox folder on your SSD.") : juce::String();
    }
    save();
}

juce::File Library::fileFor (const juce::String& rel) const
{
    return rel.startsWith ("@lib/") ? root().getParentDirectory().getChildFile (rel.substring (5)) : store().getChildFile (rel);
}

std::shared_ptr<const std::vector<float>> Library::audio (const juce::String& relFile)
{
    {
        std::lock_guard<std::mutex> l (m);
        if (auto it = cache.find (relFile); it != cache.end()) return it->second;
    }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (fileFor (relFile)));
    if (r == nullptr || r->lengthInSamples < 8) return nullptr;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 1.0));
    juce::AudioBuffer<float> b ((int) std::max (1u, r->numChannels), len);
    r->read (&b, 0, len, 0, true, true);
    // mono (stereo IRs are averaged), then 48 kHz
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int ch = 0; ch < b.getNumChannels(); ++ch) { auto* d = b.getReadPointer (ch); for (int i = 0; i < len; ++i) mono[(size_t) i] += d[i] / (float) b.getNumChannels(); }
    auto out = std::make_shared<std::vector<float>>();
    if (std::abs (r->sampleRate - rate) < 1.0) *out = std::move (mono);
    else
    {
        const double ratio = r->sampleRate / rate;
        out->resize ((size_t) (len / ratio) + 1);
        juce::LagrangeInterpolator li;
        li.process (ratio, mono.data(), out->data(), (int) out->size(), len, 0);
    }
    std::lock_guard<std::mutex> l (m);
    if (cache.size() > 400) cache.clear();
    cache[relFile] = out;
    return out;
}
}
