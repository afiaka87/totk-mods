// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace remote_bomb::engine {
template <class Original, class Blocked>
std::uint64_t recentAbilityAdmission(std::uintptr_t actor, bool remembered,
                                    std::uintptr_t player, Original original, Blocked blocked) {
    if (remembered && player && actor == player) {
        blocked(actor);
        return 0;
    }
    return original(actor);
}
}
