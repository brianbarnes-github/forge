#pragma once

#include <stdexcept>
#include <string>

namespace lotro
{

enum class SongFileErrorKind
{
    NotASongFile,
    UnsupportedVersion,
    Truncated,
    ChecksumMismatch,
    Corrupt,
    InvalidStructure
};

// Thrown by the Song file reader and by SongDocument::replaceContents. The
// message is plain English and shown verbatim in the user's error dialog.
class SongFileError : public std::runtime_error
{
public:
    SongFileError (SongFileErrorKind kindIn, const std::string& message)
        : std::runtime_error (message), errorKind (kindIn) {}

    SongFileErrorKind kind() const noexcept { return errorKind; }

private:
    SongFileErrorKind errorKind;
};

} // namespace lotro
