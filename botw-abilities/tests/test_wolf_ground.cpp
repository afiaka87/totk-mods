// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <doctest.h>
#include "../src/wolf/GroundMotion.hpp"
#include <limits>

using namespace botw_abilities::wolf;

TEST_CASE("Wolf animation motion preserves diagonal travel and signed axes") {
    const auto motion = animationGroundVelocity({{0,0,1},{0,1,0},{0,1,0},3.0f,.75f,1.0f});
    REQUIRE(motion);
    CHECK(motion->x == doctest::Approx(2.4f));
    CHECK(motion->y == 0);
    CHECK(motion->z == doctest::Approx(1.8f));
    const auto reverse = animationGroundVelocity({{0,0,1},{0,1,0},{0,1,0},3.0f,-.75f,-1.0f});
    REQUIRE(reverse);
    CHECK(reverse->x == doctest::Approx(-2.4f));
    CHECK(reverse->z == doctest::Approx(-1.8f));
}

TEST_CASE("Wolf diagonal motion uses separate slope tangents") {
    const auto motion = animationGroundVelocity({{0,0,1},{0,1,0},{0,.8f,.6f},1.5f,.75f,1.0f});
    REQUIRE(motion);
    CHECK(motion->x == doctest::Approx(1.2f));
    CHECK(motion->y == doctest::Approx(-.54f));
    CHECK(motion->z == doctest::Approx(.72f));
}

TEST_CASE("Wolf ground motion holds clip lunges to the speed limit in both directions") {
    const auto lunge = animationGroundVelocity({{0,0,1},{0,1,0},{0,1,0},86.0f,1.0f,0.0f});
    REQUIRE(lunge);
    CHECK(lunge->z == doctest::Approx(kMaxGroundSpeed));
    CHECK(lunge->x == doctest::Approx(0));
    const auto back = animationGroundVelocity({{0,0,1},{0,1,0},{0,1,0},-30.0f,1.0f,0.0f});
    REQUIRE(back);
    CHECK(back->z == doctest::Approx(-kMaxGroundSpeed));
    const auto run = animationGroundVelocity({{0,0,1},{0,1,0},{0,1,0},13.5f,1.0f,0.0f});
    REQUIRE(run);
    CHECK(run->z == doctest::Approx(13.5f));
    const auto diagonal = animationGroundVelocity({{0,0,1},{0,1,0},{0,1,0},30.0f,.6f,.8f});
    REQUIRE(diagonal);
    CHECK(diagonal->z == doctest::Approx(9.0f));
    CHECK(diagonal->x == doctest::Approx(12.0f));
}

TEST_CASE("Wolf ground motion refuses invalid bases and remains still at zero intent") {
    const GroundMotion valid{{0,0,1},{0,1,0},{0,1,0},3,1,0};
    auto m = valid;
    m.speed = 0;
    REQUIRE(animationGroundVelocity(m));
    CHECK(animationGroundVelocity(m)->z == 0);
    m = valid; m.forward = m.side = 0;
    REQUIRE(animationGroundVelocity(m));
    CHECK(animationGroundVelocity(m)->z == 0);
    m = valid; m.facing = m.up;
    CHECK_FALSE(animationGroundVelocity(m));
    m = valid; m.surface = {};
    CHECK_FALSE(animationGroundVelocity(m));
    m = valid; m.speed = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(animationGroundVelocity(m));
    m = valid; m.side = std::numeric_limits<float>::infinity();
    CHECK_FALSE(animationGroundVelocity(m));
}
