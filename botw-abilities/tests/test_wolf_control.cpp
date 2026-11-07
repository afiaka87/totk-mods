// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "../src/wolf/FollowControl.hpp"
#include <cmath>

using namespace botw_abilities::wolf;

TEST_CASE("Wolf cliff escapes retry, then wander near the player and give up far away") {
    const auto first = countCliffEscape(0, false, 100);
    CHECK(first.attempts == 1);
    CHECK_FALSE(first.wander);
    CHECK_FALSE(first.gaveUp);
    CHECK(countCliffEscape(0, true, 100).gaveUp);
    const auto third = countCliffEscape(2, false, 900);
    CHECK(third.wander);
    CHECK_FALSE(third.gaveUp);
    const auto far = countCliffEscape(2, false, std::nextafter(900.0f, 1000.0f));
    CHECK(far.wander);
    CHECK(far.gaveUp);
    CHECK(countCliffEscape(2, true, 100).gaveUp);
}

TEST_CASE("Wolf cliff escape count keeps the original's signed byte comparison") {
    const auto signedWrap = countCliffEscape(127, false, 100000);
    CHECK(signedWrap.attempts == 128);
    CHECK_FALSE(signedWrap.wander);
    const auto wrapped = countCliffEscape(255, false, 100000);
    CHECK(wrapped.attempts == 0);
    CHECK_FALSE(wrapped.wander);
}
