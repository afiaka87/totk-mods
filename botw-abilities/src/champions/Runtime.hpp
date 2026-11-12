// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "State.hpp"
#include "UseEvents.hpp"

namespace botw_abilities::champions {
bool install(std::uintptr_t base);
Context currentContext(Blessing blessing);
bool notifyUsed(UseEvent event);
std::uint64_t simulationTick();
#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks' switch, on the controller thread. Off ends every ability and removes its effects.
void switched(bool on);
#endif
} // namespace botw_abilities::champions
