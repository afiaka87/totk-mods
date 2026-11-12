// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
namespace cryonis {
// Checks the game code and installs every Cryonis hook; false when the game does not match.
bool install(std::uintptr_t base);
// Before the wheel exits: marks Cryonis as picked from a page; the caller then cancels the wheel.
bool chooseFromWheel(std::uintptr_t screen);
#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks' switch, on the controller thread.
void switched(bool on);
#endif
}
