// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <optional>
#include <span>

namespace botw_abilities::wolf {

struct CircleSample { float sine, sineDelta, cosine, cosineDelta; };
static_assert(sizeof(CircleSample) == 16);
using CircleTable = std::span<const CircleSample, 256>;

struct KeptDistanceTurn {
    totk::core::WorldPosition direction;
    std::optional<totk::core::WorldPosition> travel;
};
struct ApproachTurn {
    int category{};
    float alignment{};
    std::optional<totk::core::WorldPosition> travel;
};

struct FollowSteeringInput {
    totk::core::WorldPosition position, facing, target;
    float speed{}, angularSpeed{}, keptDistance{};
    bool insideTurn{}, predictTravel{};
};
struct FollowSteering {
    totk::core::WorldPosition velocity, target;
    bool insideTurn{};
};

// Both speeds include the authored base rate, without the separate live animation rate.
[[nodiscard]] FollowSteering steerCloseFollow(const FollowSteeringInput& input, CircleTable table);

[[nodiscard]] std::optional<KeptDistanceTurn> keptDistanceTurn(
    totk::core::WorldPosition facing, totk::core::WorldPosition towardTarget,
    float distance, float keptDistance, float minimumRadius, float lookAhead,
    bool predictTravel, CircleTable table);

// Wolf Link's WaitDistanceToLeader is zero, so the generic horse's travel clamp is inactive.
[[nodiscard]] ApproachTurn approachTurn(
    totk::core::WorldPosition facing, totk::core::WorldPosition towardTarget,
    float distance, float minimumRadius, float exitRadius, float lookAhead,
    bool predictTravel, CircleTable table);

}
