// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CliffController.hpp"

namespace botw_abilities::wolf {

// AI-owner only. The original's "at a cliff" child: he turns toward open ground and trots there.
class CliffEscape {
public:
    [[nodiscard]] FollowStatus begin(const ActorReference& actor, const totk::core::WorldTransform& transform,
        const totk::core::WorldPosition& goal, float delta);
    [[nodiscard]] FollowStatus update(const ActorReference& actor, const totk::core::WorldTransform& transform,
        const totk::core::WorldPosition& goal, float delta);
    void stop(const ActorReference& actor);
private:
    CliffController controller_;
};

}
