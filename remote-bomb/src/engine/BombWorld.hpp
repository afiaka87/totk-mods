// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombReference.hpp"
#include "NativeBombActions.hpp"
#include "totk/engine/ActorRoster.hpp"
#include <array>

namespace remote_bomb::engine {
using Handle = totk::engine::ActorHandle;
struct RosterEntry {
    Handle actor;
    Shape shape{};
    Upgrade upgrade{};
    unsigned processState{};
};
struct Roster {
    std::array<RosterEntry, 16> entries{};
    unsigned count{};
    bool complete{};
    bool contains(Handle actor) const;
};
const char *actorName(Shape shape, Upgrade upgrade);
Roster scan(std::uintptr_t base, totk::engine::SceneContext scene);
Position position(Handle actor);
}
