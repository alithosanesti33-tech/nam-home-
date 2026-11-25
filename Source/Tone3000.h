#pragma once
#include <JuceHeader.h>

class Library;

// TONE3000 integration through their official API (OAuth 2.0 with PKCE, "Select" flow):
// the user picks a tone in TONE3000's own browser page, it redirects back to namhome://tone3000,
// and the tone's model files are downloaded into the library.
class Tone3000 : private juce::Thread, private juce::AsyncUpdater
{
public:
    static constexpr const char* redirectUri = "namhome://tone3000";

    Tone3000 (Library& lib, juce::PropertiesFile& settings);
    ~Tone3000() override;

    juce::String getClientId() const;
    void setClientId (const juce::String& id);
    bool isSignedIn() const;
    void signOut();

    // Opens TONE3000's tone picker in the browser. 'format' is "nam", "ir" or empty for both.
    juce::String browse (const juce::String& format = {});
    // Called with the namhome:// URL the browser sends back.
    void handleCallback (const juce::String& url);

    std::function<void (const juce::String& message, bool warning)> onMessage;
    std::function<void()> onDownloaded;

private:
    void run() override;
    void handleAsyncUpdate() override;
    void post (const juce::String& msg, bool warning);
    bool exchangeCode (const juce::String& code);
    bool refresh();
    juce::var getJson (const juce::String& path);
    bool download (const juce::String& url, const juce::File& dest);

    Library& library;
    juce::PropertiesFile& settings;
    juce::String verifier, state;

    // job for the worker thread
    juce::CriticalSection jobLock;
    juce::String pendingCode, pendingTone;
    juce::String accessToken;
    juce::int64 accessExpiry = 0;

    struct Msg { juce::String text; bool warning; };
    juce::Array<Msg> messages;
    bool downloadedSomething = false;
    juce::Array<std::pair<juce::String, juce::String>> creditsToApply;   // path, creator
};
