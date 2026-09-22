// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace arrowbound::ragdoll_transport {
void install(std::uintptr_t mainBase);
void reset();
void retireIfInactive();
}
