// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "FollowControl.hpp"
#include "NativeAnimation.hpp"

namespace botw_abilities::wolf {

// Own on the AI worker; native animation movement applies the clip's root transform.
class CliffTurn {
public:
    [[nodiscard]] bool begin(const ActorReference& actor, const totk::core::WorldPosition& target, float delta);
    [[nodiscard]] FollowStatus update(const ActorReference& actor);
    void stop(const ActorReference& actor);
private:
    [[nodiscard]] bool aim(const ActorReference& actor);
    AnimationOverride animation_;
    totk::core::WorldPosition target_;
    FollowStatus status_ = FollowStatus::Failed;
};
static_assert(sizeof(CliffTurn) == 32);

}
