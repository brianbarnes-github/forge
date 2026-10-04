#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>

namespace lotro
{

// Publishes immutable objects (PlaybackSnapshot, tsf instances) from the
// message thread to the audio thread without locks or audio-thread frees.
//
//  * publish(): the message thread owns every object in `owned`, and puts the
//    new raw pointer into the single `pending` slot. If an earlier pending
//    object was never picked up, exchange() hands it back and it is freed
//    here (the audio thread never saw it).
//  * acquire(): the audio thread atomically takes `pending`, makes it current,
//    and pushes the previous current pointer into a fixed-size retire FIFO.
//  * collectRetired(): the message thread pops the FIFO and drops ownership.
//
// If the FIFO is full the audio thread simply leaves `pending` in place until
// the message thread drains it.
template <typename T>
class HandOff
{
public:
    void publish (std::shared_ptr<T> next)
    {
        T* raw = next.get();
        owned.emplace (raw, std::move (next));
        // Republishing the object that is still pending hands back `raw` itself;
        // erasing it would leave `pending` dangling.
        if (T* unclaimed = pending.exchange (raw, std::memory_order_acq_rel); unclaimed != nullptr && unclaimed != raw)
            owned.erase (unclaimed);
        collectRetired();
    }

    void collectRetired()
    {
        int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) owned.erase (retired[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) owned.erase (retired[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }

    T* acquire (bool& swapped) noexcept
    {
        swapped = false;
        if (pending.load (std::memory_order_acquire) != nullptr && (current == nullptr || fifo.getFreeSpace() > 0))
        {
            if (T* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (current != nullptr)
                {
                    int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
                    fifo.prepareToWrite (1, s1, n1, s2, n2);
                    retired[(size_t) s1] = current;
                    fifo.finishedWrite (1);
                }
                current = next;
                swapped = true;
            }
        }
        return current;
    }

    size_t liveCount() const { return owned.size(); }

private:
    std::atomic<T*> pending { nullptr };
    T* current = nullptr;                          // audio thread only
    juce::AbstractFifo fifo { 16 };
    std::array<T*, 16> retired {};
    std::map<T*, std::shared_ptr<T>> owned;        // message thread only
};

} // namespace lotro
