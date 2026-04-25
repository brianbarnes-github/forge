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

add_compile_options(-Wall -Wextra -Wpedantic)

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
# forge_ui — JUCE GUI editor for forge config files
# ---------------------------------------------------------------------------
juce_add_gui_app(forge_ui
    PRODUCT_NAME "forge_ui"
    COMPANY_NAME "Forge"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(forge_ui)

target_sources(forge_ui PRIVATE
    Source/UI/UiMain.cpp
    Source/UI/MainWindow.cpp
    Source/UI/EditorPane.cpp
    Source/UI/DiagnosticsPane.cpp
    Source/UI/DiagnosticListView.cpp
    Source/UI/AbcPreviewView.cpp
    Source/UI/SongPropertyPage.cpp
    Source/UI/InstrumentPropertyPage.cpp
    Source/UI/SourcePropertyPage.cpp
    Source/UI/PropertyPageHost.cpp
    Source/UI/InstrumentsTree.cpp)

target_compile_definitions(forge_ui PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:forge_ui,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:forge_ui,JUCE_VERSION>"
    JUCE_USE_DARK_SPLASH_SCREEN=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(forge_ui PRIVATE
    forge_core
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_lto_flags)

enable_testing()
add_subdirectory(Tests)
