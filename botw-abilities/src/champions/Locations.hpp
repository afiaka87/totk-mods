// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace botw_abilities::champions {

inline constexpr std::array<std::string_view, 21> kCastleLocations{{
    "HyruleCastle", "HyruleCastleUnderground",
    "HyruleCastle_Hall_0", "HyruleCastle_Hall_1", "HyruleCastle_Hall_2",
    "HyruleCastle_Room_0", "HyruleCastle_Room_1", "HyruleCastle_Room_2",
    "HyruleCastle_Room_3", "HyruleCastle_Room_5", "HyruleCastle_Room_6",
    "HyruleCastle_Room_7", "HyruleCastle_Room_8", "HyruleCastle_Room_9",
    "HyruleCastle_Room_10", "HyruleCastle_Room_11", "LargeDungeonHyruleCastle",
    "LargeDungeonHyruleCastle_1F", "LargeDungeonHyruleCastle_B1",
    "LargeDungeonHyruleCastle_B2", "LargeDungeonHyruleCastle_B3",
}};

constexpr bool castleLocation(std::string_view name) {
    for (const auto location : kCastleLocations)
        if (name == location) return true;
    return false;
}

enum class LocationError : std::uint8_t { None, MissingManager, Read, Count, Name, List };

struct LocationResult {
    bool castle = false;
    LocationError error = LocationError::None;
    explicit operator bool() const { return error == LocationError::None; }
};

// Read the 1.2.1 LocationMgr list only on its owner thread, while the scene remains live.
template <class Read>
LocationResult castleFromLocations(std::uintptr_t manager, const Read& read) {
    if (!manager) return {false, LocationError::MissingManager};
    const auto value = [&](std::uintptr_t address, auto& destination) {
        return read(address, &destination, sizeof(destination));
    };
    const auto classify = [&](std::uintptr_t string) -> LocationResult {
        std::uintptr_t text = 0;
        std::int32_t capacity = 0;
        if (!value(string, text) || !value(string + 8, capacity))
            return {false, LocationError::Read};
        if (!text || capacity < 1) return {false, LocationError::Name};
        std::array<char, 64> name{};
        const auto limit = static_cast<std::size_t>(capacity) < name.size()
                               ? static_cast<std::size_t>(capacity) : name.size();
        for (std::size_t i = 0; i < limit; ++i) {
            if (!value(text + i, name[i])) return {false, LocationError::Read};
            if (!name[i]) return {castleLocation({name.data(), i})};
        }
        return {false, LocationError::Name};
    };

    std::int32_t count = 0;
    std::uintptr_t link = 0;
    if (!value(manager + 40, count) || !value(manager + 32, link))
        return {false, LocationError::Read};
    if (count < 0 || count > 128) return {false, LocationError::Count};
    const auto sentinel = manager + 24;
    bool castle = false;
    for (int i = 0; i < count; ++i) {
        if (link < 88 || link == sentinel) return {false, LocationError::List};
        const auto location = classify(link - 88);
        if (!location) return location;
        castle |= location.castle;
        if (!value(link + 8, link)) return {false, LocationError::Read};
    }
    if (link != sentinel) return {false, LocationError::List};
    if (castle) return {true};
    return classify(manager + 3400);
}

} // namespace botw_abilities::champions
