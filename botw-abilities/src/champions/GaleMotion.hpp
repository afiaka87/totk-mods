// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cstdint>

namespace botw_abilities::champions {
// Player::setLinearVelocity uses this character-body path, not the generic actor-body slot.
template <class Valid, class Read>
std::uintptr_t galeLaunchBody(std::uintptr_t component, std::uintptr_t actor, Valid valid, Read read) {
    if (!valid(component) || read(component + 24) != actor) return 0;
    auto value = actor;
    for (std::uintptr_t offset : std::array<std::uintptr_t, 6>{0x228, 0x50, 0x20, 0x10, 0x18, 0}) {
        if (!valid(value)) return 0;
        value = read(value + offset);
    }
    return valid(value) ? value : 0;
}
} // namespace botw_abilities::champions
