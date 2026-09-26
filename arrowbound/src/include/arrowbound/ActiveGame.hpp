// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstddef>
#include <cstdint>

#include "GameProfiles.hpp"

namespace arrowbound::profiles {
// The running build's profile; null until activate() finds exactly one matching build.
const Game* active();

// Startup only, before any other initialization: identify the running main and point the
// shared engine layouts at it. Returns null (and changes nothing) on an unknown build.
const Game* activate(std::uintptr_t mainBase, std::size_t textSize);

// True when a function entry holds its original word or another mod's exlaunch detour;
// otherwise logs the words found and returns false so the caller skips the hook.
bool entryHookable(std::uintptr_t mainBase, const Site& site, const char* label);
}  // namespace arrowbound::profiles
