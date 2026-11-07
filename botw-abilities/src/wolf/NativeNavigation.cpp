// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeNavigation.hpp"
#include "CliffSearch.hpp"
#include "NativeApi.hpp"
#include "WorldMath.hpp"
#include <cmath>
#include <limits>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using totk::core::WorldPosition;
NativeApi g_api;
constexpr CodeSite kSites[]{
    {0xB29BF0, 0xD10283FF, 0xFD003BE8}, {0x1103F40, 0xD2EFF808, 0xB20A23E9},
    {0x1103F2C, 0x3940B408, 0x35000048}, {0xEA10C8, 0xF9411408, 0xB4000068},
    {0xA60304, 0xD10283FF, 0xA9067BFD},
    {0xE8D2F4, 0xD10283FF, 0xA9067BFD},
    {0x2ABEA9C, 0x52800020, 0xD65F03C0},
    {0x1024428, 0xD100C3FF, 0xA9017BFD},
    {0xC73558, 0xBD400020, 0x1E202000}, {0x141829C, 0xBD409000, 0x1E202000},
    {0x28E2074, 0xF9401108, 0xBD400121}, {0xBB3214, 0xF940012A, 0xB9400929},
    {0xA5F184, 0x394C1A68, 0x37080968}, {0xBB2FD0, 0x394C010B, 0x35FFFF2B},
    {0x1619D98, 0xD10083FF, 0xA9017BFD}, {0x156D3B8, 0xA9BF7BFD, 0x910003FD},
    {0x1619DD8, 0xAA0003E8, 0xB942E800}, {0x161A00C, 0x37F80081, 0xB942E808},
    {0x170CE88, 0xA9BE7BFD, 0xF9000BF3}, {0xE438F8, 0xB940000C, 0x379802EC},
    {0x1EE1FF4, 0xB900DC08, 0xB9430408}, {0x1EE2014, 0xF9400108, 0xF9400908},
    {0x170BA9C, 0xA9BA7BFD, 0xA9016FFC}, {0x170C31C, 0xD104C3FF, 0xA90D7BFD},
    {0xB2AEE4, 0xD10643FF, 0xA9167BFD},
};
constexpr float kUnset = std::numeric_limits<float>::quiet_NaN();

struct Request {
    WorldPosition start{kUnset, 0, 0};
    float radius = 0.1f;
    WorldPosition destination{kUnset, 0, 0};
    WorldPosition direction{kUnset, 0, 0};
    float distance = kUnset;
    float searchDistance = 10.0f;
    std::int32_t characterType = 35;
};
static_assert(sizeof(Request) == 52 && offsetof(Request, destination) == 16);
static_assert(offsetof(Request, searchDistance) == 44 && offsetof(Request, characterType) == 48);

Request directionRequest(const WorldPosition& direction, float distance) {
    Request request;
    request.direction = direction;
    request.distance = distance;
    return request;
}

struct SurfaceRequest {
    WorldPosition start, end;
    std::int32_t characterType{};
    std::uint32_t padding{};
    std::uintptr_t filter{}, costFilter{};
    std::uint16_t details{};
};
static_assert(sizeof(SurfaceRequest) == 56 && offsetof(SurfaceRequest, end) == 12);
static_assert(offsetof(SurfaceRequest, filter) == 32 && offsetof(SurfaceRequest, details) == 48);

struct NearestSurfaceRequest {
    std::int32_t characterType{};
    WorldPosition position;
    float radius;
    std::uint32_t padding{};
    std::uintptr_t filter{}, costFilter{};
    std::uint16_t details{};
};
static_assert(sizeof(NearestSurfaceRequest) == 48 && offsetof(NearestSurfaceRequest, position) == 4);
static_assert(offsetof(NearestSurfaceRequest, radius) == 16 && offsetof(NearestSurfaceRequest, filter) == 24);
static_assert(offsetof(NearestSurfaceRequest, costFilter) == 32 && offsetof(NearestSurfaceRequest, details) == 40);

// BotW's initial nearest query has no face filter; TotK substitutes one even for character type zero.
struct GeometryFilter {
    std::uintptr_t callbacks[8];
    struct Object {
        const std::uintptr_t* callbacks;
        std::uint64_t referenceCount = 0xFFFFFFFF;
        std::uint64_t flags = 1;
    } object{callbacks};

    GeometryFilter() {
        for (unsigned i = 0; i < 8; ++i)
            callbacks[i] = readMemory<std::uintptr_t>(g_api.base() + 0x45F3450 + i * sizeof(std::uintptr_t));
        callbacks[5] = g_api.base() + 0x2ABEA9C;
    }
};
static_assert(sizeof(GeometryFilter::Object) == 24 && sizeof(GeometryFilter) == 88);

struct Result {
    WorldPosition points[4]{{kUnset, 0, 0}, {kUnset, 0, 0}, {kUnset, 0, 0}, {kUnset, 0, 0}};
    alignas(8) std::byte face[88]{};
    Result() { g_api.at<void(*)(void*)>(0x1103F40)(face); }
    ~Result() { g_api.at<void(*)(void*)>(0x1103F2C)(face); }
    [[nodiscard]] bool reached() const { return face[80] != std::byte{}; }
    [[nodiscard]] NavigationSurface surface() const {
        return {points[0], readMemory<std::uint32_t>(reinterpret_cast<std::uintptr_t>(face) + 64) >> 26};
    }
};
static_assert(sizeof(Result) == 136 && offsetof(Result, face) == 48);

std::uintptr_t navigationWorld() { return g_api.singleton(0x462DF30); }

void cancelSteering(std::uintptr_t controller) { g_api.at<void(*)(std::uintptr_t)>(0x141829C)(controller); }

struct SteeringState {
    std::uint8_t pending, recovering;
    std::uint32_t flags;
    [[nodiscard]] bool idle() const { return !pending && !recovering && !(flags & 0x10001); }
};

SteeringState steeringState(std::uintptr_t controller) {
    return {readMemory<std::uint8_t>(controller + 768), readMemory<std::uint8_t>(controller + 696),
            readMemory<std::uint32_t>(controller + 772)};
}

std::uintptr_t pathController(const ActorReference& actor) {
    if (!g_api.ready() || !actor.address()) {
        Logging.Log("[wolf-link] NAV_INPUT_REFUSED api=%u actor=%p",
            unsigned(g_api.ready()), reinterpret_cast<void*>(actor.address()));
        return 0;
    }
    const auto component = g_api.at<std::uintptr_t(*)(std::uintptr_t)>(0xEA10C8)(actor.address());
    const auto controller = isPlausibleAddress(component) ? readMemory<std::uintptr_t>(component + 32) : 0;
    if (!isPlausibleAddress(controller)) {
        Logging.Log("[wolf-link] NAV_CONTROLLER_MISSING id=%u path=%p", actor.generation(),
            reinterpret_cast<void*>(controller));
        return 0;
    }
    return controller;
}

struct ReachablePoint {
    Reachability status = Reachability::Unavailable;
    std::optional<WorldPosition> position;
};

ReachablePoint queryReachable(const ActorReference& actor, Request& request) {
    const auto controller = pathController(actor);
    if (!controller) return {};
    const auto world = navigationWorld();
    if (!world) {
        Logging.Log("[wolf-link] NAV_CONTEXT_UNAVAILABLE id=%u path=%p world=%p", actor.generation(),
            reinterpret_cast<void*>(controller), reinterpret_cast<void*>(world));
        return {};
    }
    Result result;
    if (!g_api.at<bool(*)(std::uintptr_t, Request*, Result*)>(0xB29BF0)(controller, &request, &result))
        return {Reachability::Blocked, std::nullopt};
    if (!finite(result.points[2])) {
        Logging.Log("[wolf-link] NAV_RESULT_INVALID id=%u", actor.generation());
        return {};
    }
    return {Reachability::Reachable, result.points[2]};
}

ReachablePoint queryPoint(const ActorReference& actor, const WorldPosition& point) {
    if (!finite(point)) {
        Logging.Log("[wolf-link] NAV_CANDIDATE_INVALID");
        return {};
    }
    Request request;
    request.destination = point;
    return queryReachable(actor, request);
}
}

bool initializeNavigationApi(std::uintptr_t base) { return g_api.bind(base, kSites, "NAV_ABI_REFUSED"); }

std::optional<NavigationSurface> sampleNavigationSurface(const WorldPosition& start, const WorldPosition& end) {
    if (!g_api.ready() || !finite(start) || !finite(end)) {
        Logging.Log("[wolf-link] NAV_SURFACE_INPUT_REFUSED api=%u start=%u end=%u",
            unsigned(g_api.ready()), unsigned(finite(start)), unsigned(finite(end)));
        return std::nullopt;
    }
    const auto world = navigationWorld();
    if (!world) {
        Logging.Log("[wolf-link] NAV_SURFACE_WORLD_UNAVAILABLE");
        return std::nullopt;
    }
    SurfaceRequest request{start, end};
    Result result;
    using Query = bool(*)(std::uintptr_t, const SurfaceRequest*, WorldPosition*, void*);
    if (!g_api.at<Query>(0xA60304)(world, &request, &result.points[0], result.face)) return std::nullopt;
    if (!finite(result.points[0])) {
        Logging.Log("[wolf-link] NAV_SURFACE_RESULT_INVALID");
        return std::nullopt;
    }
    return result.surface();
}

std::optional<WorldPosition> reachableFollowPoint(const ActorReference& actor, const WorldPosition& candidate) {
    return queryPoint(actor, candidate).position;
}

std::optional<NavigationSurface> nearestNavigationSurface(const WorldPosition& position, float searchDistance) {
    if (!g_api.ready() || !finite(position) || !std::isfinite(searchDistance) || searchDistance < 0) {
        Logging.Log("[wolf-link] NAV_NEAREST_INPUT_REFUSED api=%u position=%u distance=%f",
            unsigned(g_api.ready()), unsigned(finite(position)), double(searchDistance));
        return std::nullopt;
    }
    const auto world = navigationWorld();
    if (!world) {
        Logging.Log("[wolf-link] NAV_NEAREST_WORLD_UNAVAILABLE");
        return std::nullopt;
    }
    NearestSurfaceRequest request{.position = position, .radius = searchDistance};
    GeometryFilter geometry;
    request.filter = reinterpret_cast<std::uintptr_t>(&geometry.object);
    Result result;
    using Query = bool(*)(std::uintptr_t, const NearestSurfaceRequest*, WorldPosition*, void*);
    if (!g_api.at<Query>(0xE8D2F4)(world, &request, &result.points[0], result.face)) return std::nullopt;
    if (!finite(result.points[0])) {
        Logging.Log("[wolf-link] NAV_NEAREST_RESULT_INVALID");
        return std::nullopt;
    }
    return result.surface();
}

Reachability probeFollowDirection(
    const ActorReference& actor, const WorldPosition& direction, float distance) {
    if (!finite(direction) || !std::isfinite(distance)) {
        Logging.Log("[wolf-link] NAV_DIRECTION_INVALID direction=%u distance=%f",
            unsigned(finite(direction)), double(distance));
        return Reachability::Unavailable;
    }
    auto request = directionRequest(direction, distance);
    return queryReachable(actor, request).status;
}

Reachability probeFollowStep(const ActorReference& actor, const totk::core::RotationBasis& rotation) {
    const WorldPosition facing{rotation.values[2], 0, rotation.values[8]};
    if (!finite(facing)) {
        Logging.Log("[wolf-link] NAV_FACING_INVALID");
        return Reachability::Unavailable;
    }
    auto request = directionRequest(horizontalUnit(facing), 0.75f);
    return queryReachable(actor, request).status;
}

Reachability probeFollowPoint(const ActorReference& actor, const WorldPosition& point) {
    return queryPoint(actor, point).status;
}

std::optional<WorldPosition> readPathGoal(const ActorReference& actor) {
    const auto controller = pathController(actor);
    if (!controller) return std::nullopt;
    // Nontrivial destruction passes the native twelve-byte result through X8.
    struct Goal {
        WorldPosition position;
        ~Goal() {}
    };
    static_assert(sizeof(Goal) == 12);
    const auto goal = g_api.at<Goal(*)(std::uintptr_t)>(0x1024428)(controller);
    if (!std::isfinite(goal.position.x)) return std::nullopt;
    if (!finite(goal.position)) {
        Logging.Log("[wolf-link] NAV_GOAL_INVALID id=%u", actor.generation());
        return std::nullopt;
    }
    return goal.position;
}

std::optional<WorldPosition> findCliffEscape(
    const ActorReference& actor, const WorldPosition& forward, const WorldPosition& reference) {
    if (!finite(forward)) {
        Logging.Log("[wolf-link] CLIFF_DIRECTION_INVALID x=%f y=%f z=%f",
            double(forward.x), double(forward.y), double(forward.z));
        return std::nullopt;
    }
    const auto controller = pathController(actor);
    const auto world = controller ? navigationWorld() : 0;
    if (!controller || !world) {
        Logging.Log("[wolf-link] CLIFF_CONTEXT_UNAVAILABLE controller=%p world=%p",
            reinterpret_cast<void*>(controller), reinterpret_cast<void*>(world));
        return std::nullopt;
    }
    const auto directions = cliffSearchDirections(forward);
    std::array<Request, kCliffDirectionCount> requests;
    std::array<Result, kCliffDirectionCount> results;
    for (unsigned i = 0; i < requests.size(); ++i) {
        requests[i] = directionRequest(directions[i], 10.0f);
        requests[i].direction.y = 0.0f;
        // BotW's negative radius sentinel becomes TotK's NaN: use the controller radius.
        requests[i].radius = kUnset;
    }
    using Query = std::uint32_t(*)(std::uintptr_t, Request*, int, Request*, int, Result*, int);
    g_api.at<Query>(0x170BA9C)(controller, requests.data(), 0, requests.data(),
                           kCliffDirectionCount, results.data(), 0);
    CliffEscapeChoice choice(results.front().points[2]);
    for (const auto& result : results)
        choice.consider(result.points[0], result.points[2], result.reached(), reference);
    const auto endpoint = choice.endpoint();
    return finite(endpoint) ? std::optional{endpoint} : std::nullopt;
}

bool setFollowSteering(const ActorReference& actor, const WorldPosition& velocity,
                       const WorldPosition& target, float speed) {
    if (!finite(velocity) || !finite(target) || !std::isfinite(speed) || speed < 0) {
        Logging.Log("[wolf-link] NAV_STEERING_INPUT_INVALID velocity=%u target=%u speed=%f",
            unsigned(finite(velocity)), unsigned(finite(target)), double(speed));
        return false;
    }
    const auto controller = pathController(actor);
    if (!controller) return false;
    const auto steering = steeringState(controller);
    if (!steering.idle()) {
        cancelSteering(controller);
        Logging.Log("[wolf-link] NAV_LOCAL_STEERING_UNAVAILABLE state=%u recovery=%u flags=%x",
            unsigned(steering.pending), unsigned(steering.recovering), steering.flags);
        return false;
    }
    const float maximum = readMemory<float>(controller + 172);
    if (!std::isfinite(maximum) || maximum < 0) {
        Logging.Log("[wolf-link] NAV_SPEED_LIMIT_INVALID maximum=%f", double(maximum));
        return false;
    }
    // Preserve BotW's raised maximum while giving its desired speed priority over vector magnitude.
    writeMemory(controller + 168, speed);
    writeMemory(controller + 172, maximum < speed ? speed : maximum);
    writeMemory(controller + 772, steering.flags & ~0x20000u);
    g_api.at<void(*)(std::uintptr_t, const WorldPosition*)>(0xC73558)(controller, &velocity);
    writeMemory(controller + 156, target);
    return true;
}

bool clearFollowSteering(const ActorReference& actor) {
    const auto controller = pathController(actor);
    if (!controller) return false;
    cancelSteering(controller);
    return true;
}

bool beginLocalFollowing(const ActorReference& actor) {
    const auto controller = pathController(actor);
    const auto requests = pointerAt(navigationWorld(), 16);
    if (!controller || !requests) {
        Logging.Log("[wolf-link] FOLLOW_CANCEL_UNAVAILABLE actor=%p", reinterpret_cast<void*>(actor.address()));
        return false;
    }
    writeMemory(controller + 220, kUnset);
    writeMemory<std::uint8_t>(controller + 696, 0);
    writeMemory<std::uint8_t>(controller + 768, 0);
    writeMemory(controller + 772, readMemory<std::uint32_t>(controller + 772) & ~0x1000u);
    cancelSteering(controller);
    const bool accepted = g_api.at<bool(*)(std::uintptr_t, std::uintptr_t, int, int)>(0xE438F8)(
        controller, requests + 40, 8, 4);
    if (!accepted) Logging.Log("[wolf-link] FOLLOW_CANCEL_QUEUE_FULL");
    return accepted;
}

std::optional<bool> localFollowingReady(const ActorReference& actor) {
    const auto controller = pathController(actor);
    if (!controller) return std::nullopt;
    return steeringState(controller).idle();
}

bool updateFollowLeader(const ActorReference& actor, const ActorReference& leader, int& slot) {
    const auto controller = pathController(actor);
    if (!controller || !leader.address()) return false;
    const auto component = g_api.at<std::uintptr_t(*)(const ActorReference*)>(0x1619D98)(&leader);
    const auto target = isPlausibleAddress(component)
        ? g_api.at<std::uintptr_t(*)(const std::uintptr_t*)>(0x156D3B8)(&component) : 0;
    if (!isPlausibleAddress(target)) return false;
    if (slot < 0) {
        slot = g_api.at<int(*)(std::uintptr_t, std::uintptr_t)>(0x1619DD8)(controller, target);
        return slot >= 0;
    }
    const auto count = readMemory<int>(controller + 744);
    if (slot >= 4 || slot >= count) {
        Logging.Log("[wolf-link] FOLLOW_LEADER_SLOT_INVALID slot=%d count=%d", slot, count);
        return false;
    }
    return g_api.at<bool(*)(std::uintptr_t, int, std::uintptr_t)>(0x161A00C)(controller, slot, target);
}

void clearFollowLeader(const ActorReference& actor, int& slot) {
    if (slot >= 0 && slot < 4 && pathController(actor))
        g_api.at<void(*)(std::uintptr_t, int)>(0x170CE88)(actor.address(), slot);
    slot = -1;
}

}
