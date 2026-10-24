// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace stasis {
// TotK 1.2.1 Recall uses only count, units and flags here; every unit points to a real native object.
struct ModelView {
    std::byte prefix[32]{};
    std::uint32_t count{};
    std::uint32_t padding{};
    std::uintptr_t* units{};
    std::byte middle[529]{};
    std::uint8_t flags{};
};
static_assert(offsetof(ModelView, count) == 32);
static_assert(offsetof(ModelView, units) == 40);
static_assert(offsetof(ModelView, flags) == 577);

inline constexpr unsigned kQueueNodeCapacity = 65536;
// Record membership before reusing the sorted unit array; native queue nodes stay untouched.
template<class Read>
unsigned selectQueueNodes(const std::uintptr_t* nodes, unsigned count,
                          std::uintptr_t* unitsAndOutput, unsigned unitCount,
                          unsigned capacity,
                          std::uint64_t (&matches)[kQueueNodeCapacity / 64], Read read) {
    if (count > kQueueNodeCapacity || unitCount > capacity) return capacity + 1;
    std::fill_n(matches, (count + 63) / 64, std::uint64_t{});
    unsigned selected = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto unit = read.template operator()<std::uintptr_t>(nodes[i] + 16);
        if (std::binary_search(unitsAndOutput, unitsAndOutput + unitCount, unit)) {
            if (selected == capacity) return capacity + 1;
            matches[i / 64] |= std::uint64_t{1} << (i % 64);
            ++selected;
        }
    }
    unsigned written = 0;
    for (unsigned i = 0; i < count; ++i)
        if (matches[i / 64] & (std::uint64_t{1} << (i % 64)))
            unitsAndOutput[written++] = nodes[i];
    return selected;
}
// Borrow only the queue header; preserve its mutex/name at +72 and disable cached draws that bypass filtering.
class ScopedQueueNodes {
    void* queue_;
    std::byte saved_[72];
    template<class T> void set(unsigned offset, T value) {
        std::memcpy(static_cast<std::byte*>(queue_) + offset, &value, sizeof(value));
    }
public:
    ScopedQueueNodes(void* queue, std::uintptr_t* nodes, unsigned count): queue_(queue) {
        std::memcpy(saved_, queue_, sizeof(saved_));
        set<std::uint8_t>(8, 0);
        set<std::uint32_t>(16, UINT32_MAX);
        set<std::uint64_t>(20, 0);
        set(32, nodes);
        set(40, count);
        set(44, count);
        set<std::uint32_t>(64, 0);
    }
    ~ScopedQueueNodes() { std::memcpy(queue_, saved_, sizeof(saved_)); }
    ScopedQueueNodes(const ScopedQueueNodes&) = delete;
    ScopedQueueNodes& operator=(const ScopedQueueNodes&) = delete;
};
} // namespace stasis
