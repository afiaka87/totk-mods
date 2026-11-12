// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/core/Units.hpp"
#include <cmath>
#include <cstddef>
#include <optional>

namespace botw_abilities::wolf {

enum class FollowSide : unsigned char { Unset, Positive, Negative };
// The faster the player moves, the closer the station point rides, so a sprinting player has the
// wolf right beside him instead of eight units ahead (HanSoloBlaster, wolf-compact-12).
struct FollowDistances {
    float lateral = 2.11f;
    float stationary = 3.5f;
    float running = 3.0f;
    float sprinting = 3.0f;
};
struct FollowOffset { float lateral{}, forward{}; };

// BotW scales each physics-velocity component before calculating horizontal speed.
[[nodiscard]] inline float originalFollowSpeed(totk::core::LinearVelocity velocity) {
    constexpr float scale = 1.0f / 30.0f;
    const float x = velocity.x * scale, z = velocity.z * scale;
    return std::sqrt(x * x + z * z);
}

[[nodiscard]] constexpr FollowSide followSide(FollowSide previous, float localX, float leaderWidth) {
    const float boundary = leaderWidth * 0.5f + 0.1f;
    if (localX > boundary) return FollowSide::Positive;
    if (localX < boundary) return FollowSide::Negative;
    return previous;
}

inline constexpr float kFollowHeadingLimit = 0.06981316953897476f;
[[nodiscard]] constexpr bool captureFollowFrame(float horizontalTravelSquared, float headingAngle) {
    return horizontalTravelSquared >= 1.0f && headingAngle < kFollowHeadingLimit;
}

// Speed is in the original actor's velocity units; native TotK conversion belongs in the caller.
[[nodiscard]] inline FollowOffset followOffset(const FollowDistances& distances, float leaderSpeed,
                                               float leaderWidth, float localX, FollowSide side) {
    constexpr float runSpeed = 0.185f;
    constexpr float sprintRange = 0.27f - runSpeed;
    const float forward = leaderSpeed <= runSpeed
        ? distances.stationary + (leaderSpeed / runSpeed) * (distances.running - distances.stationary)
        : distances.running + ((leaderSpeed - runSpeed) / sprintRange) * (distances.sprinting - distances.running);
    float lateral = distances.lateral + leaderWidth * 0.5f;
    const float distanceFromCenter = std::fabs(localX);
    if (distanceFromCenter < lateral * 0.5f) lateral += std::fabs(distanceFromCenter - lateral);
    if (side == FollowSide::Negative) lateral = -lateral;
    return {lateral, forward};
}

inline constexpr std::size_t kFollowCandidateCount = 9;

// Original fallback search repeats candidates; preserve query order and first-success behavior.
[[nodiscard]] constexpr std::optional<FollowOffset> followCandidate(FollowOffset offset, std::size_t index) {
    if (index >= kFollowCandidateCount) return std::nullopt;
    const float lateralStep = offset.lateral / 1.5f;
    const float forwardStep = offset.forward / 1.5f;
    switch (index) {
    case 2: case 5: return FollowOffset{offset.lateral - lateralStep, offset.forward - forwardStep};
    case 3: case 6: return FollowOffset{offset.lateral - (lateralStep + lateralStep),
                                      offset.forward - (forwardStep + forwardStep)};
    case 7: return FollowOffset{0, offset.forward};
    case 8: return FollowOffset{0, offset.forward + forwardStep};
    default: return offset;
    }
}

[[nodiscard]] inline totk::core::WorldPosition followWorldPoint(
    const totk::core::WorldTransform& frame, FollowOffset point) {
    const auto& r = frame.rotation.values;
    return {((point.lateral * r[0] + r[1] * 0.0f) + point.forward * r[2]) + frame.position.x,
            ((point.lateral * r[3] + r[4] * 0.0f) + point.forward * r[5]) + frame.position.y,
            ((point.lateral * r[6] + r[7] * 0.0f) + point.forward * r[8]) + frame.position.z};
}

// Probe performs the ground/nav queries and returns their adjusted point, or no usable destination.
template<class Probe>
[[nodiscard]] totk::core::WorldPosition selectFollowTarget(
    const totk::core::WorldTransform& frame, FollowOffset offset, Probe&& probe) {
    for (std::size_t i = 0; i < kFollowCandidateCount; ++i) {
        if (const auto result = probe(followWorldPoint(frame, *followCandidate(offset, i)))) return *result;
    }
    return frame.position;
}

// Surface must reject forbidden materials; an unreachable lower hit does not retry upward.
template<class Surface, class Reachable>
[[nodiscard]] std::optional<totk::core::WorldPosition> probeFollowTarget(
    totk::core::WorldPosition candidate, Surface&& surface, Reachable&& reachable) {
    auto hit = surface(candidate, totk::core::WorldPosition{candidate.x, candidate.y - 5.0f, candidate.z});
    if (!hit)
        hit = surface(candidate, totk::core::WorldPosition{candidate.x, candidate.y + 5.0f, candidate.z});
    return hit ? reachable(*hit) : std::nullopt;
}

}
