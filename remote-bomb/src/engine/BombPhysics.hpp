// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombWorld.hpp"

namespace remote_bomb::physics {
bool initialize(std::uintptr_t base);
bool queueRefresh(engine::Handle actor);
void bodyAdded(std::uintptr_t body);
void motionChanged(std::uintptr_t body, const int *preset);
}
