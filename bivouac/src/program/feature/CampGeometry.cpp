// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "CampGeometry.hpp"

namespace bivouac::feature {

float backerShift(const SiteGeometry& site,
                  const PropDefinition& prop,
                  const BackerTuning& tuning) {
    if (prop.backerLevel == 0) {
        return 0.0f;
    }
    const float levelGap =
        prop.backerLevel == 1 ? site.floorGap : site.roofGap;
    float shift = levelGap + tuning.seatClearance + tuning.extraMargin;
    if (shift > tuning.maximumShift) {
        shift = tuning.maximumShift;
    }
    return shift - static_cast<float>(prop.backerTile) * tuning.tileSpacing;
}

PropTransform worldTransform(const SiteGeometry& site,
                             const PropDefinition& prop,
                             const BackerTuning& tuning) {
    PropTransform result{};
    const float normalX = site.normalX;
    const float normalZ = site.normalZ;
    const float tangentX = normalZ;
    const float tangentZ = -normalX;

    float outwardOffset = prop.outwardOffset;
    if (prop.backerLevel != 0) {
        outwardOffset -= backerShift(site, prop, tuning);
    }

    result.position = {
        site.anchor.x + tangentX * prop.lateralOffset
            + normalX * outwardOffset,
        site.anchor.y + prop.lift,
        site.anchor.z + tangentZ * prop.lateralOffset
            + normalZ * outwardOffset,
    };

    const float rotatedX =
        normalX * prop.yawCos + normalZ * prop.yawSin;
    const float rotatedZ =
        -normalX * prop.yawSin + normalZ * prop.yawCos;
    result.column0 = {rotatedX, 0.0f, rotatedZ};
    result.column1 = {0.0f, 1.0f, 0.0f};
    result.column2 = {-rotatedZ, 0.0f, rotatedX};
    return result;
}

} // namespace bivouac::feature
