// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ActionContext.hpp"

#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace arrowbound::action {
namespace {
namespace off {
constexpr std::ptrdiff_t kAiGetActor = 0x00BC7610;
constexpr std::ptrdiff_t kAiGetPlayerComponent = 0x0107E9A0;
constexpr std::ptrdiff_t kPlayerSetLinearVelocity = 0x01621CBC;
}  // namespace off

std::uintptr_t g_mainBase = 0;

}  // namespace

bool Context::actorOk() const {
    return totk::engine::isPlausibleAddress(actor);
}

bool Context::playerOk() const {
    return totk::engine::isPlausibleAddress(playerComponent);
}

void initialize(std::uintptr_t mainBase) { g_mainBase = mainBase; }

Context resolve(void* actionObject) {
    Context context{};
    if (!actionObject || g_mainBase == 0) return context;
    const auto getActor = reinterpret_cast<std::uintptr_t (*)(void*)>(
        g_mainBase + off::kAiGetActor);
    const auto getPlayer = reinterpret_cast<std::uintptr_t (*)(void*)>(
        g_mainBase + off::kAiGetPlayerComponent);
    context.actor = getActor(actionObject);
    context.playerComponent = getPlayer(actionObject);
    return context;
}

pure::Vec3 actorPosition(const Context& context) {
    const auto* position = reinterpret_cast<const float*>(
        context.actor + totk::engine::layout::kActorPosition);
    return {position[0], position[1], position[2]};
}

pure::Vec3 actorVelocity(const Context& context) {
    const auto* velocity = reinterpret_cast<const float*>(
        context.actor + totk::engine::layout::kActorLinearVelocity);
    return {velocity[0], velocity[1], velocity[2]};
}

void setLinearVelocity(const Context& context, const pure::Vec3& velocity) {
    const float values[3] = {velocity.x, velocity.y, velocity.z};
    const auto setVelocity =
        reinterpret_cast<void (*)(std::uintptr_t, const float*, bool)>(
            g_mainBase + off::kPlayerSetLinearVelocity);
    setVelocity(context.playerComponent, values, false);
}

}  // namespace arrowbound::action
