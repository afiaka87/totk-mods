// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeActor.hpp"
#include <optional>

namespace botw_abilities::wolf {

bool initializeNavigationApi(std::uintptr_t base);
struct NavigationSurface {
    totk::core::WorldPosition position;
    std::uint32_t material{};
};

// Samples navigation geometry without assigning the native animal's traversal rules.
[[nodiscard]] std::optional<NavigationSurface> sampleNavigationSurface(
    const totk::core::WorldPosition& start, const totk::core::WorldPosition& end);

// The caller checks walkability after finding the nearest surface, as in BotW's initial cliff probe.
[[nodiscard]] std::optional<NavigationSurface> nearestNavigationSurface(
    const totk::core::WorldPosition& position, float searchDistance);

// The candidate must first pass the original vertical navigation-surface probe.
[[nodiscard]] std::optional<totk::core::WorldPosition> reachableFollowPoint(
    const ActorReference& actor, const totk::core::WorldPosition& candidate);

enum class Reachability : std::uint8_t { Unavailable, Blocked, Reachable };

// Uses the movement controller's direction and the original gear's scaled speed.
[[nodiscard]] Reachability probeFollowDirection(
    const ActorReference& actor, const totk::core::WorldPosition& direction, float distance);

// Checks a 0.75-meter corridor in the actor's horizontal facing direction: Blocked is the original's
// "no ground ahead" at speed.
[[nodiscard]] Reachability probeFollowStep(
    const ActorReference& actor, const totk::core::RotationBasis& rotation);

// The walkable line from the actor to a point.
[[nodiscard]] Reachability probeFollowPoint(
    const ActorReference& actor, const totk::core::WorldPosition& point);

[[nodiscard]] std::optional<totk::core::WorldPosition> readPathGoal(const ActorReference& actor);

// The caller supplies the original recovery reference; it is not necessarily the actor position.
[[nodiscard]] std::optional<totk::core::WorldPosition> findCliffEscape(const ActorReference& actor,
    const totk::core::WorldPosition& forward, const totk::core::WorldPosition& reference);

// Cancel the previous full path before entering local following.
[[nodiscard]] bool beginLocalFollowing(const ActorReference& actor);
[[nodiscard]] std::optional<bool> localFollowingReady(const ActorReference& actor);

// Keep the leader, including its mount, out of this actor's local avoidance list.
[[nodiscard]] bool updateFollowLeader(const ActorReference& actor, const ActorReference& leader, int& slot);
void clearFollowLeader(const ActorReference& actor, int& slot);

// AI-owner only: local avoidance input, without starting or replacing a full path request.
[[nodiscard]] bool setFollowSteering(const ActorReference& actor,
    const totk::core::WorldPosition& velocity, const totk::core::WorldPosition& target, float speed);

// Clear before leaving local following; resolve a fresh reference instead of retaining the controller.
[[nodiscard]] bool clearFollowSteering(const ActorReference& actor);

}
