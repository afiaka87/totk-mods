// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/core/Units.hpp"
#include <cmath>

namespace botw_abilities::wolf {

template<class Vector> [[nodiscard]] bool finite(const Vector& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// For units without -ffp-contract=off; those keep the original float order themselves.
[[nodiscard]] inline float flatDistance(const totk::core::WorldPosition& a, const totk::core::WorldPosition& b) {
    const float x = a.x - b.x, z = a.z - b.z;
    return std::sqrt(x * x + z * z);
}

// The way the body points as drawn: the transform's local Z axis. The movement controller's heading can
// lag a turn animation, which turned the wolf away from enemies before bites (wolf-compact-18).
[[nodiscard]] inline totk::core::WorldPosition bodyForward(const totk::core::WorldTransform& transform) {
    const auto& r = transform.rotation.values;
    return {r[2], r[5], r[8]};
}

// For -ffp-contract=off units only: the zero terms repeat the original's operation order.
[[nodiscard]] inline totk::core::WorldPosition horizontalUnit(totk::core::WorldPosition value) {
    value.y = 0;
    const float length = std::sqrt((value.x * value.x + 0.0f) + value.z * value.z);
    if (length > 0) {
        const float inverse = 1.0f / length;
        value = {value.x * inverse, 0.0f * inverse, value.z * inverse};
    }
    return value;
}

}
