// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>
#include <cstdint>

#include "PulseLattice.hpp"

namespace zonai_survey::pure {

inline constexpr std::uint32_t kWallColumns = 64;
inline constexpr std::uint32_t kWallRows = 64;
inline constexpr std::uint32_t kWallProbes = kWallColumns * kWallRows;

inline constexpr std::uint32_t kWallDepthLayers = 3;
inline constexpr std::uint32_t kWallSamples = kWallProbes * kWallDepthLayers;

inline constexpr std::uint32_t kWallMaxRaycastsPerProbe = 7;

inline constexpr float kWallJoinScale = 1.75f;

inline constexpr float kWallVerticalJoinReach = 2.0f;

inline constexpr float kWallSteepLimit = 0.85f;

inline bool wallNormalIsSteep(float normalY) {
    if (!std::isfinite(normalY)) return false;
    const float magnitude = normalY < 0.0f ? -normalY : normalY;
    return magnitude <= kWallSteepLimit;
}

inline constexpr float kWallMinElevation = -45.0f * kPi / 180.0f;
inline constexpr float kWallMaxElevation = 55.0f * kPi / 180.0f;
inline constexpr float kWallEyeHeight = 1.6f;
inline constexpr float kWallRayLength = kMaxRange + 8.0f;

inline constexpr std::uint32_t kWallProbesPerService = 192;

inline constexpr float kWallRayAdvanceMeters = 0.75f;
inline constexpr float kWallMinHitTravelMeters = 0.20f;
inline constexpr float kWallMinLayerSeparationMeters = 1.5f;

inline constexpr std::uint32_t kMaxWallSegments = 20000;

inline constexpr float kShallowRedundancyMeters = 2.5f;

inline constexpr float kWallSubmergedMeters = 4.0f;

constexpr std::uint32_t wallProbeRow(std::uint32_t index) {
    return index / kWallColumns;
}

constexpr std::uint32_t wallProbeColumn(std::uint32_t index) {
    return index % kWallColumns;
}

constexpr std::uint32_t wallSampleIndex(std::uint32_t probe,
                                        std::uint32_t layer) {
    return probe * kWallDepthLayers + layer;
}

constexpr float wallAzimuth(std::uint32_t column) {
    const float t = static_cast<float>(column) /
                    static_cast<float>(kWallColumns - 1u);
    return (t - 0.5f) * kConeRadians;
}

constexpr float wallElevation(std::uint32_t row) {
    const float t = static_cast<float>(row) /
                    static_cast<float>(kWallRows - 1u);
    return kWallMinElevation + t * (kWallMaxElevation - kWallMinElevation);
}

constexpr float wallAzimuthStep() {
    return kConeRadians / static_cast<float>(kWallColumns - 1u);
}

constexpr float wallElevationStep() {
    return (kWallMaxElevation - kWallMinElevation) /
           static_cast<float>(kWallRows - 1u);
}

struct WallRay {
    float fromX = 0.0f, fromY = 0.0f, fromZ = 0.0f;
    float toX = 0.0f, toY = 0.0f, toZ = 0.0f;
};

inline WallRay wallRayForAngles(float azimuth, float elevation,
                                float linkX, float linkY, float linkZ,
                                float headingX, float headingZ) {
    const float azimuthCos = std::cos(azimuth);
    const float azimuthSin = std::sin(azimuth);
    const float elevationCos = std::cos(elevation);
    const float elevationSin = std::sin(elevation);
    const float rightX = headingZ;
    const float rightZ = -headingX;
    const float horizontalX = headingX * azimuthCos + rightX * azimuthSin;
    const float horizontalZ = headingZ * azimuthCos + rightZ * azimuthSin;

    const float originY = linkY + kWallEyeHeight;
    return {
        linkX,
        originY,
        linkZ,
        linkX + horizontalX * elevationCos * kWallRayLength,
        originY + elevationSin * kWallRayLength,
        linkZ + horizontalZ * elevationCos * kWallRayLength,
    };
}

inline WallRay wallRayFor(std::uint32_t index, float linkX, float linkY,
                          float linkZ, float headingX, float headingZ) {
    return wallRayForAngles(wallAzimuth(wallProbeColumn(index)),
                            wallElevation(wallProbeRow(index)), linkX, linkY,
                            linkZ, headingX, headingZ);
}

struct SurveyAngles {
    float azimuth = 0.0f;
    float elevation = 0.0f;
    bool valid = false;
};

inline SurveyAngles surveyAnglesFor(float dx, float dy, float dz,
                                    float headingX, float headingZ) {
    SurveyAngles angles{};
    const float headingLengthSq = headingX * headingX + headingZ * headingZ;
    const float planarSq = dx * dx + dz * dz;
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
        return angles;
    if (headingLengthSq < 1e-6f || planarSq + dy * dy < 1e-6f) return angles;
    const float headingLength = std::sqrt(headingLengthSq);
    const float forwardX = headingX / headingLength;
    const float forwardZ = headingZ / headingLength;

    const float rightX = forwardZ;
    const float rightZ = -forwardX;
    const float forward = dx * forwardX + dz * forwardZ;
    const float right = dx * rightX + dz * rightZ;
    const float planar = std::sqrt(forward * forward + right * right);
    angles.azimuth = std::atan2(right, forward);
    angles.elevation = planar < 1e-4f ? (dy >= 0.0f ? kPi * 0.5f : -kPi * 0.5f)
                                      : std::atan2(dy, planar);
    angles.valid = true;
    return angles;
}

inline bool inSurveyAngularWindow(float dx, float dy, float dz, float headingX,
                                  float headingZ, float pitch,
                                  float halfAzimuth, float halfElevation) {
    const SurveyAngles angles = surveyAnglesFor(dx, dy, dz, headingX, headingZ);
    if (!angles.valid) return false;
    const float azimuth =
        angles.azimuth < 0.0f ? -angles.azimuth : angles.azimuth;
    const float offset = angles.elevation - pitch;
    const float elevation = offset < 0.0f ? -offset : offset;
    return azimuth <= halfAzimuth && elevation <= halfElevation;
}

}
