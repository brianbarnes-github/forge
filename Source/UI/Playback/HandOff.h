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
//  * publish(): the message thread records one ownership entry in `owned` PER
//    PUBLISH (multimap), and puts the raw pointer into the single `pending`
//    slot. If an earlier pending pointer was never picked up, exchange() hands
//    it back and exactly one of its entries is dropped here (the audio thread
//    never saw that publish).
//  * acquire(): the audio thread atomically takes `pending`, makes it current,
//    and pushes the previous current pointer into a fixed-size retire FIFO.
//  * collectRetired(): the message thread pops the FIFO and drops exactly one
//    ownership entry per retired pointer.
//
// Each publish normally passes a fresh, never-published object, but
// republishing is safe by construction: every publish adds one entry and every
// hand-back/retire removes one, so an object stays alive while it is pending or
// current. publish(nullptr) is not allowed.
//
// The pointer returned by acquire() is valid only until the NEXT acquire()
// call. The destructor frees everything (including `current`) and must only
// run after the audio thread has stopped calling acquire().
//
// If the FIFO is full the audio thread simply leaves `pending` in place until
// the message thread drains it.
template <typename T>
class HandOff
{
public:
    void publish (std::shared_ptr<T> next)
    {
        jassert (next != nullptr);
        if (next == nullptr)
            return;

        T* raw = next.get();
        owned.emplace (raw, std::move (next));
        if (T* unclaimed = pending.exchange (raw, std::memory_order_acq_rel))
            dropOne (unclaimed);
        collectRetired();
    }

    void collectRetired()
    {
        int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) dropOne (retired[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) dropOne (retired[(size_t) (s2 + i)]);
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
                    jassert (n1 == 1);   // guaranteed by the getFreeSpace() > 0 guard above
                    retired[(size_t) s1] = current;
                    fifo.finishedWrite (1);
                }
                current = next;
                swapped = true;
            }
        }
        return current;
    }

    // Number of distinct objects still owned (a republished object counts once).
    size_t liveCount() const
    {
        size_t n = 0;
        for (auto it = owned.begin(); it != owned.end(); it = owned.upper_bound (it->first))
            ++n;
        return n;
    }

private:
    void dropOne (T* p)
    {
        if (auto it = owned.find (p); it != owned.end())
            owned.erase (it);
    }

    static_assert (std::atomic<T*>::is_always_lock_free, "audio-thread handoff must not lock");

    std::atomic<T*> pending { nullptr };
    T* current = nullptr;                          // audio thread only
    juce::AbstractFifo fifo { 16 };
    std::array<T*, 16> retired {};
    std::multimap<T*, std::shared_ptr<T>> owned;   // message thread only; one entry per publish
};

} // namespace lotro
