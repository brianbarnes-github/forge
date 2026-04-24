#include "InstrumentsTree.h"
#include <set>

namespace lotro
{

namespace
{
    juce::String songLabel (const Config& config)
    {
        if (config.title.has_value() && ! config.title->empty())
            return juce::String (*config.title);
        if (! config.input.empty())
            return juce::File (juce::String (config.input)).getFileNameWithoutExtension();
        return "(no MIDI loaded)";
    }

    juce::String instrumentLabel (const ConfigInstrument& inst)
    {
        juce::String s = "X:" + juce::String (inst.x) + "  " + juce::String (inst.name);
        if (inst.label.has_value() && ! inst.label->empty())
            s += " — \"" + juce::String (*inst.label) + "\"";
        return s;
    }

    juce::String sourceLabel (const ConfigSource& src, const Song& raw)
    {
        juce::String s = "MIDI " + juce::String (src.midiTrackIndex);
        if (src.midiTrackIndex >= 0 && src.midiTrackIndex < (int) raw.tracks.size())
        {
            const auto& t = raw.tracks[(size_t) src.midiTrackIndex];
            s += ": " + juce::String (t.name)
               + " (chan " + juce::String (t.sourceMidiChannel)
               + ", " + juce::String ((int) t.notes.size()) + ")";
        }
        return s;
    }
}

// ---- SourceItem ------------------------------------------------------------
//
// Invariant for all three item classes below: the stored instrumentIdx /
// sourceIdx are valid for this item's lifetime. Any Config mutation
// (adding/removing instruments or sources) MUST call
// InstrumentsTree::rebuild() so a fresh set of items is constructed with
// correct indices. `paintItem` and `mightContainSubItems` dereference
// `config.instruments[idx]` unguarded on that assumption.

class InstrumentsTree::SourceItem : public juce::TreeViewItem
{
public:
    SourceItem (InstrumentsTree& ownerIn, int instrumentIdxIn, int sourceIdxIn)
        : owner (ownerIn), instrumentIdx (instrumentIdxIn), sourceIdx (sourceIdxIn) {}

    bool mightContainSubItems() override { return false; }
    int  getItemHeight() const override  { return 22; }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected()) g.fillAll (juce::Colours::lightblue);
        g.setColour (juce::Colours::black);
        g.setFont (13.0f);
        const auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        const auto& src  = inst.sources[(size_t) sourceIdx];
        g.drawText (sourceLabel (src, owner.raw),
                    4, 0, width - 8, height, juce::Justification::centredLeft);
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected && owner.notifySelection)
            owner.notifySelection (PropertyPageHost::Kind::Source, instrumentIdx, sourceIdx);
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        if (! e.mods.isRightButtonDown()) return;

        juce::PopupMenu m;
        m.addItem (1, "Delete Source");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (owner),
            [this] (int result)
            {
                if (result == 1) deleteSelf();
            });
    }

    void deleteSelf()
    {
        auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        if (sourceIdx >= 0 && sourceIdx < (int) inst.sources.size())
            inst.sources.erase (inst.sources.begin() + sourceIdx);
        if (owner.notifyMutation) owner.notifyMutation();
        owner.rebuild();
    }

    InstrumentsTree& owner;
    int instrumentIdx;
    int sourceIdx;
};

// ---- InstrumentItem --------------------------------------------------------

class InstrumentsTree::InstrumentItem : public juce::TreeViewItem
{
public:
    InstrumentItem (InstrumentsTree& ownerIn, int instrumentIdxIn)
        : owner (ownerIn), instrumentIdx (instrumentIdxIn) {}

    bool mightContainSubItems() override
    {
        return ! owner.config.instruments[(size_t) instrumentIdx].sources.empty();
    }
    int getItemHeight() const override { return 22; }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected()) g.fillAll (juce::Colours::lightblue);
        g.setColour (juce::Colours::black);
        g.setFont (13.0f);
        const auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        g.drawText (instrumentLabel (inst),
                    4, 0, width - 8, height, juce::Justification::centredLeft);
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected && owner.notifySelection)
            owner.notifySelection (PropertyPageHost::Kind::Instrument, instrumentIdx, -1);
    }

    // Populate children (called by JUCE on first expand).
    void itemOpennessChanged (bool isNowOpen) override
    {
        if (isNowOpen && getNumSubItems() == 0)
        {
            const auto& inst = owner.config.instruments[(size_t) instrumentIdx];
            for (size_t s = 0; s < inst.sources.size(); ++s)
                addSubItem (new SourceItem (owner, instrumentIdx, (int) s));
        }
        else if (! isNowOpen)
        {
            clearSubItems();
        }
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        if (! e.mods.isRightButtonDown()) return;

        juce::PopupMenu m;
        juce::PopupMenu addSub;
        const auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        std::set<int> already;
        for (const auto& s : inst.sources) already.insert (s.midiTrackIndex);

        int anyAvailable = 0;
        for (size_t i = 0; i < owner.raw.tracks.size(); ++i)
        {
            if (already.count ((int) i) > 0) continue;
            const auto& t = owner.raw.tracks[i];
            juce::String label = "MIDI " + juce::String ((int) i) + ": "
                               + juce::String (t.name)
                               + " (chan " + juce::String (t.sourceMidiChannel)
                               + ", " + juce::String ((int) t.notes.size()) + ")";
            addSub.addItem (100 + (int) i, label);
            ++anyAvailable;
        }
        if (anyAvailable == 0)
            addSub.addItem (-1, "(no unused MIDI tracks)", false);

        m.addSubMenu ("Add Source", addSub);
        m.addSeparator();
        m.addItem (2, "Delete Instrument");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (owner),
            [this] (int result)
            {
                if (result >= 100)
                    addSourceForMidi (result - 100);
                else if (result == 2)
                    deleteSelf();
            });
    }

    void addSourceForMidi (int midiIndex)
    {
        auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        inst.sources.push_back (ConfigSource{ midiIndex, 0, 0 });
        if (owner.notifyMutation) owner.notifyMutation();
        owner.rebuild();
    }

    void deleteSelf()
    {
        if (instrumentIdx >= 0 && instrumentIdx < (int) owner.config.instruments.size())
            owner.config.instruments.erase (owner.config.instruments.begin() + instrumentIdx);
        if (owner.notifyMutation) owner.notifyMutation();
        owner.rebuild();
    }

    InstrumentsTree& owner;
    int instrumentIdx;
};

// ---- SongItem --------------------------------------------------------------

class InstrumentsTree::SongItem : public juce::TreeViewItem
{
public:
    explicit SongItem (InstrumentsTree& ownerIn) : owner (ownerIn) {}

    bool mightContainSubItems() override
    {
        return ! owner.config.instruments.empty();
    }
    int getItemHeight() const override { return 22; }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected()) g.fillAll (juce::Colours::lightblue);
        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText (songLabel (owner.config),
                    4, 0, width - 8, height, juce::Justification::centredLeft);
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected && owner.notifySelection)
            owner.notifySelection (PropertyPageHost::Kind::Song, -1, -1);
    }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (isNowOpen && getNumSubItems() == 0)
        {
            for (size_t i = 0; i < owner.config.instruments.size(); ++i)
                addSubItem (new InstrumentItem (owner, (int) i));
        }
        else if (! isNowOpen)
        {
            clearSubItems();
        }
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        if (! e.mods.isRightButtonDown()) return;

        juce::PopupMenu m;
        m.addItem (1, "Add Instrument");
        m.addItem (2, "Clear All Instruments",
                   /*isEnabled*/ ! owner.config.instruments.empty());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (owner),
            [this] (int result)
            {
                if (result == 1) addInstrument();
                else if (result == 2) clearAllInstruments();
            });
    }

    void addInstrument()
    {
        ConfigInstrument fresh;
        int nextX = 1;
        for (const auto& inst : owner.config.instruments)
            nextX = std::max (nextX, inst.x + 1);
        fresh.x    = nextX;
        fresh.name = "LuteOfAges";
        owner.config.instruments.push_back (fresh);
        if (owner.notifyMutation) owner.notifyMutation();
        owner.rebuild();
    }

    void clearAllInstruments()
    {
        if (owner.config.instruments.empty()) return;

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withIconType     (juce::MessageBoxIconType::WarningIcon)
                .withTitle        ("Clear all instruments?")
                .withMessage      ("This will remove every instrument "
                                   "(and its sources) from the song.")
                .withButton       ("Clear")
                .withButton       ("Cancel"),
            [this] (int result)
            {
                if (result != 1) return;  // button index 1 is "Clear"
                owner.config.instruments.clear();
                if (owner.notifyMutation) owner.notifyMutation();
                owner.rebuild();
            });
    }

private:
    InstrumentsTree& owner;
};

// ---- InstrumentsTree -------------------------------------------------------

InstrumentsTree::InstrumentsTree (Config& cfgRef, const Song& rawRef,
                                  std::function<void (PropertyPageHost::Kind, int, int)> onSel,
                                  std::function<void()> onMut)
    : config (cfgRef), raw (rawRef),
      notifySelection (std::move (onSel)),
      notifyMutation  (std::move (onMut))
{
    addAndMakeVisible (treeView);
    treeView.setDefaultOpenness (true);
    rebuild();
}

InstrumentsTree::~InstrumentsTree()
{
    treeView.setRootItem (nullptr);
}

void InstrumentsTree::rebuild()
{
    treeView.setRootItem (nullptr);
    rootItem = std::make_unique<SongItem> (*this);
    treeView.setRootItem (rootItem.get());
    rootItem->setOpen (true);
    rootItem->setSelected (true, true, juce::dontSendNotification);
    if (notifySelection) notifySelection (PropertyPageHost::Kind::Song, -1, -1);
}

void InstrumentsTree::resized() { treeView.setBounds (getLocalBounds()); }

} // namespace lotro
