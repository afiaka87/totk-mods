// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "State.hpp"
#include "UseEvents.hpp"

namespace botw_abilities::champions {
bool install(std::uintptr_t base);
Context currentContext(Blessing blessing);
bool notifyUsed(UseEvent event);
std::uint64_t simulationTick();
} // namespace botw_abilities::champions
