// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace botw_abilities::champions::audio {

struct Table {
    const char* name{};
    std::uint16_t assetId{}, flags{};
    std::int32_t duration{}, parent{};
    std::uint16_t enumIndex{};
    std::uint8_t solved{}, padding{};
    std::uint32_t hash{}, alignment{};
    std::uintptr_t parameters{}, condition{};
};
struct Locator {
    const Table* table{};
    std::uintptr_t auxiliary{};
    std::uint32_t type{}, padding{};
};
static_assert(sizeof(Table) == 48 && offsetof(Table, parameters) == 32 && sizeof(Locator) == 24);

// Remote Bomb's re-serialized SLink2 leaves UI_GlobalSound's call table only 4-byte aligned.
[[nodiscard]] constexpr bool donorTableAddress(std::uintptr_t address) {
    return address >= 0x1000 && address < (1ull << 40) && (address & 3) == 0;
}

// Bounded logging for per-frame refusals: the first few, then every 1024th.
struct RefusalCounter {
    std::uint32_t count = 0;
    bool report() { return ++count <= 4 || (count & 1023) == 0; }
};

} // namespace botw_abilities::champions::audio
