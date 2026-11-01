#pragma once

#include "RemoteTarget.hpp"

namespace zonai_ascend::speed {

inline constexpr std::uint32_t kApproach = 0x80, kTravel = 0x100;
inline constexpr std::uint32_t kPreview = 0x400, kCancel = 0x80000;

constexpr float multiplier(remote::Vec3 start, remote::Vec3 end, std::uint32_t flags) {
    if (!(flags & (kApproach | kTravel)) || (flags & (kPreview | kCancel)) ||
        !remote::finite(start) || !remote::finite(end)) return 1.0f;
    const float rise = end.y-start.y;
    if (rise > pure::kReleaseReach) return 1.0f;
    if (rise > 2000.0f) return 7.5f;
    if (rise > 1500.0f) return 7.0f;
    if (rise > 1000.0f) return 6.0f;
    if (rise > 500.0f) return 3.0f;
    if (rise > 300.0f) return 2.0f;
    if (rise > 150.0f) return 1.5f;
    return 1.0f;
}

constexpr float scalePositive(float value, float factor) {
    const float scaled = value*factor;
    return value > 0 && remote::finite(value) && remote::finite(scaled) ? scaled : value;
}

}
