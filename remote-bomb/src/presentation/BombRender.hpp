// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
#include "BombHudPolicy.hpp"
namespace remote_bomb::render {
bool install(std::uintptr_t base);
void drawHud(std::uintptr_t module, std::uintptr_t args, hud_policy::View view, float alpha);
#ifdef BOTW_ABILITIES_CHAMPIONS
void drawExtraHud(std::uintptr_t module, std::uintptr_t args);
#endif
}
