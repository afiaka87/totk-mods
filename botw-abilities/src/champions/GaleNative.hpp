// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Gale.hpp"
#include "totk/engine/ActorHandle.hpp"
#include "totk/engine/Npad.hpp"
#include "totk/engine/Scene.hpp"

namespace botw_abilities::champions::gale {
bool install(std::uintptr_t base);
void reset();
void injectLaunchJump(const totk::engine::NpadFrame& input);
void service(const totk::engine::SceneContext& scene, const totk::engine::ActorHandle& player,
             Context context, std::uint64_t buttons, bool fresh, float seconds);
bool active();
Gale presentation();
std::uint32_t channelEpoch();
} // namespace botw_abilities::champions::gale
