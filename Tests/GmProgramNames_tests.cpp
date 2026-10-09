#include "UI/GmProgramNames.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("gmProgramName: General MIDI names by program number, empty out of range", "[gm-names]")
{
    CHECK (gmProgramName (0) == "Acoustic Grand Piano");
    CHECK (gmProgramName (33) == "Electric Bass (finger)");
    CHECK (gmProgramName (30) == "Distortion Guitar");
    CHECK (gmProgramName (127) == "Gunshot");
    CHECK (gmProgramName (-1).isEmpty());
    CHECK (gmProgramName (128).isEmpty());
}
