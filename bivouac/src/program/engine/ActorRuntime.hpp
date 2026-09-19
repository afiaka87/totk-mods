// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::engine {

inline constexpr std::uint64_t kOwnerUnresolved = ~std::uint64_t{0};

struct SpawnTransform {
    float position[3] = {};
    float rotation[9] = {};
    float scale[3] = {1.0f, 1.0f, 1.0f};
};

struct SpawnResult {
    bool accepted = false;
    std::uint32_t nativeResult = 0;
    void* preactor = nullptr;
};

class ActorRuntime {
public:
    void setMainBase(std::uintptr_t mainBase) { mMainBase = mainBase; }

    SpawnResult requestSpawn(const char* actorName,
                             const SpawnTransform& transform) const;
    const char* actorName(const void* actor) const;
    void* actorFromPreactor(const void* preactor) const;
    void* mainRigidBody(void* actor) const;

    void forceSetMatrix(void* actor, const float rows[12]) const;
    void requestMotionType(void* body, std::uint32_t type) const;
    void zeroLinearVelocity(void* body) const;
    void requestDelete(void* actor) const;

    std::uint64_t hitOwner(std::uint32_t bodyId) const;

private:
    std::uintptr_t mMainBase = 0;
};

} // namespace bivouac::engine
