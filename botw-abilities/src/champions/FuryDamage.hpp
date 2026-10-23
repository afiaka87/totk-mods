// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "FuryHits.hpp"

namespace botw_abilities::champions::fury_damage {
bool install(std::uintptr_t base);
void reset();
void service(std::uint64_t generation, std::uintptr_t scene, std::uint64_t now);
bool queue(FuryTarget target, FuryBalance balance, std::uint64_t expires);
}
