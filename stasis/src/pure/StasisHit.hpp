// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StasisCore.hpp"
#include <cstdint>
namespace stasis::hit_adapter {
// BotW 1.5.0 0x734140: projectiles redirect the force along their velocity, without a lift.
inline Vec3 projectileDirection(Vec3 force, Vec3 velocity) {
    const float speed = length(velocity);
    return finite(velocity) && speed > 0.0f ? scale(velocity, length(force) / speed) : force;
}
// BotW's player strike direction: keep the vertical force and horizontal magnitude, and point
// the horizontal part from the player to the object's centre of mass.
inline Vec3 playerDirection(Vec3 force, Vec3 player, Vec3 centre) {
    const float x = centre.x - player.x, z = centre.z - player.z;
    const float distance = std::sqrt((x * x + 0.0f) + z * z);
    if (!(distance > 0.0f))
        return force;
    const float inverse = 1.0f / distance;
    const float dx = x * inverse, dz = z * inverse;
    const float magnitude = std::sqrt(force.x * force.x + force.z * force.z);
    return {magnitude * dx, force.y, magnitude * dz};
}
// TotK 1.2.1 hit records: a component's actor link and the accepted hit's fields.
inline std::uintptr_t actorLink(std::uintptr_t component) { return component ? component + 24 : 0; }
struct Payload {
    Vec3 normal{}, point{};
    std::uint32_t attack[4]{};
};
template <class Reader> Payload decode(std::uintptr_t record, Reader read) {
    Payload p;
    p.normal = read.template operator()<Vec3>(record + 12);
    p.point = read.template operator()<Vec3>(record + 64);
    p.attack[0] = read.template operator()<std::uint32_t>(record + 56);
    p.attack[1] = read.template operator()<std::uint32_t>(record + 60);
    p.attack[2] = read.template operator()<std::uint32_t>(record + 152);
    p.attack[3] = read.template operator()<std::uint32_t>(record + 156);
    return p;
}
} // namespace stasis::hit_adapter
