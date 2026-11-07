// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeActor.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace botw_abilities::wolf {

void initializeMemoryLog(std::uintptr_t base);
// Both only read native memory, each read checked first, so they are safe from any thread.
void logResourceMemory(const char* moment);
void logActorHeap(const ActorReference& actor, const char* moment);

struct HeapUse {
    bool complete{};
    std::uint64_t size{}, spare{}, free{};
    unsigned spareBlocks{}, freeBlocks{};
    [[nodiscard]] std::uint64_t used() const { return size + spare - free; }
};

struct PoolUse {
    std::array<char, 24> name{};
    std::uint32_t total{}, active{}, flags{};
    std::int32_t units{};
};

// A live heap can change during the walk, so a broken or endless list ends it with complete=false.
template<class Read> HeapUse inspectActorHeap(Read read, std::uintptr_t heap) {
    constexpr unsigned kLimit = 512;
    constexpr std::uintptr_t kSize = 0x30, kFree = 176, kSpare = 224;
    const auto get = [&](std::uintptr_t address, auto& value) { return read(address, &value, sizeof(value)); };
    HeapUse use;
    std::int32_t offset{};
    std::uintptr_t link{}, block{};
    if (!get(heap + kSize, use.size) || !get(heap + kFree + 8, link) || !get(heap + kFree + 20, offset) ||
        !get(heap + kSpare, block)) return use;
    const auto end = heap + kFree;
    for (; link != end; ++use.freeBlocks) {
        std::uint64_t size{};
        if (use.freeBlocks == kLimit || !get(link - std::intptr_t(offset) + 24, size) || !get(link + 8, link)) return use;
        use.free += size;
    }
    for (; block; ++use.spareBlocks) {
        std::uint64_t size{};
        if (use.spareBlocks == kLimit || !get(block, size) || !get(block + 8, block)) return use;
        use.spare += size;
    }
    use.complete = true;
    return use;
}

template<class Read> unsigned inspectResourcePools(Read read, std::uintptr_t system, std::span<PoolUse> pools) {
    constexpr std::uintptr_t kPools = 0x5B8, kHeap = 24, kUnits = 72, kTotal = 0xA0, kActive = 0xA4, kHeapName = 0x20;
    const auto get = [&](std::uintptr_t address, auto& value) { return read(address, &value, sizeof(value)); };
    const auto head = system + kPools;
    std::int32_t count{}, offset{};
    std::uintptr_t link{};
    if (!get(head + 8, link) || !get(head + 0x10, count) || !get(head + 0x14, offset)) return 0;
    unsigned found = 0;
    while (link != head && std::int64_t(found) < count && found < pools.size()) {
        const auto pool = link - std::intptr_t(offset);
        auto& row = pools[found];
        std::uintptr_t heap{}, name{};
        if (!get(pool + 16, row.flags) || !get(pool + kUnits, row.units) || !get(pool + kTotal, row.total) ||
            !get(pool + kActive, row.active) || !get(link + 8, link)) break;
        ++found;
        if (!get(pool + kHeap, heap) || !get(heap + kHeapName, name)) continue;
        for (std::size_t i = 0; i + 1 < row.name.size(); ++i) {
            char letter{};
            if (!get(name + i, letter) || letter < 32 || letter > 126) break;
            row.name[i] = letter;
        }
    }
    return found;
}
}
