// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "State.hpp"
#include "totk/engine/ActorHandle.hpp"
#include "totk/engine/Scene.hpp"

namespace botw_abilities::champions::fury_native {
bool install(std::uintptr_t base);
void reset();
void service(const totk::engine::SceneContext& scene, const totk::engine::ActorHandle& player, Context context);
bool active();
} // namespace botw_abilities::champions::fury_native
