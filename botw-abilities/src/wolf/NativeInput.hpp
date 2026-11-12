// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "NativeActor.hpp"
#include "totk/engine/Scene.hpp"
#include <optional>

namespace botw_abilities::wolf {

struct ActorKinematics {
    totk::core::WorldTransform transform;
    totk::core::LinearVelocity velocity;
};

bool initializeInputApi(std::uintptr_t base);
[[nodiscard]] ActorReference borrowPlayer(const totk::engine::SceneContext& scene);
[[nodiscard]] std::optional<ActorKinematics> readKinematics(const ActorReference& actor);
[[nodiscard]] std::optional<float> readModelWidth(const ActorReference& actor);

}
