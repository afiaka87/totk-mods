// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

#include "BivouacPolicy.hpp"

namespace bivouac::runtime {

using RaycastFn = std::uint64_t (*)(const void* from, const void* to,
                                    const void* object, const void* out,
                                    std::uint32_t mask, std::uint32_t flag);

struct Status {
    int activeCamps = 0;
    std::uint32_t persistenceState = 0;
};

// A composed host that owns an authoritative climb hook supplies its result here.
using ClimbObservation = bivouac::policy::ClimbObservation;

void init(std::uintptr_t mainBase);
void tick(void* npadDevice,
          ClimbObservation observation = ClimbObservation::LocalSensor);
void onRaycast(RaycastFn original, const void* from, const void* object);
void installUniqueHooks();

[[nodiscard]] bool removeNearestCamp(float maximumDistance = 10.0f);
[[nodiscard]] Status status();

// Standalone entry: initializes exlaunch, installs the shared drivers and the unique hooks.
void install();

} // namespace bivouac::runtime
