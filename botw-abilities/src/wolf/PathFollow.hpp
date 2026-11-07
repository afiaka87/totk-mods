// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "FollowControl.hpp"
#include "FollowGear.hpp"
#include "NativeMove.hpp"

namespace botw_abilities::wolf {

struct PathFollowResult {
    FollowStatus status;
    std::optional<bool> limitSpeed;
};

// How near the goal counts as arrived, and whether the run eases to a walk on the way in.
struct PathGoal {
    float tolerance = 8;
    bool autoSlow = true;
};

// Own from the actor's AI callback; every call borrows a fresh actor reference.
class PathFollow {
public:
    [[nodiscard]] bool begin(const ActorReference& actor, FollowRange range,
                              const totk::core::WorldPosition& destination, PathGoal goal = {});
    // Holds one gear for the whole route, as the original lead and food paths do.
    [[nodiscard]] bool beginFixed(const ActorReference& actor, int gear,
                                  const totk::core::WorldPosition& destination, PathGoal goal = {});
    [[nodiscard]] bool retarget(const ActorReference& actor, const totk::core::WorldPosition& destination);
    [[nodiscard]] PathFollowResult update(const ActorReference& actor, float frameDelta);
    void stop(const ActorReference& actor);
private:
    [[nodiscard]] bool startGear(const ActorReference& actor);
    [[nodiscard]] bool run(const ActorReference& actor, const totk::core::WorldPosition& destination);
    NativeMove move_;
    FollowGear gear_;
    totk::core::WorldPosition destination_;
    PathGoal goal_;
    FollowRange range_{};
    FollowStatus status_ = FollowStatus::Failed;
    bool enterAction_{}, fixed_{};
};
static_assert(sizeof(PathFollow) == 72);

}
