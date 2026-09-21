// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace arrowbound::engine {

constexpr std::uintptr_t kArrowControllerActor = 24;
constexpr std::uintptr_t kArrowControllerShootable = 80;
// Shootable+112 is the shooter link; +304 belongs to the fused attachment.
constexpr std::uintptr_t kShootableShooterLink = 112;

struct ArrowIdentity {
    std::uintptr_t actor = 0;
    std::uintptr_t shootable = 0;
    std::uintptr_t shooter = 0;

    bool belongsTo(std::uintptr_t player) const {
        return player != 0 && shooter == player;
    }
};

template <class ValidPointer, class ReadPointer, class ResolveActor>
ArrowIdentity resolveArrowIdentity(std::uintptr_t controller, ValidPointer valid,
                                   ReadPointer read, ResolveActor resolve) {
    ArrowIdentity result{};
    if (!valid(controller)) return result;
    result.actor = read(controller + kArrowControllerActor);
    result.shootable = read(controller + kArrowControllerShootable);
    if (!valid(result.actor) || !valid(result.shootable)) return result;
    const auto shooter = resolve(result.shootable + kShootableShooterLink);
    if (valid(shooter)) result.shooter = shooter;
    return result;
}

}  // namespace arrowbound::engine
