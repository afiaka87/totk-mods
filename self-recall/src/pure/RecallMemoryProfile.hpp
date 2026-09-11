#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#ifndef SELF_RECALL_MEMORY_PROFILE
#define SELF_RECALL_MEMORY_PROFILE 0
#endif

namespace self_recall::pure {
template<unsigned Capacity>
class HistoryMemoryUsage {
    std::array<std::uint32_t, Capacity> sizes_{};
    unsigned first_ = 0, count_ = 0, generation_ = 0;
public:
    std::uint64_t bytes = 0, peakBytes = 0;
    unsigned peakFrames = 0;
    unsigned gaps = 0;
    void record(unsigned slot, unsigned size, unsigned generation, unsigned retained) {
        if (slot >= Capacity || !generation || !retained || retained > Capacity) return;
        if (generation != generation_ || (first_ + count_) % Capacity != slot) {
            if (generation == generation_) ++gaps;
            first_ = slot; count_ = 0; bytes = 0; generation_ = generation;
        }
        if (count_ == Capacity) pop();
        sizes_[slot] = size; bytes += size; ++count_;
        while (count_ > retained) pop();
        peakBytes = std::max(peakBytes, bytes);
        peakFrames = std::max(peakFrames, count_);
    }
private:
    void pop() { bytes -= sizes_[first_]; first_ = (first_ + 1) % Capacity; --count_; }
};
struct PoolMemoryUsage {
    std::uint64_t liveBytes = 0, peakBytes = 0, creates = 0, failures = 0;
    std::uint64_t liveAllocated = 0, peakAllocated = 0;
    std::uint64_t references = 0, peakReferences = 0;
    unsigned liveStates = 0, peakStates = 0;
    void create(unsigned bytes, unsigned allocated) {
        liveBytes += bytes; ++liveStates; ++creates;
        liveAllocated += allocated;
        retain();
        peakAllocated = std::max(peakAllocated, liveAllocated);
        peakBytes = std::max(peakBytes, liveBytes);
        peakStates = std::max(peakStates, liveStates);
    }
    void release(unsigned bytes, unsigned allocated) {
        liveBytes -= bytes; liveAllocated -= allocated; --liveStates;
    }
    void retain() { ++references; peakReferences = std::max(peakReferences, references); }
    void dropReference() { --references; }
};
}
