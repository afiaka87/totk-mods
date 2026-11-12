// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <doctest.h>
#include "../src/wolf/CliffSearch.hpp"
#include <cmath>
#include <limits>

using namespace botw_abilities::wolf;

TEST_CASE("Wolf cliff scan starts forward and visits ten distinct directions") {
    const auto directions = cliffSearchDirections({0, 0.25f, 1});
    CHECK(directions.front().x == 0);
    CHECK(directions.front().z == 1);
    CHECK(directions[1].x == doctest::Approx(0.58778524f));
    CHECK(directions[1].z == doctest::Approx(0.809017f));
    CHECK(directions[5].z == doctest::Approx(-1));
    CHECK(directions.back().x < 0);
    for (auto direction : directions) CHECK(direction.y == 0.25f);
}

TEST_CASE("Wolf cliff choice favors completed corridors before distance") {
    CliffEscapeChoice choice({1, 2, 3});
    choice.consider({0, 0, 0}, {100, 2, 0}, false, {});
    CHECK(choice.endpoint().x == 100);
    choice.consider({0, 0, 0}, {10, 3, 0}, true, {});
    CHECK(choice.endpoint().x == 10);
    choice.consider({0, 0, 0}, {200, 4, 0}, false, {});
    CHECK(choice.endpoint().x == 10);
    choice.consider({0, 0, 0}, {4, 5, 0}, true, {});
    CHECK(choice.endpoint().x == 4);
    choice.consider({0, 0, 0}, {-4, 99, 0}, true, {});
    CHECK(choice.endpoint().x == 4);
    CHECK(choice.endpoint().y == 5);
}

TEST_CASE("Wolf partial cliff choice uses each ray origin and preserves earlier ties") {
    CliffEscapeChoice choice({1, 2, 3});
    choice.consider({100, 0, 0}, {90, 1, 0}, false, {1000, 0, 0});
    choice.consider({0, 0, 0}, {9, 2, 0}, false, {1000, 0, 0});
    choice.consider({0, 0, 0}, {10, 3, 0}, false, {1000, 0, 0});
    CHECK(choice.endpoint().x == 90);
    CHECK(choice.endpoint().y == 1);
}

TEST_CASE("Wolf cliff choice preserves original missing-point selection") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CliffEscapeChoice choice({nan, 0, 0});
    choice.consider({nan, 0, 0}, {nan, 0, 0}, false, {});
    CHECK(std::isnan(choice.endpoint().x));
    choice.consider({}, {10, 1, 0}, true, {nan, 0, 0});
    choice.consider({}, {1, 2, 0}, true, {nan, 0, 0});
    CHECK(choice.endpoint().x == 10);
}
