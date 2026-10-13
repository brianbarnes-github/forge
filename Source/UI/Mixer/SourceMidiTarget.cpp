#include "UI/Mixer/SourceMidiTarget.h"

namespace lotro
{

SourceMidiTarget::SourceMidiTarget (SongDocument& document, PlaybackController& playback)
    : doc (document), controller (playback), rootNode (document.getTree())
{
    rootNode.addListener (this);
    controller.addListener (this);
}

SourceMidiTarget::~SourceMidiTarget()
{
    cancelPendingUpdate();
    controller.removeListener (this);
    rootNode.removeListener (this);
}

std::vector<MixerChannel> SourceMidiTarget::channels() const
{
    std::vector<MixerChannel> result;
    int number = 0;
    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        const auto track = doc.getTrack (i);
        if (static_cast<bool> (track.getProperty (SongIDs::isConductor, false)))
            continue;

        MixerChannel c;
        c.id = static_cast<juce::int64> (track.getProperty (SongIDs::trackId, juce::int64 { -1 }));
        c.number = ++number;
        c.name = track.getProperty (SongIDs::name).toString();
        c.argb = static_cast<juce::uint32> (static_cast<juce::int64> (track.getProperty (SongIDs::colorArgb, juce::int64 { 0 })));
        c.volume = trackPlaybackVolume (track);
        c.pan = trackPlaybackPan (track);
        c.muted = controller.isMuted (c.id);
        c.soloed = controller.isSoloed (c.id);
        c.silencedBySolo = controller.isSilencedBySolo (c.id);
        result.push_back (std::move (c));
    }
    return result;
}

void SourceMidiTarget::setVolume (juce::int64 id, int percent, bool startsGesture)
{
    auto t = doc.findTrackById (id);
    if (! t.isValid())
        return;
    if (percent >= 100)
        doc.removeProperty (t, SongIDs::playbackVolume, startsGesture);   // 100 is the implicit default
    else
        doc.setProperty (t, SongIDs::playbackVolume, percent, startsGesture);
}

void SourceMidiTarget::setPan (juce::int64 id, std::optional<int> pan, bool startsGesture)
{
    auto t = doc.findTrackById (id);
    if (! t.isValid())
        return;
    if (! pan.has_value())
        doc.removeProperty (t, SongIDs::playbackPan, startsGesture);
    else
        doc.setProperty (t, SongIDs::playbackPan, *pan, startsGesture);
}

void SourceMidiTarget::setMuted (juce::int64 id, bool muted) { controller.setMuted (id, muted); }
void SourceMidiTarget::setSoloed (juce::int64 id, bool soloed) { controller.setSoloed (id, soloed); }
int SourceMidiTarget::masterPercent() const { return controller.masterPercent(); }
void SourceMidiTarget::setMasterPercent (int percent) { controller.setMasterPercent (percent); }

void SourceMidiTarget::handleAsyncUpdate()
{
    listeners.call ([] (MixerTarget::Listener& l) { l.mixerChanged(); });
}

} // namespace lotro
