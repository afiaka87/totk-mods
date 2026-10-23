// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <cstdint>

namespace botw_abilities::champions::field {
bool install(std::uintptr_t base);
void reset();
void update(std::uintptr_t actor, const totk::core::WorldPosition& position, float seconds);
}
