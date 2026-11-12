// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "FollowControl.hpp"
#include "WorldMath.hpp"
#include "totk/core/Units.hpp"
#include <cmath>

namespace botw_abilities::wolf {

enum class CliffAction : std::uint8_t { Turn, Move };
struct CliffChildState { CliffAction action; FollowStatus status; };
struct CliffSearchResult { totk::core::WorldPosition target; bool failed{}; };
struct CliffRecoveryState {
    totk::core::WorldPosition target;
    std::uint8_t attempts{};
    bool found{};
    FollowStatus status = FollowStatus::Running;
};
static_assert(sizeof(CliffRecoveryState) == 16);

[[nodiscard]] inline bool usableCliffTarget(totk::core::WorldPosition target, totk::core::WorldPosition position) {
    const float x = target.x - position.x, z = target.z - position.z;
    return !std::isnan(target.x) && !std::isnan(target.y) && !std::isnan(target.z) &&
           !(x <= .01f && x >= -.01f && z >= -.01f && z <= .01f);
}

[[nodiscard]] inline totk::core::WorldPosition forwardCliffTarget(const totk::core::WorldTransform& transform) {
    const auto& p = transform.position;
    const auto forward = bodyForward(transform);
    return {p.x + forward.x, p.y + forward.y, p.z + forward.z};
}

// Search owns terrain acceptance; entering a child does not advance its animation or movement.
template<class Search, class Enter>
[[nodiscard]] CliffRecoveryState beginCliffRecovery(
    const totk::core::WorldTransform& transform, Search&& search, Enter&& enter) {
    const auto forward = forwardCliffTarget(transform);
    const auto result = search(forward);
    CliffRecoveryState state{result.target};
    if (result.failed) state.status = FollowStatus::Failed;
    state.found = usableCliffTarget(state.target, transform.position);
    if (!state.found) {
        state.target = forward;
        state.attempts = 1;
    }
    enter(CliffAction::Turn, state.target);
    return state;
}

// Observe the current child after any restart; child ticking remains owned by the scheduler.
template<class Search, class Enter, class Observe>
[[nodiscard]] CliffRecoveryState advanceCliffRecovery(
    CliffRecoveryState state, const totk::core::WorldTransform& transform, bool navigationAvailable,
    Search&& search, Enter&& enter, Observe&& observe) {
    if (state.status != FollowStatus::Running || !navigationAvailable) return state;
    if (!state.found) {
        const auto result = search(state.target);
        state.target = result.target;
        if (result.failed) state.status = FollowStatus::Failed;
        if (usableCliffTarget(state.target, transform.position)) {
            enter(CliffAction::Turn, state.target);
            state.found = true;
        } else if (++state.attempts >= 5) {
            state.status = FollowStatus::Failed;
            return state;
        }
    }
    const auto child = observe();
    if (!child || child->status == FollowStatus::Failed) {
        state.status = FollowStatus::Failed;
    } else if (child->status == FollowStatus::Succeeded) {
        if (child->action == CliffAction::Move) {
            state.status = FollowStatus::Succeeded;
        } else if (usableCliffTarget(state.target, transform.position)) {
            enter(CliffAction::Move, state.target);
        } else {
            state.target = forwardCliffTarget(transform);
            enter(CliffAction::Turn, state.target);
            state.status = FollowStatus::Failed;
        }
    }
    return state;
}

}
