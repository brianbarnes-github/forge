cmake_minimum_required(VERSION 3.22)
project(Forge
    VERSION 0.1.0
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_compile_definitions(_WIN32_WINNT=0x0A00 NOMINMAX)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(JUCE)

# ---------------------------------------------------------------------------
# forge_core — platform-agnostic conversion library
# ---------------------------------------------------------------------------
# The single compilation unit for everything under Source/Core. Consumed by
# both the CLI executable (below), the GUI app (further below), and the
# test binary (Tests/CMakeLists.txt). Stays JUCE-free at its public surface.
add_library(forge_core STATIC
    Source/Core/Config.cpp
    Source/Core/ConfigLoader.cpp
    Source/Core/ConfigWriter.cpp
    Source/Core/DrumMap.cpp
    Source/Core/DrumMapLoader.cpp
    Source/Core/InstrumentAssembly.cpp
    Source/Core/LotroInstrument.cpp
    Source/Core/MidiImporter.cpp
    Source/Core/Pipeline.cpp
    Source/Core/Constraints/RangeConstraint.cpp
    Source/Core/Constraints/DurationConstraint.cpp
    Source/Core/Constraints/ChordConstraint.cpp
    Source/Core/Constraints/CollisionGuard.cpp
    Source/Core/Constraints/TempoCollapse.cpp
    Source/Core/Constraints/DynamicMapper.cpp
    Source/Core/AbcWriter.cpp)

target_include_directories(forge_core PUBLIC
    Source
    Source/ThirdParty/tomlplusplus)

target_compile_definitions(forge_core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(forge_core PUBLIC
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# forge — CLI entry point
# ---------------------------------------------------------------------------
juce_add_console_app(forge
    PRODUCT_NAME "forge"
    COMPANY_NAME "Forge"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(forge)

target_sources(forge PRIVATE
    Source/Main.cpp
    Source/Cli/CliOptions.cpp)

target_compile_definitions(forge PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:forge,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:forge,JUCE_VERSION>")

target_link_libraries(forge PRIVATE
    forge_core
    juce::juce_recommended_lto_flags)

# ---------------------------------------------------------------------------
# forge_build_info — regenerates generated/ForgeBuildInfo.h (version, git
# commit count + short hash, dirty flag) on every build, for the About box.
# The header is only rewritten when a value changes. Consumers add
# FORGE_BUILD_INFO_DIR to their include path and depend on this target.
# ---------------------------------------------------------------------------
set(FORGE_BUILD_INFO_DIR "${CMAKE_BINARY_DIR}/generated")
add_custom_target(forge_build_info
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUT_FILE=${FORGE_BUILD_INFO_DIR}/ForgeBuildInfo.h
        -DTEMPLATE_FILE=${CMAKE_SOURCE_DIR}/cmake/ForgeBuildInfo.h.in
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    BYPRODUCTS ${FORGE_BUILD_INFO_DIR}/ForgeBuildInfo.h
    COMMENT "Stamping build info"
    VERBATIM)

# ---------------------------------------------------------------------------
# forge_ui — JUCE GUI editor for forge config files
# ---------------------------------------------------------------------------
juce_add_gui_app(forge_ui
    PRODUCT_NAME "song-smith"
    COMPANY_NAME "Forge"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(forge_ui)

target_sources(forge_ui PRIVATE
    Source/UI/UiMain.cpp
    Source/UI/MainWindow.cpp
    Source/UI/JuceNoteReplica.cpp
    Source/UI/MidiExport.cpp
    Source/UI/MidiImportPlan.cpp
    Source/UI/RawMidi.cpp
    Source/UI/SongDocument.cpp
    Source/UI/SongFile.cpp
    Source/UI/SongSession.cpp
    Source/UI/Playback/TempoMap.cpp
    Source/UI/Playback/PlaybackSnapshot.cpp
    Source/UI/Playback/MuteSoloState.cpp
    Source/UI/Playback/Transport.cpp
    Source/UI/Playback/PlaybackEngine.cpp
    Source/UI/Playback/PlaybackController.cpp
    Source/UI/Playback/SynthVoice.cpp
    Source/UI/Playback/TransportStrip.cpp
    Source/UI/Playback/TimelineRuler.cpp
    Source/UI/Playback/TimelineRulerMarks.cpp
    Source/UI/GridLines.cpp
    Source/UI/Playback/PlayheadOverlay.cpp
    Source/UI/Playback/AudioOutput.cpp
    Source/UI/DiscardGuard.cpp
    Source/UI/SongModelBridge.cpp
    Source/UI/SourceTrackNoteSource.cpp
    Source/UI/PianoRollGeometry.cpp
    Source/UI/PreviewPipeline.cpp
    Source/UI/PreviewNoteDiff.cpp
    Source/UI/PreviewNoteSource.cpp
    Source/UI/PianoRollComponent.cpp
    Source/UI/SourceRollEditor.cpp
    Source/UI/SplitterComponent.cpp
    Source/UI/TimelineViewState.cpp
    Source/UI/TrackNotePreview.cpp
    Source/UI/TrackRowComponent.cpp
    Source/UI/TrackListComponent.cpp
    Source/UI/TrackEditorWindow.cpp
    Source/UI/WindowPlacement.cpp
    Source/UI/BuildInfo.cpp
    Source/UI/AboutBox.cpp
    Source/UI/AssignmentChipComponent.cpp
    Source/UI/PartSlotComponent.cpp
    Source/UI/PartStripComponent.cpp
    Source/UI/PreviewAssignedPanel.cpp
    Source/UI/SongsmithMainComponent.cpp
    Source/UI/DiagnosticsPane.cpp
    Source/UI/DiagnosticListView.cpp
    Source/UI/AbcPreviewView.cpp)

target_compile_definitions(forge_ui PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:forge_ui,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:forge_ui,JUCE_VERSION>"
    JUCE_USE_DARK_SPLASH_SCREEN=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_include_directories(forge_ui PRIVATE ${FORGE_BUILD_INFO_DIR})
add_dependencies(forge_ui forge_build_info)

target_link_libraries(forge_ui PRIVATE
    forge_core
    juce::juce_audio_devices
    juce::juce_data_structures
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_lto_flags)

target_include_directories(forge_ui SYSTEM PRIVATE Source/ThirdParty/tinysoundfont)

# The GPL-2 SoundFont is deliberately not in the repo or in CI. When a local copy
# exists, copy it into resources/ beside the exe so the default SoundFont lookup finds it.
set(FORGE_SOUNDFONT "${CMAKE_SOURCE_DIR}/resources/soundfonts/TimGM6mb.sf2")
if(EXISTS "${FORGE_SOUNDFONT}")
    add_custom_command(TARGET forge_ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:forge_ui>/resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FORGE_SOUNDFONT}" "$<TARGET_FILE_DIR:forge_ui>/resources")
endif()

enable_testing()
add_subdirectory(Tests)
