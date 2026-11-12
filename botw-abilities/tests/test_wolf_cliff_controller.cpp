// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <doctest.h>
#include "../src/wolf/CliffController.hpp"
#include "../src/wolf/CliffEscape.hpp"
#include "../src/wolf/NativeAction.hpp"
#include "../src/wolf/NativeNavigation.hpp"
#include <limits>
#include <string_view>
#include <vector>

using namespace botw_abilities::wolf;
using totk::core::WorldPosition;

namespace {
struct CliffWorld {
    std::uint32_t generation = 9;
    FollowStatus turn = FollowStatus::Running;
    MoveOutput move{0, 4, 3, true};
    CliffSearchResult result{{8, 0, 0}};
    bool start = true, apply = true, movement = true;
    PathActionResult action = PathActionResult::Applied;
    std::vector<std::string_view> events;
    std::vector<bool> entries;
    std::vector<WorldPosition> targets;
    int liveStops{}, expiredStops{};
    std::optional<NavigationSurface> footing, ground;
    std::optional<WorldPosition> fan;
    WorldPosition fanFacing{}, fanGoal{};
    std::vector<float> ranges;

    CliffSearchResult search(WorldPosition) {
        events.push_back("search");
        return result;
    }
} world;

totk::core::WorldTransform transform() {
    totk::core::WorldTransform value{};
    value.rotation.values[8] = 1;
    return value;
}

void checkMoveParameters(const MoveParameters& p) {
    CHECK(p.actionVerb == 4);
    CHECK(p.minGear == 3);
    CHECK(p.maxGear == 3);
    CHECK(p.gearType == 2);
    CHECK(p.minimumSlowGear == 1);
    CHECK(p.goalTolerance == 5);
    CHECK_FALSE((p.waitAsync || p.riderGear || p.autoSlow || p.decelerate || p.waitUntilPath || p.keepGearLimit));
}
}

namespace botw_abilities::wolf {
ActorReference::ActorReference(std::uintptr_t actor) : actor_(actor) {}
ActorReference::~ActorReference() = default;
std::uint32_t ActorReference::generation() const { return world.generation; }

std::optional<NavigationSurface> nearestNavigationSurface(const WorldPosition&, float range) {
    world.ranges.push_back(range);
    return range <= 3 ? world.footing : world.ground;
}
std::optional<WorldPosition> findCliffEscape(
    const ActorReference&, const WorldPosition& forward, const WorldPosition& reference) {
    world.fanFacing = forward;
    world.fanGoal = reference;
    return world.fan;
}

bool CliffTurn::begin(const ActorReference&, const WorldPosition& target, float delta) {
    CHECK(delta == .5f);
    world.events.push_back("turn-enter");
    world.targets.push_back(target);
    status_ = world.start ? FollowStatus::Running : FollowStatus::Failed;
    return world.start;
}
FollowStatus CliffTurn::update(const ActorReference&) {
    world.events.push_back("turn-tick");
    return status_ == FollowStatus::Failed ? status_ : world.turn;
}
void CliffTurn::stop(const ActorReference&) { world.events.push_back("turn-leave"); }

bool NativeMove::owns(const ActorReference& actor) const {
    return owner_ && owner_ == actor.address() && generation_ == actor.generation();
}
bool NativeMove::start(const ActorReference& actor, const MoveParameters& p, const WorldPosition& target) {
    checkMoveParameters(p);
    world.events.push_back("move-enter");
    world.targets.push_back(target);
    if (!world.start) return false;
    owner_ = actor.address();
    generation_ = actor.generation();
    return true;
}
std::optional<MoveOutput> NativeMove::update(const ActorReference& actor, const MoveParameters& p,
                                           std::optional<int> desired) {
    CHECK(owns(actor));
    CHECK_FALSE(desired);
    checkMoveParameters(p);
    world.events.push_back("move-tick");
    if (!world.movement) return std::nullopt;
    return world.move;
}
void NativeMove::stop(const ActorReference& actor) {
    if (!owner_) return;
    world.events.push_back("move-leave");
    if (owns(actor)) ++world.liveStops;
    else ++world.expiredStops;
    owner_ = 0;
}
PathActionResult applyPathAction(const ActorReference&, const MoveOutput& move, const PathActionParameters& p, bool enter) {
    CHECK(move.gear == 3);
    CHECK(p.gearType == 2);
    CHECK(p.minimumGear == 1);
    CHECK_FALSE((p.allowGearDown || p.stopAndTurn));
    world.events.push_back("action");
    world.entries.push_back(enter);
    return world.apply ? world.action : PathActionResult::Refused;
}
}

TEST_CASE("Cliff recovery observes completion before ticking the selected child") {
    world = {};
    auto search = [](WorldPosition seed) { return world.search(seed); };
    const auto actor = ActorReference::fromLive(0x2000);
    CliffController controller;
    CHECK(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
    CHECK(world.events == std::vector<std::string_view>{"search", "turn-enter"});
    world.events.clear();
    world.turn = FollowStatus::Succeeded;
    CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    CHECK(world.events == std::vector<std::string_view>{"turn-tick"});
    world.events.clear();
    CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    CHECK(world.events == std::vector<std::string_view>{"turn-leave", "move-enter", "move-tick", "action"});
    CHECK(world.targets.back().x == 8);
    CHECK(world.entries == std::vector<bool>{true});
    world.move.nativeResult = 2;
    CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Succeeded);
    CHECK(world.entries == std::vector<bool>{true, false, false});
    controller.stop(actor);
    CHECK(world.liveStops == 1);
}

TEST_CASE("Cliff search retry restarts the turn before ticking it") {
    world = {};
    world.result.target = {};
    auto search = [](WorldPosition seed) { return world.search(seed); };
    const auto actor = ActorReference::fromLive(0x2000);
    CliffController controller;
    REQUIRE(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
    CHECK(world.targets.back().z == 1);
    world.events.clear();
    world.result.target = {12, 4, 0};
    CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    CHECK(world.events == std::vector<std::string_view>{"search", "turn-leave", "turn-enter", "turn-tick"});
    CHECK(world.targets.back().x == 12);
    controller.stop(actor);
}

TEST_CASE("Missing navigation pauses only the parent and the fifth miss still ticks the child") {
    world = {};
    world.result.target = {};
    auto search = [](WorldPosition seed) { return world.search(seed); };
    const auto actor = ActorReference::fromLive(0x2000);
    CliffController controller;
    REQUIRE(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
    world.events.clear();
    CHECK(controller.update(actor, transform(), .5f, false, search) == FollowStatus::Running);
    CHECK(world.events == std::vector<std::string_view>{"turn-tick"});
    for (int retry = 0; retry < 4; ++retry) {
        world.events.clear();
        CHECK(controller.update(actor, transform(), .5f, true, search) ==
              (retry == 3 ? FollowStatus::Failed : FollowStatus::Running));
        CHECK(world.events == std::vector<std::string_view>{"search", "turn-tick"});
    }
    world.events.clear();
    CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Failed);
    CHECK(world.events == std::vector<std::string_view>{"turn-tick"});
    controller.stop(actor);
}

TEST_CASE("A target reached during the turn reenters a forward turn before failing") {
    world = {};
    auto search = [](WorldPosition seed) { return world.search(seed); };
    const auto actor = ActorReference::fromLive(0x2000);
    CliffController controller;
    REQUIRE(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
    world.turn = FollowStatus::Succeeded;
    REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    auto moved = transform();
    moved.position = {8, 3, 0};
    world.events.clear();
    CHECK(controller.update(actor, moved, .5f, true, search) == FollowStatus::Failed);
    CHECK(world.events == std::vector<std::string_view>{"turn-leave", "turn-enter", "turn-tick"});
    CHECK(world.targets.back().x == 8);
    CHECK(world.targets.back().y == 3);
    CHECK(world.targets.back().z == 1);
    controller.stop(actor);
}

TEST_CASE("Cliff movement retains its entry action until native movement publishes") {
    world = {};
    auto search = [](WorldPosition seed) { return world.search(seed); };
    const auto actor = ActorReference::fromLive(0x2000);
    CliffController controller;
    REQUIRE(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
    world.turn = FollowStatus::Succeeded;
    REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    world.move.applyAction = false;
    REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    CHECK(world.entries.empty());
    world.move.applyAction = true;
    world.action = PathActionResult::Busy;
    REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    // A busy action slot keeps the entry pending rather than failing the move.
    CHECK(world.entries == std::vector<bool>{true, true});
    world.action = PathActionResult::Applied;
    REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    CHECK(world.entries == std::vector<bool>{true, true, true, false});
    controller.stop(actor);
}

TEST_CASE("Expired actors refuse all further searches and child ticks") {
    for (bool moving : {false, true}) {
        world = {};
        auto search = [](WorldPosition seed) { return world.search(seed); };
        const auto actor = ActorReference::fromLive(0x2000);
        CliffController controller;
        REQUIRE(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
        if (moving) {
            world.turn = FollowStatus::Succeeded;
            REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
            REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
        }
        ++world.generation;
        world.events.clear();
        CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Failed);
        CHECK(world.events == std::vector<std::string_view>{moving ? "move-leave" : "turn-leave"});
        CHECK(world.liveStops == 0);
        CHECK(world.expiredStops == int(moving));
    }
}

TEST_CASE("Cliff recovery refuses invalid timing before search and reports child failures on the next update") {
    world = {};
    auto search = [](WorldPosition seed) { return world.search(seed); };
    const auto actor = ActorReference::fromLive(0x2000);
    CliffController controller;
    CHECK(controller.begin(actor, transform(), -1, search) == FollowStatus::Failed);
    CHECK(world.events.empty());
    REQUIRE(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
    world.turn = FollowStatus::Failed;
    CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
    CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Failed);
    controller.stop(actor);

    for (int failure = 0; failure < 4; ++failure) {
        world = {};
        REQUIRE(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
        world.turn = FollowStatus::Succeeded;
        REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
        if (failure == 0) world.start = false;
        if (failure == 1) world.move.nativeResult = 3;
        if (failure == 2) world.movement = false;
        if (failure == 3) world.apply = false;
        REQUIRE(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Running);
        CHECK(controller.update(actor, transform(), .5f, true, search) == FollowStatus::Failed);
        controller.stop(actor);
    }
    world = {};
    REQUIRE(controller.begin(actor, transform(), .5f, search) == FollowStatus::Running);
    world.events.clear();
    CHECK(controller.update(actor, transform(), std::numeric_limits<float>::quiet_NaN(), true, search) == FollowStatus::Failed);
    CHECK(world.events == std::vector<std::string_view>{"turn-leave"});
}

TEST_CASE("With footing nearby the escape fans out, preferring ground near where he was heading") {
    world = {};
    world.footing = NavigationSurface{{1, 0, 0}};
    world.fan = WorldPosition{6, 0, 2};
    const auto actor = ActorReference::fromLive(0x2000);
    CliffEscape escape;
    CHECK(escape.begin(actor, transform(), {30, 0, 40}, .5f) == FollowStatus::Running);
    REQUIRE(world.targets.size() == 1);
    CHECK(world.targets[0].x == 6);
    CHECK(world.ranges == std::vector<float>{3});
    CHECK(world.fanFacing.z == 1);
    CHECK(world.fanGoal.x == 30);
    CHECK(world.fanGoal.z == 40);
    escape.stop(actor);
}

TEST_CASE("Without footing the escape makes for the nearest ground") {
    world = {};
    world.ground = NavigationSurface{{4, 0, 9}};
    world.fan = WorldPosition{6, 0, 2};
    const auto actor = ActorReference::fromLive(0x2000);
    CliffEscape escape;
    CHECK(escape.begin(actor, transform(), {30, 0, 40}, .5f) == FollowStatus::Running);
    REQUIRE(world.targets.size() == 1);
    CHECK(world.targets[0].z == 9);
    CHECK(world.ranges == std::vector<float>{3, 10});
    escape.stop(actor);
}

TEST_CASE("With no ground in reach, or no open direction, the escape reports failure") {
    const auto actor = ActorReference::fromLive(0x2000);
    for (const bool footing : {false, true}) {
        world = {};
        if (footing) world.footing = NavigationSurface{{1, 0, 0}};
        CliffEscape escape;
        CHECK(escape.begin(actor, transform(), {30, 0, 40}, .5f) == FollowStatus::Failed);
        escape.stop(actor);
    }
}
