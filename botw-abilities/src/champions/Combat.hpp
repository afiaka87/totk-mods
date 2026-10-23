// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "State.hpp"

namespace botw_abilities::champions {

constexpr bool guardingFromEquipment(std::uint64_t flags) {
    return (flags & 0x3C00u) != 0;
}
constexpr unsigned guardSlot(std::uint64_t flags) {
    return (flags & 0xC00u) ? 0 : (flags & 0x3000u) ? 4 : 8;
}

struct IncomingHit {
    std::uint32_t type = 0;
    std::uint32_t attributes = 0;
    std::uint32_t reaction = 0;
    std::uint8_t flags = 0;
};

constexpr bool activeHit(const IncomingHit& hit) {
    return hit.type && ((hit.flags & 8u) || (hit.attributes && hit.reaction));
}

constexpr bool canBlock(const State& state, const Context& context,
                        std::uint64_t equipmentFlags, const IncomingHit& hit) {
    return activeHit(hit) && barrier(state, context, guardingFromEquipment(equipmentFlags));
}

// Called for the native charged-attack entry, not for the charging selector or an input edge.
constexpr bool canReleaseFury(const State& state, const Context& context,
                              unsigned nativeChargeLevel, bool ownedPlayer) {
    return ownedPlayer && nativeChargeLevel >= 1 && nativeChargeLevel <= 2 &&
           fury(state, context, true);
}

} // namespace botw_abilities::champions
