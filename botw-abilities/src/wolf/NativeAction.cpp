// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "NativeAction.hpp"
#include "NativeApi.hpp"
#include <limits>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
NativeApi g_api;
constexpr totk::port::Site kSites[]{
    {0xA29340, 0xD10243FF, 0xA9067BFD},
    {0x1BC8B38, 0x91083008, 0x720000BF},
};
constexpr std::int32_t kRun = 4, kWait = 40, kBusy = 3;
constexpr unsigned kBusyLogs = 24;
unsigned g_busyLogged{};
constexpr float kUnspecified = std::numeric_limits<float>::quiet_NaN();

struct ActionRequest {
    totk::core::WorldPosition target{kUnspecified, 0, 0};
    totk::core::WorldPosition facing{kUnspecified, 0, 0};
    totk::core::WorldPosition velocity{kUnspecified, 0, 0};
    std::int32_t result = 4, overrideVerb = 284;
    float distance = kUnspecified, rate = kUnspecified;
    std::int32_t targetSlot = 18;
    const char* variation{};
    bool hasVariation{};
    std::uint8_t padding[7]{};
    std::int32_t slot{}, verb{};
    std::uint64_t flags{};
    std::uintptr_t invoker{};
};
static_assert(sizeof(ActionRequest) == 96 && offsetof(ActionRequest, invoker) == 88);
static_assert(offsetof(ActionRequest, variation) == 56 && offsetof(ActionRequest, verb) == 76);
static_assert(offsetof(ActionRequest, result) == 36 && offsetof(ActionRequest, flags) == 80);

}

bool initializeActionApi(std::uintptr_t base) { return g_api.bind(base, kSites, "ACTION_ABI_REFUSED"); }

PathActionResult applyPathAction(const ActorReference& actor, const MoveOutput& move,
                                 const PathActionParameters& parameters, bool enter) {
    if (!g_api.ready() || !actor.address() || (move.actionVerb != kRun && move.actionVerb != kWait)) {
        Logging.Log("[wolf-link] PATH_ACTION_REFUSED api=%u actor=%p verb=%d",
            unsigned(g_api.ready()), reinterpret_cast<void*>(actor.address()), move.actionVerb);
        return PathActionResult::Refused;
    }
    const auto bsa = actorComponent(actor.address(), 0x158);
    const auto quadruped = actorComponent(actor.address(), 0x3E0);
    const auto slot = pointerAt(bsa, 0x1F0);
    if (!quadruped || !slot) {
        Logging.Log("[wolf-link] PATH_ACTION_COMPONENT_MISSING bsa=%p quad=%p slot=%p",
            reinterpret_cast<void*>(bsa), reinterpret_cast<void*>(quadruped), reinterpret_cast<void*>(slot));
        return PathActionResult::Refused;
    }
    g_api.at<void(*)(std::uintptr_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t, bool, bool)>(
        0x1BC8B38)(quadruped, parameters.gearType, move.gear, parameters.minimumGear,
                  parameters.stopAndTurn ? 2 : 0, parameters.allowGearDown, false);
    ActionRequest request{};
    request.verb = move.actionVerb;
    const auto result = g_api.at<std::int32_t(*)(std::uintptr_t, const ActionRequest*, bool)>(0xA29340)(
        bsa, &request, enter);
    if (result == 0) return PathActionResult::Applied;
    if (result == kBusy) {
        if (g_busyLogged++ < kBusyLogs)
            Logging.Log("[wolf-link] PATH_ACTION_BUSY verb=%d gear=%d enter=%u", move.actionVerb, move.gear, unsigned(enter));
        return PathActionResult::Busy;
    }
    Logging.Log("[wolf-link] PATH_ACTION_REJECTED verb=%d gear=%d enter=%u native=%d",
        move.actionVerb, move.gear, unsigned(enter), result);
    return PathActionResult::Refused;
}

}
