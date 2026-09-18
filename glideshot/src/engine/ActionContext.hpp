// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// The velocity setter is the Player component call at main+0x01621CBC, not the shared rigid-body lane. What an
// action-thread hook may touch: the actor and Player component resolved from the action's own context.
#pragma once

#include <cstdint>

#include "Vec3.hpp"

namespace zonai_hookshot::action {
struct Context {
    std::uintptr_t actor = 0;
    std::uintptr_t playerComponent = 0;

    [[nodiscard]] bool actorOk() const;
    [[nodiscard]] bool playerOk() const;
    [[nodiscard]] bool ok() const { return actorOk() && playerOk(); }
};

void initialize(std::uintptr_t mainBase);

// Verified in the 1.2.1 disassembly: ExecuteBase::getActor main+0x00BC7610, GetPlayerComponent
// main+0x0107E9A0.
Context resolve(void* action);

pure::Vec3 actorPosition(const Context& context);
pure::Vec3 actorVelocity(const Context& context);

// scale=false: raw metres per second (the true path multiplies by 30).
void setLinearVelocity(const Context& context, const pure::Vec3& velocity);

}  // namespace zonai_hookshot::action
