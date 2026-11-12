// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
namespace arrowbound::ragdoll_transport {
void install(std::uintptr_t mainBase);
void reset();
void retireIfInactive();
}
