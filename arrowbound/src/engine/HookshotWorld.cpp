// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "HookshotWorld.hpp"

#include <lib.hpp>

#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Totk121Offsets.hpp"
#include "totk/engine/Transform.hpp"
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
#include "../program/modules/arrowbound/HookshotLog.hpp"
#include "FlightDiagnostics.hpp"
#endif

namespace arrowbound::world {
namespace {
namespace comp {
// Offsets into the component table.
constexpr std::ptrdiff_t kPerimeter = 0x388;
constexpr std::ptrdiff_t kPlayer = 0x3A8;
}  // namespace comp

namespace off {
constexpr std::ptrdiff_t kPlayerResetStepRate = 0x01D2D91C;
}  // namespace off

namespace perimeter {
constexpr std::ptrdiff_t kFlags = 0x294;
constexpr std::uint32_t kClimbEngaged = 0x08;
}  // namespace perimeter

WorldState g_state{};

totk::engine::TransformService& transforms() {
    static totk::engine::TransformService service{
        totk::engine::TransformFunctions::fromMainBase(g_state.mainBase)};
    return service;
}

totk::engine::ActorHandle handleFor(std::uintptr_t address) {
    if (!totk::engine::isPlausibleAddress(address)) return {};
    const auto namePointer = totk::engine::readMemory<std::uintptr_t>(
        address + totk::engine::layout::kActorNamePointer);
    return totk::engine::ActorHandle{address, namePointer, g_state.sceneToken};
}

pure::Vec3 positionOf(std::uintptr_t actor) {
    if (!totk::engine::isPlausibleAddress(actor)) return {};
    const auto* position = reinterpret_cast<const float*>(
        actor + totk::engine::layout::kActorPosition);
    return {position[0], position[1], position[2]};
}

bool rotationOf(std::uintptr_t actor, float out[9]) {
    if (!totk::engine::isPlausibleAddress(actor)) return false;
    const auto* rotation = reinterpret_cast<const float*>(
        actor + totk::engine::layout::kActorRotation);
    for (int i = 0; i < 9; ++i) out[i] = rotation[i];
    return true;
}

}  // namespace

void initialize(std::uintptr_t mainBase) {
    g_state.mainBase = mainBase;
    (void)transforms();
}

const WorldState& state() { return g_state; }

void* playerActor() { return reinterpret_cast<void*>(g_state.player); }

void refresh(SessionResetFn reset) {
    const auto scene = totk::engine::resolveScene(g_state.mainBase);
    if (!scene) {
        g_state.player = 0;
        g_state.camera = 0;
        g_state.stable = 0;
        g_state.havePlayerPos = false;
        return;
    }

    if (g_state.sceneToken.isValid() && scene.value.token != g_state.sceneToken) {
        // The reset bumps the world generation, which invalidates in-flight ray results; only an
        // unclaimed request is taken back.
        reset("scene");
    }
    g_state.sceneToken = scene.value.token;

    std::uintptr_t player = 0;
    std::uintptr_t camera = 0;
    (void)totk::engine::visitResidentActors(
        scene.value, [&](const totk::engine::ResidentActorView& actor) {
            if (actor.name.equals("Player")) player = actor.handle.address;
            else if (actor.name.equals("PlayerCamera"))
                camera = actor.handle.address;
            return totk::engine::VisitControl::Continue;
        });
    if (player != g_state.player) {
        g_state.player = player;
        g_state.stable = 0;
        g_state.havePlayerPos = false;
    } else if (player != 0 && g_state.stable < 1000000) {
        ++g_state.stable;
    }
    g_state.camera = camera;

    // Teleport and shrine transitions can keep both pointers alive; a position discontinuity is
    // the reliable signal.
    if (g_state.player != 0) {
        const pure::Vec3 position = positionOf(g_state.player);
        if (pure::finite3(position)) {
            if (g_state.havePlayerPos &&
                pure::distance(position, g_state.lastPlayerPos) >
                    kTeleportResetDistance) {
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
                ZHLOG("TRACE_WORLD_RESET reason=teleport step_cm=%d limit_cm=%d",
                      pure::traceNumber(pure::distance(position, g_state.lastPlayerPos)),
                      pure::traceNumber(kTeleportResetDistance));
#endif
                reset("teleport");
            }
            g_state.lastPlayerPos = position;
            g_state.havePlayerPos = true;
        }
    }
}

bool ready() {
    return g_state.player != 0 && g_state.camera != 0 &&
           g_state.stable >= kStableMin;
}

pure::Vec3 playerPosition() { return positionOf(g_state.player); }
pure::Vec3 cameraPosition() { return positionOf(g_state.camera); }

bool readPlayerRotation(float out[9]) {
    return rotationOf(g_state.player, out);
}

bool readCameraRotation(float out[9]) {
    return rotationOf(g_state.camera, out);
}

bool forcePlayerPose(const float rotation[9], const pure::Vec3& position) {
    totk::core::WorldTransform transform{};
    for (int i = 0; i < 9; ++i) transform.rotation.values[i] = rotation[i];
    transform.position = {position.x, position.y, position.z};
    return transforms().force(handleFor(g_state.player), g_state.sceneToken,
                              transform, 0U) ==
           totk::engine::TransformError::None;
}

bool resetPlayerStepRate() {
    if (!totk::engine::isPlausibleAddress(g_state.player) || g_state.mainBase == 0)
        return false;
    const auto registry = totk::engine::readMemory<std::uintptr_t>(
        g_state.player + totk::engine::layout::kActorComponentRegistry);
    if (!totk::engine::isPlausibleAddress(registry)) return false;
    const auto player =
        totk::engine::readMemory<std::uintptr_t>(registry + comp::kPlayer);
    if (!totk::engine::isPlausibleAddress(player)) return false;
    const auto reset = reinterpret_cast<void (*)(std::uintptr_t)>(
        g_state.mainBase + off::kPlayerResetStepRate);
    reset(player);
    return true;
}

bool climbBitEngaged() {
    if (!totk::engine::isPlausibleAddress(g_state.player)) return false;
    const auto registry = totk::engine::readMemory<std::uintptr_t>(
        g_state.player + totk::engine::layout::kActorComponentRegistry);
    if (!totk::engine::isPlausibleAddress(registry)) return false;
    const auto analyser =
        totk::engine::readMemory<std::uintptr_t>(registry + comp::kPerimeter);
    if (!totk::engine::isPlausibleAddress(analyser)) return false;
    return (totk::engine::readMemory<std::uint32_t>(analyser +
                                                    perimeter::kFlags) &
            perimeter::kClimbEngaged) != 0;
}

}  // namespace arrowbound::world
