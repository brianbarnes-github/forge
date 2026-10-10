add_subdirectory(Catch2)

add_executable(forge_tests
    smoke_test.cpp
    Config_tests.cpp
    ConfigLoader_tests.cpp
    ConfigLoaderXml_tests.cpp
    ConfigLoaderToml_tests.cpp
    ConfigPipeline_tests.cpp
    ConfigWriter_tests.cpp
    DrumMap_tests.cpp
    MidiImporter_tests.cpp
    Pipeline_tests.cpp
    RangeConstraint_tests.cpp
    DurationConstraint_tests.cpp
    ChordConstraint_tests.cpp
    CollisionGuard_tests.cpp
    TempoCollapse_tests.cpp
    TempoPipeline_tests.cpp
    DynamicMapper_tests.cpp
    AbcWriter_tests.cpp
    AbcWriterConfig_tests.cpp
    BarAlignment_tests.cpp
    EndToEnd_tests.cpp
    EndToEndConfig_tests.cpp
    InstrumentAssembly_tests.cpp
    Provenance_tests.cpp
    SongDocument_tests.cpp
    SongFile_tests.cpp
    SongFileRoundTrip_tests.cpp
    PlaybackError_tests.cpp
    TempoMap_tests.cpp
    TimelineRulerMarks_tests.cpp
    GridLines_tests.cpp
    SectionEdit_tests.cpp
    NoteMerge_tests.cpp
    ProgramChanges_tests.cpp
    InstrumentEdit_tests.cpp
    PlaybackSnapshot_tests.cpp
    MuteSoloState_tests.cpp
    Transport_tests.cpp
    PlaybackEngine_tests.cpp
    PlaybackController_tests.cpp
    PlayheadOverlay_tests.cpp
    TransportStrip_tests.cpp
    SynthVoice_tests.cpp
    HandOff_tests.cpp
    SongSession_tests.cpp
    DiscardGuard_tests.cpp
    SongModelBridge_tests.cpp
    SongsmithRoundTrip_tests.cpp
    SourceTrackNoteSource_tests.cpp
    PianoRollGeometry_tests.cpp
    TimelineViewState_tests.cpp
    TrackListComponent_tests.cpp
    PartStripComponent_tests.cpp
    PreviewPipeline_tests.cpp
    PreviewNoteDiff_tests.cpp
    PreviewNoteSource_tests.cpp
    SongsmithMainComponent_tests.cpp
    PianoRollComponent_tests.cpp
    TrackEditorWindow_tests.cpp
    SourceRollEditor_tests.cpp
    HistoryKeys_tests.cpp
    MenuModel_tests.cpp
    PreviewAssignedPanel_tests.cpp
    GridSize_tests.cpp
    SplitterComponent_tests.cpp
    TrackNotePreview_tests.cpp
    SongsmithColours_tests.cpp
    TrackRowComponent_tests.cpp
    TrackHeadComponent_tests.cpp
    GmProgramNames_tests.cpp
    WindowPlacement_tests.cpp
    AboutBox_tests.cpp
    AppSettings_tests.cpp
    PreferencesDialog_tests.cpp
    PlaybackPreferencesPage_tests.cpp
    ViewPreferencesPages_tests.cpp
    ImportOptionsDialog_tests.cpp
    JuceNoteReplica_tests.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/JuceNoteReplica.cpp
    MidiImportPlan_tests.cpp
    MidiImport_tests.cpp
    MidiFidelity_tests.cpp
    ImportOptions_tests.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/MidiExport.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/MidiImportPlan.cpp
    RawMidi_tests.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/RawMidi.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SongDocument.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SongFile.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SongSession.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/TempoMap.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/PlaybackSnapshot.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/MuteSoloState.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/Transport.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/PlaybackEngine.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/PlaybackController.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/SynthVoice.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/TransportStrip.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/TimelineRuler.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/TimelineRulerMarks.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GridLines.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SectionEdit.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/NoteMerge.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/ProgramChanges.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/InstrumentEdit.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Playback/PlayheadOverlay.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/DiscardGuard.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SongModelBridge.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SourceTrackNoteSource.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PianoRollGeometry.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/TimelineViewState.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/TrackListComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/TrackRowComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/TrackHeadComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PreviewPipeline.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PreviewNoteDiff.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PreviewNoteSource.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PianoRollComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/TrackEditorWindow.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/WindowPlacement.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/BuildInfo.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/AboutBox.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/ImportOptionsDialog.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Preferences/GeneralPreferencesPage.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Preferences/ImportPreferencesPage.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Preferences/PlaybackPreferencesPage.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Preferences/AppearancePreferencesPage.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Preferences/EditingPreferencesPage.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Preferences/PreferencesDialog.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SourceRollEditor.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SplitterComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/TrackNotePreview.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/AssignmentChipComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PartSlotComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PartStripComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PreviewAssignedPanel.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/DiagnosticListView.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SongsmithMainComponent.cpp)

target_include_directories(forge_tests PRIVATE ${FORGE_BUILD_INFO_DIR})
target_include_directories(forge_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/Source/ThirdParty/tinysoundfont)
add_dependencies(forge_tests forge_build_info)

target_link_libraries(forge_tests PRIVATE
    Catch2::Catch2WithMain
    forge_core
    juce::juce_audio_basics
    juce::juce_data_structures
    juce::juce_gui_basics)

# TrackListComponent_tests.cpp's real-async-listener-path test needs a
# bounded, non-blocking way to pump JUCE's message loop once
# (MessageManager::runDispatchLoopUntil) rather than the plain
# runDispatchLoop()/stopDispatchLoop() pair, whose "stop" is a one-shot
# latch on MessageManager's singleton that's easy to leave in a stale state
# across unrelated tests sharing the same test binary/process.
target_compile_definitions(forge_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

include(${CMAKE_SOURCE_DIR}/Tests/Catch2/extras/Catch.cmake)
catch_discover_tests(forge_tests)
