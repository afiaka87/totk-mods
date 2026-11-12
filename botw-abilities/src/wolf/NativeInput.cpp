// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeInput.hpp"
#include "NativeApi.hpp"
#include "WorldMath.hpp"
#include <cmath>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
NativeApi g_api;
constexpr totk::port::Site kSites[]{
    {0xB7EF98, 0xA9BE7BFD, 0xA9014FF4}, {0x753530, 0xA9BB7BFD, 0xF9000BF9},
    {0x9EE828, 0xA9BD7BFD, 0xA90157F6}, {0xDBD3C8, 0xFD416000, 0x910B0008},
    {0x823FD8, 0xD103C3FF, 0xFD0033EC},
    {0x14D9330, 0xF9400008, 0xB40001E8},
};
}

bool initializeInputApi(std::uintptr_t base) { return initializeActorApi(base) && g_api.bind(base, kSites, "INPUT_ABI_REFUSED"); }

ActorReference borrowPlayer(const SceneContext& scene) {
    if (!g_api.ready() || !scene.isReady()) return {};
    const auto manager = scene.residentActorManager;
    const auto config = readMemory<std::uintptr_t>(manager + 24);
    if (!isPlausibleAddress(config) || !isPlausibleAddress(readMemory<std::uintptr_t>(config + 336)) ||
        readMemory<std::int32_t>(manager + layout::kResidentCount) <= 0 ||
        !isPlausibleAddress(readMemory<std::uintptr_t>(manager + layout::kResidentList))) return {};
    const auto link = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0xB7EF98)(manager);
    if (!isPlausibleAddress(link) || !g_api.at<bool(*)(std::uintptr_t)>(0x9EE828)(link)) return {};
    return g_api.at<ActorReference(*)(std::uintptr_t)>(0x753530)(link);
}

std::optional<ActorKinematics> readKinematics(const ActorReference& actor) {
    if (!g_api.ready() || !actor.address()) return std::nullopt;
    const auto address = actor.address();
    ActorKinematics result{
        {readMemory<totk::core::RotationBasis>(address + layout::kActorRotation),
         readMemory<totk::core::WorldPosition>(address + layout::kActorPosition)},
        readMemory<totk::core::LinearVelocity>(address + layout::kActorLinearVelocity)};
    for (float value : result.transform.rotation.values) {
        if (!std::isfinite(value)) {
            Logging.Log("[wolf-link] INPUT_ROTATION_INVALID id=%u", actor.generation());
            return std::nullopt;
        }
    }
    if (!finite(result.transform.position) || !finite(result.velocity)) {
        Logging.Log("[wolf-link] INPUT_MOTION_INVALID id=%u", actor.generation());
        return std::nullopt;
    }
    return result;
}

std::optional<float> readModelWidth(const ActorReference& actor) {
    if (!g_api.ready() || !actor.address()) return std::nullopt;
    totk::core::WorldPosition minimum{}, maximum{};
    using Bounds = bool(*)(const ActorReference*, totk::core::WorldPosition*, totk::core::WorldPosition*);
    if (!g_api.at<Bounds>(0x14D9330)(&actor, &minimum, &maximum)) {
        Logging.Log("[wolf-link] INPUT_BOUNDS_UNAVAILABLE id=%u", actor.generation());
        return std::nullopt;
    }
    const float width = maximum.x - minimum.x;
    if (!std::isfinite(width) || width < 0) {
        Logging.Log("[wolf-link] INPUT_WIDTH_INVALID id=%u width=%f", actor.generation(), double(width));
        return std::nullopt;
    }
    return width;
}

}
