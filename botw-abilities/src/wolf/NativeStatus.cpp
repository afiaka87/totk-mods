// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeStatus.hpp"
#include "NativeApi.hpp"
#include <algorithm>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
NativeApi g_api;
constexpr CodeSite kSites[]{
    {0x84986C, 0xF9411408, 0xB4000068}, {0xADDBC4, 0xF9411408, 0xB4000068},
    {0xA0BF2C, 0x36F803E1, 0xF940240B}, {0x1192CBC, 0xD10083FF, 0xA9017BFD},
    {0xFA7F50, 0xF9411408, 0xB4000068},
};
constexpr int kAllParts = -1;
// One record per body part, each carrying what the elements did to that part.
constexpr std::uintptr_t kPartCount = 320, kParts = 328, kPartSize = 256, kPartEvents = 40, kPartStates = 44, kPartGust = 156;
constexpr std::uint32_t kElectrified = 0x2, kGustEvent = 0x8000;
constexpr std::uint32_t kPartLimit = 32;
// The life record keeps this frame's strongest hit here. The game clears it every damage step.
constexpr std::uintptr_t kHitStrength = 596;

}

bool initializeStatusApi(std::uintptr_t base) { return g_api.bind(base, kSites, "STATUS_ABI_REFUSED"); }

CompanionStatus readStatus(const ActorReference& actor) {
    CompanionStatus status;
    if (!g_api.ready() || !actor.address()) return status;
    const auto life = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0xFA7F50)(actor.address());
    if (isPlausibleAddress(life)) status.hitStrength = readMemory<std::uint8_t>(life + kHitStrength);
    const auto chemical = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0x84986C)(actor.address());
    if (isPlausibleAddress(chemical)) status.burning = g_api.at<bool(*)(std::uintptr_t, int)>(0xA0BF2C)(chemical, kAllParts);
    const auto condition = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0xADDBC4)(actor.address());
    if (!isPlausibleAddress(condition)) return status;
    status.frozen = g_api.at<bool(*)(std::uintptr_t, int)>(0x1192CBC)(condition, kAllParts);
    const auto parts = readMemory<std::uintptr_t>(condition + kParts);
    if (!isPlausibleAddress(parts)) return status;
    const auto count = std::min(readMemory<std::uint32_t>(condition + kPartCount), kPartLimit);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto part = parts + i * kPartSize;
        status.shocked |= (readMemory<std::uint32_t>(part + kPartStates) & kElectrified) != 0;
        status.gust |= (readMemory<std::uint32_t>(part + kPartEvents) & kGustEvent) != 0 ||
                       readMemory<std::uint32_t>(part + kPartGust) != 0;
    }
    return status;
}

}
