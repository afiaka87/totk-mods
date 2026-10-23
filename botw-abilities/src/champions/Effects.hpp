// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "ChampionEffects.hpp"
#include "State.hpp"
#include "totk/engine/ActorHandle.hpp"
#include "totk/engine/Scene.hpp"

namespace botw_abilities::champions::effects {
bool install(std::uintptr_t base);
void reset();
bool pulse(Effect effect, float seconds, float delay = 0);
void hold(Effect effect, bool active);
bool launchUpdraft(const totk::core::WorldTransform& placement);
bool launchStrike(const totk::core::WorldTransform& placement);
bool strikeEnemy(const totk::core::WorldTransform& placement);
bool updraftActive();
void used(Blessing blessing);
void service(const totk::engine::SceneContext& scene, const totk::engine::ActorHandle& player, float seconds);
} // namespace botw_abilities::champions::effects
