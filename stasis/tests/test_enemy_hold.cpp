// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "doctest.h"
#include "StasisEnemyHold.hpp"
#include "StasisCore.hpp"
#include "StasisActorPolicy.hpp"
#include <array>
TEST_CASE("Blupees use a creature hold without promoting other animals or helpers") {
    for (const auto name : {"Animal_RupeeRabbit_A", "Animal_RupeeRabbit_FromCaveMaster",
                           "Animal_RupeeRabbit_NushiShop", "Enemy_Bokoblin_Junior"})
        CHECK(stasis::livingTarget(name));
    CHECK_FALSE(stasis::livingTarget("Animal_RupeeRabbit_Unknown"));
    CHECK_FALSE(stasis::livingTarget("Animal_Squirrel_A"));
    CHECK_FALSE(stasis::livingTarget("Obj_WoodBox_A"));
    CHECK_FALSE(stasis::livingTarget(""));
}
TEST_CASE("Enemy hold follows the physics owner and replaces movement queued each frame") {
    // TotK GamePhysics::preCalc reads the actor at component+24, not a Character context.
    constexpr std::uintptr_t component = 0x1000, enemy = 0x2000, other = 0x3000;
    std::uintptr_t owner = enemy;
    unsigned reads = 0, stops = 0;
    const auto read = [&](std::uintptr_t address) {
        CHECK(address == component + 24);
        ++reads;
        return owner;
    };
    std::array<std::array<float, 6>, 2> queued{};
    const auto stop = [&](std::uintptr_t actor) {
        CHECK(actor == enemy);
        ++stops;
        for (auto &body : queued)
            body.fill(0);
        return true;
    };
    CHECK_FALSE(stasis::holdEnemyPhysics(0, component, read, stop));
    CHECK_FALSE(stasis::holdEnemyPhysics(enemy, 0, read, stop));
    CHECK(reads == 0);
    owner = other;
    CHECK_FALSE(stasis::holdEnemyPhysics(enemy, component, read, stop));
    CHECK(stops == 0);
    owner = enemy;
    for (unsigned frame = 0; frame < 150; ++frame) {
        // Old cached velocity and newly submitted movement: ApplyVelocity may be skipped.
        queued[0] = {4, 0, -3, 0, 2, 0};
        queued[1] = {-2, 1, 1, 1, 0, 1};
        REQUIRE(stasis::holdEnemyPhysics(enemy, component, read, stop));
        for (const auto &body : queued)
            for (float velocity : body)
                CHECK(velocity == 0);
    }
    CHECK(stops == 150);
    queued[0] = {4, 0, -3, 0, 2, 0};
    const auto resumed = queued;
    CHECK_FALSE(stasis::holdEnemyPhysics(0, component, read, stop));
    CHECK(queued == resumed);
    CHECK_FALSE(
        stasis::holdEnemyPhysics(enemy, component, read, [](std::uintptr_t) { return false; }));
}
TEST_CASE("Five-second enemy override preserves the recovered object parameters") {
    CHECK(stasis::kEnemyHoldFrames / 30.0f == 5.0f);
    CHECK(stasis::Parameters{}.shortFrames == 60.0f);
    CHECK(stasis::Parameters{}.longFrames / 30.0f == 10.0f);
}
TEST_CASE("World anchor stops transform-driven movement that ignores zero body velocity") {
    const std::array<float, 12> anchor{0, 0, 1, -130, 0, 1, 0, 129, -1, 0, 0, 481};
    auto actor = anchor, physics = anchor, output = anchor;
    std::array<float, 14> motion;
    motion.fill(9);
    unsigned originalCalls = 0, forces = 0, commits = 0;
    bool held = true;
    const auto original = [&]() -> std::uintptr_t {
        ++originalCalls;
        actor[11] -= 0.1f;
        output = actor;
        for (unsigned i = 1; i <= stasis::kActorMotionFloats; ++i)
            motion[i] = 3;
        return 0;
    };
    const auto force = [&](const float *pose) {
        ++forces;
        std::memcpy(physics.data(), pose, sizeof(physics));
    };
    const auto commit = [&](const float *pose) {
        ++commits;
        std::memcpy(actor.data(), pose, sizeof(actor));
        for (unsigned i = 1; i <= stasis::kActorMotionFloats; ++i)
            motion[i] = 0;
    };
    // Non-vacuous failed control: zero body speed does not stop direct transform updates.
    for (unsigned frame = 0; frame < 150; ++frame) {
        original();
        motion.fill(0);
    }
    CHECK(anchor[11] - actor[11] == doctest::Approx(15).epsilon(0.001));
    actor = anchor;
    originalCalls = 0;
    motion.fill(9);
    for (unsigned frame = 0; frame < 150; ++frame) {
        CHECK(stasis::updateAnchoredEnemy(anchor.data(), output.data(), original, force, commit,
                                          [&] { return held; }) == 1);
        CHECK(actor == anchor);
        CHECK(physics == anchor);
        CHECK(output == anchor);
        for (unsigned i = 1; i <= stasis::kActorMotionFloats; ++i)
            CHECK(motion[i] == 0);
        CHECK(motion.front() == 9);
        CHECK(motion.back() == 9);
    }
    CHECK(originalCalls == 150);
    CHECK(forces == 300);
    CHECK(commits == 150);
    held = false;
    CHECK(stasis::updateAnchoredEnemy(anchor.data(), output.data(), original, force, commit,
                                      [&] { return held; }) == 0);
    CHECK(actor != anchor);
    CHECK(output == actor);
    CHECK(forces == 300);
    CHECK(commits == 150);
}
TEST_CASE("Anchor identity and release during a native position update prevent stale pose writes") {
    const stasis::EnemyAnchor anchor{123, 456, 7, {1, 0, 0, 2, 0, 1, 0, 3, 0, 0, 1, 4}};
    CHECK(stasis::matchesEnemyAnchor(anchor, 123, 456, 123, 123, 7));
    CHECK_FALSE(stasis::matchesEnemyAnchor(anchor, 0, 456, 123, 123, 7));
    CHECK_FALSE(stasis::matchesEnemyAnchor(anchor, 789, 456, 123, 123, 7));
    CHECK_FALSE(stasis::matchesEnemyAnchor(anchor, 123, 789, 123, 123, 7));
    CHECK_FALSE(stasis::matchesEnemyAnchor(anchor, 123, 456, 0, 123, 7));
    CHECK_FALSE(stasis::matchesEnemyAnchor(anchor, 123, 456, 123, 0, 7));
    CHECK_FALSE(stasis::matchesEnemyAnchor(anchor, 123, 456, 123, 123, 8));
    float output[12]{};
    unsigned forces = 0, commits = 0;
    bool held = true;
    const auto result = stasis::updateAnchoredEnemy(
        anchor.matrix, output,
        [&]() -> std::uintptr_t {
            held = false;
            output[3] = 42;
            return 4;
        },
        [&](const float *) { ++forces; }, [&](const float *) { ++commits; }, [&] { return held; });
    CHECK(result == 4);
    CHECK(output[3] == 42);
    CHECK(forces == 1);
    CHECK(commits == 0);
}
