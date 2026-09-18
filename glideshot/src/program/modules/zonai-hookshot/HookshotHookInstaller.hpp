// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace zonai_hookshot::hooks {
// Hookshot-specific action and controller hooks; the host owns the process-wide Npad and raycast
// hooks.
void installUnique(std::uintptr_t mainBase);

// Runs at the end of the host's Npad callback, after every feature has masked human input.
void afterNpad(void* device);

}  // namespace zonai_hookshot::hooks
