// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "HandheldPose.hpp"
#include <cstdint>

namespace zonai_hookshot::pure {
// Older native merges need a common shift on their sampled world-space matrices.
struct TravelBodyReadShift {
    std::uintptr_t thread{}, bodies[64]{};
    unsigned count{};
    Vec3 shift{};

    bool apply(std::uintptr_t caller, std::uintptr_t body, handheld::Matrix* matrix) const {
        if (!thread || caller != thread || !body || !matrix || count > 64 || !finite3(shift))
            return false;
        bool owned = false;
        for (unsigned i = 0; i < count; ++i)
            owned |= bodies[i] == body;
        if (!owned)
            return false;
        const auto translated = add(handheld::position(*matrix), shift);
        if (!finite3(translated))
            return false;
        matrix->v[3] = translated.x;
        matrix->v[7] = translated.y;
        matrix->v[11] = translated.z;
        return true;
    }
};
}
