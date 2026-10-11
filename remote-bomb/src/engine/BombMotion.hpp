// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombReference.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace remote_bomb::physics {
// The motion properties the game passes to a physics body (TotK 1.2.1 layout).
struct alignas(16) MotionProperties {
    std::array<std::uint32_t, 4> prefix;
    float maxLinearVelocity, maxAngularVelocity;
    float linearDamping, angularDamping;
    std::array<std::uint32_t, 20> remainder;
};
static_assert(sizeof(MotionProperties) == 112);
static_assert(offsetof(MotionProperties, maxLinearVelocity) == 16);
static_assert(offsetof(MotionProperties, maxAngularVelocity) == 20);
static_assert(offsetof(MotionProperties, linearDamping) == 24);
static_assert(offsetof(MotionProperties, angularDamping) == 28);
inline MotionProperties withBombMotion(MotionProperties original, Shape shape) {
    original.maxLinearVelocity = kMaxLinearVelocity;
    original.maxAngularVelocity = kMaxAngularVelocity;
    original.linearDamping = bodyProfile(shape).damping;
    original.angularDamping = bodyProfile(shape).damping;
    return original;
}
inline constexpr unsigned motionPreset(unsigned requested, std::uint64_t flags, unsigned count) {
    const unsigned index = requested + ((flags & 0x10000000000ULL) ? 41U : 0U);
    return index < count ? index : 0;
}
}
