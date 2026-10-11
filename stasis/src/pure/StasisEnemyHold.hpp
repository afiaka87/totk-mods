// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
#include <cstring>
namespace stasis {
inline constexpr float kEnemyHoldFrames = 150.0f;         // Five seconds at 30 frames per second.
inline constexpr std::uintptr_t kPhysicsOwnerOffset = 24; // Owning actor of a physics component.
template <class Read, class Stop>
bool holdEnemyPhysics(std::uintptr_t held, std::uintptr_t component, Read read, Stop stop) {
    if (!held || !component)
        return false;
    const auto owner = read(component + kPhysicsOwnerOffset);
    if (owner != held)
        return false;
    return stop(owner);
}
struct EnemyAnchor {
    std::uintptr_t actor{}, name{};
    std::uint32_t generation{};
    float matrix[12]{};
};
inline bool matchesEnemyAnchor(const EnemyAnchor &anchor, std::uintptr_t actor, std::uintptr_t name,
                               std::uintptr_t frozen, std::uintptr_t paused,
                               std::uint32_t generation) {
    return actor && anchor.actor == actor && anchor.name == name && frozen == actor &&
           paused == actor && anchor.generation == generation;
}
// Linear and angular motion, current and previous: four vectors cleared after a pose correction.
inline constexpr std::uintptr_t kActorMotionOffset = 800;
inline constexpr unsigned kActorMotionFloats = 12;
template <class Original, class Force, class Commit, class Active>
std::uintptr_t updateAnchoredEnemy(const float *anchor, float *output, Original original,
                                   Force force, Commit commit, Active active) {
    if (!anchor || !active())
        return original();
    force(anchor);
    const auto result = original(); // Keep native requests, bindings and post-update callbacks.
    if (!active())
        return result;
    force(anchor);
    commit(anchor);
    std::memcpy(output, anchor, 12 * sizeof(float));
    return result | std::uintptr_t{1}; // Caller must consume the anchored matrix as changed.
}
} // namespace stasis
