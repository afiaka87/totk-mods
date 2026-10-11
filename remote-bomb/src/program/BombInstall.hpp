// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace remote_bomb {
// Checks the game code and installs every Remote Bomb hook; false when the game does not match.
bool install(std::uintptr_t base);
}
