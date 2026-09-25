// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "ScanReduction.hpp"
#include "WallSurvey.hpp"

namespace zonai_survey::pure {

enum class WallEdgeAxis : std::uint8_t {
    Across,
    Vertical,
};

inline constexpr float kWallMinJoinMeters = 3.0f;

inline constexpr float kWallMinViewCosine = 0.30f;
inline constexpr float kCleanWallMinDepthAllowance = 2.0f;
inline constexpr float kCleanWallDepthScale = 1.25f;
inline constexpr float kCleanWallMinNormalDot = 0.50f;
inline constexpr float kCleanWallMaxNormalChordDot = 0.55f;

inline constexpr float kWallBendFloorMeters = 0.75f;
inline constexpr float kWallBendScale = 0.85f;

inline bool acceptedWallSample(const TerrainSample& sample) {
    return sample.hit && wallNormalIsSteep(sample.normalY) &&
           std::isfinite(sample.distance);
}

inline float wallSampleSpacing(const TerrainSample& sample,
                               WallEdgeAxis axis) {
    const float angle = axis == WallEdgeAxis::Across ? wallAzimuthStep()
                                                     : wallElevationStep();
    return sample.distance * angle;
}

inline float wallViewCosine(const TerrainSample& a, const TerrainSample& b) {
    float cosine = a.viewCosine < b.viewCosine ? a.viewCosine : b.viewCosine;
    if (!std::isfinite(cosine) || cosine > 1.0f) cosine = 1.0f;
    return cosine > kWallMinViewCosine ? cosine : kWallMinViewCosine;
}

inline float wallViewTangent(const TerrainSample& a, const TerrainSample& b) {
    const float cosine = wallViewCosine(a, b);
    const float sineSquared = 1.0f - cosine * cosine;
    const float sine = sineSquared > 0.0f ? std::sqrt(sineSquared) : 0.0f;
    return sine / cosine;
}

inline float wallJoinAllowanceBase(const TerrainSample& a,
                                   const TerrainSample& b,
                                   WallEdgeAxis axis,
                                   float angleScale = 1.0f) {
    const float angle = axis == WallEdgeAxis::Across
                            ? wallAzimuthStep()
                            : wallElevationStep();
    const float meanDistance = 0.5f * (a.distance + b.distance);

    const float scaled = meanDistance * angle * angleScale * kWallJoinScale /
                         wallViewCosine(a, b);
    return scaled > kWallMinJoinMeters ? scaled : kWallMinJoinMeters;
}

inline float wallJoinAllowance(const TerrainSample& a, const TerrainSample& b,
                               WallEdgeAxis axis,
                               float angleScale = 1.0f) {
    const float base = wallJoinAllowanceBase(a, b, axis, angleScale);
    if (axis != WallEdgeAxis::Vertical) return base;
    return base * kWallVerticalJoinReach;
}

inline float wallJoinOvershoot(const TerrainSample& a, const TerrainSample& b,
                               WallEdgeAxis axis,
                               float angleScale = 1.0f) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float separation = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float base = wallJoinAllowanceBase(a, b, axis, angleScale);
    if (!std::isfinite(separation) || !std::isfinite(base) || base <= 0.0f)
        return std::numeric_limits<float>::infinity();
    return separation / base;
}

inline bool wallNormalsContinuous(const TerrainSample& a,
                                  const TerrainSample& b) {
    const float aLengthSquared = a.normalX * a.normalX + a.normalY * a.normalY +
                                 a.normalZ * a.normalZ;
    const float bLengthSquared = b.normalX * b.normalX + b.normalY * b.normalY +
                                 b.normalZ * b.normalZ;
    if (!std::isfinite(aLengthSquared) || !std::isfinite(bLengthSquared) ||
        aLengthSquared < 0.25f || bLengthSquared < 0.25f)
        return false;
    const float dot = a.normalX * b.normalX + a.normalY * b.normalY +
                      a.normalZ * b.normalZ;
    const float normalized = dot / std::sqrt(aLengthSquared * bLengthSquared);
    return std::isfinite(normalized) &&
           normalized >= kCleanWallMinNormalDot;
}

inline bool wallChordFollowsSurface(const TerrainSample& a,
                                    const TerrainSample& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float chordLengthSquared = dx * dx + dy * dy + dz * dz;
    const float aLengthSquared = a.normalX * a.normalX + a.normalY * a.normalY +
                                 a.normalZ * a.normalZ;
    const float bLengthSquared = b.normalX * b.normalX + b.normalY * b.normalY +
                                 b.normalZ * b.normalZ;
    if (!std::isfinite(chordLengthSquared) ||
        !std::isfinite(aLengthSquared) || !std::isfinite(bLengthSquared) ||
        chordLengthSquared < 0.0025f || aLengthSquared < 0.25f ||
        bLengthSquared < 0.25f)
        return false;
    const float aProjection =
        std::fabs(a.normalX * dx + a.normalY * dy + a.normalZ * dz) /
        std::sqrt(aLengthSquared * chordLengthSquared);
    const float bProjection =
        std::fabs(b.normalX * dx + b.normalY * dy + b.normalZ * dz) /
        std::sqrt(bLengthSquared * chordLengthSquared);
    return std::isfinite(aProjection) && std::isfinite(bProjection) &&
           aProjection <= kCleanWallMaxNormalChordDot &&
           bProjection <= kCleanWallMaxNormalChordDot;
}

enum class WallJoinFailure : std::uint8_t {
    None = 0,
    MissingSample,
    Separation,
    Depth,
    Normal,
    Chord,
};

inline float wallDepthAllowance(const TerrainSample& a,
                                const TerrainSample& b,
                                WallEdgeAxis axis,
                                float angleScale = 1.0f) {
    const float angle = axis == WallEdgeAxis::Across
                            ? wallAzimuthStep()
                            : wallElevationStep();
    const float meanDistance = 0.5f * (a.distance + b.distance);
    const float expectedCell = meanDistance * angle * angleScale;

    const float scaledDepth =
        expectedCell * (kCleanWallDepthScale + wallViewTangent(a, b));
    return scaledDepth > kCleanWallMinDepthAllowance
               ? scaledDepth
               : kCleanWallMinDepthAllowance;
}

inline bool wallDepthContinuous(const TerrainSample& a,
                                const TerrainSample& b,
                                WallEdgeAxis axis,
                                float angleScale = 1.0f) {
    if (!acceptedWallSample(a) || !acceptedWallSample(b)) return false;
    const float allowance =
        wallDepthAllowance(a, b, axis, angleScale);
    const float depthDelta = a.distance - b.distance;
    const float magnitude = depthDelta < 0.0f ? -depthDelta : depthDelta;
    return std::isfinite(allowance) && std::isfinite(magnitude) &&
           magnitude <= allowance;
}

inline WallJoinFailure wallJoinFailure(const TerrainSample& a,
                                       const TerrainSample& b,
                                       WallEdgeAxis axis,
                                       float angleScale = 1.0f) {
    if (!acceptedWallSample(a) || !acceptedWallSample(b))
        return WallJoinFailure::MissingSample;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float separation = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(separation) ||
        separation > wallJoinAllowance(a, b, axis, angleScale))
        return WallJoinFailure::Separation;

    if (!wallDepthContinuous(a, b, axis, angleScale))
        return WallJoinFailure::Depth;
    if (!wallNormalsContinuous(a, b)) return WallJoinFailure::Normal;
    if (!wallChordFollowsSurface(a, b)) return WallJoinFailure::Chord;
    return WallJoinFailure::None;
}

inline bool wallLinkable(const TerrainSample& a, const TerrainSample& b,
                         WallEdgeAxis axis,
                         float angleScale = 1.0f) {
    return wallJoinFailure(a, b, axis, angleScale) ==
           WallJoinFailure::None;
}

inline float wallRevealWave(const TerrainSample& a, const TerrainSample& b) {

    const float distance = a.distance > b.distance ? a.distance : b.distance;
    float wave = std::ceil(waveCoordForRadius(distance));
    if (wave < 1.0f) wave = 1.0f;
    if (wave > static_cast<float>(kRings)) wave = static_cast<float>(kRings);
    return wave;
}

inline std::uint32_t reduceWallEdge(const TerrainSample& a,
                                    const TerrainSample& b,
                                    WallEdgeAxis axis, ScanSegment* out,
                                    float angleScale = 1.0f) {
    if (out == nullptr || !wallLinkable(a, b, axis, angleScale))
        return 0;
    *out = {};
    out->ax = a.x;
    out->ay = a.y;
    out->az = a.z;
    out->bx = b.x;
    out->by = b.y;
    out->bz = b.z;
    out->revealWave = wallRevealWave(a, b);
    out->surface = SegmentClass::Cliff;
    const float steepness = segmentSteepness(a, b);
    out->slopeBand = slopeBandFor(steepness);
    out->steepness = steepness;
    out->isArc = axis == WallEdgeAxis::Across;
    return 1;
}

inline std::uint32_t reduceShallowAcross(const TerrainSample& a,
                                         const TerrainSample& b,
                                         ScanSegment* out) {
    if (out == nullptr || !a.hit || !b.hit) return 0;
    if (!std::isfinite(a.distance) || !std::isfinite(b.distance)) return 0;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float separation = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(separation) ||
        separation > wallJoinAllowance(a, b, WallEdgeAxis::Across))
        return 0;
    if (!surfaceContinuous(a, b)) return 0;
    *out = {};
    out->ax = a.x;
    out->ay = a.y;
    out->az = a.z;
    out->bx = b.x;
    out->by = b.y;
    out->bz = b.z;
    out->revealWave = wallRevealWave(a, b);
    out->surface = classifyContinuousStep(a, b);
    const float steepness = segmentSteepness(a, b);
    out->slopeBand = slopeBandFor(steepness);
    out->steepness = steepness;
    out->isArc = true;
    out->origin = SegmentOrigin::ShallowGround;
    return 1;
}

inline constexpr std::uint32_t kCrestTopRowTolerance = 2;

struct WallTrack {
    TerrainSample head{};
    float previous = 0.0f;
    bool hasPrevious = false;
    bool active = false;
};

inline float wallTrackPrediction(const WallTrack& track) {
    if (!track.hasPrevious) return track.head.distance;
    return track.head.distance + (track.head.distance - track.previous);
}

inline float wallBendAllowance(const WallTrack& track, WallEdgeAxis axis) {
    const float angle = axis == WallEdgeAxis::Across
                            ? wallAzimuthStep()
                            : wallElevationStep();
    const float scaled = track.head.distance * angle * kWallBendScale;
    return scaled > kWallBendFloorMeters ? scaled : kWallBendFloorMeters;
}

inline bool wallTrackAdmits(const WallTrack& track, const TerrainSample& next,
                            WallEdgeAxis axis) {
    if (!track.active) return false;
    if (!wallLinkable(track.head, next, axis)) return false;

    if (!track.hasPrevious) return true;
    const float bend = next.distance - wallTrackPrediction(track);
    const float magnitude = bend < 0.0f ? -bend : bend;
    return std::isfinite(magnitude) &&
           magnitude <= wallBendAllowance(track, axis);
}

struct WallTrackSet {
    WallTrack tracks[kWallDepthLayers]{};
    std::uint32_t count = 0;
};

enum class WallTrackRejection : std::uint8_t {
    JoinSeparation = 0,
    JoinDepth,
    JoinNormal,
    JoinChord,
    Bend,
    Assignment,
};

using WallTrackRejectFn = void (*)(void* context, const TerrainSample& from,
                                   const TerrainSample& to,
                                   WallTrackRejection reason);

struct WallTrackObserver {
    void* context = nullptr;
    WallTrackRejectFn rejected = nullptr;
};

inline std::uint32_t advanceWallTracks(WallTrackSet& set,
                                       const TerrainSample* samples,
                                       std::uint32_t sampleCount,
                                       WallEdgeAxis axis, ScanSegment* out,
                                       std::uint32_t capacity,
                                       WallTrackObserver observer = {}) {
    if (samples == nullptr) sampleCount = 0;
    if (sampleCount > kWallDepthLayers) sampleCount = kWallDepthLayers;

    std::uint8_t order[kWallDepthLayers]{};
    std::uint32_t accepted = 0;
    for (std::uint32_t i = 0; i < sampleCount; ++i)
        if (acceptedWallSample(samples[i]))
            order[accepted++] = static_cast<std::uint8_t>(i);
    for (std::uint32_t i = 1; i < accepted; ++i) {
        const std::uint8_t key = order[i];
        std::uint32_t j = i;
        while (j > 0 &&
               samples[order[j - 1u]].distance > samples[key].distance) {
            order[j] = order[j - 1u];
            --j;
        }
        order[j] = key;
    }

    const std::uint32_t trackCount =
        set.count < kWallDepthLayers ? set.count : kWallDepthLayers;

    struct Cell {
        std::uint8_t count = 0;
        std::uint8_t choice = 0;
        float score = 0.0f;
    };
    Cell table[kWallDepthLayers + 1u][kWallDepthLayers + 1u]{};
    for (int ti = static_cast<int>(trackCount) - 1; ti >= 0; --ti) {
        for (int si = static_cast<int>(accepted) - 1; si >= 0; --si) {
            Cell best = table[ti + 1][si];
            best.choice = 0;
            const Cell& skipSample = table[ti][si + 1];
            if (skipSample.count > best.count ||
                (skipSample.count == best.count &&
                 skipSample.score < best.score)) {
                best = skipSample;
                best.choice = 1;
            }

            const WallTrack& track = set.tracks[static_cast<std::uint32_t>(ti)];
            const TerrainSample& sample =
                samples[order[static_cast<std::uint32_t>(si)]];
            if (wallTrackAdmits(track, sample, axis)) {
                const Cell& rest = table[ti + 1][si + 1];
                const float error = sample.distance - wallTrackPrediction(track);
                Cell joined{};
                joined.count = static_cast<std::uint8_t>(rest.count + 1u);
                joined.score = rest.score + (error < 0.0f ? -error : error);
                joined.choice = 2;
                if (joined.count > best.count ||
                    (joined.count == best.count && joined.score < best.score)) {
                    best = joined;
                }
            }
            table[ti][si] = best;
        }
    }

    WallTrackSet next{};
    bool trackUsed[kWallDepthLayers]{};
    bool sampleUsed[kWallDepthLayers]{};
    std::uint32_t written = 0;
    std::uint32_t ti = 0;
    std::uint32_t si = 0;
    while (ti < trackCount && si < accepted) {
        const std::uint8_t choice = table[ti][si].choice;
        if (choice == 2) {
            const WallTrack& track = set.tracks[ti];
            const std::uint32_t sampleIndex = order[si];
            const TerrainSample& sample = samples[sampleIndex];
            if (out != nullptr && written < capacity &&
                reduceWallEdge(track.head, sample, axis,
                               &out[written]) != 0) {

                out[written].origin = wallOriginFor(next.count);

                out[written].buried =
                    track.head.buried && sample.buried;
                ++written;
            }
            WallTrack& extended = next.tracks[next.count++];
            extended.head = sample;
            extended.previous = track.head.distance;
            extended.hasPrevious = true;
            extended.active = true;
            trackUsed[ti] = true;
            sampleUsed[sampleIndex] = true;
            ++ti;
            ++si;
        } else if (choice == 0) {
            ++ti;
        } else {
            ++si;
        }
    }

    if (observer.rejected != nullptr) {
        for (std::uint32_t trackIndex = 0; trackIndex < trackCount;
             ++trackIndex) {
            if (trackUsed[trackIndex]) continue;
            const WallTrack& track = set.tracks[trackIndex];
            const TerrainSample* candidate = nullptr;
            WallTrackRejection reason = WallTrackRejection::JoinSeparation;
            std::uint32_t bestRank = 3u;
            float bestError = 0.0f;
            for (std::uint32_t candidateIndex = 0; candidateIndex < accepted;
                 ++candidateIndex) {
                const TerrainSample& sample = samples[order[candidateIndex]];
                const WallJoinFailure failure =
                    wallJoinFailure(track.head, sample, axis);
                std::uint32_t rank = 2u;
                WallTrackRejection candidateReason =
                    WallTrackRejection::JoinSeparation;
                if (failure == WallJoinFailure::None) {
                    const bool admitted =
                        wallTrackAdmits(track, sample, axis);
                    rank = admitted ? 0u : 1u;
                    candidateReason = admitted
                                          ? WallTrackRejection::Assignment
                                          : WallTrackRejection::Bend;
                } else {
                    switch (failure) {
                        case WallJoinFailure::Depth:
                            candidateReason = WallTrackRejection::JoinDepth;
                            break;
                        case WallJoinFailure::Normal:
                            candidateReason = WallTrackRejection::JoinNormal;
                            break;
                        case WallJoinFailure::Chord:
                            candidateReason = WallTrackRejection::JoinChord;
                            break;
                        case WallJoinFailure::MissingSample:
                        case WallJoinFailure::Separation:
                        case WallJoinFailure::None:
                            candidateReason =
                                WallTrackRejection::JoinSeparation;
                            break;
                    }
                }
                const float delta =
                    sample.distance - wallTrackPrediction(track);
                const float error = delta < 0.0f ? -delta : delta;
                if (candidate == nullptr || rank < bestRank ||
                    (rank == bestRank && error < bestError)) {
                    candidate = &sample;
                    reason = candidateReason;
                    bestRank = rank;
                    bestError = error;
                }
            }
            if (candidate != nullptr)
                observer.rejected(observer.context, track.head, *candidate,
                                  reason);
        }
    }

    for (std::uint32_t i = 0; i < accepted; ++i) {
        const std::uint32_t sampleIndex = order[i];
        if (sampleUsed[sampleIndex] || next.count >= kWallDepthLayers) continue;
        WallTrack& fresh = next.tracks[next.count++];
        fresh.head = samples[sampleIndex];
        fresh.previous = 0.0f;
        fresh.hasPrevious = false;
        fresh.active = true;
    }

    for (std::uint32_t i = 1; i < next.count; ++i) {
        const WallTrack key = next.tracks[i];
        std::uint32_t j = i;
        while (j > 0 && next.tracks[j - 1u].head.distance > key.head.distance) {
            next.tracks[j] = next.tracks[j - 1u];
            --j;
        }
        next.tracks[j] = key;
    }

    set = next;
    return written;
}

inline std::uint32_t reduceWallLayers(
    const TerrainSample* a, std::uint32_t aCount, const TerrainSample* b,
    std::uint32_t bCount, WallEdgeAxis axis,
    ScanSegment* out, std::uint32_t capacity) {
    if (!a || !b || !out || capacity == 0) return 0;
    WallTrackSet set{};
    advanceWallTracks(set, a, aCount, axis, nullptr, 0);
    return advanceWallTracks(set, b, bCount, axis, out, capacity);
}

inline constexpr std::uint32_t kBuriedSearchWindow = 2;
inline constexpr float kBuriedMinToleranceMeters = 3.0f;
inline constexpr float kBuriedToleranceCells = 2.0f;

inline float buriedTolerance(float distance) {
    const float azimuth = wallAzimuthStep();
    const float elevation = wallElevationStep();
    const float angle = azimuth > elevation ? azimuth : elevation;
    const float scaled = distance * angle * kBuriedToleranceCells;
    return scaled > kBuriedMinToleranceMeters ? scaled : kBuriedMinToleranceMeters;
}

inline bool buriedSampleSurfaces(const TerrainSample& deep,
                                 const TerrainSample& nearest) {
    if (!acceptedWallSample(nearest)) return false;
    const float dx = nearest.x - deep.x;
    const float dy = nearest.y - deep.y;
    const float dz = nearest.z - deep.z;
    const float separation = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(separation) ||
        separation > buriedTolerance(deep.distance))
        return false;
    return wallNormalsContinuous(deep, nearest) &&
           wallChordFollowsSurface(deep, nearest);
}

inline void judgeBuriedSamples(TerrainSample* samples, const std::uint8_t* counts,
                               std::uint32_t surfacedOut[kWallDepthLayers],
                               std::uint32_t buriedOut[kWallDepthLayers]) {
    for (std::uint32_t layer = 0; layer < kWallDepthLayers; ++layer) {
        surfacedOut[layer] = 0;
        buriedOut[layer] = 0;
    }
    if (samples == nullptr || counts == nullptr || kWallColumns == 0 ||
        kWallRows == 0)
        return;

    const auto nearestOf = [&](std::uint32_t probe) -> const TerrainSample* {
        const std::uint32_t stored = counts[probe];
        for (std::uint32_t layer = 0; layer < stored && layer < kWallDepthLayers;
             ++layer) {
            const TerrainSample& sample = samples[wallSampleIndex(probe, layer)];
            if (acceptedWallSample(sample)) return &sample;
        }
        return nullptr;
    };

    for (std::uint32_t row = 0; row < kWallRows; ++row) {
        for (std::uint32_t column = 0; column < kWallColumns; ++column) {
            const std::uint32_t probe = row * kWallColumns + column;
            const std::uint32_t stored = counts[probe];
            for (std::uint32_t layer = 0;
                 layer < stored && layer < kWallDepthLayers; ++layer) {
                TerrainSample& sample = samples[wallSampleIndex(probe, layer)];
                if (!acceptedWallSample(sample)) continue;
                if (layer == 0) {

                    sample.buried = false;
                    ++surfacedOut[0];
                    continue;
                }

                bool surfaces = false;
                const std::uint32_t rowFrom =
                    row > kBuriedSearchWindow ? row - kBuriedSearchWindow : 0u;
                const std::uint32_t rowTo =
                    row + kBuriedSearchWindow < kWallRows - 1u
                        ? row + kBuriedSearchWindow
                        : kWallRows - 1u;
                const std::uint32_t colFrom =
                    column > kBuriedSearchWindow ? column - kBuriedSearchWindow
                                                 : 0u;
                const std::uint32_t colTo =
                    column + kBuriedSearchWindow < kWallColumns - 1u
                        ? column + kBuriedSearchWindow
                        : kWallColumns - 1u;
                for (std::uint32_t r = rowFrom; r <= rowTo && !surfaces; ++r) {
                    for (std::uint32_t c = colFrom; c <= colTo && !surfaces;
                         ++c) {
                        const std::uint32_t neighbour = r * kWallColumns + c;
                        if (neighbour == probe) continue;
                        const TerrainSample* nearest = nearestOf(neighbour);
                        if (nearest != nullptr &&
                            buriedSampleSurfaces(sample, *nearest))
                            surfaces = true;
                    }
                }
                sample.buried = !surfaces;
                if (surfaces)
                    ++surfacedOut[layer];
                else
                    ++buriedOut[layer];
            }
        }
    }
}

}
