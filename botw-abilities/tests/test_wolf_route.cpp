// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <doctest.h>
#include "../src/wolf/NativeNavigation.hpp"
#include "../src/wolf/RouteMove.hpp"
#include <string>
#include <vector>

using namespace botw_abilities::wolf;
using totk::core::WorldPosition;

namespace {
struct RouteWorld {
    std::vector<std::string> events;
    Reachability line = Reachability::Reachable;
    FollowStatus path = FollowStatus::Running;
    bool beginPath = true, retargetPath = true;
    Reachability ahead = Reachability::Reachable;
    FollowStatus escape = FollowStatus::Running;
    int gear{};
    int lineChecks{};
    PathGoal goal{-1, true};
    void reset() { *this = {}; }
} world;

ActorReference actor() { return ActorReference{}; }

totk::core::WorldTransform g_frame{};

RouteRequest follow(WorldPosition target) {
    RouteRequest request;
    request.target = target;
    request.delta = 1;
    request.routes = true;
    request.routeDistance = kFollowRouteDistance;
    return request;
}

bool saw(const char* event) {
    for (const auto& seen : world.events)
        if (seen == event) return true;
    return false;
}
}

namespace botw_abilities::wolf {
ActorReference::~ActorReference() = default;

bool CloseFollow::begin(const ActorReference&) {
    world.events.push_back("close-begin");
    return true;
}
FollowStatus CloseFollow::update(const ActorReference&, const ActorReference&, const WorldPosition&, float, bool, int,
                                 int) {
    world.events.push_back("close-update");
    gear_ = std::uint8_t(world.gear);
    return FollowStatus::Running;
}
void CloseFollow::stop(const ActorReference&) { world.events.push_back("close-stop"); }

bool PathFollow::begin(const ActorReference&, FollowRange range, const WorldPosition&, PathGoal goal) {
    world.events.push_back(range == FollowRange::Near ? "path-near" : "path-far");
    world.goal = goal;
    return world.beginPath;
}
bool PathFollow::beginFixed(const ActorReference&, int gear, const WorldPosition&, PathGoal goal) {
    world.events.push_back("path-gear-" + std::to_string(gear));
    world.goal = goal;
    return world.beginPath;
}
bool PathFollow::retarget(const ActorReference&, const WorldPosition&) {
    world.events.push_back("path-retarget");
    return world.retargetPath;
}
PathFollowResult PathFollow::update(const ActorReference&, float) {
    world.events.push_back("path-update");
    return {world.path, std::nullopt};
}
void PathFollow::stop(const ActorReference&) { world.events.push_back("path-stop"); }

Reachability probeFollowStep(const ActorReference&, const totk::core::RotationBasis&) { return world.ahead; }

FollowStatus CliffEscape::begin(const ActorReference&, const totk::core::WorldTransform&, const WorldPosition&, float) {
    world.events.push_back("escape-begin");
    return world.escape;
}
FollowStatus CliffEscape::update(const ActorReference&, const totk::core::WorldTransform&, const WorldPosition&, float) {
    world.events.push_back("escape-update");
    return world.escape;
}
void CliffEscape::stop(const ActorReference&) { world.events.push_back("escape-stop"); }

Reachability probeFollowPoint(const ActorReference&, const WorldPosition&) {
    ++world.lineChecks;
    return world.line;
}
}

TEST_CASE("A close target with an open line is followed straight") {
    world.reset();
    RouteMove move;
    CHECK(move.update(actor(), actor(), {}, follow({5, 0, 0})) == FollowStatus::Running);
    CHECK(saw("close-begin"));
    CHECK(saw("close-update"));
    CHECK_FALSE(saw("path-far"));
}

TEST_CASE("Past the path distance he plans a far route, and drops it on coming close") {
    world.reset();
    RouteMove move;
    static_cast<void>(move.update(actor(), actor(), {}, follow({30, 0, 0})));
    CHECK(saw("path-far"));
    CHECK(saw("path-update"));
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {25, 0, 0}, follow({30, 0, 0})));
    CHECK(saw("path-stop"));
    CHECK(saw("close-begin"));
}

TEST_CASE("A line blocked on two consecutive probes uses the near route") {
    world.reset();
    world.line = Reachability::Blocked;
    RouteMove move;
    static_cast<void>(move.update(actor(), actor(), {}, follow({5, 0, 0})));
    CHECK_FALSE(saw("path-near"));
    for (int i = 0; i < 8; ++i) static_cast<void>(move.update(actor(), actor(), {}, follow({5, 0, 0})));
    CHECK(saw("path-near"));
}

TEST_CASE("A line query that cannot answer reads as open") {
    world.reset();
    world.line = Reachability::Unavailable;
    RouteMove move;
    static_cast<void>(move.update(actor(), actor(), {}, follow({5, 0, 0})));
    CHECK_FALSE(saw("path-near"));
    CHECK(saw("close-update"));
}

TEST_CASE("The line is only asked for every eight ticks") {
    world.reset();
    RouteMove move;
    for (int i = 0; i < 16; ++i) static_cast<void>(move.update(actor(), actor(), {}, follow({5, 0, 0})));
    CHECK(world.lineChecks == 2);
}

TEST_CASE("A running far route gets its new destination every sixty ticks") {
    world.reset();
    RouteMove move;
    WorldPosition position{};
    int retargets = 0;
    for (int i = 0; i < 121; ++i) {
        position.z += 1;
        world.events.clear();
        static_cast<void>(move.update(actor(), actor(), position, follow({50, 0, position.z})));
        retargets += saw("path-retarget");
    }
    CHECK(retargets == 2);
}

TEST_CASE("A lead holds its fixed gear and restarts when the gear changes") {
    world.reset();
    RouteMove move;
    auto lead = follow({40, 0, 0});
    lead.always = true;
    lead.routeGear = 2;
    static_cast<void>(move.update(actor(), actor(), {}, lead));
    CHECK(saw("path-gear-2"));
    world.events.clear();
    lead.routeGear = 4;
    static_cast<void>(move.update(actor(), actor(), {}, lead));
    CHECK(saw("path-stop"));
    CHECK(saw("path-gear-4"));
}

TEST_CASE("A lead that has reached its point stops planning") {
    world.reset();
    RouteMove move;
    auto lead = follow({7, 0, 0});
    lead.always = true;
    lead.routeGear = 2;
    static_cast<void>(move.update(actor(), actor(), {}, lead));
    CHECK(saw("close-update"));
    CHECK(world.lineChecks == 0);
}

TEST_CASE("Three refused routes run him straight, then routes are tried again") {
    world.reset();
    world.beginPath = false;
    RouteMove move;
    const auto far = follow({30, 0, 0});
    for (int i = 0; i < RoutePlan::kStrikes; ++i) static_cast<void>(move.update(actor(), actor(), {}, far));
    CHECK_FALSE(saw("close-update"));
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {}, far));
    CHECK(saw("close-update"));
    CHECK_FALSE(saw("path-far"));
    for (int i = 0; i < int(RoutePlan::kOffTicks); ++i) static_cast<void>(move.update(actor(), actor(), {}, far));
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {}, far));
    CHECK(saw("path-far"));
}

TEST_CASE("A route that gave up is reported; one that arrived is not a failure") {
    world.reset();
    RouteMove move;
    const auto far = follow({30, 0, 0});
    world.path = FollowStatus::Failed;
    CHECK(move.update(actor(), actor(), {}, far) == FollowStatus::Failed);
    CHECK(saw("path-stop"));
    world.path = FollowStatus::Succeeded;
    CHECK(move.update(actor(), actor(), {}, far) == FollowStatus::Succeeded);
}

TEST_CASE("A route that leaves him standing counts against routes") {
    world.reset();
    RouteMove move;
    const auto far = follow({30, 0, 0});
    for (int i = 0; i < int(RoutePlan::kStallTicks) * RoutePlan::kStrikes + 4; ++i)
        static_cast<void>(move.update(actor(), actor(), {}, far));
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {}, far));
    CHECK(saw("close-update"));
}

TEST_CASE("Travelling along a route clears earlier refusals") {
    world.reset();
    RouteMove move;
    auto far = follow({100, 0, 0});
    world.beginPath = false;
    static_cast<void>(move.update(actor(), actor(), {}, far));
    static_cast<void>(move.update(actor(), actor(), {}, far));
    world.beginPath = true;
    static_cast<void>(move.update(actor(), actor(), {}, far));
    static_cast<void>(move.update(actor(), actor(), {2, 0, 0}, far));
    world.beginPath = false;
    world.path = FollowStatus::Failed;
    static_cast<void>(move.update(actor(), actor(), {4, 0, 0}, far));
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {4, 0, 0}, far));
    CHECK_FALSE(saw("close-update"));
}

TEST_CASE("With routes off he only runs straight and never asks about the line") {
    world.reset();
    RouteMove move;
    auto far = follow({30, 0, 0});
    far.routes = false;
    static_cast<void>(move.update(actor(), actor(), {}, far));
    CHECK(saw("close-update"));
    CHECK(world.lineChecks == 0);
}

namespace {
RouteRequest chase(WorldPosition target, bool rush) {
    RouteRequest request;
    request.target = target;
    request.delta = 1;
    request.routes = request.chase = true;
    request.routeGear = 4;
    request.arrival = rush ? kRushTolerance : kBattleArrival;
    request.goal = rush ? PathGoal{kRushTolerance, false} : PathGoal{kBattleTolerance, true};
    request.retarget = rush ? kRushRetarget : RoutePlan::kNearRetarget;
    return request;
}
}

TEST_CASE("A rush plans a full-run route onto the enemy even with an open line") {
    world.reset();
    RouteMove move;
    CHECK(move.update(actor(), actor(), {}, chase({7, 0, 0}, true)) == FollowStatus::Running);
    CHECK(saw("path-gear-4"));
    CHECK(world.goal.tolerance == 0.0f);
    CHECK_FALSE(world.goal.autoSlow);
    CHECK_FALSE(saw("close-update"));
    CHECK(world.lineChecks == 0);
}

TEST_CASE("The battle approach plans beyond 9 units, keeps its route to 6, and goes straight inside") {
    world.reset();
    RouteMove move;
    static_cast<void>(move.update(actor(), actor(), {}, chase({20, 0, 0}, false)));
    CHECK(saw("path-gear-4"));
    CHECK(world.goal.tolerance == 6.0f);
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {12, 0, 0}, chase({20, 0, 0}, false)));
    CHECK(saw("path-update"));
    CHECK_FALSE(saw("close-update"));
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {15, 0, 0}, chase({20, 0, 0}, false)));
    CHECK(saw("path-stop"));
    CHECK(saw("close-update"));
}

TEST_CASE("Moving from the approach to the rush restarts the route with the new tolerance") {
    world.reset();
    RouteMove move;
    static_cast<void>(move.update(actor(), actor(), {}, chase({20, 0, 0}, false)));
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {}, chase({20, 0, 0}, true)));
    CHECK(saw("path-stop"));
    CHECK(saw("path-gear-4"));
    CHECK(world.goal.tolerance == 0.0f);
}

TEST_CASE("The rush updates its goal every ten ticks") {
    world.reset();
    RouteMove move;
    int retargets = 0;
    for (int i = 0; i < 31; ++i) {
        world.events.clear();
        static_cast<void>(move.update(actor(), actor(), {float(i % 2), 0, 0}, chase({30, 0, float(i)}, true)));
        retargets += saw("path-retarget");
    }
    CHECK(retargets == 3);
}

TEST_CASE("A refused chase route is reported every time and never run straight") {
    world.reset();
    world.beginPath = false;
    RouteMove move;
    for (int i = 0; i < RoutePlan::kStrikes * 3; ++i)
        CHECK(move.update(actor(), actor(), {}, chase({10, 0, 0}, true)) == FollowStatus::Failed);
    CHECK_FALSE(saw("close-update"));
    world.beginPath = true;
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {}, follow({30, 0, 0})));
    CHECK(saw("path-far"));
}

TEST_CASE("A chase route that gives up or stalls is reported, with no cliff escape") {
    world.reset();
    world.path = FollowStatus::Failed;
    RouteMove move;
    CHECK(move.update(actor(), actor(), {}, chase({30, 0, 0}, true)) == FollowStatus::Failed);
    CHECK_FALSE(saw("escape-begin"));
    world.reset();
    RouteMove stalled;
    auto status = FollowStatus::Running;
    for (int i = 0; i < int(RoutePlan::kStallTicks) + 2 && status == FollowStatus::Running; ++i)
        status = stalled.update(actor(), actor(), {}, chase({30, 0, 0}, true));
    CHECK(status == FollowStatus::Failed);
}

TEST_CASE("Stopping ends only what is running") {
    world.reset();
    RouteMove move;
    move.stop(actor());
    CHECK(world.events.empty());
    static_cast<void>(move.update(actor(), actor(), {}, follow({30, 0, 0})));
    world.events.clear();
    move.stop(actor());
    CHECK(saw("path-stop"));
    CHECK_FALSE(saw("close-stop"));
    world.events.clear();
    move.stop(actor());
    CHECK(world.events.empty());
}

namespace {
RouteRequest withCliff(WorldPosition target, float leaderDistance = 5) {
    auto request = follow(target);
    request.frame = &g_frame;
    request.leaderDistance = leaderDistance;
    return request;
}

// Starts an escape from a failed route, then lets it finish with the given result.
FollowStatus escapeOnce(RouteMove& move, const RouteRequest& request, FollowStatus result) {
    world.path = FollowStatus::Failed;
    world.escape = FollowStatus::Running;
    static_cast<void>(move.update(actor(), actor(), {}, request));
    world.escape = result;
    return move.update(actor(), actor(), {}, request);
}
}

TEST_CASE("No ground ahead at speed for several ticks starts the cliff escape") {
    world.reset();
    world.gear = 3;
    world.ahead = Reachability::Blocked;
    RouteMove move;
    for (int i = 0; i < 4; ++i) static_cast<void>(move.update(actor(), actor(), {}, withCliff({5, 0, 0})));
    CHECK_FALSE(saw("escape-begin"));
    static_cast<void>(move.update(actor(), actor(), {}, withCliff({5, 0, 0})));
    CHECK(saw("escape-begin"));
    CHECK(saw("close-stop"));
    world.events.clear();
    static_cast<void>(move.update(actor(), actor(), {}, withCliff({5, 0, 0})));
    CHECK(saw("escape-update"));
    CHECK_FALSE(saw("close-update"));
}

TEST_CASE("Walking, standing, an open corridor or an unanswered query does not start it") {
    for (const int gear : {0, 1}) {
        world.reset();
        world.gear = gear;
        world.ahead = Reachability::Blocked;
        RouteMove move;
        for (int i = 0; i < 6; ++i) static_cast<void>(move.update(actor(), actor(), {}, withCliff({5, 0, 0})));
        CHECK_FALSE(saw("escape-begin"));
    }
    for (const auto answer : {Reachability::Reachable, Reachability::Unavailable}) {
        world.reset();
        world.gear = 3;
        world.ahead = answer;
        RouteMove move;
        for (int i = 0; i < 6; ++i) static_cast<void>(move.update(actor(), actor(), {}, withCliff({5, 0, 0})));
        CHECK_FALSE(saw("escape-begin"));
    }
}

TEST_CASE("A move with no cliff child ignores the corridor") {
    world.reset();
    world.gear = 3;
    world.ahead = Reachability::Blocked;
    RouteMove move;
    for (int i = 0; i < 6; ++i) static_cast<void>(move.update(actor(), actor(), {}, follow({5, 0, 0})));
    CHECK_FALSE(saw("escape-begin"));
}

TEST_CASE("A route that gives up escapes first, where the move has a cliff child") {
    world.reset();
    world.path = FollowStatus::Failed;
    RouteMove move;
    CHECK(move.update(actor(), actor(), {}, withCliff({30, 0, 0})) == FollowStatus::Running);
    CHECK(saw("escape-begin"));
}

TEST_CASE("A finished escape returns him to following") {
    world.reset();
    RouteMove move;
    CHECK(escapeOnce(move, withCliff({30, 0, 0}), FollowStatus::Succeeded) == FollowStatus::Running);
    CHECK(saw("escape-stop"));
    world.events.clear();
    world.path = FollowStatus::Running;
    static_cast<void>(move.update(actor(), actor(), {}, withCliff({30, 0, 0})));
    CHECK(saw("path-update"));
}

TEST_CASE("An escape that fails is reported at once") {
    world.reset();
    RouteMove move;
    CHECK(escapeOnce(move, withCliff({30, 0, 0}), FollowStatus::Failed) == FollowStatus::Failed);
}

TEST_CASE("The third escape gives up far from Link and carries on near him") {
    for (const float distance : {40.f, 20.f}) {
        world.reset();
        RouteMove move;
        const auto far = withCliff({30, 0, 0}, distance);
        CHECK(escapeOnce(move, far, FollowStatus::Succeeded) == FollowStatus::Running);
        CHECK(escapeOnce(move, far, FollowStatus::Succeeded) == FollowStatus::Running);
        const auto third = escapeOnce(move, far, FollowStatus::Succeeded);
        CHECK(third == (distance > 30 ? FollowStatus::Failed : FollowStatus::Running));
    }
}

TEST_CASE("A quiet stretch clears the escape count") {
    world.reset();
    RouteMove move;
    const auto far = withCliff({30, 0, 0}, 40);
    CHECK(escapeOnce(move, far, FollowStatus::Succeeded) == FollowStatus::Running);
    CHECK(escapeOnce(move, far, FollowStatus::Succeeded) == FollowStatus::Running);
    world.path = FollowStatus::Running;
    auto quiet = far;
    quiet.delta = 601;
    static_cast<void>(move.update(actor(), actor(), {}, quiet));
    CHECK(escapeOnce(move, far, FollowStatus::Succeeded) == FollowStatus::Running);
}

TEST_CASE("Leaving the move or stopping ends an escape") {
    world.reset();
    RouteMove move;
    world.gear = 3;
    world.ahead = Reachability::Blocked;
    for (int i = 0; i < 5; ++i) static_cast<void>(move.update(actor(), actor(), {}, withCliff({5, 0, 0})));
    world.events.clear();
    world.ahead = Reachability::Reachable;
    static_cast<void>(move.update(actor(), actor(), {}, follow({5, 0, 0})));
    CHECK(saw("escape-stop"));
    CHECK(saw("close-update"));
    world.ahead = Reachability::Blocked;
    for (int i = 0; i < 5; ++i) static_cast<void>(move.update(actor(), actor(), {}, withCliff({5, 0, 0})));
    world.events.clear();
    move.stop(actor());
    CHECK(saw("escape-stop"));
}
