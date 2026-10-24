// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StasisEngine.hpp"
#include "StasisActorPolicy.hpp"
#include "StasisPresentation.hpp"
#include "StasisTarget.hpp"
#include "totk/engine/ActorHandle.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace stasis::engine {
using Handle = totk::engine::ActorHandle;
inline constexpr std::size_t kRosterCapacity = 4096;
// Rigid body motion types.
inline constexpr unsigned kKeyframed = 1, kDynamic = 2;
// A counted actor reference returned by the game; nontrivial, so it comes back through memory.
struct ActorReference {
    std::uintptr_t actor{};
    bool owns{};
    ~ActorReference() {
        if (owns && valid(actor)) {
            std::atomic_ref<int> count(*reinterpret_cast<int *>(actor + 432));
            if (count.load(std::memory_order_relaxed) >= 1)
                count.fetch_sub(1, std::memory_order_relaxed);
        }
    }
};
static_assert(sizeof(ActorReference) == 16 && offsetof(ActorReference, owns) == 8);
inline ActorReference actorReference(std::uintptr_t link) {
    return call<ActorReference (*)(std::uintptr_t)>(offsets::kActorLinkBaseGetReference)(link);
}
struct Bodies {
    std::uintptr_t address[32]{};
    unsigned mode[32]{};
    unsigned count{};
    bool overflow{};
};
// A sword (0), two-handed sword (1) or spear (2) behind a hit, and who swung it.
struct Weapon {
    int family{-1};
    std::uintptr_t user{};
};

bool startsWith(const char *value, const char *prefix);
const char *actorName(std::uintptr_t actor);
inline bool isEnemy(std::uintptr_t actor) { return startsWith(actorName(actor), "Enemy_"); }
inline bool isLivingTarget(std::uintptr_t actor) {
    const auto* name = actorName(actor);
    return name && livingTarget(name);
}
Vec3 position(std::uintptr_t actor);
std::uintptr_t controllerSet(std::uintptr_t actor);
std::uintptr_t physicsStructure(std::uintptr_t set);
std::uintptr_t mainBody(std::uintptr_t actor);
float mass(std::uintptr_t body);
Bodies bodies(std::uintptr_t set);
bool worldBounds(std::uintptr_t actor, Bounds &out);
Vec3 centerOfMass(std::uintptr_t body);
std::uintptr_t animationController(std::uintptr_t actor);
Weapon weaponOf(std::uintptr_t attacker);
// Every named actor in the scene, idle ones included.
std::size_t snapshotRoster(totk::core::SceneToken scene, Handle *out, std::size_t capacity);
// Objects Stasis may hold: loose dynamic objects and ordinary enemies.
bool eligible(Handle handle);
// False, with a log, when another power already holds the object.
bool holdAvailable(std::uintptr_t structure);
bool readPose(std::uintptr_t actor, float (&matrix)[12]);
target::Group collisionGroup(std::uintptr_t actor);
void resolveHitBody(std::uintptr_t body, target::Hit &hit);

void freeze(std::uintptr_t set);
void unfreeze(std::uintptr_t set);
void setMotionType(std::uintptr_t body, unsigned mode);
void stopBody(std::uintptr_t body);
void applyImpulse(std::uintptr_t body, Vec3 impulse);
void forcePose(std::uintptr_t actor, const float *pose);
void commitPose(std::uintptr_t actor, const float *pose, float frameScale);
} // namespace stasis::engine
