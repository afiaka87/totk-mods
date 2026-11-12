// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "doctest.h"
#include "../src/wolf/MemoryLog.hpp"
#include <cstring>
#include <string_view>
#include <vector>

using namespace botw_abilities::wolf;

namespace {
bool hostRead(std::uintptr_t address, void* output, std::size_t size) {
    if (address < 0x1000) return false;
    std::memcpy(output, reinterpret_cast<const void*>(address), size);
    return true;
}

template<class T> void put(std::vector<std::byte>& memory, std::size_t offset, T value) {
    std::memcpy(memory.data() + offset, &value, sizeof(value));
}

std::uintptr_t at(std::vector<std::byte>& memory, std::size_t offset) {
    return reinterpret_cast<std::uintptr_t>(memory.data()) + offset;
}
}

TEST_CASE("A heap's use is its reserved and spare bytes less its free bytes") {
    std::vector<std::byte> memory(0x400);
    constexpr std::size_t heap = 0, first = 0x100, second = 0x180, spare = 0x200;
    put<std::uint64_t>(memory, heap + 0x30, 1000);
    put<std::uintptr_t>(memory, heap + 184, at(memory, first + 0x20));
    put<std::int32_t>(memory, heap + 196, 0x20);
    put<std::uint64_t>(memory, first + 24, 120);
    put<std::uintptr_t>(memory, first + 0x28, at(memory, second + 0x20));
    put<std::uint64_t>(memory, second + 24, 80);
    put<std::uintptr_t>(memory, second + 0x28, at(memory, heap + 176));
    put<std::uintptr_t>(memory, heap + 224, at(memory, spare));
    put<std::uint64_t>(memory, spare, 500);
    const auto use = inspectActorHeap(hostRead, at(memory, heap));
    CHECK(use.complete);
    CHECK(use.size == 1000);
    CHECK(use.spare == 500);
    CHECK(use.free == 200);
    CHECK(use.freeBlocks == 2);
    CHECK(use.spareBlocks == 1);
    CHECK(use.used() == 1300);
}

TEST_CASE("A heap list that loops or breaks ends the walk and is marked incomplete") {
    std::vector<std::byte> memory(0x400);
    put<std::uintptr_t>(memory, 184, at(memory, 0x100));
    put<std::uintptr_t>(memory, 0x108, at(memory, 0x100));
    CHECK_FALSE(inspectActorHeap(hostRead, at(memory, 0)).complete);
    put<std::uintptr_t>(memory, 0x108, 0);
    CHECK_FALSE(inspectActorHeap(hostRead, at(memory, 0)).complete);
}

TEST_CASE("Each registered pool is read with its name, and the walk stops at the list head") {
    std::vector<std::byte> memory(0x1000);
    constexpr std::size_t head = 0x5B8, pool = 0x700, heap = 0x900, text = 0x980;
    constexpr std::int32_t offset = 0x20;
    put<std::uintptr_t>(memory, head + 8, at(memory, pool + offset));
    put<std::int32_t>(memory, head + 0x10, 1);
    put<std::int32_t>(memory, head + 0x14, offset);
    put<std::uintptr_t>(memory, pool + offset + 8, at(memory, head));
    put<std::uint32_t>(memory, pool + 16, 4);
    put<std::int32_t>(memory, pool + 72, 3);
    put<std::uint32_t>(memory, pool + 0xA0, 900);
    put<std::uint32_t>(memory, pool + 0xA4, 700);
    put<std::uintptr_t>(memory, pool + 24, at(memory, heap));
    put<std::uintptr_t>(memory, heap + 0x20, at(memory, text));
    std::memcpy(memory.data() + text, "ForResourceU", 13);
    std::array<PoolUse, 4> pools;
    REQUIRE(inspectResourcePools(hostRead, at(memory, 0), pools) == 1);
    CHECK(std::string_view(pools[0].name.data()) == "ForResourceU");
    CHECK(pools[0].total == 900);
    CHECK(pools[0].active == 700);
    CHECK(pools[0].units == 3);
    CHECK(pools[0].flags == 4);
}

TEST_CASE("A pool walk never overruns its table, and an unreadable name leaves the numbers") {
    std::vector<std::byte> memory(0x1000);
    constexpr std::size_t head = 0x5B8, pool = 0x700;
    put<std::uintptr_t>(memory, head + 8, at(memory, pool));
    put<std::int32_t>(memory, head + 0x10, 99);
    put<std::uintptr_t>(memory, pool + 8, at(memory, pool));
    put<std::uint32_t>(memory, pool + 0xA0, 5);
    std::array<PoolUse, 2> pools;
    CHECK(inspectResourcePools(hostRead, at(memory, 0), pools) == 2);
    CHECK(pools[1].total == 5);
    CHECK(pools[1].name[0] == 0);
}
