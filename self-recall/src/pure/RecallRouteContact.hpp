#pragma once
#include "RecallRouteProbePlan.hpp"

namespace self_recall::pure {
inline bool tolerableRouteContact(const RouteProbeSegment& segment,
        totk::core::WorldPosition hit, totk::core::WorldPosition normal) {
    const auto step = distance3(segment.from, segment.to);
    const auto start = distance3(segment.from, hit);
    const auto end = distance3(segment.to, hit);
    const auto length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (!std::isfinite(step) || !std::isfinite(start) || !std::isfinite(end) ||
        !std::isfinite(length) || length < 0.9f || length > 1.1f) return false;
    const float inward = -((segment.to.x - segment.from.x) * normal.x +
        (segment.to.y - segment.from.y) * normal.y + (segment.to.z - segment.from.z) * normal.z) / length;
    if (start <= 0.005f && (inward <= 0.001f || (step <= 0.1f && inward <= 0.06f))) return true;
    return normal.y / length >= 0.65f && step <= 0.1f && end <= 0.02f;
}
}
