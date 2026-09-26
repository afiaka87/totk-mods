// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Scene, Link and the camera: the gameplay thread's only window into guest memory.
#pragma once

#include "EngineNamespace.hpp"

#include <cstdint>

#include "Vec3.hpp"
#include "totk/core/Units.hpp"

namespace HOOKSHOT_ENGINE_NS::world {
// Ticks a freshly resolved player must survive before the mod acts.
constexpr int kStableMin = 12;
// A one-tick position jump this large is a teleport or loading transition, never gameplay motion.
constexpr float kTeleportResetDistance = 30.0f;

using SessionResetFn = void (*)(const char* reason);

struct WorldState {
    std::uintptr_t mainBase = 0;
    totk::core::SceneToken sceneToken{};
    std::uintptr_t player = 0;
    std::uintptr_t camera = 0;
    int stable = 0;
    pure::Vec3 lastPlayerPos{};
    bool havePlayerPos = false;
};

void initialize(std::uintptr_t mainBase);
const WorldState& state();

// One refresh per tick; reset is called with "scene" or "teleport".
void refresh(SessionResetFn reset);

[[nodiscard]] bool ready();
[[nodiscard]] inline std::uintptr_t mainBase() { return state().mainBase; }
[[nodiscard]] inline bool havePlayer() { return state().player != 0; }
void* playerActor();

pure::Vec3 playerPosition();
pure::Vec3 cameraPosition();
bool readPlayerRotation(float out[9]);
bool readCameraRotation(float out[9]);

// The carrier write (ActorBase forceSetMatrix).
bool forcePlayerPose(const float rotation[9], const pure::Vec3& position);

// Native bow-exit cleanup: restore the Player component's ordinary step rate.
bool resetPlayerStepRate();

// Wall-contact bit, telemetry only: it can blink during genuine climbing.
bool climbBitEngaged();

}  // namespace HOOKSHOT_ENGINE_NS::world
