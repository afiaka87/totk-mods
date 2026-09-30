// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <limits>
#include <initializer_list>
#include "TravelBodyAnchor.hpp"
#include "TravelBodyReadShift.hpp"
using namespace zonai_hookshot::pure;

TEST_CASE("travel anchor follows rotated translated basis and preserves relative limb motion") {
    const handheld::Matrix basis{{0, 0, 1, 1400, 0, 1, 0, 300, -1, 0, 0, -2900}};
    const Vec3 animated{0.03f, 0.98f, -0.04f};
    const auto target = handheld::point(basis, animated);
    for (float lag : {0.f, 0.1f, 2.f, 8.f}) {
        const auto physical = sub(target, Vec3{lag, 0.2f * lag, -0.3f * lag});
        const auto foot = add(physical, Vec3{0.2f, -0.8f, 0.4f});
        Vec3 cached{}, shift{};
        REQUIRE(travelBodyAnchor(basis, animated, physical, cached, shift));
        const auto nativeShift = sub(handheld::position(basis), cached);
        CHECK(distance(add(physical, nativeShift), target) < 0.001f);
        CHECK(distance(sub(add(foot, nativeShift), add(physical, nativeShift)),
                       sub(foot, physical)) < 0.001f);
    }
}
TEST_CASE("travel anchor rejects invalid pose data") {
    Vec3 cached{}, shift{};
    handheld::Matrix basis{};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(travelBodyAnchor(basis, {nan, 0, 0}, {}, cached, shift));
    CHECK_FALSE(travelBodyAnchor(basis, {}, {0, nan, 0}, cached, shift));
    basis.v[0] = nan;
    CHECK_FALSE(travelBodyAnchor(basis, {}, {}, cached, shift));
}

TEST_CASE("body read shift is confined to the merge thread and owned bodies") {
    TravelBodyReadShift correction{};
    correction.thread = 11;
    correction.bodies[0] = 21;
    correction.bodies[1] = 22;
    correction.count = 2;
    correction.shift = {8, -3, 5};
    const handheld::Matrix original{{0, 0, 1, 1400, 0, 1, 0, 300, -1, 0, 0, -2900}};
    auto unrelated = original;
    CHECK_FALSE(correction.apply(12, 21, &unrelated));
    CHECK_FALSE(correction.apply(11, 23, &unrelated));
    CHECK_FALSE(correction.apply(11, 0, &unrelated));
    CHECK_FALSE(correction.apply(11, 21, nullptr));
    CHECK(distance(handheld::position(unrelated), handheld::position(original)) == 0);
    auto hip = original, foot = original;
    foot.v[7] -= .8f;
    const auto difference = sub(handheld::position(foot), handheld::position(hip));
    REQUIRE(correction.apply(11, 21, &hip));
    REQUIRE(correction.apply(11, 22, &foot));
    CHECK(distance(handheld::position(hip), add(handheld::position(original), correction.shift)) <
          .001f);
    CHECK(distance(sub(handheld::position(foot), handheld::position(hip)), difference) < .001f);
    for (unsigned i : {0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u})
        CHECK(hip.v[i] == original.v[i]);
    correction = {};
    CHECK_FALSE(correction.apply(11, 21, &hip));
}

TEST_CASE("body read shift refuses invalid inputs before changing the matrix") {
    TravelBodyReadShift correction{};
    correction.thread = 11;
    correction.bodies[0] = 21;
    correction.count = 1;
    handheld::Matrix matrix{};
    correction.shift.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(correction.apply(11, 21, &matrix));
    CHECK(matrix.v[3] == 0);
    correction.shift = {};
    correction.count = 65;
    CHECK_FALSE(correction.apply(11, 21, &matrix));
    correction.count = 1;
    matrix.v[7] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(correction.apply(11, 21, &matrix));
    CHECK(matrix.v[3] == 0);
}
