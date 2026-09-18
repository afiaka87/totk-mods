// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include "PositionZip.hpp"

using namespace zonai_hookshot::pure;

TEST_CASE("position zip freezes a straight endpoint half a metre before anchor") {
    PositionZipState state{};
    REQUIRE(beginPositionZip(state, {0.0f, 0.0f, 0.0f}, {30.0f, 40.0f, 0.0f}));
    CHECK(state.travelDistance == doctest::Approx(49.5f));
    CHECK(state.endpoint.x == doctest::Approx(29.7f));
    CHECK(state.endpoint.y == doctest::Approx(39.6f));
    CHECK(distance(state.endpoint, Vec3{30.0f, 40.0f, 0.0f}) ==
          doctest::Approx(0.5f));
}

TEST_CASE("position zip advances exactly one metre per 60 Hz update") {
    PositionZipState state{};
    REQUIRE(beginPositionZip(state, {2.0f, 3.0f, 4.0f}, {12.0f, 3.0f, 4.0f}));
    Vec3 next{};
    CHECK(stepPositionZip(state, next) == PositionZipResult::Continue);
    CHECK(next.x == doctest::Approx(3.0f));
    CHECK(next.y == doctest::Approx(3.0f));
    CHECK(next.z == doctest::Approx(4.0f));
    CHECK(state.advanced == doctest::Approx(1.0f));
}

TEST_CASE("position zip reaches exact endpoint without overshooting") {
    PositionZipState state{};
    REQUIRE(beginPositionZip(state, {0.0f, 0.0f, 0.0f}, {3.5f, 0.0f, 0.0f}));
    Vec3 next{};
    CHECK(stepPositionZip(state, next) == PositionZipResult::Continue);
    CHECK(next.x == doctest::Approx(1.0f));
    CHECK(stepPositionZip(state, next) == PositionZipResult::Continue);
    CHECK(next.x == doctest::Approx(2.0f));
    CHECK(stepPositionZip(state, next) == PositionZipResult::Reached);
    CHECK(next.x == doctest::Approx(3.0f));
    CHECK(positionZipRemaining(state) == doctest::Approx(0.0f));
    CHECK_FALSE(state.active);
}

TEST_CASE("position zip rejects spans inside the standoff and bad configuration") {
    PositionZipState state{};
    CHECK_FALSE(beginPositionZip(state, {}, {0.5f, 0.0f, 0.0f}));
    PositionZipConfig bad{};
    bad.updateRate = 0.0f;
    CHECK_FALSE(beginPositionZip(state, {}, {20.0f, 0.0f, 0.0f}, bad));
}

TEST_CASE("position zip timeout is bounded") {
    PositionZipConfig config{};
    config.maxTicks = 2;
    PositionZipState state{};
    REQUIRE(beginPositionZip(state, {}, {20.0f, 0.0f, 0.0f}, config));
    Vec3 next{};
    CHECK(stepPositionZip(state, next, config) == PositionZipResult::Continue);
    CHECK(stepPositionZip(state, next, config) == PositionZipResult::Continue);
    CHECK(stepPositionZip(state, next, config) == PositionZipResult::Timeout);
}
