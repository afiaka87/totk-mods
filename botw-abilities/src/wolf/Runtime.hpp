// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>

namespace botw_abilities::wolf {
bool install(std::uintptr_t base);
bool chooseFromWheel(std::uintptr_t screen);
bool canSummon();
// The wheel caption while a summon is refused; null leaves the wedge's own name.
const char16_t* wheelCaption();
#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks' switch, on the controller thread. Off sends the wolf away and refuses a summon.
void switched(bool on);
#endif
}
