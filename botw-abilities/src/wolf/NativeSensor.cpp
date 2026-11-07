// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeSensor.hpp"
#include "NativeApi.hpp"
#include "WorldMath.hpp"
#include <cmath>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using totk::core::WorldPosition;
NativeApi g_api;
constexpr CodeSite kSites[]{
    {0x12309B4, 0xA9BE7BFD, 0xA9014FF4}, {0x10EE74C, 0xA9BD7BFD, 0xF9000BF5},
    {0xE40444, 0xD10403FF, 0x6D073BEF}, {0x19B7004, 0xF9430808, 0xB9469C09},
};
constexpr std::uintptr_t kFeatureSlot = 0x462E180, kPlayingSlot = 0x462EC80;
constexpr int kSensorFeature = 19;
// What the sensor's own update keeps: the nearest shrine, then the nearest item the player chose.
constexpr std::uintptr_t kFlags = 264, kNearest = 196, kNearestId = 208, kNearestRange = 212;
constexpr std::uintptr_t kChosen = 216, kChosenId = 228, kChosenRange = 256;
constexpr std::uint32_t kReacting = 1;
constexpr int kOff = 0, kChosenMode = 2;

}

bool initializeSensorApi(std::uintptr_t base) { return g_api.bind(base, kSites, "SENSOR_ABI_REFUSED"); }

std::optional<WorldPosition> readSensorTarget() {
    const auto owner = g_api.singleton(kFeatureSlot);
    if (!owner) return std::nullopt;
    const auto sensor = g_api.at<std::uintptr_t(*)(std::uintptr_t, int)>(0x12309B4)(owner, kSensorFeature);
    if (!isPlausibleAddress(sensor) || !(readMemory<std::uint32_t>(sensor + kFlags) & kReacting)) return std::nullopt;
    const int mode = g_api.at<int(*)(std::uintptr_t, bool)>(0x10EE74C)(sensor, false);
    if (mode == kOff) return std::nullopt;
    auto point = readMemory<WorldPosition>(sensor + kNearest);
    if (mode == kChosenMode) {
        const float range = readMemory<float>(sensor + kChosenRange);
        const bool same = readMemory<std::uint32_t>(sensor + kChosenId) == readMemory<std::uint32_t>(sensor + kNearestId);
        if (std::isfinite(range) && (same || range < readMemory<float>(sensor + kNearestRange)))
            point = readMemory<WorldPosition>(sensor + kChosen);
    }
    return finite(point) ? std::optional{point} : std::nullopt;
}

bool playerRiding() {
    const auto playing = g_api.singleton(kPlayingSlot);
    return playing && g_api.at<bool(*)(std::uintptr_t)>(0x19B7004)(playing);
}

}
