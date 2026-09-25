// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cmath>
#include <cstdint>

namespace zonai_survey::pure {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kRadiansToDegrees = 180.0f / kPi;
inline constexpr float kConeDegrees = 70.0f;
inline constexpr float kConeRadians = kConeDegrees * kPi / 180.0f;

inline constexpr uint32_t kSpokes = 64;

inline constexpr uint32_t kRings = 37;

inline constexpr float kNearSpacing = 2.5f;
inline constexpr uint32_t kNearRings = 10;

inline constexpr uint32_t kInnerRings = 3;
inline constexpr float kInnerSpacing = kNearSpacing / 4.0f;

inline constexpr float kRingGrowth = 1.135f;

struct RingTable {
    float radius[kRings]{};
    float spacing[kRings]{};
};

constexpr RingTable makeRingTable() {
    RingTable table{};
    float gap = kInnerSpacing;
    float radius = 0.0f;
    for (uint32_t ring = 0; ring < kRings; ++ring) {

        if (ring == kInnerRings + 1u) gap = kNearSpacing;
        if (ring >= kInnerRings + kNearRings) gap *= kRingGrowth;
        radius += gap;
        table.spacing[ring] = gap;
        table.radius[ring] = radius;
    }
    return table;
}

inline constexpr RingTable kRingTable = makeRingTable();
inline constexpr float kMaxRange = kRingTable.radius[kRings - 1];
inline constexpr uint32_t kProbeCount = kSpokes * kRings;

inline constexpr float kAcceptedGroundRange = 60.0f;

constexpr float ringRadius(uint32_t ring) {
    return kRingTable.radius[ring < kRings ? ring : kRings - 1];
}

constexpr float ringSpacing(uint32_t ring) {
    return kRingTable.spacing[ring < kRings ? ring : kRings - 1];
}

inline float ringArcChord(uint32_t ring) {
    return 2.0f * kPi * ringRadius(ring) / static_cast<float>(kSpokes);
}

inline float ringSpokeSpacing(uint32_t ring) {
    return kConeRadians * ringRadius(ring) / static_cast<float>(kSpokes - 1);
}

inline constexpr uint32_t kProbesPerTick = 96;

inline constexpr uint32_t kPulseTicks = 30;

inline float wavePosition(uint32_t tick) {
    return static_cast<float>(kRings) * static_cast<float>(tick) / static_cast<float>(kPulseTicks);
}

constexpr float waveArrivalOf(uint32_t ring) { return static_cast<float>(ring + 1); }

inline uint32_t ringsReached(uint32_t tick) {
    const float position = wavePosition(tick);
    if (position <= 0.0f) return 0;
    const uint32_t reached = static_cast<uint32_t>(position);
    return reached > kRings ? kRings : reached;
}

inline float wavefrontRadius(uint32_t tick) {
    const float position = wavePosition(tick);
    if (position <= 0.0f) return 0.0f;
    if (position >= static_cast<float>(kRings)) return kMaxRange;
    const uint32_t ring = static_cast<uint32_t>(position);
    const float fraction = position - static_cast<float>(ring);
    const float inner = ring == 0 ? 0.0f : kRingTable.radius[ring - 1];
    return inner + fraction * (kRingTable.radius[ring] - inner);
}

inline float waveCoordForRadius(float radius) {
    if (radius <= 0.0f) return 0.0f;
    if (radius >= kMaxRange) return static_cast<float>(kRings);
    for (uint32_t ring = 0; ring < kRings; ++ring) {
        if (radius <= kRingTable.radius[ring]) {
            const float inner = ring == 0 ? 0.0f : kRingTable.radius[ring - 1];
            const float span = kRingTable.radius[ring] - inner;
            const float fraction = span > 0.0f ? (radius - inner) / span : 0.0f;
            return static_cast<float>(ring) + fraction;
        }
    }
    return static_cast<float>(kRings);
}

constexpr uint32_t probeRing(uint32_t index) { return index / kSpokes; }
constexpr uint32_t probeSpoke(uint32_t index) { return index % kSpokes; }
constexpr uint32_t probeIndex(uint32_t ring, uint32_t spoke) {
    return ring * kSpokes + spoke;
}

inline constexpr float kSurveyWidthDegrees = 100.0f;
inline constexpr float kSurveyWidthRadians = kSurveyWidthDegrees * kPi / 180.0f;

inline float surveyTanHalfWidth() {
    return std::tan(0.5f * kSurveyWidthRadians);
}

constexpr float spokeSignedFraction(uint32_t spoke) {
    const uint32_t clamped = spoke < kSpokes ? spoke : kSpokes - 1;
    return 2.0f * static_cast<float>(clamped) / static_cast<float>(kSpokes - 1) -
           1.0f;
}

inline void sampleOffset(uint32_t ring, uint32_t spoke, float headingX,
                         float headingZ, float& outX, float& outZ) {
    const float radius = ringRadius(ring);
    const float lateral =
        radius * surveyTanHalfWidth() * spokeSignedFraction(spoke);
    outX = headingX * radius + headingZ * lateral;
    outZ = headingZ * radius - headingX * lateral;
}

inline bool sampleWithinRange(uint32_t ring, uint32_t spoke) {
    const float radius = ringRadius(ring);
    const float lateral =
        radius * surveyTanHalfWidth() * spokeSignedFraction(spoke);
    const float bound = kMaxRange + ringSpacing(ring);
    return radius * radius + lateral * lateral <= bound * bound;
}

inline float sampleRowSpacing(uint32_t ring) {
    return 2.0f * ringRadius(ring) * surveyTanHalfWidth() /
           static_cast<float>(kSpokes - 1);
}

inline float sampleRadialScale(uint32_t spoke) {
    const float slope = surveyTanHalfWidth() * spokeSignedFraction(spoke);
    return std::sqrt(1.0f + slope * slope);
}

inline uint32_t authorisedProbeCount(uint32_t tick) {
    return ringsReached(tick) * kSpokes;
}

constexpr uint32_t probeBatchSize(uint32_t authorised, uint32_t completed) {
    const uint32_t reachable =
        authorised < kProbeCount ? authorised : kProbeCount;
    if (completed >= reachable) return 0;
    const uint32_t available = reachable - completed;
    return available < kProbesPerTick ? available : kProbesPerTick;
}

inline constexpr float kMinProbeCeiling = 40.0f;
inline constexpr float kMinProbeFloor = 80.0f;

inline constexpr float kCeilingPerSpacing = 1.5f;
inline constexpr float kFloorPerSpacing = 3.0f;

constexpr float probeCeilingFor(float spacingMeters) {
    const float scaled = spacingMeters * kCeilingPerSpacing;
    return scaled > kMinProbeCeiling ? scaled : kMinProbeCeiling;
}

constexpr float probeFloorFor(float spacingMeters) {
    const float scaled = spacingMeters * kFloorPerSpacing;
    return scaled > kMinProbeFloor ? scaled : kMinProbeFloor;
}

constexpr float probeCeiling(uint32_t ring) {
    return probeCeilingFor(ringSpacing(ring));
}

constexpr float probeFloor(uint32_t ring) { return probeFloorFor(ringSpacing(ring)); }

inline constexpr float kRecoveryCeilingPerSpacing = 4.0f;
inline constexpr float kRecoveryFloorPerSpacing = 6.0f;
inline constexpr float kMinRecoveryCeiling = 120.0f;
inline constexpr float kMinRecoveryFloor = 160.0f;

constexpr float recoveryCeilingFor(float spacingMeters) {
    const float scaled = spacingMeters * kRecoveryCeilingPerSpacing;
    return scaled > kMinRecoveryCeiling ? scaled : kMinRecoveryCeiling;
}

constexpr float recoveryFloorFor(float spacingMeters) {
    const float scaled = spacingMeters * kRecoveryFloorPerSpacing;
    return scaled > kMinRecoveryFloor ? scaled : kMinRecoveryFloor;
}

constexpr float recoveryCeiling(uint32_t ring) {
    return recoveryCeilingFor(ringSpacing(ring));
}

constexpr float recoveryFloor(uint32_t ring) {
    return recoveryFloorFor(ringSpacing(ring));
}

struct FanRay {
    float fromX = 0.0f, fromY = 0.0f, fromZ = 0.0f;
    float toX = 0.0f, toY = 0.0f, toZ = 0.0f;
};

inline FanRay fanRayFor(uint32_t index, float linkX, float linkZ, float referenceY,
                        float headingX, float headingZ) {
    const uint32_t ring = probeRing(index);
    const uint32_t spoke = probeSpoke(index);
    float offX = 0.0f;
    float offZ = 0.0f;
    sampleOffset(ring, spoke, headingX, headingZ, offX, offZ);

    const float x = linkX + offX;
    const float z = linkZ + offZ;
    const float spacing = ringSpacing(ring) * sampleRadialScale(spoke);
    return FanRay{x, referenceY + probeCeilingFor(spacing), z,
                  x, referenceY - probeFloorFor(spacing), z};
}

inline FanRay recoveryRayFor(uint32_t index, float linkX, float linkZ,
                             float referenceY, float headingX, float headingZ) {
    const FanRay narrow =
        fanRayFor(index, linkX, linkZ, referenceY, headingX, headingZ);
    const float spacing =
        ringSpacing(probeRing(index)) * sampleRadialScale(probeSpoke(index));
    return FanRay{narrow.fromX, referenceY + recoveryCeilingFor(spacing), narrow.fromZ,
                  narrow.toX,   referenceY - recoveryFloorFor(spacing),   narrow.toZ};
}

}
