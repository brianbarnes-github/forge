#pragma once

#include <stdexcept>
#include <string>

namespace lotro
{

enum class PlaybackErrorKind
{
    SoundFontMissing,
    SoundFontInvalid,
    AudioDeviceUnavailable
};

// Thrown by SynthVoice (SoundFont loading) and AudioOutput (device start-up).
// The message is plain English and shown verbatim in the user's error dialog.
class PlaybackError : public std::runtime_error
{
public:
    PlaybackError (PlaybackErrorKind kindIn, const std::string& message)
        : std::runtime_error (message), errorKind (kindIn) {}

    PlaybackErrorKind kind() const noexcept { return errorKind; }

private:
    PlaybackErrorKind errorKind;
};

} // namespace lotro
