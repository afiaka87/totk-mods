// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "BombWorld.hpp"
#include <optional>

namespace remote_bomb::physics {
// 1.4.3's routine (body, immediate in bit 0) that adds a body to its physics world; it returns nothing.
inline constexpr std::ptrdiff_t kBodyAdd143 = 0x1882A0;
bool initialize(std::uintptr_t base);
bool queueRefresh(engine::Handle actor);
void bodyAdded(std::uintptr_t body);
// 1.4.3: whether the body is in its physics world; empty when the world cannot be read.
std::optional<bool> inWorld(std::uintptr_t body);
void motionChanged(std::uintptr_t body, const int *preset);
}
