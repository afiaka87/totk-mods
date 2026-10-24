// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StasisCore.hpp"
#include "StasisTrail.hpp"
#include "totk/engine/ActorHandle.hpp"

namespace stasis {
struct Bounds {
    Vec3 min{}, max{};
};
static_assert(sizeof(Bounds) == 24);
inline Vec3 centre(Bounds b) { return scale(add(b.min, b.max), 0.5f); }
// Half the longest side.
inline float radius(Bounds b) {
    return 0.5f * std::fmax(std::fmax(b.max.x - b.min.x, b.max.y - b.min.y), b.max.z - b.min.z);
}

// What the renderer and surface pass draw this frame, published by the input hook.
struct Presentation {
    Vec3 previewOrigin;
    Vec3 position;
    Vec3 arrowOrigin;
    Vec3 direction;
    float ratio{};
    float remainingFrames{};
    float durationFrames{};
    float cooldownFrames{};
    float elapsedFrames{};
    float hitFlash{};
    float releaseFlash{};
    float activationAge{-1.0f};
    Trail trail{};
    float trailRadius{};
    bool aiming{};
    bool selected{};
    bool frozen{};
    bool enemy{};
    Bounds bounds{};
    totk::engine::ActorHandle surfaceActor{};
    totk::core::SceneToken scene{};
};
} // namespace stasis
