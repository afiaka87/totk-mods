// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace botw_abilities::wolf {
bool install(std::uintptr_t base);
bool chooseFromWheel(std::uintptr_t screen);
bool canSummon();
// The wheel caption while a summon is refused; null leaves the wedge's own name.
const char16_t* wheelCaption();
}
