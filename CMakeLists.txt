cmake_minimum_required(VERSION 3.22)
project(LotroAbcConverter
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
# converter_core — platform-agnostic conversion library
# ---------------------------------------------------------------------------
# The single compilation unit for everything under Source/Core. Consumed by
# both the CLI executable (below) and the test binary (Tests/CMakeLists.txt).
# A future GUI target will link this same library — keep it free of CLI- or
# UI-specific code.
add_library(converter_core STATIC
    Source/Core/AutoInstrument.cpp
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

target_include_directories(converter_core PUBLIC
    Source
    Source/ThirdParty/tomlplusplus)

target_compile_definitions(converter_core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(converter_core PUBLIC
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# converter — CLI entry point
# ---------------------------------------------------------------------------
juce_add_console_app(converter
    PRODUCT_NAME "LotroAbcConverter"
    COMPANY_NAME "LotroAbcConverter"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(converter)

target_sources(converter PRIVATE
    Source/Main.cpp
    Source/Cli/CliOptions.cpp)

target_compile_definitions(converter PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:converter,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:converter,JUCE_VERSION>")

target_link_libraries(converter PRIVATE
    converter_core
    juce::juce_recommended_lto_flags)

# ---------------------------------------------------------------------------
# converter_ui — JUCE GUI editor for converter config files
# ---------------------------------------------------------------------------
juce_add_gui_app(converter_ui
    PRODUCT_NAME "converter_ui"
    COMPANY_NAME "LotroAbcConverter"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(converter_ui)

target_sources(converter_ui PRIVATE
    Source/UI/UiMain.cpp
    Source/UI/MainWindow.cpp
    Source/UI/EditorPane.cpp
    Source/UI/GlobalSettingsView.cpp
    Source/UI/InstrumentsTable.cpp
    Source/UI/InstrumentDetailForm.cpp
    Source/UI/DiagnosticsPane.cpp
    Source/UI/DiagnosticListView.cpp
    Source/UI/AbcPreviewView.cpp
    Source/UI/SongPropertyPage.cpp
    Source/UI/InstrumentPropertyPage.cpp
    Source/UI/SourcePropertyPage.cpp
    Source/UI/PropertyPageHost.cpp
    Source/UI/InstrumentsTree.cpp)

target_compile_definitions(converter_ui PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:converter_ui,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:converter_ui,JUCE_VERSION>"
    JUCE_USE_DARK_SPLASH_SCREEN=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(converter_ui PRIVATE
    converter_core
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_lto_flags)

enable_testing()
add_subdirectory(Tests)
