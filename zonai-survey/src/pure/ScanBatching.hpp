// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cmath>
#include <cstdint>

#include "ScanReduction.hpp"
#include "totk/render/PrimitiveGeometry.hpp"
#include "SurveyPalette.hpp"
#include "SurveySweep.hpp"

namespace zonai_survey::pure {

inline constexpr float kArcWidth = 1.8f;
inline constexpr float kRibWidth = 1.1f;
inline constexpr float kAlphaCutoff = 0.03f;

inline float segmentWidth(bool isArc) {
    return (isArc ? kArcWidth : kRibWidth) * kSurveyWidthGain;
}

inline float surveyPassAlpha(bool crestline, float revealWave,
                             std::uint32_t tick) {
    return crestline ? sweepCrestlineAlpha(revealWave, tick)
                     : sweepAlpha(revealWave, tick);
}

inline bool drawableSegment(const ScanSegment& segment) {
    const bool finite = std::isfinite(segment.ax) && std::isfinite(segment.ay) &&
                        std::isfinite(segment.az) && std::isfinite(segment.bx) &&
                        std::isfinite(segment.by) && std::isfinite(segment.bz) &&
                        std::isfinite(segment.revealWave);
    if (!finite) return false;
    const float dx = segment.bx - segment.ax;
    const float dy = segment.by - segment.ay;
    const float dz = segment.bz - segment.az;
    const float lengthSquared = dx * dx + dy * dy + dz * dz;
    return lengthSquared > 0.0025f && lengthSquared < 40000.0f;
}

inline bool segmentPasses(const ScanSegment& segment, std::uint32_t tick) {
    if (!drawableSegment(segment)) return false;
    if (!surveyShows(segment)) return false;
    return surveyPassAlpha(segment.crestline, segment.revealWave, tick) >
           kAlphaCutoff;
}

inline bool segmentStaticVisible(const ScanSegment& segment) {
    return drawableSegment(segment) && surveyShows(segment);
}

inline bool groupTickPasses(float revealWave, std::uint32_t tick,
                            bool crestline = false) {

    return surveyPassAlpha(crestline, revealWave, tick) > kAlphaCutoff;
}

constexpr bool isCliffVertical(const ScanSegment& segment) {
    return !segment.isArc && originIsWall(segment.origin);
}

inline constexpr float kCliffVerticalAlphaScale = 0.90f;

struct BatchKey {
    float revealWave = -1.0f;
    SlopeLane lane = SlopeLane::Cool;
    bool isArc = false;

    bool cliffVertical = false;

    bool crestline = false;

    [[nodiscard]] constexpr bool sameAs(const BatchKey& other) const {
        return revealWave == other.revealWave && lane == other.lane &&
               isArc == other.isArc && crestline == other.crestline &&
               cliffVertical == other.cliffVertical;
    }
};

constexpr BatchKey batchKeyFor(const ScanSegment& segment) {
    return {segment.revealWave, segment.lane, segment.isArc,
            isCliffVertical(segment), segment.crestline};
}

inline constexpr std::uint32_t kBatchWaveSteps = (kRings + 1u) * kRevealSteps;

inline constexpr std::uint32_t kBatchKindCount = 4u;
inline constexpr std::uint32_t kBatchBucketCount =
    kBatchWaveSteps * kBatchKindCount * kSlopeLaneCount;

constexpr std::uint32_t batchKindOf(bool isArc, bool crestline,
                                    bool cliffVertical) {
    if (crestline) return 2u;
    if (isArc) return 1u;
    return cliffVertical ? 3u : 0u;
}

inline std::uint32_t batchBucket(const ScanSegment& segment) {
    const float wave = segment.revealWave;
    const float steps = wave * static_cast<float>(kRevealSteps);
    std::uint32_t step = steps > 0.0f
                             ? static_cast<std::uint32_t>(steps + 0.5f)
                             : 0u;
    if (step >= kBatchWaveSteps) step = kBatchWaveSteps - 1u;
    const std::uint32_t kind = batchKindOf(segment.isArc, segment.crestline,
                                          isCliffVertical(segment));
    return (step * kBatchKindCount + kind) * kSlopeLaneCount +
           laneIndex(segment.lane);
}

struct SegmentGroup {
    BatchKey key{};
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint32_t drawn = 0;
};

template <typename Emit>
inline std::uint32_t planGroups(const ScanSegment* segments, std::uint32_t count,
                                std::uint32_t tick, Emit&& emit) {
    if (segments == nullptr || count == 0) return 0;

    std::uint32_t emitted = 0;
    SegmentGroup open{};
    bool grouping = false;

    const auto flush = [&]() {
        if (!grouping) return;
        if (open.drawn != 0) {
            emit(static_cast<const SegmentGroup&>(open));
            ++emitted;
        }
        grouping = false;
    };

    for (std::uint32_t i = 0; i < count; ++i) {
        const ScanSegment& segment = segments[i];
        const BatchKey key = batchKeyFor(segment);
        if (!grouping || !key.sameAs(open.key)) {
            flush();
            open = SegmentGroup{key, i, 0, 0};
            grouping = true;
        }
        ++open.count;
        if (segmentPasses(segment, tick)) ++open.drawn;
    }

    flush();
    return emitted;
}

inline bool framePackedForBatching(const ScanSegment* segments,
                                   std::uint32_t count) {
    if (segments == nullptr || count < 2) return true;
    for (std::uint32_t i = 1; i < count; ++i) {
        const ScanSegment& previous = segments[i - 1u];
        const ScanSegment& current = segments[i];
        if (current.revealWave > previous.revealWave) continue;
        if (current.revealWave < previous.revealWave) return false;
        const std::uint32_t previousKind = batchKindOf(
            previous.isArc, previous.crestline, isCliffVertical(previous));
        const std::uint32_t currentKind = batchKindOf(
            current.isArc, current.crestline, isCliffVertical(current));
        if (currentKind > previousKind) continue;
        if (currentKind < previousKind) return false;

        if (laneIndex(current.lane) < laneIndex(previous.lane)) return false;
    }
    return true;
}

struct GroupSpan {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

inline std::uint32_t buildGroupSpans(const ScanSegment* segments,
                                     std::uint32_t count, GroupSpan* out,
                                     std::uint32_t capacity) {
    if (segments == nullptr || count == 0 || out == nullptr || capacity == 0)
        return 0;
    std::uint32_t written = 0;
    std::uint32_t first = 0;
    BatchKey open = batchKeyFor(segments[0]);
    for (std::uint32_t i = 1; i < count; ++i) {
        const BatchKey key = batchKeyFor(segments[i]);
        if (key.sameAs(open)) continue;
        if (written < capacity) out[written] = {first, i - first};
        ++written;
        first = i;
        open = key;
    }
    if (written < capacity) out[written] = {first, count - first};
    ++written;
    return written < capacity ? written : capacity;
}

inline constexpr std::uint32_t kRingVertexBytes = 36;
inline constexpr std::uint32_t kRingBlockBytes = 256;

inline constexpr std::uint32_t kRingFramesInFlight = 4;

constexpr std::uint32_t roundToRingBlock(std::uint32_t bytes) {
    return ((bytes + kRingBlockBytes - 1u) / kRingBlockBytes) * kRingBlockBytes;
}

constexpr std::uint32_t surveyRingBytesPerPass(std::uint32_t segments,
                                               std::uint32_t groups) {
    return roundToRingBlock(segments * 2u * kRingVertexBytes) + groups * kRingBlockBytes;
}

constexpr std::uint32_t surveyBeamRingBytesPerFrame(std::uint32_t segments,
                                                    std::uint32_t groups) {
    return roundToRingBlock(segments * totk::render::kBeamStripBudgetVertices * kRingVertexBytes) +
           groups * kRingBlockBytes;
}

inline constexpr std::uint32_t kMaxSurveyGroups = kBatchBucketCount;

inline constexpr std::uint32_t kSurveySharedSlackBytes = 4u * 1024u * 1024u;
inline constexpr std::uint32_t kSurveyRequiredRingBytes =
    kRingFramesInFlight *
    surveyBeamRingBytesPerFrame(kMaxDrawnSegments, kMaxSurveyGroups);
inline constexpr std::uint32_t kSurveyRingBytes = roundToRingBlock(
    kSurveyRequiredRingBytes + kSurveySharedSlackBytes);

static_assert(kSurveyRingBytes >= kSurveyRequiredRingBytes,
              "every in-flight Survey frame must fit before shared slack");
static_assert(kSurveyRingBytes % kRingBlockBytes == 0,
              "the drawer's allocator only deals in whole blocks");

static_assert(kSurveyRingBytes <= 48u * 1024u * 1024u,
              "the overlay refuses anything past its sane ceiling");

}
