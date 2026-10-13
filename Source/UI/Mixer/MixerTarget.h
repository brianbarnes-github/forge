#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace lotro
{

// One mixer strip's worth of state, as plain values.
struct MixerChannel
{
    juce::int64        id = -1;           // MIDI_TRACK trackId
    int                number = 0;        // 1-based display number among non-conductor tracks
    juce::String       name;
    juce::uint32       argb = 0;
    int                volume = 100;      // 0..100
    std::optional<int> pan;               // nullopt = follows the source
    bool               muted = false;
    bool               soloed = false;
    bool               silencedBySolo = false;
};

// What the mixer panel talks to; today only the source-MIDI implementation exists.
class MixerTarget
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void mixerChanged() = 0;
    };

    virtual ~MixerTarget() = default;
    virtual std::vector<MixerChannel> channels() const = 0;       // conductor excluded
    virtual void setVolume (juce::int64 id, int percent, bool startsGesture) = 0;
    virtual void setPan (juce::int64 id, std::optional<int> pan, bool startsGesture) = 0;   // nullopt clears
    virtual void setMuted (juce::int64 id, bool muted) = 0;
    virtual void setSoloed (juce::int64 id, bool soloed) = 0;
    virtual int  masterPercent() const = 0;
    virtual void setMasterPercent (int percent) = 0;
    virtual void addListener (Listener*) = 0;
    virtual void removeListener (Listener*) = 0;
};

} // namespace lotro
