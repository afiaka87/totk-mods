// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "../src/wolf/FollowTurning.hpp"
#include "../src/wolf/NativeMotion.hpp"
#include <array>
#include <cmath>
#include <numbers>

using namespace botw_abilities::wolf;

namespace {
const auto circle = [] {
    std::array<CircleSample, 256> table{};
    for (unsigned i = 0; i < table.size(); ++i) {
        const double angle = 2 * std::numbers::pi * i / 256;
        const double next = 2 * std::numbers::pi * (i + 1) / 256;
        table[i] = {float(std::sin(angle)), float(std::sin(next) - std::sin(angle)),
                    float(std::cos(angle)), float(std::cos(next) - std::cos(angle))};
    }
    return table;
}();
}

TEST_CASE("Wolf kept-distance turn leaves a coincident target unresolved") {
    CHECK_FALSE(keptDistanceTurn({0, 0, 1}, {1, 0, 0}, 0, 1, 2, 3, true, circle));
}

TEST_CASE("Wolf kept-distance turn curves toward its current heading") {
    const auto result = keptDistanceTurn({0, 0, 1}, {1, 0, 0}, 3, 1, 2, 3, true, circle);
    REQUIRE(result);
    CHECK(result->direction.x == doctest::Approx(std::cos(std::numbers::pi / 8)));
    CHECK(result->direction.z == doctest::Approx(std::sin(std::numbers::pi / 8)));
    CHECK(result->direction.y == 0);
    REQUIRE(result->travel);
    CHECK(result->travel->x == doctest::Approx(3 - 2 * std::cos(1.5)).epsilon(.0002));
    CHECK(result->travel->z == doctest::Approx(2 * std::sin(1.5)).epsilon(.0002));
    CHECK(result->travel->y == 0);
}

TEST_CASE("Wolf approach keeps the original left and right turning circles") {
    const auto right = approachTurn({0, 0, 1}, {1, 0, 0}, 10, 1, 1, 2, true, circle);
    const auto left = approachTurn({0, 0, 1}, {-1, 0, 0}, 10, 1, 1, 2, true, circle);
    CHECK(right.category == 0);
    CHECK(left.category == 2);
    CHECK(right.alignment == 0);
    REQUIRE(right.travel);
    REQUIRE(left.travel);
    CHECK(right.travel->x == doctest::Approx(5 * (1 - std::cos(.4))).epsilon(.002));
    CHECK(right.travel->z == doctest::Approx(5 * std::sin(.4)).epsilon(.0002));
    CHECK(left.travel->x == -right.travel->x);
    CHECK(left.travel->z == right.travel->z);
}

TEST_CASE("Wolf approach switches side classification inside the minimum turning radius") {
    CHECK(approachTurn({0, 0, 1}, {1, 0, 0}, 1, 1, 2, 2, false, circle).category == 3);
    CHECK(approachTurn({0, 0, 1}, {-1, 0, 0}, 1, 1, 2, 2, false, circle).category == 1);
    CHECK(approachTurn({0, 0, 1}, {1, 0, 0}, 2, 1, 2, 2, false, circle).category == 0);
}

TEST_CASE("Wolf approach handles aligned headings without an infinite-radius arc") {
    const auto result = approachTurn({0, 0, 5}, {0, 0, 1}, 10, 1, 2, 8, true, circle);
    REQUIRE(result.travel);
    CHECK(result.alignment == 1);
    CHECK(result.travel->x == 0);
    CHECK(result.travel->z == 8);
    CHECK_FALSE(approachTurn({0, 0, 1}, {1, 0, 0}, 1, 1, 2, 2, false, circle).travel);
}

TEST_CASE("Wolf close steering retains its turn until the wider exit circle clears") {
    FollowSteeringInput input{{100, 7, -20}, {0, 0, 1}, {101, 100, -20}, 1, 1, 0, false, false};
    auto result = steerCloseFollow(input, circle);
    REQUIRE(result.insideTurn);
    CHECK(result.velocity.x == 0);
    CHECK(result.velocity.z == 1);
    CHECK(result.target.y == 7);
    input.target.x = 103;
    CHECK_FALSE(steerCloseFollow(input, circle).insideTurn);
    input.insideTurn = true;
    CHECK(steerCloseFollow(input, circle).insideTurn);
    input.target.x = 106;
    result = steerCloseFollow(input, circle);
    CHECK_FALSE(result.insideTurn);
    CHECK(result.velocity.x == 1);
    CHECK(result.velocity.z == 0);
}

TEST_CASE("Wolf close steering projects travel only when navigation requests it") {
    FollowSteeringInput input{{100, 7, -20}, {0, 0, 1}, {100, 100, 20}, 1, 1, 0, false, true};
    const auto result = steerCloseFollow(input, circle);
    CHECK(result.target.x == 100);
    CHECK(result.target.y == 7);
    CHECK(result.target.z == -12);
    CHECK(result.velocity.z == 1);
    CHECK_FALSE(result.insideTurn);
    input.predictTravel = false;
    CHECK(steerCloseFollow(input, circle).target.z == 20);
}

TEST_CASE("Wolf kept-distance steering preserves turn state and coincident heading") {
    const auto result = steerCloseFollow({{1, 2, 3}, {.6f, .2f, .8f}, {1, 100, 3},
                                         2, 1, 1, true, true}, circle);
    CHECK(result.insideTurn);
    CHECK(result.target.x == 1);
    CHECK(result.target.y == 2);
    CHECK(result.target.z == 3);
    CHECK(result.velocity.x == doctest::Approx(1.2));
    CHECK(result.velocity.y == doctest::Approx(.4));
    CHECK(result.velocity.z == doctest::Approx(1.6));
}

TEST_CASE("Wolf native pending-state sentinels do not hold the old follow gear") {
    for (const auto state : {MotionGears{2, 1, 6, 3, 5}, MotionGears{2, 1, 2, 5, 5}}) {
        const auto transition = closeFollowTransition(state);
        CHECK(transition.pending == 0);
        CHECK(transition.remaining == 0);
        CHECK(closeFollowGear(20, transition) == 3);
    }
    CHECK(closeFollowGear(20, closeFollowTransition({2, 1, 2, 3, 5})) == 3);
    CHECK(closeFollowGear(20, closeFollowTransition({2, 1, 3, 0, 5})) == 1);
    CHECK(closeFollowGear(20, closeFollowTransition({2, 3, 2, 3, 5})) == 4);
}
