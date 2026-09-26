// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ActionContext.hpp"

#include "totk/engine/Pointer.hpp"

namespace HOOKSHOT_ENGINE_NS::action {
namespace {
std::uintptr_t g_mainBase = 0;
const arrowbound::profiles::Action* g_profile =
    arrowbound::profiles::action(arrowbound::profiles::GameVersion::V121);

}  // namespace

bool Context::actorOk() const {
    return totk::engine::isPlausibleAddress(actor);
}

bool Context::playerOk() const {
    return totk::engine::isPlausibleAddress(playerComponent);
}

void initialize(std::uintptr_t mainBase) { g_mainBase = mainBase; }

bool useGameProfile(arrowbound::profiles::GameVersion version) {
    g_profile = arrowbound::profiles::action(version);
    return g_profile != nullptr;
}

Context resolve(void* actionObject) {
    Context context{};
    if (!actionObject || g_mainBase == 0 || !g_profile) return context;
    const auto getActor = reinterpret_cast<std::uintptr_t (*)(void*)>(
        g_mainBase + g_profile->getActor);
    const auto getPlayer = reinterpret_cast<std::uintptr_t (*)(void*)>(
        g_mainBase + g_profile->getPlayerComponent);
    context.actor = getActor(actionObject);
    context.playerComponent = getPlayer(actionObject);
    return context;
}

pure::Vec3 actorPosition(const Context& context) {
    const auto* position = reinterpret_cast<const float*>(
        context.actor + g_profile->actorPosition);
    return {position[0], position[1], position[2]};
}

pure::Vec3 actorVelocity(const Context& context) {
    const auto* velocity = reinterpret_cast<const float*>(
        context.actor + g_profile->actorVelocity);
    return {velocity[0], velocity[1], velocity[2]};
}

void setLinearVelocity(const Context& context, const pure::Vec3& velocity) {
    const float values[3] = {velocity.x, velocity.y, velocity.z};
    const auto setVelocity =
        reinterpret_cast<void (*)(std::uintptr_t, const float*, bool)>(
            g_mainBase + g_profile->setLinearVelocity);
    setVelocity(context.playerComponent, values, false);
}

}  // namespace HOOKSHOT_ENGINE_NS::action
