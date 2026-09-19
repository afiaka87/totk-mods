// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "PlayerService.hpp"

namespace bivouac::engine {
namespace {

constexpr std::ptrdiff_t kSceneModuleInstanceOffset = 0x04728538;
constexpr std::ptrdiff_t kActorNameOffset = 0x218;
constexpr std::ptrdiff_t kActorComponentRegistryOffset = 0x228;
constexpr std::ptrdiff_t kActorPositionOffset = 0x2B4;
constexpr std::ptrdiff_t kActorRotationOffset = 0x2C0;
constexpr std::ptrdiff_t kActorForwardXOffset = 0x2C8;
constexpr std::ptrdiff_t kActorForwardYOffset = 0x2D4;
constexpr std::ptrdiff_t kActorForwardZOffset = 0x2E0;
constexpr std::ptrdiff_t kPerimeterAnalyzerOffset = 0x388;
constexpr std::ptrdiff_t kPerimeterFlagsOffset = 0x294;
constexpr std::uint32_t kClimbEngaged = 0x08;

bool validPointer(std::uintptr_t value) {
    return value >= 0x1000000 && value < 0x8000000000ull
        && (value & 0x3) == 0;
}

bool nameEquals(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (int index = 0; index < 64; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

void* PlayerService::resolve() const {
    const auto sceneModule =
        *reinterpret_cast<std::uintptr_t*>(mMainBase + kSceneModuleInstanceOffset);
    if (sceneModule == 0) {
        return nullptr;
    }
    const auto scene = *reinterpret_cast<std::uintptr_t*>(sceneModule + 0x1E8);
    if (scene == 0) {
        return nullptr;
    }
    const auto componentList =
        *reinterpret_cast<std::uintptr_t**>(scene + 0x58);
    if (componentList == nullptr) {
        return nullptr;
    }
    const auto actorManager = componentList[13];
    if (actorManager == 0) {
        return nullptr;
    }
    const auto count = *reinterpret_cast<std::int32_t*>(actorManager + 0x20);
    const auto list = *reinterpret_cast<std::uintptr_t*>(actorManager + 0x28);
    if (list == 0 || count <= 0 || count > 256) {
        return nullptr;
    }

    constexpr std::size_t kLinkStride = 0x70;
    for (std::int32_t index = 0; index < count; ++index) {
        const auto link = list + static_cast<std::uintptr_t>(index) * kLinkStride;
        const auto linkData = *reinterpret_cast<std::uintptr_t*>(link + 0x08);
        if (linkData == 0) {
            continue;
        }
        const auto actor = *reinterpret_cast<std::uintptr_t*>(linkData + 0x40);
        if (actor == 0) {
            continue;
        }
        const auto name =
            *reinterpret_cast<const char**>(actor + kActorNameOffset);
        if (nameEquals(name, "Player")) {
            return reinterpret_cast<void*>(actor);
        }
    }
    return nullptr;
}

PlayerSnapshot PlayerService::snapshot(void* player) const {
    PlayerSnapshot result{};
    result.actor = player;
    if (player == nullptr) {
        return result;
    }

    const auto address = reinterpret_cast<std::uintptr_t>(player);
    result.position = {
        *reinterpret_cast<float*>(address + kActorPositionOffset + 0),
        *reinterpret_cast<float*>(address + kActorPositionOffset + 4),
        *reinterpret_cast<float*>(address + kActorPositionOffset + 8),
    };
    result.forward = {
        *reinterpret_cast<float*>(address + kActorForwardXOffset),
        *reinterpret_cast<float*>(address + kActorForwardYOffset),
        *reinterpret_cast<float*>(address + kActorForwardZOffset),
    };

    const auto registry =
        *reinterpret_cast<std::uintptr_t*>(address + kActorComponentRegistryOffset);
    if (!validPointer(registry)) {
        return result;
    }
    const auto perimeter =
        *reinterpret_cast<std::uintptr_t*>(registry + kPerimeterAnalyzerOffset);
    if (validPointer(perimeter)) {
        const auto flags =
            *reinterpret_cast<std::uint32_t*>(perimeter + kPerimeterFlagsOffset);
        result.climbing = (flags & kClimbEngaged) != 0;
    }
    return result;
}

void PlayerService::copyRotation(void* player, float output[9]) const {
    if (player == nullptr || output == nullptr) {
        return;
    }
    const auto source = reinterpret_cast<const float*>(
        reinterpret_cast<std::uintptr_t>(player) + kActorRotationOffset);
    for (int index = 0; index < 9; ++index) {
        output[index] = source[index];
    }
}

} // namespace bivouac::engine
