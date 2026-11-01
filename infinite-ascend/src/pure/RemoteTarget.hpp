#pragma once

#include <bit>
#include <cstdint>

#include "ReachConfig.hpp"

namespace zonai_ascend::remote {

struct Vec3 { float x, y, z; };
inline constexpr float kNaN = std::bit_cast<float>(0x7FC00000u);

// VolumeStats::Context::raycastForCeilingClipper's 1.2.1 output, not actor memory.
struct Layers {
    std::uint32_t flags = 0;
    float x = kNaN, z = kNaN;
    float ceiling = kNaN, floor = kNaN, nextCeiling = kNaN;
};
static_assert(sizeof(Layers) == 24);

struct Candidate { Vec3 entry{}, exit{}; };
enum class Verdict : unsigned {
    Accepted, Unavailable, Incomplete, ColumnMismatch,
    Ordering, Reach, Headroom, BlockedExit,
};

constexpr bool finite(float value) {
    return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
}
constexpr bool finite(Vec3 value) {
    return finite(value.x) && finite(value.y) && finite(value.z);
}
constexpr float abs(float value) { return value < 0.0f ? -value : value; }
constexpr bool near(Vec3 a, Vec3 b, float tolerance) {
    return finite(a) && finite(b) && abs(a.x-b.x) <= tolerance &&
           abs(a.y-b.y) <= tolerance && abs(a.z-b.z) <= tolerance;
}
constexpr bool mayReplaceCeilingFailure(std::uint16_t reasons) {
    return (reasons & ~0x04F8u) == 0 && (reasons & 0x00B0u) != 0;
}
constexpr bool mayReplaceFloorFailure(std::uint16_t reasons) {
    return (reasons & ~0x2080u) == 0;
}

constexpr Verdict makeCandidate(bool querySucceeded, const Layers& layers,
                                Vec3 origin, Candidate& candidate) {
    if (!querySucceeded || !(layers.flags & 1)) return Verdict::Unavailable;
    if (!finite(origin) || !finite(layers.x) || !finite(layers.z) ||
        !finite(layers.ceiling) || !finite(layers.floor)) return Verdict::Incomplete;
    // Native voxel centers are floor(world coordinate) + 0.5, including negative coordinates.
    if (abs(layers.x-origin.x) > 0.501f || abs(layers.z-origin.z) > 0.501f)
        return Verdict::ColumnMismatch;
    if (layers.ceiling <= origin.y+0.5f || layers.floor <= layers.ceiling)
        return Verdict::Ordering;
    if (layers.floor-origin.y < 1.8f || layers.floor-origin.y > pure::kReleaseReach)
        return Verdict::Reach;
    // A NaN next ceiling is the native successful query's 'no further ceiling' sentinel.
    if (layers.nextCeiling == layers.nextCeiling &&
        (!finite(layers.nextCeiling) || layers.nextCeiling-layers.floor < 2.0f))
        return Verdict::Headroom;
    candidate = {{layers.x, layers.ceiling, layers.z},
                 {layers.x, layers.floor+0.05f, layers.z}};
    return Verdict::Accepted;
}

}
