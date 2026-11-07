// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "../src/wolf/TurnFacing.hpp"
#include <limits>
#include <numbers>

using namespace botw_abilities::wolf;

TEST_CASE("Wolf turn input preserves original clockwise degree sign") {
    constexpr float pi = std::numbers::pi_v<float>;
    CHECK(turnFacing({}, {0, 0, 1}, {4, 0, 0}, pi).animationAngle == doctest::Approx(90));
    CHECK(turnFacing({}, {0, 0, 1}, {-4, 0, 0}, pi).animationAngle == doctest::Approx(-90));
    CHECK(turnFacing({}, {0, 0, 1}, {0, 0, -4}, pi).animationAngle == doctest::Approx(-180));
}

TEST_CASE("Wolf turn clamps the animation blend without hiding the remaining angle") {
    const auto turn = turnFacing({}, {0, 0, 1}, {3, 300, 0}, std::numbers::pi_v<float> / 4);
    CHECK(turn.animationAngle == doctest::Approx(45));
    CHECK(turn.remaining == doctest::Approx(std::numbers::pi_v<float> / 2));
}

TEST_CASE("Wolf turn falls back to current facing for absent or coincident targets") {
    constexpr float pi = std::numbers::pi_v<float>;
    CHECK(turnFacing({1, 2, 3}, {0, 0, 1}, {1, -50, 3}, pi).remaining == 0);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(turnFacing({}, {0, 0, 1}, {nan, 0, nan}, pi).remaining == 0);
}
