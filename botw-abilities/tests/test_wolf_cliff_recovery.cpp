// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <doctest.h>
#include "../src/wolf/CliffRecovery.hpp"
#include <optional>
#include <limits>
#include <vector>

using namespace botw_abilities::wolf;
using totk::core::WorldPosition;

namespace {
struct RecoveryWorld {
    totk::core::WorldTransform transform{};
    CliffSearchResult search{{8, 0, 0}};
    std::optional<CliffChildState> child;
    std::vector<int> calls;
    WorldPosition entered{};

    RecoveryWorld() { transform.rotation.values[8] = 1; }
    auto searcher() {
        return [this](WorldPosition) { calls.push_back(0); return search; };
    }
    auto starter() {
        return [this](CliffAction action, WorldPosition target) {
            calls.push_back(1 + int(action));
            entered = target;
            child = CliffChildState{action, FollowStatus::Running};
        };
    }
    auto observer() {
        return [this] { calls.push_back(3); return child; };
    }
    CliffRecoveryState begin() { return beginCliffRecovery(transform, searcher(), starter()); }
    CliffRecoveryState step(CliffRecoveryState state, bool available = true) {
        return advanceCliffRecovery(state, transform, available, searcher(), starter(), observer());
    }
};
}

TEST_CASE("Wolf cliff recovery turns before moving and finishes after movement") {
    RecoveryWorld world;
    auto state = world.begin();
    CHECK(state.found);
    CHECK(state.attempts == 0);
    CHECK(world.calls == std::vector<int>{0, 1});
    world.child->status = FollowStatus::Succeeded;
    state = world.step(state);
    CHECK(state.status == FollowStatus::Running);
    CHECK(world.child->action == CliffAction::Move);
    CHECK(world.entered.x == 8);
    CHECK(world.calls == std::vector<int>{0, 1, 3, 2});
    world.child->status = FollowStatus::Succeeded;
    CHECK(world.step(state).status == FollowStatus::Succeeded);
}

TEST_CASE("Wolf cliff recovery includes the initial miss in its five-attempt limit") {
    RecoveryWorld world;
    world.search.target = {0, 99, 0};
    auto state = world.begin();
    CHECK_FALSE(state.found);
    CHECK(state.attempts == 1);
    CHECK(world.entered.z == 1);
    for (int attempt = 2; attempt <= 5; ++attempt) {
        state = world.step(state);
        CHECK(state.attempts == attempt);
        CHECK(state.status == (attempt == 5 ? FollowStatus::Failed : FollowStatus::Running));
    }
    CHECK(world.calls.back() == 0);
    const auto calls = world.calls.size();
    CHECK(world.step(state).status == FollowStatus::Failed);
    CHECK(world.calls.size() == calls);
}

TEST_CASE("Wolf cliff recovery observes a newly restarted turn before advancing it") {
    RecoveryWorld world;
    world.search.target = {};
    auto state = world.begin();
    world.child->status = FollowStatus::Succeeded;
    world.search.target = {6, 0, 0};
    world.calls.clear();
    state = world.step(state);
    CHECK(state.found);
    CHECK(state.attempts == 1);
    CHECK(state.status == FollowStatus::Running);
    CHECK(world.child->action == CliffAction::Turn);
    CHECK(world.calls == std::vector<int>{0, 1, 3});
}

TEST_CASE("Wolf cliff recovery pauses on missing navigation and preserves search failure") {
    RecoveryWorld world;
    world.search.target = {};
    auto state = world.begin();
    world.calls.clear();
    state = world.step(state, false);
    CHECK(state.attempts == 1);
    CHECK(world.calls.empty());
    world.search.failed = true;
    state = world.step(state);
    CHECK(state.status == FollowStatus::Failed);
    CHECK(world.calls == std::vector<int>{0, 3});
    world.search.target = {4, 0, 0};
    state = world.begin();
    CHECK(state.status == FollowStatus::Failed);
    CHECK(state.found);
    CHECK(world.child->action == CliffAction::Turn);
}

TEST_CASE("Wolf cliff recovery rejects missing or failed children and targets reached during a turn") {
    for (bool missing : {false, true}) {
        RecoveryWorld world;
        auto state = world.begin();
        if (missing) world.child.reset();
        else world.child->status = FollowStatus::Failed;
        CHECK(world.step(state).status == FollowStatus::Failed);
    }
    RecoveryWorld world;
    auto state = world.begin();
    world.transform.position = {8, 0, 0};
    world.child->status = FollowStatus::Succeeded;
    state = world.step(state);
    CHECK(state.status == FollowStatus::Failed);
    CHECK(world.child->action == CliffAction::Turn);
    CHECK(world.entered.x == 8);
    CHECK(world.entered.z == 1);
}

TEST_CASE("Wolf cliff target tolerance is horizontal and componentwise") {
    CHECK_FALSE(usableCliffTarget({.01f, 90, -.01f}, {}));
    CHECK(usableCliffTarget({.010001f, 0, 0}, {}));
    CHECK(usableCliffTarget({0, 0, -.010001f}, {}));
    CHECK_FALSE(usableCliffTarget({1, std::numeric_limits<float>::quiet_NaN(), 0}, {}));
}
