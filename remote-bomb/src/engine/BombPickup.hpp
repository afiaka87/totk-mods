// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombWorld.hpp"
namespace remote_bomb::pickup {
bool validate(std::uintptr_t base);
bool ready(std::uintptr_t base, engine::Handle player);
bool spawn(std::uintptr_t base, engine::Handle player, Shape shape, Upgrade upgrade);
bool focused(std::uintptr_t base, engine::Handle player, engine::Handle bomb);
}
