// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeSensor.hpp"
#include "NativeApi.hpp"
#include "WorldMath.hpp"
#include <cmath>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using namespace totk::port::literals;
using totk::core::WorldPosition;
NativeApi g_api;
constexpr totk::port::Site kSites[]{
    {0x12309B4, 0xA9BE7BFD, 0xA9014FF4}, {0x10EE74C, 0xA9BD7BFD, 0xF9000BF5},
    {0xE40444, 0xD10403FF, 0x6D073BEF}, {0x19B7004, 0xF9430808, 0xB9469C09},
};
constexpr totk::port::Code kFeatureSlot = 0x462E180, kPlayingSlot = 0x462EC80;
constexpr int kSensorFeature = 19;
// What the sensor's own update keeps: the nearest shrine, then the nearest item the player chose.
constexpr std::uintptr_t kFlags = 264, kNearest = 196, kNearestId = 208, kNearestRange = 212;
constexpr std::uintptr_t kChosen = 216, kChosenId = 228, kChosenRange = 256;
constexpr std::uint32_t kReacting = 1;
constexpr int kOff = 0, kChosenMode = 2;
// 1.4.3's getter skips 1.2.1's type cast; its callers pass this type record to the feature table's first entry.
constexpr auto kSensorType143 = 0xC1E518_v143;
bool g_typeReady143{};

std::uintptr_t sensorFeature(std::uintptr_t owner) {
    const auto feature = g_api.at<std::uintptr_t(*)(std::uintptr_t, int)>(0x12309B4)(owner, kSensorFeature);
    if (!totk::port::is143()) return feature;
    const auto table = g_typeReady143 ? pointerAt(feature, 0) : 0;
    const auto cast = table ? readMemory<std::uintptr_t>(table) : 0;
    if (!isPlausibleStringAddress(cast) || (cast & 3)) return 0;
    const auto type = reinterpret_cast<std::uintptr_t(*)()>(g_api.base() + kSensorType143)();
    return reinterpret_cast<std::uintptr_t(*)(std::uintptr_t, std::uintptr_t)>(cast)(feature, type);
}

}

bool initializeSensorApi(std::uintptr_t base) {
    const bool first = !g_api.ready();
    if (!g_api.bind(base, kSites, "SENSOR_ABI_REFUSED")) return false;
    if (first && totk::port::is143()) {
        g_typeReady143 = totk::suite::checkedCode(base + kSensorType143) == 0xA9BF7BFD &&
                         totk::suite::checkedCode(base + kSensorType143 + 8) == 0xF0016A48;
        if (!g_typeReady143)
            Logging.Log("[botw-abilities] sensor target off on 1.4.3: the sensor type routine is not the recorded one");
    }
    return true;
}

std::optional<WorldPosition> readSensorTarget() {
    const auto owner = g_api.singleton(kFeatureSlot);
    if (!owner) return std::nullopt;
    const auto sensor = sensorFeature(owner);
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
