// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "PathFollow.hpp"
#include "NativeAction.hpp"
#include "NativeInput.hpp"
#include "NativeMotion.hpp"
#include "NativeNavigation.hpp"
#include <lib.hpp>
#include <cmath>

namespace botw_abilities::wolf {
namespace {
constexpr PathActionParameters kAction{2, 1, true, false};

MoveParameters parameters(int gear, PathGoal goal) {
    return {.actionVerb = 4, .minGear = gear, .maxGear = gear, .gearType = kAction.gearType,
            .minimumSlowGear = kAction.minimumGear, .goalTolerance = goal.tolerance, .autoSlow = goal.autoSlow};
}

struct CorridorProbe {
    const ActorReference& actor;
    const totk::core::WorldPosition& direction;
    bool available = true;

    bool operator()(int gear) {
        if (!available) return false;
        const auto speed = readFollowLinearSpeed(actor, gear);
        const auto result = speed ? probeFollowDirection(actor, direction, *speed) : Reachability::Unavailable;
        available = result != Reachability::Unavailable;
        return result == Reachability::Reachable;
    }
};

// The gear to ask for at this distance from the goal; empty when a needed reading is missing.
std::optional<int> gearTowardsGoal(const ActorReference& actor, int gear) {
    const auto goal = readPathGoal(actor);
    if (!goal) return gear;
    const auto kinematics = readKinematics(actor);
    const auto linear = readFollowLinearSpeed(actor, gear);
    const auto angular = readFollowAngularSpeed(actor, gear);
    if (!kinematics || !linear || !angular) return std::nullopt;
    return pathFollowGear(gear, *linear, *angular, kinematics->transform.position, *goal);
}
}

bool PathFollow::startGear(const ActorReference& actor) {
    move_.stop(actor);
    enterAction_ = true;
    return move_.start(actor, parameters(gear_.gear, goal_), destination_);
}

bool PathFollow::run(const ActorReference& actor, const totk::core::WorldPosition& destination) {
    destination_ = destination;
    if (!startGear(actor)) return false;
    status_ = FollowStatus::Running;
    return true;
}

bool PathFollow::begin(const ActorReference& actor, FollowRange range,
                       const totk::core::WorldPosition& destination, PathGoal goal) {
    stop(actor);
    const auto motion = readMotion(actor);
    if (!motion) return false;
    CorridorProbe probe{actor, motion->direction};
    gear_ = beginFollowGear(range, probe);
    if (!probe.available) return false;
    range_ = range;
    fixed_ = false;
    goal_ = goal;
    return run(actor, destination);
}

bool PathFollow::beginFixed(const ActorReference& actor, int gear, const totk::core::WorldPosition& destination,
                            PathGoal goal) {
    stop(actor);
    gear_ = {};
    gear_.gear = gear;
    fixed_ = true;
    goal_ = goal;
    return run(actor, destination);
}

bool PathFollow::retarget(const ActorReference& actor, const totk::core::WorldPosition& destination) {
    if (status_ != FollowStatus::Running || !move_.retarget(actor, destination)) {
        Logging.Log("[wolf-link] FOLLOW_RETARGET_REFUSED state=%u", unsigned(status_));
        stop(actor);
        return false;
    }
    destination_ = destination;
    return true;
}

PathFollowResult PathFollow::update(const ActorReference& actor, float frameDelta) {
    if (!move_.owns(actor) || !std::isfinite(frameDelta) || frameDelta < 0) {
        Logging.Log("[wolf-link] FOLLOW_UPDATE_REFUSED owner=%u delta=%f",
            unsigned(move_.owns(actor)), double(frameDelta));
        stop(actor);
    }
    if (status_ != FollowStatus::Running) return {status_, std::nullopt};
    const auto fail = [&](std::optional<bool> limitSpeed) {
        stop(actor);
        return PathFollowResult{status_, limitSpeed};
    };
    const auto motion = readMotion(actor);
    if (!motion) return fail(std::nullopt);
    CorridorProbe probe{actor, motion->direction};
    const auto change = fixed_ ? FollowGearChange{gear_, std::nullopt, false}
                               : advanceFollowGear(gear_, range_, frameDelta, probe);
    if (!probe.available) return fail(std::nullopt);
    gear_ = change.state;
    if (change.changed && !startGear(actor)) return fail(change.limitSpeed);
    const auto desired = goal_.autoSlow ? gearTowardsGoal(actor, gear_.gear) : std::optional<int>{gear_.gear};
    if (!desired) return fail(change.limitSpeed);
    const auto output = move_.update(actor, parameters(gear_.gear, goal_), *desired);
    if (!applyMoveOutput(actor, output, kAction, enterAction_)) return fail(change.limitSpeed);
    if (output->nativeResult == 2) status_ = FollowStatus::Succeeded;
    return {status_, change.limitSpeed};
}

void PathFollow::stop(const ActorReference& actor) {
    move_.stop(actor);
    status_ = FollowStatus::Failed;
    enterAction_ = false;
}

}
