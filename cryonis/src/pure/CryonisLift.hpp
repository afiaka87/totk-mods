// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisGrowth.hpp"
namespace cryonis::lift {
struct Bounds {
    Vec3 min, max;
};
inline bool valid(Bounds b) {
    const Vec3 size = sub(b.max, b.min);
    return finite(b.min) && finite(b.max) && size.x > 0 && size.y > 0 && size.z > 0 &&
           length(size) < 100;
}
inline bool footprint(Vec3 base, Bounds b, float yaw = 0) {
    // Four separating axes for the world AABB and rotated square footprint.
    const float c = std::fabs(std::cos(yaw)), s = std::fabs(std::sin(yaw)), half = kWidth * .5f;
    const float x = (b.min.x + b.max.x) * .5f - base.x, z = (b.min.z + b.max.z) * .5f - base.z;
    const float ex = (b.max.x - b.min.x) * .5f, ez = (b.max.z - b.min.z) * .5f;
    const auto local = rotateYaw({x, 0, z}, -yaw);
    return std::fabs(x) < ex + half * (c + s) && std::fabs(z) < ez + half * (c + s) &&
           std::fabs(local.x) < half + c * ex + s * ez &&
           std::fabs(local.z) < half + s * ex + c * ez;
}
inline bool capture(Vec3 base, Bounds b, float yaw = 0) {
    return valid(b) && footprint(base, b, yaw) && b.max.y >= base.y - kCaptureDepth &&
           b.min.y <= base.y + kRiderClearance;
}
// Relative bounds avoid reusing a delayed physics AABB after a new pose request.
inline Bounds translated(Bounds relative, Vec3 position) {
    return {add(relative.min, position), add(relative.max, position)};
}
inline float rise(Vec3 base, float frame, Bounds b, float yaw = 0) {
    if (!valid(b) || !footprint(base, b, yaw))
        return 0;
    return std::max(0.0f, base.y + emergenceHeight(frame) + kRiderClearance - b.min.y);
}
}
