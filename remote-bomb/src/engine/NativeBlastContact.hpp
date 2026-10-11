// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombPressure.hpp"
#include <cstddef>
#include <cstring>

namespace remote_bomb::engine::explosion {
inline constexpr std::size_t kPressureOffset = 248, kPressurePointOffset = 260, kShockResultSize = 26;
struct ContactForces { blast::Vector pressure, point; };
static_assert(sizeof(ContactForces) == 24 && sizeof(blast::Vector) == 12);
inline ContactForces captureForces(const void *contact) {
    ContactForces forces;
    std::memcpy(&forces, static_cast<const unsigned char *>(contact) + kPressureOffset, sizeof(forces));
    return forces;
}
inline void restoreForces(void *contact, ContactForces forces) {
    std::memcpy(static_cast<unsigned char *>(contact) + kPressureOffset, &forces, sizeof(forces));
}
inline void copyShockResult(void *destination, const void *result) {
    if (destination)
        std::memcpy(destination, result, kShockResultSize);
}
inline bool shockDamageFlag(const void *result) {
    return static_cast<const unsigned char *>(result)[24] != 0;
}
inline void setShockDamageFlag(void *result, bool active) {
    static_cast<unsigned char *>(result)[24] = active ? 1 : 0;
}
}
