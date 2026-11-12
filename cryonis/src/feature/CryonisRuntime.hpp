// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "CryonisHookContext.hpp"
#include "CryonisPresentation.hpp"
namespace cryonis::runtime {
void initialize(std::uintptr_t base);
void tick(void *device);
#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks' switch. Off: the next tick breaks the standing pillars, and nothing new starts.
void setSwitchedOn(bool on);
bool switchedOn();
#endif
bool presentation(Presentation &out, bool hud = false);
engine::HookContext hooks();
}
