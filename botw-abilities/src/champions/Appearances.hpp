// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "State.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Transform.hpp"

namespace botw_abilities::champions::appearances {
bool install(std::uintptr_t base);
void reset();
void used(Blessing blessing);
void beginFrame();
bool collect(const totk::engine::ActiveProcessView& actor);
void service(const totk::engine::SceneContext& scene,
             const totk::core::WorldTransform& placement, float seconds);
}
