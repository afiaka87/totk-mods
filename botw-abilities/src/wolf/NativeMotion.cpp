// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeMotion.hpp"
#include "NativeApi.hpp"
#include "WorldMath.hpp"
#include <cmath>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using namespace totk::port::literals;
NativeApi g_api;
constexpr totk::port::Site kSites[]{
    {0x1B5CAF8, 0xF9411408, 0xB4000068}, {0x13335DC, 0xF9411408, 0xF9402908},
    {0x11C2004, 0xF9400400, 0xF9400008}, {0xD07788, 0xFC1C0FE8, 0xA9017BFD},
    {0x1D96C24, 0xBD400100, 0xBD011C00},
    {0x6A5530, 0xBD411E69, 0x39431F48}, {0x6C03D8, 0x1E280929, 0xB9405668},
    {0xF5ED74, 0xFC1C0FE8, 0xA9017BFD},
};
// 1.4.3 has no separate copy of this routine (it is inline in a caller). Nothing here calls it.
constexpr totk::port::Site kSites121[]{{0xD07764, 0xAA0003E8, 0x9100900A}};
constexpr std::uintptr_t kGears = 0x20, kPlayRate = 0x11C;
constexpr int kAttackMode = 2, kEscapeExtraMode = 4;

std::uintptr_t quadruped(const ActorReference& actor) {
    const auto value = g_api.ready() ? actorComponent(actor.address(), 0x3E0) : 0;
    if (!value) Logging.Log("[wolf-link] MOTION_COMPONENT_MISSING api=%u actor=%p",
        unsigned(g_api.ready()), reinterpret_cast<void*>(actor.address()));
    return value;
}

bool validGears(const MotionGears& gears) {
    return gears.mode >= 0 && gears.mode < 6 && gears.gear >= 0 && gears.gear < 5 &&
           gears.pendingMode >= 0 && gears.pendingMode <= 6 &&
           gears.pendingGear >= 0 && gears.pendingGear <= 5 && std::isfinite(gears.remaining);
}
}

bool initializeMotionApi(std::uintptr_t base) { return g_api.bind(base, kSites, "MOTION_ABI_REFUSED", kSites121); }

std::optional<CircleTable> nativeCircleTable() {
    if (!g_api.ready()) {
        Logging.Log("[wolf-link] MOTION_TABLE_UNAVAILABLE api=0");
        return std::nullopt;
    }
    return CircleTable{reinterpret_cast<const CircleSample*>(g_api.base() + 0x3829844_v121), 256};
}

std::optional<MotionState> readMotion(const ActorReference& actor) {
    const auto quad = quadruped(actor);
    if (!quad) return std::nullopt;
    const auto physics = actorComponent(actor.address(), 0x50);
    const auto controller = pointerAt(pointerAt(physics, 0x20), 0x10);
    const auto backend = pointerAt(controller, 8);
    if (!backend || !pointerAt(backend, 0)) {
        Logging.Log("[wolf-link] MOTION_CONTROLLER_MISSING actor=%p controller=%p backend=%p",
            reinterpret_cast<void*>(actor.address()), reinterpret_cast<void*>(controller),
            reinterpret_cast<void*>(backend));
        return std::nullopt;
    }
    const auto direction = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0x11C2004)(controller);
    if (!isPlausibleStringAddress(direction) || (direction & 3)) {
        Logging.Log("[wolf-link] MOTION_DIRECTION_MISSING address=%p", reinterpret_cast<void*>(direction));
        return std::nullopt;
    }
    MotionState state{readMemory<totk::core::WorldPosition>(direction),
                      readMemory<MotionGears>(quad + kGears), readMemory<float>(quad + kPlayRate)};
    const auto& d = state.direction;
    if (!finite(d) || !validGears(state.gears) || !std::isfinite(state.playRate) || state.playRate < 0) {
        Logging.Log("[wolf-link] MOTION_STATE_INVALID dir=%f,%f,%f gears=%d,%d,%d,%d time=%f rate=%f",
            double(d.x), double(d.y), double(d.z), state.gears.mode, state.gears.gear,
            state.gears.pendingMode, state.gears.pendingGear, double(state.gears.remaining), double(state.playRate));
        return std::nullopt;
    }
    return state;
}

namespace {
enum class SpeedKind { Linear, Angular };

std::optional<float> readFollowSpeed(const ActorReference& actor, int gear, SpeedKind kind) {
    if (gear < 0 || gear > 5) {
        Logging.Log("[wolf-link] MOTION_GEAR_INVALID gear=%d", gear);
        return std::nullopt;
    }
    const auto quad = quadruped(actor);
    if (!quad) return std::nullopt;
    // BotW's fifth probe crosses into EscapeEx; TotK's separate arrays must stay in bounds.
    const int mode = gear == 5 ? kEscapeExtraMode : kAttackMode;
    const totk::port::Code offset = kind == SpeedKind::Linear ? 0xD07788 : 0xF5ED74;
    const float speed = g_api.at<float(*)(std::uintptr_t, int, int)>(offset)(quad, mode, gear == 5 ? 1 : gear);
    if (!std::isfinite(speed) || speed < 0) {
        Logging.Log("[wolf-link] MOTION_SPEED_INVALID kind=%d gear=%d speed=%f", int(kind), gear, double(speed));
        return std::nullopt;
    }
    return speed;
}
}

std::optional<float> readFollowLinearSpeed(const ActorReference& actor, int gear) {
    return readFollowSpeed(actor, gear, SpeedKind::Linear);
}

std::optional<float> readFollowAngularSpeed(const ActorReference& actor, int gear) {
    return readFollowSpeed(actor, gear, SpeedKind::Angular);
}

}
