// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/core/Units.hpp"
#include <cstdint>

namespace botw_abilities::champions::field {
bool install(std::uintptr_t base);
void reset();
void update(std::uintptr_t actor, const totk::core::WorldPosition& position, float seconds);
}
