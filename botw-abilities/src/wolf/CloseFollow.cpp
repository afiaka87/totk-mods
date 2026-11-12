// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "CloseFollow.hpp"
#include "NativeAction.hpp"
#include "NativeInput.hpp"
#include "NativeMotion.hpp"
#include "NativeNavigation.hpp"
#include "WorldMath.hpp"
#include <lib.hpp>
#include <algorithm>
#include <cmath>

namespace botw_abilities::wolf {
namespace {
constexpr PathActionParameters kAction{2, 2, false, false};
}

bool CloseFollow::owns(const ActorReference& actor) const {
    return owner_ && owner_ == actor.address() && generation_ == actor.generation();
}

bool CloseFollow::begin(const ActorReference& actor) {
    stop(actor);
    if (!beginLocalFollowing(actor)) return false;
    owner_ = actor.address();
    generation_ = actor.generation();
    enterAction_ = true;
    return true;
}

FollowStatus CloseFollow::update(const ActorReference& actor, const ActorReference& leader,
    const totk::core::WorldPosition& target, float keptDistance, bool predictTravel,
    int gearLimit, int gearFloor) {
    const auto fail = [&] {
        stop(actor);
        return FollowStatus::Failed;
    };
    if (!owns(actor) || !finite(target) || !std::isfinite(keptDistance)) return fail();
    const auto ready = localFollowingReady(actor);
    if (!ready || !updateFollowLeader(actor, leader, leaderSlot_)) return fail();
    if (!*ready) {
        const auto applied = applyPathAction(actor, {0, 40, 0}, kAction, !waiting_);
        if (applied == PathActionResult::Refused) return fail();
        if (applied == PathActionResult::Applied) waiting_ = true;
        enterAction_ = true;
        return FollowStatus::Running;
    }
    waiting_ = false;
    const auto motion = readMotion(actor);
    const auto kinematics = readKinematics(actor);
    const auto table = nativeCircleTable();
    if (!motion || !kinematics || !table) return fail();
    const auto position = kinematics->transform.position;
    const float x = target.x - position.x, z = target.z - position.z;
    const float distance = std::sqrt((x * x + 0.0f) + z * z);
    const int gear = std::clamp(closeFollowGear(distance, closeFollowTransition(motion->gears)),
                                std::min(gearFloor, gearLimit), gearLimit);
    gear_ = std::uint8_t(gear);
    if (gear == 0) {
        if (!clearFollowSteering(actor)) return fail();
    } else {
        const auto speed = readFollowLinearSpeed(actor, gear);
        const auto angular = readFollowAngularSpeed(actor, gear);
        if (!speed || !angular) return fail();
        const auto steering = steerCloseFollow(
            {position, motion->direction, target, *speed, *angular, keptDistance, insideTurn_, predictTravel}, *table);
        if (!setFollowSteering(actor, steering.velocity, steering.target, *speed)) return fail();
        insideTurn_ = steering.insideTurn;
    }
    const auto applied = applyPathAction(actor, {0, gear ? 4 : 40, gear}, kAction, enterAction_);
    if (applied == PathActionResult::Refused) return fail();
    if (applied == PathActionResult::Applied) enterAction_ = false;
    return FollowStatus::Running;
}

void CloseFollow::stop(const ActorReference& actor) {
    if (owns(actor)) {
        clearFollowLeader(actor, leaderSlot_);
        static_cast<void>(clearFollowSteering(actor));
    } else if (owner_) {
        Logging.Log("[wolf-link] CLOSE_FOLLOW_OWNER_GONE generation=%u", generation_);
    }
    owner_ = 0;
    leaderSlot_ = -1;
    insideTurn_ = enterAction_ = waiting_ = false;
    gear_ = 0;
}

}
