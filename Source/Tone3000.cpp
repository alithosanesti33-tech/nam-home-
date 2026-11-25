#include "Tone3000.h"
#include "Library.h"

namespace
{
    const juce::String api = "https://www.tone3000.com/api/v1";

    juce::String base64Url (const void* data, size_t size)
    {
        return juce::Base64::toBase64 (data, size).replace ("+", "-").replace ("/", "_").trimCharactersAtEnd ("=");
    }

    juce::String randomToken (int bytes)
    {
        juce::HeapBlock<juce::uint8> b ((size_t) bytes);
        juce::Random r (juce::Time::getHighResolutionTicks() ^ (juce::int64) juce::Random::getSystemRandom().nextInt64());
        for (int i = 0; i < bytes; ++i) b[i] = (juce::uint8) r.nextInt (256);
        return base64Url (b.get(), (size_t) bytes);
    }

    juce::String queryValue (const juce::String& url, const juce::String& key)
    {
        const auto q = url.fromFirstOccurrenceOf ("?", false, false);
        for (auto& kv : juce::StringArray::fromTokens (q, "&", {}))
            if (kv.upToFirstOccurrenceOf ("=", false, false) == key)
                return juce::URL::removeEscapeChars (kv.fromFirstOccurrenceOf ("=", false, false).replace ("+", " "));
        return {};
    }
}

Tone3000::Tone3000 (Library& lib, juce::PropertiesFile& s) : juce::Thread ("TONE3000"), library (lib), settings (s)
{
    // move an older sign-in out of the shared settings file
    if (settings.getValue ("t3kRefresh").isNotEmpty() && getRefresh().isEmpty())
    {
        setRefresh (settings.getValue ("t3kRefresh"));
        settings.removeValue ("t3kRefresh");
        settings.saveIfNeeded();
    }
}

// TONE3000 state lives in its own small file, written only by the app: sign-in token, the
// requests waiting for TONE3000 to answer (so an answer still works after a restart or if
// macOS starts a fresh copy of the app), and recently answered ones (so a duplicate is quiet).
juce::File Tone3000::stateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support").getChildFile ("NAM Home").getChildFile ("tone3000.json");
}

juce::var Tone3000::loadState() const
{
    auto v = juce::JSON::parse (stateFile());
    return v.isObject() ? v : juce::var (new juce::DynamicObject());
}

void Tone3000::saveState (const juce::var& v) const
{
    stateFile().getParentDirectory().createDirectory();
    stateFile().replaceWithText (juce::JSON::toString (v));
}

juce::String Tone3000::getRefresh() const { return loadState().getProperty ("refresh", "").toString(); }

void Tone3000::setRefresh (const juce::String& t)
{
    auto v = loadState();
    v.getDynamicObject()->setProperty ("refresh", t);
    saveState (v);
}

Tone3000::~Tone3000()
{
    stopThread (4000);
    cancelPendingUpdate();
}

juce::String Tone3000::getClientId() const { return settings.getValue ("t3kClientId"); }
void Tone3000::setClientId (const juce::String& id) { settings.setValue ("t3kClientId", id.trim()); settings.saveIfNeeded(); }
bool Tone3000::isSignedIn() const { return getRefresh().isNotEmpty(); }

void Tone3000::signOut()
{
    setRefresh ({});
    const juce::ScopedLock sl (jobLock);
    accessToken = {};
}

juce::String Tone3000::browse (const juce::String& format)
{
    if (getClientId().isEmpty())
        return "Add your TONE3000 publishable key in Settings > TONE3000 first.";

    verifier = randomToken (48);
    state = randomToken (16);
    juce::SHA256 sha (verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
    const auto digest = sha.getRawData();
    const auto challenge = base64Url (digest.getData(), digest.getSize());

    juce::URL u (api + "/oauth/authorize");
    u = u.withParameter ("client_id", getClientId())
         .withParameter ("redirect_uri", redirectUri)
         .withParameter ("response_type", "code")
         .withParameter ("code_challenge", challenge)
         .withParameter ("code_challenge_method", "S256")
         .withParameter ("state", state)
         .withParameter ("prompt", "select_tone")
         .withParameter ("menubar", "true");
    if (format.isNotEmpty()) u = u.withParameter ("format", format);

    {
        auto st = loadState();
        juce::Array<juce::var> keep;
        const auto now = juce::Time::currentTimeMillis();
        if (auto* arr = st.getProperty ("pending", {}).getArray())
            for (auto& e : *arr)
                if (now - (juce::int64) e.getProperty ("t", 0) < 3 * 3600 * 1000) keep.add (e);
        while (keep.size() > 9) keep.remove (0);
        auto* e = new juce::DynamicObject();
        e->setProperty ("state", state);
        e->setProperty ("verifier", verifier);
        e->setProperty ("t", (double) now);
        keep.add (juce::var (e));
        st.getDynamicObject()->setProperty ("pending", keep);
        saveState (st);
    }
    if (! u.launchInDefaultBrowser()) return "Couldn't open your web browser.";
    return {};
}

void Tone3000::handleCallback (const juce::String& rawUrl)
{
    const auto url = rawUrl.unquoted().trim();
    if (! url.startsWithIgnoreCase ("namhome://")) return;

    if (queryValue (url, "error").isNotEmpty()) { post ("TONE3000 sign-in failed: " + queryValue (url, "error"), true); return; }
    {
        // match the answer to one of our recent requests (it may come to a freshly started app)
        const auto got = queryValue (url, "state");
        auto st = loadState();
        juce::Array<juce::var> keep, used;
        if (auto* u = st.getProperty ("used", {}).getArray()) used = *u;
        juce::String found;
        if (auto* arr = st.getProperty ("pending", {}).getArray())
            for (auto& e : *arr)
            {
                if (got.isNotEmpty() && e.getProperty ("state", "").toString() == got) found = e.getProperty ("verifier", "").toString();
                else keep.add (e);
            }
        if (found.isEmpty())
        {
            if (! used.contains (juce::var (got)))   // a second copy of an answer we already used: say nothing
                post ("That TONE3000 page belongs to an older request. Click Browse TONE3000 again and pick the tone.", true);
            return;
        }
        used.add (got);
        while (used.size() > 20) used.remove (0);
        st.getDynamicObject()->setProperty ("pending", keep);
        st.getDynamicObject()->setProperty ("used", used);
        saveState (st);
        const juce::ScopedLock sl (jobLock);
        verifier = found;
        state = {};
    }

    const auto code = queryValue (url, "code");
    const auto tone = queryValue (url, "tone_id");
    if (queryValue (url, "canceled") == "true" && tone.isEmpty())
    {
        if (code.isNotEmpty()) { const juce::ScopedLock sl (jobLock); pendingCode = code; startThread(); }
        return;
    }
    {
        const juce::ScopedLock sl (jobLock);
        pendingCode = code;
        pendingTone = tone;
    }
    post ("Downloading from TONE3000...", false);
    startThread();
}

void Tone3000::post (const juce::String& msg, bool warning)
{
    {
        const juce::ScopedLock sl (jobLock);
        messages.add ({ msg, warning });
    }
    triggerAsyncUpdate();
}

void Tone3000::handleAsyncUpdate()
{
    juce::Array<Msg> ms;
    juce::Array<std::pair<juce::String, juce::String>> credits;
    bool dl;
    {
        const juce::ScopedLock sl (jobLock);
        ms.swapWith (messages);
        credits.swapWith (creditsToApply);
        dl = std::exchange (downloadedSomething, false);
    }
    for (auto& c : credits) library.setCreator (c.first, c.second);
    if (dl) { library.rescan(); if (onDownloaded) onDownloaded(); }
    for (auto& m : ms) if (onMessage) onMessage (m.text, m.warning);
}

bool Tone3000::exchangeCode (const juce::String& code)
{
    juce::URL u (api + "/oauth/token");
    u = u.withParameter ("grant_type", "authorization_code")
         .withParameter ("code", code)
         .withParameter ("code_verifier", verifier)
         .withParameter ("redirect_uri", redirectUri)
         .withParameter ("client_id", getClientId());
    int status = 0;
    auto in = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                       .withExtraHeaders ("Content-Type: application/x-www-form-urlencoded")
                                       .withConnectionTimeoutMs (15000).withStatusCode (&status));
    if (in == nullptr) return false;
    const auto v = juce::JSON::parse (in->readEntireStreamAsString());
    if (status >= 300 || ! v.hasProperty ("access_token")) return false;
    const juce::ScopedLock sl (jobLock);
    accessToken = v["access_token"].toString();
    accessExpiry = juce::Time::currentTimeMillis() + (juce::int64) ((double) v.getProperty ("expires_in", 3000) * 900.0);
    if (v.hasProperty ("refresh_token")) setRefresh (v["refresh_token"].toString());
    return true;
}

bool Tone3000::refresh()
{
    const auto rt = getRefresh();
    if (rt.isEmpty()) return false;
    juce::URL u (api + "/oauth/token");
    u = u.withParameter ("grant_type", "refresh_token").withParameter ("refresh_token", rt).withParameter ("client_id", getClientId());
    int status = 0;
    auto in = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                       .withExtraHeaders ("Content-Type: application/x-www-form-urlencoded")
                                       .withConnectionTimeoutMs (15000).withStatusCode (&status));
    if (in == nullptr) return false;
    const auto v = juce::JSON::parse (in->readEntireStreamAsString());
    if (status >= 300 || ! v.hasProperty ("access_token")) { setRefresh ({}); return false; }
    const juce::ScopedLock sl (jobLock);
    accessToken = v["access_token"].toString();
    accessExpiry = juce::Time::currentTimeMillis() + (juce::int64) ((double) v.getProperty ("expires_in", 3000) * 900.0);
    if (v.hasProperty ("refresh_token")) setRefresh (v["refresh_token"].toString());
    return true;
}

juce::var Tone3000::getJson (const juce::String& path)
{
    juce::String token;
    { const juce::ScopedLock sl (jobLock); token = accessToken; }
    int status = 0;
    auto in = juce::URL (api + path).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                            .withExtraHeaders ("Authorization: Bearer " + token)
                                                            .withConnectionTimeoutMs (15000).withStatusCode (&status));
    if (in == nullptr || status >= 300) return {};
    return juce::JSON::parse (in->readEntireStreamAsString());
}

bool Tone3000::download (const juce::String& url, const juce::File& dest)
{
    juce::String token;
    { const juce::ScopedLock sl (jobLock); token = accessToken; }
    int status = 0;
    auto in = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                     .withExtraHeaders ("Authorization: Bearer " + token)
                                                     .withConnectionTimeoutMs (20000).withStatusCode (&status));
    if (in == nullptr || status >= 300) return false;
    dest.getParentDirectory().createDirectory();
    juce::FileOutputStream out (dest);
    if (! out.openedOk()) return false;
    out.setPosition (0); out.truncate();
    return out.writeFromInputStream (*in, -1) > 0;
}

void Tone3000::run()
{
    juce::String code, tone;
    {
        const juce::ScopedLock sl (jobLock);
        code = std::exchange (pendingCode, {});
        tone = std::exchange (pendingTone, {});
    }
    if (code.isNotEmpty() && ! exchangeCode (code)) { post ("Couldn't sign in to TONE3000. Check your publishable key and the redirect address in Settings.", true); return; }
    if (tone.isEmpty()) { post ("Signed in to TONE3000.", false); return; }
    if (accessToken.isEmpty() || juce::Time::currentTimeMillis() > accessExpiry)
        if (! refresh()) { post ("Please sign in to TONE3000 again (Browse TONE3000).", true); return; }

    const auto toneInfo = getJson ("/tones/" + tone);
    const auto title = toneInfo.getProperty ("title", "TONE3000 tone " + tone).toString();
    const auto format = toneInfo.getProperty ("format", "nam").toString();
    juce::String creator;
    if (auto user = toneInfo.getProperty ("user", {}); user.isObject()) creator = user.getProperty ("username", {}).toString();
    if (creator.isEmpty()) creator = toneInfo.getProperty ("username", {}).toString();

    const auto models = getJson ("/models?tone_id=" + tone + "&page_size=300");
    auto* list = models.getProperty ("data", {}).getArray();
    if (list == nullptr || list->isEmpty()) { post ("That tone has no downloadable models.", true); return; }

    // prefer full-size models; small "lite/feather/nano" variants are for hardware
    juce::Array<juce::var> chosen;
    for (auto& m : *list)
    {
        const auto size = m.getProperty ("size", "standard").toString();
        if (size == "standard" || size == "custom" || size.isEmpty()) chosen.add (m);
    }
    if (chosen.isEmpty()) chosen = *list;

    const auto folder = library.getFolder().getChildFile ("TONE3000").getChildFile (juce::File::createLegalFileName (title));
    const auto ext = format == "ir" ? juce::String (".wav") : juce::String (".nam");
    int ok = 0;
    for (auto& m : chosen)
    {
        if (threadShouldExit()) return;
        auto name = juce::File::createLegalFileName (m.getProperty ("name", "model").toString());
        if (name.isEmpty()) name = "model " + m.getProperty ("id", ok).toString();
        auto dest = folder.getChildFile (name + ext);
        if (dest.existsAsFile()) dest = dest.getNonexistentSibling();
        if (download (m.getProperty ("model_url", {}).toString(), dest))
        {
            ++ok;
            if (creator.isNotEmpty())
            {
                const juce::ScopedLock sl (jobLock);
                creditsToApply.add ({ dest.getFullPathName(), creator });
            }
        }
    }
    {
        const juce::ScopedLock sl (jobLock);
        downloadedSomething = ok > 0;
    }
    post (ok > 0 ? "Added " + juce::String (ok) + (ok == 1 ? " file" : " files") + " from TONE3000: " + title
                 : "Couldn't download that tone from TONE3000.", ok == 0);
}
