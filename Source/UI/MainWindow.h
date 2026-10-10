#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "DiscardGuard.h"
#include "MenuModel.h"
#include "MidiImportPlan.h"
#include "Playback/AudioOutput.h"
#include "Playback/PlaybackController.h"
#include "Playback/SynthVoice.h"
#include "Preferences/PreferencesServices.h"
#include "SongDocument.h"
#include "SongSession.h"

#include <functional>
#include <memory>

namespace lotro
{

class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel,
                   public juce::FileDragAndDropTarget
{
public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex,
                                       const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

    // Opens without asking about unsaved changes -- callers guard.
    void openSongFromPath (const juce::File& file);
    // Guarded: asks about unsaved changes first (File > Open, .songsmith drops).
    void requestOpenSong (const juce::File& file);
    // Nothing is open yet at startup, so this skips the guard.
    void openSongOnStartup (const juce::File& file);
    // Guarded quit.
    void requestQuit();

private:
    MenuState currentMenuState();

    class Body;
    // Declared before `body` so it's constructed first (member init order
    // follows declaration order) — Body's SongsmithMainComponent needs a
    // reference to it at construction time.
    SongDocument                             songDocument;
    // Before `body`; its onChanged is wired in the constructor body.
    SongSession                              session { songDocument };
    // Playback. Order matters: `synth` and `playback` outlive `body` (the
    // transport strip talks to the controller) and `audioOutput` (declared
    // last, so destroyed first: the audio thread must be stopped before the
    // engine/synth it renders from are destroyed).
    SynthVoice                               synth;
    // Frees retired synth instances (message thread, 2 Hz).
    struct SynthGc : juce::Timer
    {
        explicit SynthGc (SynthVoice& s) : voice (s) { startTimerHz (2); }
        ~SynthGc() override { stopTimer(); }
        void timerCallback() override { voice.collectGarbage(); }
        SynthVoice& voice;
    };
    SynthGc                                  synthGc { synth };
    PlaybackController                       playback { songDocument, synth };
    std::unique_ptr<Body>                   body;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<juce::FileChooser>      fileChooser;

    // Per-user app settings (window position so far), in the OS's usual
    // per-user settings folder, e.g. %APPDATA%\SongSmith\SongSmith.settings.
    std::unique_ptr<juce::PropertiesFile>   settings;
    // Typed view over `settings`; declared after it so it is built from a live file.
    AppSettings                             appSettings { *settings };
    // The SoundFont currently loaded in `synth` (empty = none). Set on every successful load.
    juce::File                              activeSoundFont;
    // What the Preferences pages may ask of this window. Declared after appSettings (it refers to it).
    PreferencesServices                     preferencesServices
    {
        appSettings,
        [this] (const juce::File& file) { return loadSoundFontAndRemember (file); },
        [this] { return useBundledSoundFont(); },
        [this] { return activeSoundFontLabel(); }
    };
    // The open Preferences window, if any. Closed in ~MainWindow: the dialog holds
    // `preferencesServices`, whose lambdas capture `this` and which refers to `appSettings`.
    juce::Component::SafePointer<juce::DialogWindow> preferencesWindow;

    // Declared after everything it renders from so it is destroyed first.
    // Created lazily on the message thread by ensurePlaybackReady().
    std::unique_ptr<AudioOutput>            audioOutput;

    void chooseSoundFont();
    void loadStartupSoundFont();
    juce::File bundledSoundFont() const;
    SoundFontLoad loadSoundFontAndRemember (const juce::File& file);
    SoundFontLoad useBundledSoundFont();
    juce::String activeSoundFontLabel() const;
    bool ensurePlaybackReady();

    void openMidiViaDialog();
    void openMidiFromPath (const juce::File& file);
    void importMidiWithOptions (const juce::File& file, const ImportOptions& options);
    void runConversion();
    void saveAbcAs();
    void exportMidiAs();

    void refreshTitle();
    void resetToEmptySong();
    void openSongViaDialog();
    void saveSong (std::function<void (bool)> done = {});
    void saveSongAs (std::function<void (bool)> done = {});
    void writeSongTo (const juce::File& file, std::function<void (bool)> done);
    DiscardGuardHooks guardHooks();
    void guarded (std::function<void()> action);
    void afterDocumentReplaced();

    std::string lastAbc;   // populated by runConversion(); consumed by saveAbcAs()
};

} // namespace lotro
