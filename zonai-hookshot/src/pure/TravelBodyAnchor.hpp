// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "HandheldPose.hpp"

namespace zonai_hookshot::pure {
// Native merge adds (basis.position - cachedPosition) to every simulated bone.
inline bool travelBodyAnchor(const handheld::Matrix& basis, Vec3 animatedModel,
                             Vec3 physicalWorld, Vec3& cachedPosition, Vec3& shift) {
    for(float value:basis.v)if(!std::isfinite(value))return false;
    if(!finite3(animatedModel)||!finite3(physicalWorld))return false;
    shift=sub(handheld::point(basis,animatedModel),physicalWorld);
    cachedPosition=sub(handheld::position(basis),shift);
    return finite3(shift)&&finite3(cachedPosition);
}
}
