// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
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

constexpr std::uintptr_t kPhysicsTime = 0xC8, kPauseFlags = 0x18, kTargetActor = 24;
// 1.4.3: camera state 72 bytes deeper; the clock skips one of two frame paths, so hook the frame-rate update.
struct Places {
    std::uintptr_t physicsSystem, cameraState, cameraTarget;
    engine::CodeSite camera, clock;
};
constexpr Places k121{0x471DCB0, 1096, 3144, {0xA6DF60, 0xD102C3FF, 0x6D033BEF},
                      {0x7EDC00, 0xF9400828, 0xB40002A8}};
constexpr Places k143{0x3AD27C8, 1168, 3216, {0x717668, 0xD10283FF, 0x6D033BEF},
                      {0x3E67F0, 0x6DBD23E9, 0xA9017BFD}};
// The 1.4.3 frame-rate update stores the physics frame scale at +16 here.
constexpr engine::CodeSite kFrameScaleStore143{0x3E6820, 0xBD001001, 0};
const Places* places{};

// Resolve ownership from this callback, never from a retained camera pointer.
std::uintptr_t cameraActor(std::uintptr_t state) {
    const auto component = state - places->cameraState;
    const auto target =
        isPlausibleAddress(component) ? readMemory<std::uintptr_t>(component + places->cameraTarget) : 0;
    return isPlausibleAddress(target) ? readMemory<std::uintptr_t>(target + kTargetActor) : 0;
}

void publishRequest(Request next) {
    while (requestLock.test_and_set(std::memory_order_acquire)) {}
    request = next;
    requestLock.clear(std::memory_order_release);
}

void publishPhysicsTime(float frameScale) {
    const auto system = readMemory<std::uintptr_t>(moduleBase + places->physicsSystem);
    const auto time = isPlausibleAddress(system) ? readMemory<std::uintptr_t>(system + kPhysicsTime) : 0;
    const bool active = isPlausibleAddress(time) && !(readMemory<std::uint32_t>(time + kPauseFlags) & 1) &&
                        std::isfinite(frameScale) && frameScale > 0 && frameScale < 100;
    if (active)
        physicsTicks.fetch_add(static_cast<std::uint64_t>(double(frameScale) * 4294967296.0 + 0.5),
                               std::memory_order_release);
    physicsRunning.store(active, std::memory_order_release);
}

HOOK_DEFINE_TRAMPOLINE(PhysicsClockHook) {
    static void Callback(void* module, const void* pauseContext, float frameScale) {
        Orig(module, pauseContext, frameScale);
        publishPhysicsTime(frameScale);
    }
};

HOOK_DEFINE_TRAMPOLINE(FrameRateHook) {
    static void Callback(void* rate, float frames) {
        Orig(rate, frames);
        publishPhysicsTime(readMemory<float>(reinterpret_cast<std::uintptr_t>(rate) + 16));
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
    using arrowbound::profiles::GameVersion;
    const auto* game = arrowbound::profiles::active();
    if (installed || !game) return;
    if (game->version != GameVersion::V121 && game->version != GameVersion::V143) {
        Logging.Log("[hookshot-motion] gyro aim off: game %s has no camera places", game->name);
        return;
    }
    const bool v143 = game->version == GameVersion::V143;
    const auto& at = v143 ? k143 : k121;
    const engine::CodeSite sites[]{at.camera, at.clock, v143 ? kFrameScaleStore143 : at.clock};
    if (!engine::codeMatches(base, sites, "hookshot-motion", "HOOK_REFUSED")) return;
#if defined(TOTK_PORT) && TOTK_PORT
    gyro::validate(base);  // the suite's port layer translates the 1.2.1 places
#else
    gyro::validate(base, v143);
#endif
    if (!gyro::supported) return;
    moduleBase = base;
    places = &at;
    if (v143)
        FrameRateHook::InstallAtOffset(at.clock.offset);
    else
        PhysicsClockHook::InstallAtOffset(at.clock.offset);
    AimCameraHook::InstallAtOffset(at.camera.offset);
    installed = true;
    Logging.Log("[hookshot-motion] READY game=%s shared-gyro=ultrahand", game->name);
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
