// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "CliffController.hpp"
#include "NativeAction.hpp"
#include <lib.hpp>
#include <cmath>

namespace botw_abilities::wolf {
namespace {
constexpr PathActionParameters kAction{2, 1, false, false};
constexpr MoveParameters kMove{
    .actionVerb = 4, .minGear = 3, .maxGear = 3, .gearType = kAction.gearType,
    .minimumSlowGear = kAction.minimumGear, .goalTolerance = 5.0f};

bool validDelta(float delta) { return std::isfinite(delta) && delta >= 0; }
}

bool CliffController::bind(const ActorReference& actor, float delta) {
    if (!actor.address() || !validDelta(delta)) {
        Logging.Log("[wolf-link] CLIFF_BEGIN_REFUSED actor=%p delta=%f",
            reinterpret_cast<void*>(actor.address()), double(delta));
        return false;
    }
    owner_ = actor.address();
    generation_ = actor.generation();
    return true;
}

bool CliffController::acceptUpdate(const ActorReference& actor, float delta) {
    if (owner_ && owner_ == actor.address() && generation_ == actor.generation() && validDelta(delta))
        return true;
    Logging.Log("[wolf-link] CLIFF_UPDATE_REFUSED actor=%p generation=%u delta=%f",
        reinterpret_cast<void*>(actor.address()), actor.generation(), double(delta));
    stop(actor);
    return false;
}

std::optional<CliffChildState> CliffController::observe() const {
    if (std::holds_alternative<CliffTurn>(child_)) return CliffChildState{CliffAction::Turn, childStatus_};
    if (std::holds_alternative<NativeMove>(child_)) return CliffChildState{CliffAction::Move, childStatus_};
    return std::nullopt;
}

void CliffController::enter(const ActorReference& actor, CliffAction action,
                            totk::core::WorldPosition target, float delta) {
    stopChild(actor);
    const bool started = action == CliffAction::Turn ? child_.emplace<CliffTurn>().begin(actor, target, delta)
                                                   : child_.emplace<NativeMove>().start(actor, kMove, target);
    childStatus_ = started ? FollowStatus::Running : FollowStatus::Failed;
    enterAction_ = true;
}

void CliffController::tick(const ActorReference& actor) {
    if (auto* turn = std::get_if<CliffTurn>(&child_)) {
        childStatus_ = turn->update(actor);
    } else if (auto* move = std::get_if<NativeMove>(&child_); move && move->owns(actor)) {
        const auto output = move->update(actor, kMove);
        if (!applyMoveOutput(actor, output, kAction, enterAction_)) stopChild(actor);
        else childStatus_ = output->nativeResult == 2 ? FollowStatus::Succeeded : FollowStatus::Running;
    }
}

void CliffController::stopChild(const ActorReference& actor) {
    if (auto* turn = std::get_if<CliffTurn>(&child_)) turn->stop(actor);
    if (auto* move = std::get_if<NativeMove>(&child_)) move->stop(actor);
    child_.emplace<std::monostate>();
    childStatus_ = FollowStatus::Failed;
    enterAction_ = false;
}

void CliffController::stop(const ActorReference& actor) {
    stopChild(actor);
    state_ = {{}, 0, false, FollowStatus::Failed};
    owner_ = 0;
    generation_ = 0;
}

}
