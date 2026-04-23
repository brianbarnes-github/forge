add_subdirectory(Catch2)

add_executable(converter_tests
    smoke_test.cpp
    AutoInstrument_tests.cpp
    Config_tests.cpp
    ConfigLoader_tests.cpp
    ConfigLoaderXml_tests.cpp
    ConfigLoaderToml_tests.cpp
    ConfigPipeline_tests.cpp
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
    Provenance_tests.cpp)

target_link_libraries(converter_tests PRIVATE
    Catch2::Catch2WithMain
    converter_core)

include(${CMAKE_SOURCE_DIR}/Tests/Catch2/extras/Catch.cmake)
catch_discover_tests(converter_tests)
