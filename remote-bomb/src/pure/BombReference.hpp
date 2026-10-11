// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

namespace remote_bomb {
enum class Shape { Sphere, Cube };
enum class Upgrade { Base, Plus };

struct BodyProfile {
    float width, height, depth, centerY, mass, damping, buoyancyScale;
};

struct AbilityProfile {
    unsigned damage;
    float rechargeSeconds;
};

// The BotW 1.5.0 Remote Bomb actors' body parameters.
inline constexpr BodyProfile bodyProfile(Shape shape) {
    return shape == Shape::Sphere ? BodyProfile{.5f, .5f, .5f, .25f, 55.f, .5f, 1.f}
                                  : BodyProfile{.5f, .4f, .5f, .2f, 55.f, 1.f, .7f};
}
inline constexpr AbilityProfile abilityProfile(Upgrade upgrade) {
    return upgrade == Upgrade::Base ? AbilityProfile{12, 6.f} : AbilityProfile{24, 3.f};
}
inline constexpr float kBlastRadius = 6.f;
inline constexpr float kInitialBlastRadius = .001f;
inline constexpr float kBlastGrowthFrames = 3.f;
inline constexpr float kBlastLifetimeFrames = 5.f;
inline constexpr float kShockPower = 500.f;
inline constexpr float kWindRatio = 3.f;
inline constexpr float kThrownMass = 10.f;
inline constexpr float kCubeConvexRadius = .05f;
inline constexpr float kMaxLinearVelocity = 200.f;
inline constexpr float kMaxAngularVelocity = 198.967529f;
}
