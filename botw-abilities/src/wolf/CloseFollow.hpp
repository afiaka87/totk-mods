// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "FollowControl.hpp"
#include "NativeActor.hpp"

namespace botw_abilities::wolf {

// AI-owner only; target distance and travel prediction come from the parent behavior.
class CloseFollow {
public:
    [[nodiscard]] bool begin(const ActorReference& actor);
    [[nodiscard]] FollowStatus update(const ActorReference& actor, const ActorReference& leader,
        const totk::core::WorldPosition& target, float keptDistance, bool predictTravel,
        int gearLimit = 4, int gearFloor = 0);
    void stop(const ActorReference& actor);
    [[nodiscard]] int gear() const { return gear_; }
private:
    [[nodiscard]] bool owns(const ActorReference& actor) const;
    std::uintptr_t owner_{};
    std::uint32_t generation_{};
    int leaderSlot_ = -1;
    bool insideTurn_{}, enterAction_{}, waiting_{};
    std::uint8_t gear_{};
};
static_assert(sizeof(CloseFollow) == 24);

}
