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
    SongModelBridge_tests.cpp
    SongsmithRoundTrip_tests.cpp
    SourceTrackNoteSource_tests.cpp
    PianoRollGeometry_tests.cpp
    TrackListComponent_tests.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SongDocument.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SongModelBridge.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/SourceTrackNoteSource.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/PianoRollGeometry.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/TrackListComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/TrackRowComponent.cpp)

target_link_libraries(forge_tests PRIVATE
    Catch2::Catch2WithMain
    forge_core
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
