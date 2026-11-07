// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeActor.hpp"
#include "FollowGear.hpp"
#include "FollowTurning.hpp"
#include <optional>

namespace botw_abilities::wolf {

struct MotionGears {
    std::int32_t mode{}, gear{}, pendingMode{}, pendingGear{};
    float remaining{};
};
static_assert(sizeof(MotionGears) == 20 && offsetof(MotionGears, remaining) == 16);

[[nodiscard]] constexpr FollowGearTransition closeFollowTransition(const MotionGears& state) {
    if (state.pendingMode == 6 || state.pendingGear == 5) return {state.gear, 0, 0, true};
    return {state.gear, state.pendingGear, state.remaining, state.mode == state.pendingMode};
}

struct MotionState {
    totk::core::WorldPosition direction;
    MotionGears gears;
    float playRate{};
};

bool initializeMotionApi(std::uintptr_t base);
[[nodiscard]] std::optional<CircleTable> nativeCircleTable();

// Read only from the owning actor's AI callback, using its scoped reference.
[[nodiscard]] std::optional<MotionState> readMotion(const ActorReference& actor);
[[nodiscard]] std::optional<float> readFollowLinearSpeed(const ActorReference& actor, int gear);
[[nodiscard]] std::optional<float> readFollowAngularSpeed(const ActorReference& actor, int gear);

}
