// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace zonai_hookshot::hooks {
struct Observers {
    void (*controller)(void*) = nullptr;
    void (*parasailEnter)(void*) = nullptr;
    void (*parasailUpdate)(void*) = nullptr;
    void (*parasailLeave)(void*) = nullptr;
    void (*fallEnter)(void*) = nullptr;
    void (*fallUpdate)(void*) = nullptr;
    void (*fallLeave)(void*) = nullptr;
    void (*climbUpdate)(void*) = nullptr;
    bool (*glideEntry)(bool) = nullptr;
    void (*gripHooksReady)(bool) = nullptr;
};

// Hookshot-specific action and controller hooks; the host owns the process-wide Npad and raycast
// hooks.
void installUnique(std::uintptr_t mainBase, const Observers& observers = {});

// Runs at the end of the host's Npad callback, after every feature has masked human input.
void afterNpad(void* device);

}  // namespace zonai_hookshot::hooks
