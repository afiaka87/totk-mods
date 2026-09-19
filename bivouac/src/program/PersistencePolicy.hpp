// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace bivouac::persistence {

inline constexpr uint16_t kStampSlotNone = 0xFFFFu;

struct PackedSiteMeta {
    uint8_t tier = 0;
    float floorGap = 0.0f;
    float roofGap = 0.0f;
    uint16_t stampSlot = kStampSlotNone;
};

constexpr uint32_t quantizeGap2cm(float meters) {
    return static_cast<uint32_t>(meters * 50.0f + 0.5f) & 0xFFu;
}

constexpr uint32_t packSiteMeta(uint8_t tier, float floorGap, float roofGap,
                                uint16_t stampSlot) {
    const uint32_t stampByte =
        stampSlot != kStampSlotNone && stampSlot <= 254u
        ? static_cast<uint32_t>(stampSlot + 1u) : 0u;
    return static_cast<uint32_t>(tier)
        | (quantizeGap2cm(floorGap) << 8)
        | (quantizeGap2cm(roofGap) << 16)
        | (stampByte << 24);
}

constexpr PackedSiteMeta unpackSiteMeta(uint32_t packed, uint8_t maximumTier) {
    PackedSiteMeta meta{};
    meta.tier = static_cast<uint8_t>(packed & 0xFFu);
    if (meta.tier > maximumTier) meta.tier = 0;
    meta.floorGap = static_cast<float>((packed >> 8) & 0xFFu) * 0.02f;
    meta.roofGap = static_cast<float>((packed >> 16) & 0xFFu) * 0.02f;
    const uint32_t stampByte = (packed >> 24) & 0xFFu;
    meta.stampSlot = stampByte == 0u
        ? kStampSlotNone : static_cast<uint16_t>(stampByte - 1u);
    return meta;
}

constexpr bool payloadShapeMatches(int64_t fileSize, size_t headerSize, size_t siteStride,
                                   uint32_t count, uint32_t maximumSites) {
    if (fileSize < 0 || count > maximumSites) return false;
    const uint64_t expected = static_cast<uint64_t>(headerSize)
        + static_cast<uint64_t>(count) * static_cast<uint64_t>(siteStride);
    return expected == static_cast<uint64_t>(fileSize);
}

constexpr bool positionsOverlap(float ax, float ay, float az,
                                float bx, float by, float bz,
                                float separationMeters) {
    const float dx = ax - bx;
    const float dy = ay - by;
    const float dz = az - bz;
    return dx * dx + dy * dy + dz * dz
        < separationMeters * separationMeters;
}

consteval bool contracts() {
    const uint32_t packed = packSiteMeta(2, 1.24f, 2.50f, 7);
    const PackedSiteMeta decoded = unpackSiteMeta(packed, 2);
    if (decoded.tier != 2 || decoded.floorGap != 1.24f
        || decoded.roofGap != 2.50f || decoded.stampSlot != 7) return false;
    if (unpackSiteMeta(packSiteMeta(9, 0, 0, kStampSlotNone), 2).tier != 0) return false;
    if (unpackSiteMeta(packSiteMeta(0, 0, 0, 254), 2).stampSlot != 254) return false;
    if (unpackSiteMeta(packSiteMeta(0, 0, 0, 255), 2).stampSlot != kStampSlotNone) return false;
    if (!payloadShapeMatches(32 + 4 * 32, 32, 32, 4, 64)) return false;
    if (payloadShapeMatches(32 + 4 * 32 + 1, 32, 32, 4, 64)) return false;
    if (payloadShapeMatches(32, 32, 32, 65, 64)) return false;
    if (!positionsOverlap(0, 0, 0, 19.99f, 0, 0, 20.0f)) return false;
    if (positionsOverlap(0, 0, 0, 20.0f, 0, 0, 20.0f)) return false;
    return true;
}

static_assert(contracts(), "Bivouac persistence policy contracts must hold");

} // namespace bivouac::persistence
