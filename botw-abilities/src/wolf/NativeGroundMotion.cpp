// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeGroundMotion.hpp"
#include "GroundMotion.hpp"
#include "NativeBrain.hpp"
#include "NativeApi.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using namespace totk::port::literals;
using Vector = totk::core::WorldPosition;
NativeApi g_api;
std::atomic<unsigned> g_reported{}, g_capped{};
std::atomic<float> g_speedScale{1.0f};
constexpr unsigned kCappedLogs = 12;
constexpr totk::port::Site kSites[]{
    {0xB0E5B8, 0xD10183FF, 0xFD0013E8},
    {0xFCE23C, 0xF9408808, 0x9001B30A},
    {0x2A8CCD8, 0xF9401008, 0x9106A100},
};

bool firstFault(unsigned bit) { return !(g_reported.fetch_or(bit, std::memory_order_relaxed) & bit); }

std::optional<Vector> wolfVelocity(std::uintptr_t frame, const Vector& surface) {
    const auto property = pointerAt(pointerAt(frame, 88), 8);
    const auto body = pointerAt(property, 24);
    if (!body) return std::nullopt;
    const auto link = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0xFCE23C)(body);
    if (!isPlausibleAddress(link) || !isBrainOwner(readMemory<std::uint32_t>(link + 16)))
        return std::nullopt;
    const auto actor = g_api.at<ActorReference(*)(std::uintptr_t)>(0x13C358C)(link);
    if (!actor.address() || !isBrainOwner(actor.generation())) {
        if (firstFault(1)) Logging.Log("[wolf-link] GROUND_OWNER_UNAVAILABLE link=%p", reinterpret_cast<void*>(link));
        return std::nullopt;
    }
    if (!isWolfActor(actor.address())) {
        if (firstFault(2)) Logging.Log("[wolf-link] GROUND_OWNER_MISMATCH actor=%p", reinterpret_cast<void*>(actor.address()));
        return std::nullopt;
    }
    const auto component = actorComponent(actor.address(), 0x100);
    const auto state = pointerAt(property, 32);
    const auto collision = pointerAt(frame, 80);
    if (!component || !state || !collision) {
        if (firstFault(4)) Logging.Log("[wolf-link] GROUND_COMPONENT_MISSING root=%p motion=%p collision=%p",
            reinterpret_cast<void*>(component), reinterpret_cast<void*>(state), reinterpret_cast<void*>(collision));
        return std::nullopt;
    }
    // Non-animation movement and wall sliding retain their native handling.
    if (!(readMemory<std::uint8_t>(component + 56) & 1) ||
        readMemory<int>(component + 44) != 0 || (readMemory<unsigned>(collision + 272) & 0x10))
        return std::nullopt;
    const GroundMotion motion{readMemory<Vector>(state + 424), readMemory<Vector>(frame + 36), surface,
        readMemory<float>(state + 448), readMemory<float>(state + 452), readMemory<float>(state + 456)};
    if (std::fabs(motion.speed) > kMaxGroundSpeed && g_capped.fetch_add(1, std::memory_order_relaxed) < kCappedLogs)
        Logging.Log("[wolf-link] GROUND_SPEED_CAPPED speed=%.1f forward=%.2f side=%.2f",
            double(motion.speed), double(motion.forward), double(motion.side));
    const auto velocity = animationGroundVelocity(motion);
    if (!velocity && firstFault(8)) Logging.Log("[wolf-link] GROUND_INPUT_INVALID speed=%f forward=%f side=%f",
        double(motion.speed), double(motion.forward), double(motion.side));
    return velocity;
}

HOOK_DEFINE_TRAMPOLINE(GroundVelocity) {
    static float Callback(std::uintptr_t component, Vector* output, std::uintptr_t frame, const Vector* surface) {
        const auto velocity = wolfVelocity(frame, *surface);
        if (!velocity) return Orig(component, output, frame, surface);
        const float scale = g_speedScale.load(std::memory_order_relaxed);
        output->x += velocity->x * scale;
        output->y += velocity->y * scale;
        output->z += velocity->z * scale;
        return output->z;
    }
};
}

void setGroundSpeedScale(float scale) {
    g_speedScale.store(std::isfinite(scale) ? std::clamp(scale, 1.0f, 1.5f) : 1.0f,
                       std::memory_order_relaxed);
}

bool installGroundMotionApi(std::uintptr_t base) {
    if (g_api.ready()) return g_api.base() == base;
    if (!initializeActorApi(base) || !g_api.bind(base, kSites, "GROUND_ABI_REFUSED")) return false;
    GroundVelocity::InstallAtOffset(0xB0E5B8_v121);
    return true;
}

}
