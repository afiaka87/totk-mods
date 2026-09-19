// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::feature {

struct GeometryVector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct SiteGeometry {
    GeometryVector3 anchor{};
    float normalX = 0.0f;
    float normalZ = 0.0f;
    float floorGap = 0.0f;
    float roofGap = 0.0f;
};

struct PropDefinition {
    const char* actorName = nullptr;
    float lateralOffset = 0.0f;
    float outwardOffset = 0.0f;
    float lift = 0.0f;
    float yawSin = 0.0f;
    float yawCos = 1.0f;
    std::uint8_t freeze = 0;
    std::uint8_t expectBody = 0;
    std::uint8_t backerLevel = 0;
    std::uint8_t backerTile = 0;
    std::uint8_t dependsOn = 0xFF;
    std::uint8_t tierMask = 0;
    float spawnRadius = 0.0f;
};

struct BackerTuning {
    float seatClearance = 0.0f;
    float extraMargin = 0.0f;
    float maximumShift = 0.0f;
    float tileSpacing = 0.0f;
};

struct PropTransform {
    GeometryVector3 position{};
    GeometryVector3 column0{};
    GeometryVector3 column1{};
    GeometryVector3 column2{};
};

float backerShift(const SiteGeometry& site,
                  const PropDefinition& prop,
                  const BackerTuning& tuning);

PropTransform worldTransform(const SiteGeometry& site,
                             const PropDefinition& prop,
                             const BackerTuning& tuning);

} // namespace bivouac::feature
