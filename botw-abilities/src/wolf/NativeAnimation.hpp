// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeActor.hpp"
#include <optional>

namespace botw_abilities::wolf {

bool initializeAnimationApi(std::uintptr_t base);

struct AnimationState { bool current{}, finished{}, noChange{}; };
[[nodiscard]] std::optional<AnimationState> readAnimation(const ActorReference& actor, const char* command);
[[nodiscard]] bool playAnimation(const ActorReference& actor, const char* command,
                                 float rate, float delta, bool ignoreSame);
[[nodiscard]] bool setAnimationFloat(const ActorReference& actor, const char* name, float value);

// Owned by the AI worker; suppress only the quadruped's automatic AS command selection.
class AnimationOverride {
public:
    AnimationOverride() = default;
    AnimationOverride(const AnimationOverride&) = delete;
    AnimationOverride& operator=(const AnimationOverride&) = delete;
    [[nodiscard]] bool acquire(const ActorReference& actor);
    [[nodiscard]] bool owns(const ActorReference& actor) const;
    void release(const ActorReference& actor);
private:
    std::uintptr_t owner_{};
    std::uint32_t generation_{};
    bool wasSuppressed_{};
};
static_assert(sizeof(AnimationOverride) == 16);

}
