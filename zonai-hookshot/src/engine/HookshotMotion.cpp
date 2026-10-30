// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "HookshotMotion.hpp"
#include "HookshotWorld.hpp"
#include "../program/modules/zonai-hookshot/HookshotRuntime.hpp"
#include <arrowbound/ActiveGame.hpp>
#include <totk/engine/MotionCamera.hpp>
#include <atomic>
#include <cmath>

namespace zonai_hookshot::motion {
namespace {
namespace engine = totk::engine;
namespace gyro = engine::motion;
using engine::readMemory;
using engine::isPlausibleAddress;
std::uintptr_t moduleBase{};
bool installed{};
std::atomic<std::uint64_t> physicsTicks{};
std::atomic<bool> physicsRunning{};
std::atomic_flag requestLock = ATOMIC_FLAG_INIT;
struct Request {
    std::uintptr_t player{}, generation{};
    gyro::Vector pivot{};
};
Request request{};
gyro::AimState aim{};

constexpr std::uintptr_t kPhysicsSystem = 0x471DCB0;
constexpr std::uintptr_t kPhysicsTime = 0xC8, kPauseFlags = 0x18;
constexpr std::uintptr_t kCameraState = 1096, kCameraTarget = 3144, kTargetActor = 24;
constexpr engine::CodeSite kCameraHook{0xA6DF60, 0xD102C3FF, 0x6D033BEF};
constexpr engine::CodeSite kClockHook{0x7EDC00, 0xF9400828, 0xB40002A8};

// Resolve ownership from this callback, never from a retained camera pointer.
std::uintptr_t cameraActor(std::uintptr_t state) {
    const auto component = state - kCameraState;
    const auto target = isPlausibleAddress(component) ? readMemory<std::uintptr_t>(component + kCameraTarget) : 0;
    return isPlausibleAddress(target) ? readMemory<std::uintptr_t>(target + kTargetActor) : 0;
}

void publishRequest(Request next) {
    while (requestLock.test_and_set(std::memory_order_acquire)) {}
    request = next;
    requestLock.clear(std::memory_order_release);
}

HOOK_DEFINE_TRAMPOLINE(PhysicsClockHook) {
    static void Callback(void* module, const void* pauseContext, float frameScale) {
        Orig(module, pauseContext, frameScale);
        const auto system = readMemory<std::uintptr_t>(moduleBase + kPhysicsSystem);
        const auto time = isPlausibleAddress(system) ? readMemory<std::uintptr_t>(system + kPhysicsTime) : 0;
        const bool active = isPlausibleAddress(time) && !(readMemory<std::uint32_t>(time + kPauseFlags) & 1) &&
                            std::isfinite(frameScale) && frameScale > 0 && frameScale < 100;
        if (active)
            physicsTicks.fetch_add(static_cast<std::uint64_t>(double(frameScale) * 4294967296.0 + 0.5),
                            std::memory_order_release);
        physicsRunning.store(active, std::memory_order_release);
    }
};

HOOK_DEFINE_TRAMPOLINE(AimCameraHook) {
    static void Callback(std::uintptr_t state, float* output, float frameScale) {
        Orig(state, output, frameScale);
        if (requestLock.test_and_set(std::memory_order_acquire)) return;
        const auto current = request;
        requestLock.clear(std::memory_order_release);
        if (!current.player) {
            aim = {};
            return;
        }
        if (cameraActor(state) != current.player) {
            aim = {};
            return;
        }
        gyro::applyCamera(aim, {state, current.generation, physicsTicks.load(std::memory_order_acquire),
                               true, physicsRunning.load(std::memory_order_acquire), false},
                          output, current.pivot);
    }
};
}

void install(std::uintptr_t base) {
    const auto* game = arrowbound::profiles::active();
    if (installed || !game || game->version != arrowbound::profiles::GameVersion::V121) return;
    constexpr engine::CodeSite sites[]{kCameraHook, kClockHook};
    if (!engine::codeMatches(base, sites, "hookshot-motion", "HOOK_REFUSED")) return;
    gyro::validate(base);
    if (!gyro::supported) return;
    moduleBase = base;
    PhysicsClockHook::InstallAtOffset(kClockHook.offset);
    AimCameraHook::InstallAtOffset(kCameraHook.offset);
    installed = true;
    Logging.Log("[hookshot-motion] READY game=1.2.1 shared-gyro=ultrahand");
}

void publish(const HookshotRuntime& runtime) {
    if (!installed) return;
    const auto phase = runtime.machine.phase;
    if (!world::ready() || (phase != pure::Phase::Targeting && phase != pure::Phase::Confirming)) {
        reset();
        return;
    }
    const auto position = world::playerPosition();
    publishRequest({world::state().player, runtime.session.worldGen,
                    {position.x, position.y, position.z}});
}

void reset() {
    publishRequest({});
}
}
