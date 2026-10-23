// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/ActorHandle.hpp"
#include "totk/engine/Scene.hpp"

namespace botw_abilities::champions::pose {
bool install(std::uintptr_t base);
void publish(const totk::engine::SceneContext& scene, const totk::engine::ActorHandle& player);
} // namespace botw_abilities::champions::pose
