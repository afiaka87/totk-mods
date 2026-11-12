// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "CliffTurn.hpp"
#include "NativeInput.hpp"
#include "NativeMotion.hpp"
#include "TurnFacing.hpp"
#include "WorldMath.hpp"
#include <lib.hpp>
#include <cmath>
#include <numbers>

namespace botw_abilities::wolf {
namespace {
constexpr const char* kCommand = "Turn_Escape_NoGrowl";
}

bool CliffTurn::aim(const ActorReference& actor) {
    const auto motion = readMotion(actor);
    const auto kinematics = readKinematics(actor);
    if (!motion || !kinematics) return false;
    const auto facing = turnFacing(kinematics->transform.position, motion->direction, target_, std::numbers::pi_v<float>);
    return setAnimationFloat(actor, "StickX", 0) &&
           setAnimationFloat(actor, "DirectionAngle", facing.animationAngle);
}

bool CliffTurn::begin(const ActorReference& actor, const totk::core::WorldPosition& target, float delta) {
    stop(actor);
    if (!finite(target) || !std::isfinite(delta) || delta < 0) {
        Logging.Log("[wolf-link] CLIFF_TURN_INPUT_INVALID target=%f,%f,%f delta=%f",
            double(target.x), double(target.y), double(target.z), double(delta));
        return false;
    }
    target_ = target;
    if (!animation_.acquire(actor) || !aim(actor) || !playAnimation(actor, kCommand, 1, delta, true)) {
        stop(actor);
        return false;
    }
    status_ = FollowStatus::Running;
    return true;
}

FollowStatus CliffTurn::update(const ActorReference& actor) {
    if (!animation_.owns(actor)) {
        Logging.Log("[wolf-link] CLIFF_TURN_OWNER_EXPIRED actor=%p/%u",
            reinterpret_cast<void*>(actor.address()), actor.generation());
        stop(actor);
    }
    if (status_ != FollowStatus::Running) return status_;
    const auto animation = readAnimation(actor, kCommand);
    if (!animation) {
        stop(actor);
    } else if (animation->current && animation->finished) {
        // The original negative finish-angle threshold leaves completion to the animation.
        status_ = FollowStatus::Succeeded;
    } else if (!animation->noChange && !aim(actor)) {
        stop(actor);
    }
    return status_;
}

void CliffTurn::stop(const ActorReference& actor) {
    animation_.release(actor);
    status_ = FollowStatus::Failed;
}

}
