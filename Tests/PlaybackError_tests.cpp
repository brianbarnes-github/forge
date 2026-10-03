#include "UI/Playback/PlaybackError.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("PlaybackError: carries its kind and a plain-English message", "[playback]")
{
    const PlaybackError e (PlaybackErrorKind::SoundFontInvalid, "That file is not a SoundFont.");
    CHECK (e.kind() == PlaybackErrorKind::SoundFontInvalid);
    CHECK (std::string (e.what()) == "That file is not a SoundFont.");
}
