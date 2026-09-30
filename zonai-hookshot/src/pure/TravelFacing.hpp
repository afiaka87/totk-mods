// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "ArrowboundPure.hpp"

namespace zonai_hookshot::pure {
inline constexpr float kTravelTurnFinishFraction = 0.9f;

inline bool prepareTravelFacing(YawState& yaw, float basis[9], Vec3 direction,
                                Vec3 wallFacing, float travelDistance) {
    if (!finiteBasis(basis) || !finite3(direction) ||
        !std::isfinite(travelDistance) || travelDistance <= 0.f) return false;
    float aligned[9]{};
    float angle{};
    // A vertical route has no horizontal heading; retain the captured heading.
    if (length(Vec3{direction.x,0,direction.z}) >= 0.05f) {
        if (!signedHorizontalYaw({basis[2],0,basis[8]},direction,angle)) return false;
    }
    if (!rotateBasisWorldYaw(basis,angle,aligned) ||
        !prepareYaw(yaw,aligned,wallFacing,travelDistance,YawTiming::Early)) return false;
    yaw.durationDistance = travelDistance * kTravelTurnFinishFraction;
    for (unsigned i=0;i<9;++i) basis[i]=aligned[i];
    return true;
}

inline void updateTravelFacing(YawState& yaw,float advanced) {
    if (!yaw.valid || !yaw.started || yaw.finished || !std::isfinite(advanced)) return;
    // PositionZip owns distance. Repeated callbacks or endpoint holds add no turn.
    if (advanced > yaw.elapsedDistance) {
        yaw.elapsedDistance = advanced < yaw.durationDistance ? advanced : yaw.durationDistance;
        yaw.finished = yaw.elapsedDistance >= yaw.durationDistance;
    }
}
}
