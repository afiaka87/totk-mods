// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cmath>
#include <cstdint>

#include "PulseLattice.hpp"
#include "WallSurvey.hpp"

namespace zonai_survey::pure {

struct TerrainSample {
    bool hit = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float normalY = 1.0f;
    float distance = 0.0f;

    float normalX = 0.0f;
    float normalZ = 0.0f;

    bool buried = false;

    float viewCosine = 1.0f;
};

inline float rayViewCosine(float normalX, float normalY, float normalZ,
                           float directionX, float directionY,
                           float directionZ) {
    const float normalLengthSquared =
        normalX * normalX + normalY * normalY + normalZ * normalZ;
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared < 0.25f)
        return 1.0f;
    float cosine = (normalX * directionX + normalY * directionY +
                    normalZ * directionZ) /
                   std::sqrt(normalLengthSquared);
    if (cosine < 0.0f) cosine = -cosine;
    if (!std::isfinite(cosine)) return 1.0f;
    return cosine > 1.0f ? 1.0f : cosine;
}

inline constexpr float kMaxLinkRise = 8.0f;
inline constexpr float kMaxLinkSlope = 1.0f;

inline float linkRiseAllowance(const TerrainSample& a, const TerrainSample& b) {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float run = std::sqrt(dx * dx + dz * dz);
    const float allowed = run * kMaxLinkSlope;
    return allowed > kMaxLinkRise ? allowed : kMaxLinkRise;
}

inline bool linkable(const TerrainSample& a, const TerrainSample& b) {
    if (!a.hit || !b.hit) return false;
    const float gap = a.y - b.y;
    return (gap < 0.0f ? -gap : gap) <= linkRiseAllowance(a, b);
}

enum class SegmentClass : uint8_t {
    Ground,
    Steep,
    Cliff,
};

enum class SlopeBand : uint8_t {
    Level,
    Rolling,
    Inclined,
    Steep,
    Severe,
    Vertical,
};

inline constexpr std::uint32_t kSlopeBandCount = 6;
static_assert(static_cast<std::uint32_t>(SlopeBand::Vertical) + 1u ==
                  kSlopeBandCount,
              "a new Survey slope band needs a palette and batch slot");

enum class SlopeLane : std::uint8_t {
    Cool,
    Warm,
};

inline constexpr std::uint32_t kSlopeLaneCount = 2;

constexpr std::uint32_t laneIndex(SlopeLane lane) {
    return static_cast<std::uint32_t>(lane) < kSlopeLaneCount
               ? static_cast<std::uint32_t>(lane)
               : 0u;
}

enum class SegmentOrigin : std::uint8_t {
    Ground = 0,
    WallNear = 1,
    WallMid = 2,
    WallFar = 3,

    ShallowGround = 4,
};

inline constexpr std::uint32_t kSegmentOriginCount = 5;

constexpr SegmentOrigin wallOriginFor(std::uint32_t layer) {
    return layer >= 2u   ? SegmentOrigin::WallFar
           : layer == 1u ? SegmentOrigin::WallMid
                         : SegmentOrigin::WallNear;
}

constexpr bool originIsWall(SegmentOrigin origin) {
    return origin == SegmentOrigin::WallNear || origin == SegmentOrigin::WallMid ||
           origin == SegmentOrigin::WallFar;
}

struct ScanSegment {
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float bx = 0.0f, by = 0.0f, bz = 0.0f;

    float revealWave = 0.0f;
    SegmentClass surface = SegmentClass::Ground;
    SlopeBand slopeBand = SlopeBand::Level;

    float steepness = -1.0f;
    bool isArc = false;

    SegmentOrigin origin = SegmentOrigin::Ground;
    SlopeLane lane = SlopeLane::Cool;

    bool buried = false;

    bool adaptiveRepair = false;

    bool microStitch = false;

    bool crestline = false;
};

constexpr bool surveyShows(const ScanSegment& segment) {
    return segment.isArc || originIsWall(segment.origin);
}

constexpr bool surveyBuriedShows(const ScanSegment& segment) {
    return !(originIsWall(segment.origin) && segment.buried);
}

inline constexpr float kWalkableSlope = 0.45f;
inline constexpr float kCliffSlope = 1.20f;

inline constexpr float kWalkableNormalY = 0.913f;
inline constexpr float kSteepNormalY = 0.643f;

inline float normalSteepness(float normalY) {
    float n = normalY < 0.0f ? -normalY : normalY;
    if (!std::isfinite(n)) return 1.0f;
    if (n > 1.0f) n = 1.0f;
    const float horizontalSquared = 1.0f - n * n;
    return std::sqrt(horizontalSquared > 0.0f ? horizontalSquared : 0.0f);
}

inline float segmentSteepness(const TerrainSample& a,
                              const TerrainSample& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float lengthSquared = dx * dx + dy * dy + dz * dz;
    float chord = 0.0f;
    if (std::isfinite(lengthSquared) && lengthSquared > 0.0001f)
        chord = (dy < 0.0f ? -dy : dy) / std::sqrt(lengthSquared);
    const float aNormal = normalSteepness(a.normalY);
    const float bNormal = normalSteepness(b.normalY);
    float result = aNormal > bNormal ? aNormal : bNormal;
    if (chord > result) result = chord;
    return result > 1.0f ? 1.0f : result;
}

inline SlopeBand slopeBandFor(float steepness) {
    if (!std::isfinite(steepness)) return SlopeBand::Vertical;
    if (steepness <= 0.208f) return SlopeBand::Level;
    if (steepness <= 0.407f) return SlopeBand::Rolling;
    if (steepness <= 0.588f) return SlopeBand::Inclined;
    if (steepness <= 0.766f) return SlopeBand::Steep;
    if (steepness <= 0.940f) return SlopeBand::Severe;
    return SlopeBand::Vertical;
}

inline constexpr float kCliffRise = 2.0f;

inline constexpr float kVerticalRun = 0.35f;

inline float slopeFromNormalY(float normalY) {
    float n = normalY < 0.0f ? -normalY : normalY;
    if (n > 1.0f) n = 1.0f;
    if (n < 0.01f) return 100.0f;
    const float horizontalSquared = 1.0f - n * n;
    return std::sqrt(horizontalSquared > 0.0f ? horizontalSquared : 0.0f) / n;
}

inline bool surfaceContinuous(const TerrainSample& a, const TerrainSample& b) {
    if (!linkable(a, b)) return false;
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float run = std::sqrt(dx * dx + dz * dz);
    const float rise = a.y - b.y;
    const float absRise = rise < 0.0f ? -rise : rise;
    if (absRise < kCliffRise) return true;
    if (run < kVerticalRun) {
        const float aNormal = a.normalY < 0.0f ? -a.normalY : a.normalY;
        const float bNormal = b.normalY < 0.0f ? -b.normalY : b.normalY;
        return aNormal < kWalkableNormalY || bNormal < kWalkableNormalY;
    }

    const float observedSlope = absRise / run;
    const float aSlope = slopeFromNormalY(a.normalY);
    const float bSlope = slopeFromNormalY(b.normalY);
    const float measuredSurfaceSlope = aSlope > bSlope ? aSlope : bSlope;
    const float normalAllowance = measuredSurfaceSlope * 1.75f + 0.10f;
    const float allowedSlope =
        normalAllowance > 0.35f ? normalAllowance : 0.35f;
    return observedSlope <= allowedSlope;
}

inline SegmentClass classifyBySlope(float slope) {
    if (slope <= kWalkableSlope) return SegmentClass::Ground;
    if (slope <= kCliffSlope) return SegmentClass::Steep;
    return SegmentClass::Cliff;
}

inline SegmentClass classifyByNormal(float normalY) {
    const float n = normalY < 0.0f ? -normalY : normalY;
    if (n >= kWalkableNormalY) return SegmentClass::Ground;
    if (n >= kSteepNormalY) return SegmentClass::Steep;
    return SegmentClass::Cliff;
}

inline SegmentClass worseOf(SegmentClass a, SegmentClass b) {
    return static_cast<uint8_t>(a) >= static_cast<uint8_t>(b) ? a : b;
}

inline constexpr float kSpikeRise = 1.8f;

inline constexpr float kSmoothLimit = 1.2f;
inline constexpr float kSmoothWeight = 0.25f;

inline constexpr float kSpikeRisePerChord = 0.60f;
inline constexpr float kSmoothLimitPerChord = 0.35f;

inline float spikeRiseFor(uint32_t ring) {
    const float scaled = ringArcChord(ring) * kSpikeRisePerChord;
    return scaled > kSpikeRise ? scaled : kSpikeRise;
}

inline float smoothLimitFor(uint32_t ring) {
    const float scaled = ringArcChord(ring) * kSmoothLimitPerChord;
    return scaled > kSmoothLimit ? scaled : kSmoothLimit;
}

inline float medianOfFive(float* values, uint32_t count) {
    for (uint32_t i = 1; i < count; ++i) {
        const float key = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > key) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = key;
    }
    return count == 0 ? 0.0f : values[count / 2];
}

inline uint32_t despikeRing(TerrainSample* ring, uint32_t count, uint32_t ringIndex = 0) {
    if (ring == nullptr || count < 5) return 0;

    const float spikeRise = spikeRiseFor(ringIndex);
    bool outlier[kSpokes]{};
    uint32_t dropped = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (!ring[i].hit) continue;

        float window[5];
        uint32_t gathered = 0;
        for (int32_t offset = -2; offset <= 2; ++offset) {

            const int32_t probe = static_cast<int32_t>(i) + offset;
            if (probe < 0 || probe >= static_cast<int32_t>(count)) continue;
            const uint32_t index = static_cast<uint32_t>(probe);
            if (ring[index].hit) window[gathered++] = ring[index].y;
        }
        if (gathered < 3) continue;

        const float median = medianOfFive(window, gathered);
        const float delta = ring[i].y - median;
        if ((delta < 0.0f ? -delta : delta) > spikeRise) {
            outlier[i] = true;
            ++dropped;
        }
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (outlier[i]) ring[i] = TerrainSample{};
    }
    return dropped;
}

inline void smoothRing(TerrainSample* ring, uint32_t count, uint32_t ringIndex = 0) {
    if (ring == nullptr || count < 3) return;

    const float smoothLimit = smoothLimitFor(ringIndex);
    float original[kSpokes]{};
    for (uint32_t i = 0; i < count; ++i) original[i] = ring[i].y;

    for (uint32_t i = 0; i < count; ++i) {
        if (!ring[i].hit) continue;

        if (i == 0 || i + 1 >= count) continue;
        const uint32_t prev = i - 1;
        const uint32_t next = i + 1;
        if (!ring[prev].hit || !ring[next].hit) continue;

        const float toPrev = original[prev] - original[i];
        const float toNext = original[next] - original[i];
        if ((toPrev < 0.0f ? -toPrev : toPrev) > smoothLimit) continue;
        if ((toNext < 0.0f ? -toNext : toNext) > smoothLimit) continue;

        ring[i].y = original[i] + kSmoothWeight * (toPrev + toNext);
    }
}

inline SegmentClass classifyContinuousStep(const TerrainSample& a,
                                            const TerrainSample& b) {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float rise = b.y - a.y;
    const float absRise = rise < 0.0f ? -rise : rise;
    const float run = std::sqrt(dx * dx + dz * dz);
    SegmentClass byGeometry = SegmentClass::Ground;
    if (run < kVerticalRun) {
        byGeometry = absRise > kVerticalRun ? SegmentClass::Cliff
                                            : SegmentClass::Steep;
    } else {
        byGeometry = classifyBySlope(absRise / run);
    }
    return worseOf(byGeometry,
                   worseOf(classifyByNormal(a.normalY),
                           classifyByNormal(b.normalY)));
}

enum class GroundJoinRule : std::uint8_t {

    Continuous,

    Recovered,
};

inline SegmentClass classifyRecoveredStep(const TerrainSample& a,
                                          const TerrainSample& b) {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float rise = b.y - a.y;
    const float absRise = rise < 0.0f ? -rise : rise;
    const float run = std::sqrt(dx * dx + dz * dz);

    SegmentClass byGeometry = SegmentClass::Ground;
    if (run < kVerticalRun) {
        byGeometry = absRise > kVerticalRun ? SegmentClass::Cliff : SegmentClass::Steep;
    } else if (absRise >= kCliffRise) {
        byGeometry = SegmentClass::Cliff;
    } else {
        byGeometry = classifyBySlope(absRise / run);
    }
    return worseOf(byGeometry,
                   worseOf(classifyByNormal(a.normalY), classifyByNormal(b.normalY)));
}

inline bool groundJoinAdmits(const TerrainSample& a, const TerrainSample& b,
                             GroundJoinRule rule) {
    return rule == GroundJoinRule::Continuous ? surfaceContinuous(a, b)
                                              : linkable(a, b);
}

inline SegmentClass classifyGroundStep(const TerrainSample& a,
                                       const TerrainSample& b,
                                       GroundJoinRule rule) {
    return rule == GroundJoinRule::Continuous ? classifyContinuousStep(a, b)
                                              : classifyRecoveredStep(a, b);
}

inline uint32_t reduceRadialStep(const TerrainSample& near, const TerrainSample& far,
                                 uint32_t outerRing, ScanSegment* out,
                                 GroundJoinRule rule) {
    if (out == nullptr) return 0;
    if (!groundJoinAdmits(near, far, rule)) return 0;

    ScanSegment& segment = *out;
    segment = ScanSegment{};
    segment.surface = classifyGroundStep(near, far, rule);
    const float steepness = segmentSteepness(near, far);
    segment.slopeBand = slopeBandFor(steepness);
    segment.steepness = steepness;
    segment.isArc = false;

    segment.revealWave = waveArrivalOf(outerRing);
    segment.ax = near.x;
    segment.ay = near.y;
    segment.az = near.z;
    segment.bx = far.x;
    segment.by = far.y;
    segment.bz = far.z;
    return 1;
}

inline uint32_t reduceArcStep(const TerrainSample& a, const TerrainSample& b, uint32_t ring,
                              ScanSegment* out, GroundJoinRule rule) {
    if (out == nullptr) return 0;
    if (!groundJoinAdmits(a, b, rule)) return 0;

    const SegmentClass surface = classifyGroundStep(a, b, rule);

    ScanSegment& segment = *out;
    segment = ScanSegment{};
    segment.surface = surface;
    const float steepness = segmentSteepness(a, b);
    segment.slopeBand = slopeBandFor(steepness);
    segment.steepness = steepness;
    segment.isArc = true;
    segment.revealWave = waveArrivalOf(ring);
    segment.ax = a.x;
    segment.ay = a.y;
    segment.az = a.z;
    segment.bx = b.x;
    segment.by = b.y;
    segment.bz = b.z;
    return 1;
}

inline constexpr float kGroundBendFloorMeters = 0.75f;

inline constexpr float kGroundBendSlopeChange = 0.50f;

struct GroundTrack {
    float headY = 0.0f;
    float previousY = 0.0f;
    bool hasPrevious = false;
    bool active = false;
};

inline constexpr float kGroundBendSteepScale = 1.0f;

inline float groundBendAllowance(float spacingMeters, float steepness = 0.0f) {
    const float span = spacingMeters > 0.0f ? spacingMeters : 0.0f;
    const float steep = steepness > 0.0f ? steepness : 0.0f;
    const float slopeChange = steep * kGroundBendSteepScale > kGroundBendSlopeChange
                                  ? steep * kGroundBendSteepScale
                                  : kGroundBendSlopeChange;
    const float scaled = span * slopeChange;
    return scaled > kGroundBendFloorMeters ? scaled : kGroundBendFloorMeters;
}

inline float groundTrackPrediction(const GroundTrack& track) {
    return track.hasPrevious ? track.headY + (track.headY - track.previousY)
                             : track.headY;
}

inline bool groundTrackAdmits(const GroundTrack& track, float nextY,
                              float spacingMeters, float steepness = 0.0f) {
    if (!track.active || !track.hasPrevious) return true;
    if (!std::isfinite(nextY)) return false;
    const float error = nextY - groundTrackPrediction(track);
    const float magnitude = error < 0.0f ? -error : error;
    return magnitude <= groundBendAllowance(spacingMeters, steepness);
}

inline void groundTrackAdvance(GroundTrack& track, float nextY, bool joined) {
    if (joined && track.active) {
        track.previousY = track.headY;
        track.hasPrevious = true;
    } else {
        track.hasPrevious = false;
    }
    track.headY = nextY;
    track.active = true;
}

inline void groundTrackBreak(GroundTrack& track) { track = GroundTrack{}; }

enum class GroundRefusal : std::uint8_t { None = 0, Track = 1, Pairwise = 2 };

inline float groundNormalAgreement(const TerrainSample& a,
                                   const TerrainSample& b) {
    const float aLength = std::sqrt(a.normalX * a.normalX + a.normalY * a.normalY +
                                    a.normalZ * a.normalZ);
    const float bLength = std::sqrt(b.normalX * b.normalX + b.normalY * b.normalY +
                                    b.normalZ * b.normalZ);
    if (!(aLength > 0.001f) || !(bLength > 0.001f)) return 0.0f;
    float dot = (a.normalX * b.normalX + a.normalY * b.normalY +
                 a.normalZ * b.normalZ) /
                (aLength * bLength);
    if (dot < 0.0f) dot = -dot;
    return dot > 1.0f ? 1.0f : dot;
}

inline bool predictGroundPlaneY(const TerrainSample& a,
                                const TerrainSample& b,
                                const TerrainSample& c,
                                const TerrainSample& target,
                                float& predictedY) {
    const float x1 = b.x - a.x;
    const float z1 = b.z - a.z;
    const float y1 = b.y - a.y;
    const float x2 = c.x - a.x;
    const float z2 = c.z - a.z;
    const float y2 = c.y - a.y;
    const float determinant = x1 * z2 - z1 * x2;
    const float absDeterminant = determinant < 0.0f ? -determinant : determinant;
    if (absDeterminant < 0.001f) return false;
    const float slopeX = (y1 * z2 - z1 * y2) / determinant;
    const float slopeZ = (x1 * y2 - y1 * x2) / determinant;
    predictedY = a.y + slopeX * (target.x - a.x) +
                 slopeZ * (target.z - a.z);
    return std::isfinite(predictedY);
}

inline float groundCellResidualAllowance(const TerrainSample& a,
                                         const TerrainSample& b,
                                         const TerrainSample& c,
                                         const TerrainSample& d) {
    const auto span = [](const TerrainSample& p, const TerrainSample& q) {
        const float dx = q.x - p.x;
        const float dz = q.z - p.z;
        return std::sqrt(dx * dx + dz * dz);
    };
    float longest = span(a, b);
    const float spans[] = {span(a, c), span(c, d), span(d, b)};
    for (float value : spans)
        if (value > longest) longest = value;
    float allowance = 0.75f + longest * 0.05f;
    if (allowance > 2.0f) allowance = 2.0f;
    return allowance;
}

inline bool groundCellRecoversEdge(const TerrainSample& a,
                                   const TerrainSample& b,
                                   const TerrainSample& c,
                                   const TerrainSample& d,
                                   GroundRefusal refusal) {
    if (!a.hit || !b.hit || !c.hit || !d.hit) return false;
    if (!surfaceContinuous(a, c) || !surfaceContinuous(c, d) ||
        !surfaceContinuous(d, b)) {
        return false;
    }
    if (refusal == GroundRefusal::Track) return surfaceContinuous(a, b);
    if (refusal != GroundRefusal::Pairwise || !linkable(a, b)) return false;
    if (groundNormalAgreement(a, b) < 0.65f) return false;

    float predictedB = 0.0f;
    float predictedA = 0.0f;
    if (!predictGroundPlaneY(a, c, d, b, predictedB) ||
        !predictGroundPlaneY(b, d, c, a, predictedA)) {
        return false;
    }
    const float residualB = predictedB - b.y;
    const float residualA = predictedA - a.y;
    const float absResidualB = residualB < 0.0f ? -residualB : residualB;
    const float absResidualA = residualA < 0.0f ? -residualA : residualA;
    const float allowance = groundCellResidualAllowance(a, b, c, d);
    return absResidualA <= allowance && absResidualB <= allowance;
}

inline std::uint32_t trackedArcStep(GroundTrack& track, const TerrainSample& a,
                                    const TerrainSample& b, std::uint32_t ring,
                                    ScanSegment* out,
                                    GroundRefusal* why = nullptr,
                                    float rowSpacingMeters = -1.0f) {
    if (why != nullptr) *why = GroundRefusal::None;
    if (!a.hit || !b.hit) {
        groundTrackBreak(track);
        if (b.hit) groundTrackAdvance(track, b.y, false);
        return 0;
    }
    if (!track.active) groundTrackAdvance(track, a.y, false);

    const float rowSpacing =
        rowSpacingMeters > 0.0f ? rowSpacingMeters : ringSpokeSpacing(ring);
    const bool admits =
        groundTrackAdmits(track, b.y, rowSpacing, segmentSteepness(a, b));
    const std::uint32_t written =
        admits ? reduceArcStep(a, b, ring, out, GroundJoinRule::Continuous) : 0u;
    if (why != nullptr && written == 0u)
        *why = admits ? GroundRefusal::Pairwise : GroundRefusal::Track;
    groundTrackAdvance(track, b.y, written != 0u);
    return written;
}

inline std::uint32_t trackedRadialStep(GroundTrack& track,
                                       const TerrainSample& near,
                                       const TerrainSample& far,
                                       std::uint32_t ring, ScanSegment* out,
                                       GroundRefusal* why = nullptr) {
    if (why != nullptr) *why = GroundRefusal::None;
    if (!near.hit || !far.hit) {
        groundTrackBreak(track);
        if (far.hit) groundTrackAdvance(track, far.y, false);
        return 0;
    }
    if (!track.active) groundTrackAdvance(track, near.y, false);

    const bool admits = groundTrackAdmits(track, far.y, ringSpacing(ring),
                                         segmentSteepness(near, far));
    const std::uint32_t written =
        admits ? reduceRadialStep(near, far, ring, out, GroundJoinRule::Continuous)
               : 0u;
    if (why != nullptr && written == 0u)
        *why = admits ? GroundRefusal::Pairwise : GroundRefusal::Track;
    groundTrackAdvance(track, far.y, written != 0u);
    return written;
}

inline constexpr float kCrestWidthRings = 2.2f;
inline constexpr float kCrestLiftMeters = 0.45f;

inline constexpr float kCrestProminenceMeters = 1.5f;
inline constexpr float kCrestProminenceSpacingScale = 0.10f;

inline float crestProminenceFor(float radialSpacingMeters) {
    const float scaled = radialSpacingMeters * kCrestProminenceSpacingScale;
    return scaled > kCrestProminenceMeters ? scaled : kCrestProminenceMeters;
}

inline bool groundRidgePoint(const TerrainSample& inner,
                             const TerrainSample& mid,
                             const TerrainSample& outer,
                             float radialSpacingMeters) {
    if (!inner.hit || !mid.hit || !outer.hit) return false;
    const float prominence = crestProminenceFor(radialSpacingMeters);
    return (mid.y - inner.y) >= prominence && (mid.y - outer.y) >= prominence;
}

inline constexpr float kGroundClearanceMeters = 0.25f;

inline constexpr uint32_t kScanLifetimeTicks = 331;

inline bool scanExpired(uint32_t tick) { return tick >= kScanLifetimeTicks; }

inline constexpr uint32_t kRadialSpokeStride = 1;

inline constexpr uint32_t kWorstCaseArcs = kSpokes * kRings;
inline constexpr uint32_t kWorstCaseRibs = (kSpokes / kRadialSpokeStride) * (kRings - 1);

inline constexpr uint32_t kMaxGroundSegments =
    kWorstCaseArcs + kWorstCaseRibs + 112;

inline constexpr uint32_t kMaxDrawnSegments = 23000;
static_assert(kMaxDrawnSegments <= kMaxGroundSegments + kMaxWallSegments,
              "the compose bound may not exceed what the producers can stage");
static_assert(kWorstCaseArcs + kWorstCaseRibs <= kMaxDrawnSegments,
              "the 1x control lattice must never need dropping");

struct DrawSelection {
    uint32_t drawn = 0;
    uint32_t dropped = 0;
};

inline DrawSelection selectForDraw(uint32_t candidateCount, uint32_t cap = kMaxDrawnSegments) {
    DrawSelection selection{};
    selection.drawn = candidateCount < cap ? candidateCount : cap;
    selection.dropped = candidateCount - selection.drawn;
    return selection;
}

}
