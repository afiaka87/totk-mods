// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace arrowbound::pure {

// Verified against the 1.2.1 GameData schema and WellCollection reward event.
inline constexpr std::uint32_t kWellQuestHash = 0xAA6A0618;
inline constexpr std::uint32_t kWellComplete = 0x4C0A63F4;
inline constexpr std::uint32_t kWellNotReady = 0x57B849F0;
inline constexpr std::uint32_t kWellReady = 0x17BFAA46;
inline constexpr std::uint32_t kWellSearch = 0xFF34FC0E;
inline constexpr std::uint32_t kKeyItemNamesHash = 0x22C6530A;
inline constexpr std::uint32_t kKeyItemStocksHash = 0x60FAC288;
inline constexpr char kSavedCarrierName[] = "Obj_CaveWellHonor_00";

enum class CarrierSaveResult { Invalid, UnknownQuest, Earned, Absent, Removed };

inline std::uint32_t saveWord(const unsigned char* data, std::size_t offset) {
    std::uint32_t value;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

inline CarrierSaveResult filterCarrierSave(unsigned char* data, std::size_t size) {
    if (!data || size < 0x28 || size > 16 * 1024 * 1024 ||
        saveWord(data, 0) != 0x01020304) return CarrierSaveResult::Invalid;
    const std::size_t tableEnd = saveWord(data, 8);
    if (tableEnd < 0x28 || tableEnd > size || (tableEnd - 0x20) % 8)
        return CarrierSaveResult::Invalid;
    std::size_t quest = 0, names = 0, stocks = 0;
    for (std::size_t offset = 0x20; offset < tableEnd; offset += 8) {
        const auto hash = saveWord(data, offset);
        auto* found = hash == kWellQuestHash ? &quest :
                      hash == kKeyItemNamesHash ? &names :
                      hash == kKeyItemStocksHash ? &stocks : nullptr;
        if (found) {
            if (*found) return CarrierSaveResult::Invalid;
            *found = offset + 4;
        }
    }
    if (!quest) return CarrierSaveResult::UnknownQuest;
    const auto step = saveWord(data, quest);
    if (step == kWellComplete) return CarrierSaveResult::Earned;
    if (step != kWellNotReady && step != kWellReady && step != kWellSearch)
        return CarrierSaveResult::UnknownQuest;
    if (!names || !stocks) return CarrierSaveResult::Invalid;
    names = saveWord(data, names);
    stocks = saveWord(data, stocks);
    constexpr std::size_t kCapacity = 200;
    constexpr std::size_t kNamesBytes = 4 + kCapacity * 64;
    constexpr std::size_t kStocksBytes = 4 + kCapacity * 4;
    if (names < tableEnd || stocks < tableEnd || names > size || stocks > size ||
        kNamesBytes > size - names || kStocksBytes > size - stocks ||
        (names < stocks + kStocksBytes && stocks < names + kNamesBytes) ||
        saveWord(data, names) != kCapacity || saveWord(data, stocks) != kCapacity)
        return CarrierSaveResult::Invalid;
    // Validate all slot strings before any write; malformed input is byte-for-byte unchanged.
    for (std::size_t i = 0; i < kCapacity; ++i)
        if (!std::memchr(data + names + 4 + i * 64, 0, 64)) return CarrierSaveResult::Invalid;
    bool removed = false;
    for (std::size_t i = 0; i < kCapacity; ++i) {
        auto* name = data + names + 4 + i * 64;
        if (std::memcmp(name, kSavedCarrierName, sizeof(kSavedCarrierName)) != 0) continue;
        std::memset(name, 0, 64);
        std::memset(data + stocks + 4 + i * 4, 0, 4);
        removed = true;
    }
    return removed ? CarrierSaveResult::Removed : CarrierSaveResult::Absent;
}

}  // namespace arrowbound::pure
