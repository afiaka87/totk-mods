// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
#include <initializer_list>

namespace cryonis::engine {
template <class Original, class Blocked>
std::uint64_t recentAbilityAdmission(std::uintptr_t actor, bool remembered,
                                    std::uintptr_t player, Original original, Blocked blocked) {
    if (remembered && player && actor == player) {
        blocked(actor);
        return 0;
    }
    return original(actor);
}

template <class Read, class Valid>
std::uintptr_t executeActor(std::uintptr_t node, Read read, Valid valid) {
    auto pointer = node;
    for (const auto offset : {std::uintptr_t{8}, std::uintptr_t{8}, std::uintptr_t{0x1F8}}) {
        if (!valid(pointer) || !valid(pointer + offset))
            return 0;
        pointer = read(pointer + offset);
    }
    return valid(pointer) ? pointer : 0;
}
}
