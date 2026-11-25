// Custom standalone app: a modern native window (title bar, green full-screen button),
// automatic MIDI device opening, and the namhome:// link TONE3000 sends back.
#include <JuceHeader.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginEditor.h"
#include "NAM/get_dsp.h"

using namespace juce;

class NamWindow : public DocumentWindow
{
public:
    explicit NamWindow (std::unique_ptr<StandalonePluginHolder> h)
        : DocumentWindow ("NAM Home", Colours::black, DocumentWindow::allButtons), holder (std::move (h))
    {
        setUsingNativeTitleBar (true);
        if (auto* ed = holder->processor->createEditorIfNeeded())
        {
            setContentOwned (ed, true);
            setResizable (true, false);
            setResizeLimits (1000, 640, 6000, 4000);
        }
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~NamWindow() override
    {
        holder->savePluginState();
        clearContentComponent();
    }

    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    NAMHomeEditor* editor() { return dynamic_cast<NAMHomeEditor*> (getContentComponent()); }
    std::unique_ptr<StandalonePluginHolder> holder;
};

class NamApp : public JUCEApplication
{
public:
    NamApp()
    {
        PropertiesFile::Options o;
        o.applicationName = "NAM Home";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = "NAM Home";
        props.setStorageParameters (o);
    }

    const String getApplicationName() override { return "NAM Home"; }
    const String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const String& commandLine) override
    {
        // build check: "NAM Home --check-load file.nam ..." loads each capture and exits
        if (commandLine.contains ("--check-load"))
        {
            int failures = 0;
            for (auto& path : StringArray::fromTokens (commandLine.fromFirstOccurrenceOf ("--check-load", false, false), true))
            {
                const auto p = path.unquoted();
                if (p.isEmpty()) continue;
                try { auto d = nam::get_dsp (std::filesystem::path (p.toStdString())); std::printf ("OK   %s\n", p.toRawUTF8()); }
                catch (const std::exception& e) { std::printf ("FAIL %s: %s\n", p.toRawUTF8(), e.what()); ++failures; }
            }
            setApplicationReturnValue (failures);
            quit();
            return;
        }

        auto holder = std::make_unique<StandalonePluginHolder> (props.getUserSettings(), false, String{}, nullptr,
                                                                Array<StandalonePluginHolder::PluginInOuts>{}, true);
        window = std::make_unique<NamWindow> (std::move (holder));
        handleUrl (commandLine);
    }

    void shutdown() override
    {
        window = nullptr;
        props.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
            Timer::callAfterDelay (100, [] { if (auto* app = JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); });
        else
            quit();
    }

    void anotherInstanceStarted (const String& commandLine) override { handleUrl (commandLine); }

private:
    void handleUrl (const String& s)
    {
        const auto t = s.unquoted().trim();
        if (window != nullptr && t.startsWithIgnoreCase ("namhome://"))
            if (auto* ed = window->editor()) ed->handleUrl (t);
    }

    ApplicationProperties props;
    std::unique_ptr<NamWindow> window;
};

JUCE_CREATE_APPLICATION_DEFINE (NamApp)
