// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
namespace remote_bomb {
// Checks the game code and installs every Remote Bomb hook; false when the game does not match.
bool install(std::uintptr_t base);
bool chooseSphereFromWheel(std::uintptr_t screen);
bool chooseCubeFromWheel(std::uintptr_t screen);
#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks' switch, on the controller thread.
void switched(bool on);
#endif
}
