// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "DescendGeometry.hpp"
#include <cstdint>

namespace descend::pure {

// Hint policy only. A cached point never authorizes a descent.
struct Proximity {
    Vec3 spot{};
    std::uint64_t checked = 0, sampled = 0, touched = 0;
    std::uintptr_t actor = 0;
    unsigned cursor = 0;
    bool known = false;

    static float distance(Vec3 a, Vec3 b) {
        const float x = a.x - b.x, z = a.z - b.z;
        return std::sqrt(x * x + z * z);
    }
    void begin(std::uintptr_t owner, Vec3 feet, std::uint64_t ms) {
        if (owner != actor || ms < touched || ms - touched > 500) *this = {};
        actor = owner;
        touched = ms;
        if (known && (ms - checked > 1000 || std::fabs(spot.y - feet.y) > 1.5f || distance(spot, feet) > 6))
            known = false;
    }
    bool due(std::uint64_t ms) const { return !sampled || ms - sampled >= 100; }
    Vec3 next(Vec3 feet, std::uint64_t ms, bool& refresh) {
        sampled = ms;
        refresh = known && ms - checked >= 400;
        if (refresh) return spot;
        const unsigned i = cursor++ % 16;
        return offsetBy(feet, viewOffset(static_cast<int>(i % 8)), i < 8 ? 1.5f : 4.0f);
    }
    void observe(Vec3 feet, Vec3 candidate, bool valid, bool refresh, std::uint64_t ms) {
        if (refresh) known = false;
        if (valid && std::fabs(candidate.y - feet.y) <= 1.5f &&
            (!known || distance(candidate, feet) < distance(spot, feet))) {
            spot = candidate;
            checked = ms;
            known = true;
        }
    }
    float warmth(Vec3 feet) const {
        if (!known) return 0;
        // Keep some red until the current position passes the real planner.
        return 0.8f * std::fmax(0.0f, 1.0f - distance(spot, feet) / 6.0f);
    }
};
} // namespace descend::pure
