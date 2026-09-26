// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Action hooks resolve the actor and Player component from the active game profile.
#pragma once

#include "EngineNamespace.hpp"

#include <cstdint>

#include <arrowbound/GameProfiles.hpp>
#include "Vec3.hpp"

namespace HOOKSHOT_ENGINE_NS::action {
struct Context {
    std::uintptr_t actor = 0;
    std::uintptr_t playerComponent = 0;

    [[nodiscard]] bool actorOk() const;
    [[nodiscard]] bool playerOk() const;
    [[nodiscard]] bool ok() const { return actorOk() && playerOk(); }
};

void initialize(std::uintptr_t mainBase);
bool useGameProfile(arrowbound::profiles::GameVersion version);

Context resolve(void* action);

pure::Vec3 actorPosition(const Context& context);
pure::Vec3 actorVelocity(const Context& context);

// scale=false: raw metres per second (the true path multiplies by 30).
void setLinearVelocity(const Context& context, const pure::Vec3& velocity);

}  // namespace HOOKSHOT_ENGINE_NS::action
