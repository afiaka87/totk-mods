// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeMove.hpp"
#include "NativeApi.hpp"
#include "WorldMath.hpp"
#include <cmath>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
NativeApi g_api;
constexpr totk::port::Site kSites[]{
    {0x11E72F0, 0xA9BC7BFD, 0xA9015FF8}, {0x11B1AB4, 0xD10543FF, 0xFD0063EC},
    {0x1422320, 0xA9BE7BFD, 0xA9014FF4}, {0x1340090, 0xBD409000, 0x1E202000},
};

struct Arguments {
    std::int32_t actionVerb, minGear, maxGear, gearType, minimumSlowGear;
    float goalTolerance;
    bool waitAsync, requestPath, riderGear, autoSlow, decelerate, waitUntilPath, waitGearChange;
    std::uint8_t padding{};
    const totk::core::WorldPosition* destination;
    bool keepGearLimit;
    std::uint8_t padding2[7]{};
    std::uintptr_t actor;
};
static_assert(sizeof(Arguments) == 56 && offsetof(Arguments, destination) == 32);
static_assert(offsetof(Arguments, waitAsync) == 24 && offsetof(Arguments, keepGearLimit) == 40);
static_assert(offsetof(Arguments, actor) == 48 && offsetof(Arguments, goalTolerance) == 20);

Arguments arguments(std::uintptr_t actor, const MoveParameters& p,
                    const totk::core::WorldPosition& destination, bool request) {
    return {p.actionVerb, p.minGear, p.maxGear, p.gearType, p.minimumSlowGear, p.goalTolerance,
            p.waitAsync, request, p.riderGear, p.autoSlow, p.decelerate, p.waitUntilPath, true,
            0, &destination, p.keepGearLimit, {}, actor};
}

std::uintptr_t pathController(std::uintptr_t actor) {
    return actorComponent(actor, 0x3E0) ? pointerAt(actorComponent(actor, 0x370), 0x20) : 0;
}

bool movementStopped(std::uintptr_t path, bool waitAsync) {
    if (waitAsync) {
        const auto request = readMemory<std::uintptr_t>(path + 320);
        if (request && readMemory<std::uint32_t>(request + 8) == 1) return false;
    }
    if (readMemory<std::uint8_t>(path + 696) || (readMemory<std::uint32_t>(path + 772) & 0x10001))
        return false;
    const auto state = readMemory<std::uint8_t>(path + 768);
    return state == 0 || state == 3;
}
}

bool initializeMoveApi(std::uintptr_t base) { return g_api.bind(base, kSites, "MOVE_ABI_REFUSED"); }

bool NativeMove::owns(const ActorReference& actor) const {
    return owner_ && owner_ == actor.address() && generation_ == actor.generation();
}

bool NativeMove::start(const ActorReference& actor, const MoveParameters& parameters,
                       const totk::core::WorldPosition& destination) {
    if (!g_api.ready() || owner_ || !pathController(actor.address()) || !finite(destination) ||
        !std::isfinite(parameters.goalTolerance)) {
        Logging.Log("[wolf-link] MOVE_START_REFUSED api=%u active=%u actor=%p finite=%u",
            unsigned(g_api.ready()), unsigned(owner_ != 0), reinterpret_cast<void*>(actor.address()),
            unsigned(finite(destination) && std::isfinite(parameters.goalTolerance)));
        return false;
    }
    state_ = {};
    const auto args = arguments(actor.address(), parameters, destination, true);
    g_api.at<void(*)(State*, const Arguments*)>(0x11E72F0)(&state_, &args);
    if (state_.result == 3) {
        Logging.Log("[wolf-link] MOVE_START_FAILED native=%u", state_.result);
        return false;
    }
    owner_ = actor.address();
    generation_ = actor.generation();
    keepGearLimit_ = parameters.keepGearLimit;
    return true;
}

bool NativeMove::retarget(const ActorReference& actor, const totk::core::WorldPosition& destination) {
    const auto path = owns(actor) ? pathController(actor.address()) : 0;
    if (!path || !finite(destination)) {
        Logging.Log("[wolf-link] MOVE_RETARGET_REFUSED actor=%p path=%p finite=%u",
            reinterpret_cast<void*>(actor.address()), reinterpret_cast<void*>(path), unsigned(finite(destination)));
        return false;
    }
    g_api.at<void(*)(std::uintptr_t, const totk::core::WorldPosition*)>(0x1340090)(path, &destination);
    return true;
}

std::optional<MoveOutput> NativeMove::update(const ActorReference& actor, const MoveParameters& parameters,
                                            std::optional<int> desiredGear) {
    const auto path = owns(actor) ? pathController(actor.address()) : 0;
    if (!path || !std::isfinite(parameters.goalTolerance) ||
        (desiredGear && (*desiredGear < 0 || *desiredGear > 4))) {
        Logging.Log("[wolf-link] MOVE_UPDATE_REFUSED actor=%p tolerance=%f gear=%d",
            reinterpret_cast<void*>(actor.address()), double(parameters.goalTolerance), desiredGear.value_or(-1));
        return std::nullopt;
    }
    const totk::core::WorldPosition unused{};
    const auto args = arguments(actor.address(), parameters, unused, false);
    MoveOutput result{};
    result.applyAction = !movementStopped(path, parameters.waitAsync);
    const auto selectedGear = state_.selectedGear;
    if (desiredGear) state_.selectedGear = *desiredGear;
    g_api.at<void(*)(State*, const Arguments*, std::int32_t*, std::int32_t*)>(0x11B1AB4)(
        &state_, &args, &result.actionVerb, &result.gear);
    state_.selectedGear = selectedGear;
    result.nativeResult = state_.result;
    keepGearLimit_ = parameters.keepGearLimit;
    return result;
}

void NativeMove::stop(const ActorReference& actor) {
    if (!owner_) return;
    if (owns(actor)) {
        Arguments args{};
        args.actor = actor.address();
        args.keepGearLimit = keepGearLimit_;
        g_api.at<void(*)(State*, const Arguments*)>(0x1422320)(&state_, &args);
    } else {
        Logging.Log("[wolf-link] MOVE_STOP_OWNER_GONE generation=%u", generation_);
    }
    owner_ = 0;
    state_ = {};
}

}
