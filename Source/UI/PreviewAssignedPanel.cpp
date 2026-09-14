#include "PreviewAssignedPanel.h"
#include "SongsmithColours.h"

#include "Core/LotroInstrument.h"

namespace lotro
{

namespace
{
    // Mirrors TrackRowComponent.cpp's own local pitchName helper (standard
    // MIDI naming, 60 = C4) — no shared MIDI-pitch-to-name helper exists in
    // Source/Core, and this is display-only text scoped to this file, not
    // worth promoting to forge_core for a second UI-only caller.
    juce::String pitchName (int midiPitch)
    {
        static const char* const names[12] =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int octave = midiPitch / 12 - 1;
        const int pitchClass = ((midiPitch % 12) + 12) % 12;
        return juce::String (names[pitchClass]) + juce::String (octave);
    }
}

PreviewAssignedPanel::PreviewAssignedPanel()
{
}

void PreviewAssignedPanel::clear()
{
    hasSelection = false;
    assignedRows.clear();
    instrumentName = {};
    rangeText = {};
    totalNotes = 0;
    droppedNotes = 0;
    repaint();
}

void PreviewAssignedPanel::setPreview (SongDocument& doc, juce::int64 partId, const PreviewResult& result,
                                        const std::vector<PreviewNote>& diff)
{
    auto part = doc.findPartById (partId);
    if (! part.isValid())
    {
        clear();
        return;
    }

    hasSelection = true;
    assignedRows.clear();

    for (int i = 0; i < SongDocument::getNumAssignments (part); ++i)
    {
        auto assignment = SongDocument::getAssignment (part, i);
        const auto trackId = (juce::int64) assignment.getProperty (SongIDs::trackId);
        auto track = doc.findTrackById (trackId);

        AssignedRow row;
        row.trackName = track.isValid() ? track.getProperty (SongIDs::name).toString() : juce::String ("?");
        row.swatch = track.isValid() ? (juce::uint32) (int) track.getProperty (SongIDs::colorArgb)
                                      : SongsmithColours::textMuted;
        row.transposeSemitones = (int) assignment.getProperty (SongIDs::transposeSemitones);
        assignedRows.push_back (row);
    }

    instrumentName = part.getProperty (SongIDs::instrumentName).toString();

    LotroInstrument instrument;
    if (parseName (instrumentName.toStdString(), instrument).empty())
    {
        const auto range = rangeFor (instrument);
        rangeText = "Range: " + pitchName (range.midiLow) + juce::String::fromUTF8 (" \xe2\x80\x93 ")
                        + pitchName (range.midiHigh);
    }
    else
    {
        rangeText = {};
    }

    totalNotes = 0;
    for (const auto& track : result.pipelined.tracks)
        totalNotes += (int) track.notes.size();

    droppedNotes = 0;
    for (const auto& note : diff)
        if (note.state == NoteState::Dropped)
            ++droppedNotes;

    repaint();
}

void PreviewAssignedPanel::paint (juce::Graphics& g)
{
    using namespace SongsmithColours;

    g.fillAll (juce::Colour (background));

    if (! hasSelection)
    {
        g.setColour (juce::Colour (textMuted));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawFittedText ("No part selected", getLocalBounds().reduced (8),
                           juce::Justification::centredTop, 2);
        return;
    }

    auto area = getLocalBounds().reduced (8, 6);
    const int lineHeight = 14;

    auto drawSectionHeader = [&] (const juce::String& label)
    {
        g.setColour (juce::Colour (textMuted));
        g.setFont (juce::Font (juce::FontOptions (9.0f)).withExtraKerningFactor (0.04f));
        g.drawText (label.toUpperCase(), area.removeFromTop (lineHeight), juce::Justification::centredLeft);
    };

    drawSectionHeader ("Assigned");
    if (assignedRows.empty())
    {
        g.setColour (juce::Colour (textMuted));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("none", area.removeFromTop (lineHeight), juce::Justification::centredLeft);
    }
    for (const auto& row : assignedRows)
    {
        auto chip = area.removeFromTop (20);
        g.setColour (juce::Colour (0xFF2E2E2Eu));
        g.fillRect (chip);
        g.setColour (juce::Colour (row.swatch));
        g.fillRect (chip.removeFromLeft (2));

        auto textArea = chip.reduced (6, 1);
        g.setColour (juce::Colour (text));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (row.trackName, textArea.removeFromTop (11), juce::Justification::centredLeft);

        g.setColour (juce::Colour (textMuted));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
        const juce::String transposeText = (row.transposeSemitones >= 0 ? "+" : "")
                                          + juce::String (row.transposeSemitones) + " semi";
        g.drawText (transposeText, textArea, juce::Justification::centredLeft);

        area.removeFromTop (2);
    }

    area.removeFromTop (8);
    drawSectionHeader ("Instrument");
    g.setColour (juce::Colour (text));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (instrumentName, area.removeFromTop (lineHeight), juce::Justification::centredLeft);
    g.setColour (juce::Colour (textMuted));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)));
    g.drawText (rangeText, area.removeFromTop (lineHeight), juce::Justification::centredLeft);

    area.removeFromTop (8);
    drawSectionHeader ("Range policy");
    g.setColour (juce::Colour (text));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("Octave shift", area.removeFromTop (lineHeight), juce::Justification::centredLeft);

    area.removeFromTop (8);
    drawSectionHeader ("Output stats");
    g.setColour (juce::Colour (text));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
    g.drawText (juce::String (totalNotes) + " notes", area.removeFromTop (lineHeight), juce::Justification::centredLeft);

    if (droppedNotes > 0)
    {
        g.setColour (juce::Colour (outOfRangeFill));
        g.drawText (juce::String (droppedNotes) + " dropped", area.removeFromTop (lineHeight),
                     juce::Justification::centredLeft);
    }
}

} // namespace lotro
