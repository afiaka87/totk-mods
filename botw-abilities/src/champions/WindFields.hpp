// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <array>
#include <span>

namespace botw_abilities::champions {
struct WindFieldOwner {
    bool owned = false;
    std::uint32_t actorId = 0;
    totk::core::WorldPosition anchor{};
};
struct WindFieldActor {
    std::uint32_t actorId = 0;
    totk::core::WorldPosition position{};
};

inline std::array<int, 3> matchWindFields(const std::array<WindFieldOwner, 3>& owners,
                                        std::span<const WindFieldActor> actors) {
    std::array<int, 3> result{-1, -1, -1};
    const auto used = [&](std::size_t index) {
        for (int value : result) if (value == static_cast<int>(index)) return true;
        return false;
    };
    for (std::size_t slot = 0; slot < owners.size(); ++slot)
        if (owners[slot].owned && owners[slot].actorId)
            for (std::size_t i = 0; i < actors.size(); ++i)
                if (!used(i) && actors[i].actorId == owners[slot].actorId) {
                    result[slot] = static_cast<int>(i);
                    break;
                }
    for (std::size_t slot = 0; slot < owners.size(); ++slot) {
        if (!owners[slot].owned || owners[slot].actorId) continue;
        float nearest = 0.25f;
        for (std::size_t i = 0; i < actors.size(); ++i) {
            if (used(i)) continue;
            const auto& a = owners[slot].anchor;
            const auto& b = actors[i].position;
            const auto x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
            const auto distance = x*x + y*y + z*z;
            if (distance < nearest) { nearest = distance; result[slot] = static_cast<int>(i); }
        }
    }
    return result;
}
} // namespace botw_abilities::champions
