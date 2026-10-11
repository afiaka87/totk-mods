// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cstdint>

namespace remote_bomb::blast {
enum class Admission { Apply, Duplicate, Expired, Capacity, Unavailable };
enum class Activation { Started, Existing, Capacity, Unavailable };
template <unsigned Sources = 8, unsigned Targets = 256>
struct Windows {
    struct Source {
        std::uintptr_t identity{};
        std::uint64_t born{}, visitedAt{};
        std::array<std::uintptr_t, Targets> targets{};
        unsigned count{};
    };
    std::array<Source, Sources> entries{};

    void reset(std::uintptr_t source) {
        for (auto &entry : entries)
            if (entry.identity == source)
                entry = {};
    }
    Activation activate(std::uintptr_t source, std::uint64_t clock) {
        if (!source)
            return Activation::Unavailable;
        for (const auto &entry : entries)
            if (entry.identity == source)
                return Activation::Existing;
        for (auto &entry : entries)
            if (!entry.identity) {
                entry.identity = source;
                entry.born = clock;
                entry.visitedAt = clock;
                return Activation::Started;
            }
        return Activation::Capacity;
    }
    Admission admit(std::uintptr_t source, std::uintptr_t target, std::uint64_t clock) {
        if (!source || !target)
            return Admission::Unavailable;
        Source *slot{};
        for (auto &entry : entries)
            if (entry.identity == source) {
                slot = &entry;
                break;
            }
        if (!slot)
            return Admission::Unavailable;
        if (clock < slot->born || clock - slot->born >= (std::uint64_t{1} << 32))
            return Admission::Expired;
        if (clock != slot->visitedAt) {
            slot->count = 0;
            slot->visitedAt = clock;
        }
        for (unsigned i = 0; i < slot->count; ++i)
            if (slot->targets[i] == target)
                return Admission::Duplicate;
        if (slot->count == Targets)
            return Admission::Capacity;
        slot->targets[slot->count++] = target;
        return Admission::Apply;
    }
};
}
