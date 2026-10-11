// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace stasis {
// Checks the game code and installs every Stasis hook; false when the game does not match.
bool install(std::uintptr_t base);
} // namespace stasis
