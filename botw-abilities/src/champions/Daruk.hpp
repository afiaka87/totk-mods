// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "State.hpp"
#include "totk/engine/ActorHandle.hpp"

namespace botw_abilities::champions::daruk {
bool install(std::uintptr_t base);
void reset();
void impact(const totk::engine::ActorHandle& player);
void service(Context context, bool zlHeld, bool freshInput);
bool active();
} // namespace botw_abilities::champions::daruk
