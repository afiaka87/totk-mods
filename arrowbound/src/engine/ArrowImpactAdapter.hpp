// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <cstring>
#include "ArrowHookshot.hpp"

namespace arrowbound::engine {

inline pure::Vec3 impactVector(const void* address) {
    pure::Vec3 value{};
    std::memcpy(&value, address, sizeof(value));
    return value;
}

inline pure::ArrowImpact nativeArrowImpact(const void* rawController, pure::ArrowImpactSource source,
                                           int sensorType, bool water, const float* adjustedHit = nullptr,
                                           const void* motionContext = nullptr) {
    using namespace pure;
    const auto* controller = static_cast<const unsigned char*>(rawController);
    ArrowImpact hit{source, impactVector(controller + 288), impactVector(controller + 276), sensorType, water};
    // Reconstruct the raw contact from the sensor's pre-standoff point.
    if (source == ArrowImpactSource::Sensor && sensorType == 6 && adjustedHit && motionContext) {
        float standoff = 0;
        std::memcpy(&standoff, static_cast<const unsigned char*>(motionContext) + 56, sizeof(standoff));
        hit.contact = add(impactVector(adjustedHit), mul(hit.direction, standoff));
    }
    return hit;
}

} // namespace arrowbound::engine
