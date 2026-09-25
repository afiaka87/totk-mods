// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis
#pragma once

#include <cstdint>

namespace wwpg {

using RaycastFn = std::uint64_t (*)(const void*, const void*, const void*, const void*,
                                    std::uint32_t, std::uint32_t);

struct Module {
    const char* name;
    const char* actors;
    const char* controls;
    const char* requirement;
    void (*init)(std::uintptr_t mainBase);
    void (*enter)();
    void (*tick)(void* npadDevice);
    bool (*requestExit)();
    const char* (*status)();
    void (*onRaycast)(RaycastFn original, const void* from, const void* to,
                      const void* object, const void* out, std::uint32_t mask,
                      std::uint32_t flag);
    // Optional (may be null): a self-contained live status line for the standalone HUD.
    // Kept out of status() so its churn never retriggers the status-change banner.
    const char* (*aim)();
};

} // namespace wwpg
