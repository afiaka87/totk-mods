// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "GroundMotion.hpp"
#include "WorldMath.hpp"
#include <algorithm>
#include <cmath>

namespace botw_abilities::wolf {
namespace {
using Vector = totk::core::WorldPosition;

Vector cross(Vector a, Vector b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

std::optional<Vector> normalized(Vector v) {
    const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (!std::isfinite(length) || length <= 0) return std::nullopt;
    const float inverse = 1.0f / length;
    return Vector{v.x * inverse, v.y * inverse, v.z * inverse};
}
}

std::optional<totk::core::WorldPosition> animationGroundVelocity(const GroundMotion& m) {
    if (!finite(m.facing) || !finite(m.up) || !finite(m.surface) ||
        !std::isfinite(m.speed) || !std::isfinite(m.forward) || !std::isfinite(m.side))
        return std::nullopt;
    const float length = std::sqrt(m.forward * m.forward + m.side * m.side);
    if (!std::isfinite(length)) return std::nullopt;
    if (length == 0 || m.speed == 0) return Vector{};
    const auto left = normalized(cross(m.facing, m.up));
    if (!left) return std::nullopt;
    const auto forward = normalized(cross(m.surface, *left));
    if (!forward) return std::nullopt;
    const auto right = normalized(cross(m.surface, *forward));
    if (!right) return std::nullopt;
    const float speed = std::clamp(m.speed, -kMaxGroundSpeed, kMaxGroundSpeed) / length;
    const float f = speed * m.forward, s = speed * m.side;
    const Vector velocity{forward->x * f + right->x * s,
                          forward->y * f + right->y * s,
                          forward->z * f + right->z * s};
    return finite(velocity) ? std::optional{velocity} : std::nullopt;
}

}
