// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CliffRecovery.hpp"
#include "CliffTurn.hpp"
#include "NativeMove.hpp"
#include <variant>

namespace botw_abilities::wolf {

// Own on the AI worker; search applies the original terrain policy to a fresh actor snapshot.
class CliffController {
public:
    template<class Search>
    [[nodiscard]] FollowStatus begin(const ActorReference& actor, const totk::core::WorldTransform& transform,
                                     float delta, Search&& search) {
        stop(actor);
        if (!bind(actor, delta)) return state_.status;
        state_ = beginCliffRecovery(transform, search, [&](CliffAction action, auto target) {
            enter(actor, action, target, delta);
        });
        return state_.status;
    }

    template<class Search>
    [[nodiscard]] FollowStatus update(const ActorReference& actor, const totk::core::WorldTransform& transform,
                                      float delta, bool navigationAvailable, Search&& search) {
        if (!acceptUpdate(actor, delta)) return state_.status;
        state_ = advanceCliffRecovery(state_, transform, navigationAvailable, search,
            [&](CliffAction action, auto target) { enter(actor, action, target, delta); },
            [&] { return observe(); });
        // The original scheduler ticks the child even when its parent has just finished or failed.
        tick(actor);
        return state_.status;
    }

    void stop(const ActorReference& actor);
private:
    [[nodiscard]] bool bind(const ActorReference& actor, float delta);
    [[nodiscard]] bool acceptUpdate(const ActorReference& actor, float delta);
    [[nodiscard]] std::optional<CliffChildState> observe() const;
    void enter(const ActorReference& actor, CliffAction action, totk::core::WorldPosition target, float delta);
    void tick(const ActorReference& actor);
    void stopChild(const ActorReference& actor);

    std::variant<std::monostate, CliffTurn, NativeMove> child_;
    CliffRecoveryState state_{{}, 0, false, FollowStatus::Failed};
    std::uintptr_t owner_{};
    std::uint32_t generation_{};
    FollowStatus childStatus_ = FollowStatus::Failed;
    bool enterAction_{};
};
static_assert(sizeof(CliffController) == 72);

}
