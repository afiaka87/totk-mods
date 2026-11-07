// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "../src/wolf/CliffTurn.hpp"
#include "../src/wolf/NativeInput.hpp"
#include "../src/wolf/NativeMotion.hpp"
#include <array>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

using namespace botw_abilities::wolf;
using totk::core::WorldPosition;

namespace {
struct TurnWorld {
    std::uint32_t generation = 9;
    bool available = true, playable = true, writable = true, acquirable = true;
    WorldPosition position{}, forward{0, 0, 1};
    AnimationState animation{true, false, false};
    std::vector<std::pair<std::string_view, float>> writes;
    int plays{}, reads{}, releases{}, liveReleases{};
} world;
}

namespace botw_abilities::wolf {
ActorReference::ActorReference(std::uintptr_t actor) : actor_(actor) {}
ActorReference::~ActorReference() = default;
std::uint32_t ActorReference::generation() const { return world.generation; }
bool AnimationOverride::owns(const ActorReference& actor) const {
    return owner_ && owner_ == actor.address() && generation_ == actor.generation();
}
bool AnimationOverride::acquire(const ActorReference& actor) {
    if (!world.acquirable) return false;
    owner_ = actor.address();
    generation_ = actor.generation();
    return true;
}
void AnimationOverride::release(const ActorReference& actor) {
    if (owner_) {
        ++world.releases;
        world.liveReleases += owns(actor);
        owner_ = 0;
    }
}
std::optional<MotionState> readMotion(const ActorReference&) {
    ++world.reads;
    if (!world.available) return std::nullopt;
    return MotionState{world.forward, {}, 1};
}
std::optional<ActorKinematics> readKinematics(const ActorReference&) {
    if (!world.available) return std::nullopt;
    ActorKinematics result{};
    result.transform.position = world.position;
    return result;
}
std::optional<AnimationState> readAnimation(const ActorReference&, const char* command) {
    CHECK(std::string_view(command) == "Turn_Escape_NoGrowl");
    if (!world.available) return std::nullopt;
    return world.animation;
}
bool playAnimation(const ActorReference&, const char* command, float rate, float delta, bool ignoreSame) {
    CHECK(std::string_view(command) == "Turn_Escape_NoGrowl");
    CHECK(rate == 1);
    CHECK(delta == .5f);
    CHECK(ignoreSame);
    ++world.plays;
    return world.playable;
}
bool setAnimationFloat(const ActorReference&, const char* name, float value) {
    world.writes.emplace_back(name, value);
    return world.writable;
}
}

TEST_CASE("Wolf cliff turn drives the original clip and follows physical facing") {
    world = {};
    const auto actor = ActorReference::fromLive(0x2000);
    CliffTurn turn;
    REQUIRE(turn.begin(actor, {4, 0, 0}, .5f));
    REQUIRE(world.writes.size() == 2);
    CHECK(world.writes[0] == std::pair<std::string_view, float>{"StickX", 0.0f});
    CHECK(world.writes[1].first == "DirectionAngle");
    CHECK(world.writes[1].second == doctest::Approx(90));
    world.forward = {1, 0, 0};
    CHECK(turn.update(actor) == FollowStatus::Running);
    CHECK(world.writes.back().second == 0);
    CHECK(world.plays == 1);
    turn.stop(actor);
    CHECK(world.liveReleases == 1);
}

TEST_CASE("Wolf cliff turn honors NoChangeAS and requires its own animation to finish") {
    world = {};
    const auto actor = ActorReference::fromLive(0x2000);
    CliffTurn turn;
    REQUIRE(turn.begin(actor, {4, 0, 0}, .5f));
    world.animation = {false, true, true};
    const auto reads = world.reads;
    const auto writes = world.writes.size();
    CHECK(turn.update(actor) == FollowStatus::Running);
    CHECK(world.reads == reads);
    CHECK(world.writes.size() == writes);
    world.animation = {true, true, true};
    CHECK(turn.update(actor) == FollowStatus::Succeeded);
    CHECK(world.writes.size() == writes);
    CHECK(world.releases == 0);
    turn.stop(actor);
    CHECK(world.liveReleases == 1);
    turn.stop(actor);
    CHECK(world.liveReleases == 1);
}

TEST_CASE("Wolf cliff turn abandons expired generations without touching a replacement") {
    world = {};
    const auto actor = ActorReference::fromLive(0x2000);
    CliffTurn turn;
    REQUIRE(turn.begin(actor, {4, 0, 0}, .5f));
    ++world.generation;
    CHECK(turn.update(actor) == FollowStatus::Failed);
    CHECK(world.releases == 1);
    CHECK(world.liveReleases == 0);
    CHECK(world.writes.size() == 2);
}

TEST_CASE("Wolf cliff turn restores ownership on unavailable animation inputs") {
    for (int failure = 0; failure < 4; ++failure) {
        world = {};
        const auto actor = ActorReference::fromLive(0x2000);
        CliffTurn turn;
        if (failure == 0) world.available = false;
        if (failure == 1) world.playable = false;
        if (failure == 2) world.writable = false;
        if (failure == 3) world.acquirable = false;
        CHECK_FALSE(turn.begin(actor, {4, 0, 0}, .5f));
        CHECK(world.liveReleases == (failure == 3 ? 0 : 1));
        CHECK(turn.update(actor) == FollowStatus::Failed);
    }
    world = {};
    const auto actor = ActorReference::fromLive(0x2000);
    CliffTurn turn;
    REQUIRE(turn.begin(actor, {4, 0, 0}, .5f));
    world.available = false;
    CHECK(turn.update(actor) == FollowStatus::Failed);
    CHECK(world.liveReleases == 1);
}

TEST_CASE("Wolf cliff turn rejects malformed targets and deltas before taking ownership") {
    for (int failure = 0; failure < 4; ++failure) {
        world = {};
        const auto actor = ActorReference::fromLive(0x2000);
        CliffTurn turn;
        WorldPosition target{4, 0, 0};
        float delta = .5f;
        if (failure == 0) target.x = std::numeric_limits<float>::infinity();
        if (failure == 1) target.z = std::numeric_limits<float>::quiet_NaN();
        if (failure == 2) delta = -1;
        if (failure == 3) delta = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(turn.begin(actor, target, delta));
        CHECK(world.releases == 0);
        CHECK(world.plays == 0);
    }
}
