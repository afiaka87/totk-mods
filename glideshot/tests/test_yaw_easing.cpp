// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include <cmath>
#include <limits>
#include <string>

#include "YawEasing.hpp"

using namespace zonai_hookshot::pure;

namespace {
constexpr float kPi = 3.14159265358979323846f;

void yawBasis(float radians, float out[9]) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    REQUIRE(rotateBasisWorldYaw(basis, radians, out));
    CHECK(out[2] == doctest::Approx(s).epsilon(0.0001));
    CHECK(out[8] == doctest::Approx(c).epsilon(0.0001));
}
}  // namespace

TEST_CASE("accepted outward wall normal becomes inward Link facing") {
    const Vec3 desired = wallFacingDirection({-1, 0, 0}, {0, 0, 1});
    CHECK(desired.x == doctest::Approx(1.0f));
    CHECK(desired.y == doctest::Approx(0.0f));
    CHECK(desired.z == doctest::Approx(0.0f));

    const Vec3 fallback = wallFacingDirection({0, 1, 0}, {0.6f, 0.8f, 0.2f});
    CHECK(fallback.x == doctest::Approx(0.6f));
    CHECK(fallback.y == doctest::Approx(0.0f));
    CHECK(fallback.z == doctest::Approx(0.2f));
}

TEST_CASE("yaw planner chooses the shortest angular path across wrap") {
    float basis[9]{};
    yawBasis(170.0f * kPi / 180.0f, basis);
    const Vec3 desired{std::sin(-170.0f * kPi / 180.0f), 0.0f,
                       std::cos(-170.0f * kPi / 180.0f)};
    YawState state{};
    REQUIRE(prepareYaw(state, basis, desired, 100.0f, YawTiming::Early));
    CHECK(state.signedRadians * 180.0f / kPi == doctest::Approx(20.0f).epsilon(0.001));
}

TEST_CASE("glider yaw remains dormant until exact admission then obeys journey bounds") {
    const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    YawState state{};
    REQUIRE(prepareYaw(state, basis, {1, 0, 0}, 100.0f,
                       YawTiming::OnParasail));
    CHECK_FALSE(state.started);
    CHECK(yawEasedProgress(state) == doctest::Approx(0.0f));
    REQUIRE(startYawOnParasail(state, 10.0f, 100.0f));
    CHECK(state.durationDistance == doctest::Approx(20.0f));
    advanceYaw(state, 20.0f);
    CHECK(state.finished);
}

TEST_CASE("late short-trip glider admission compresses to one bounded update") {
    const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    YawState state{};
    REQUIRE(prepareYaw(state, basis, {-1, 0, 0}, 12.0f,
                       YawTiming::OnParasail));
    REQUIRE(startYawOnParasail(state, 10.0f, 12.0f));
    CHECK(state.durationDistance == doctest::Approx(1.0f));
    advanceYaw(state, 1.0f);
    CHECK(state.finished);
}

TEST_CASE("world yaw preserves captured pitch and roll basis") {
    const float c = std::cos(30.0f * kPi / 180.0f);
    const float s = std::sin(30.0f * kPi / 180.0f);
    const float tilted[9] = {1, 0, 0, 0, c, -s, 0, s, c};
    float out[9]{};
    REQUIRE(rotateBasisWorldYaw(tilted, 90.0f * kPi / 180.0f, out));
    CHECK(out[3] == doctest::Approx(tilted[3]));
    CHECK(out[4] == doctest::Approx(tilted[4]));
    CHECK(out[5] == doctest::Approx(tilted[5]));
    for (int column = 0; column < 3; ++column) {
        const float before = std::sqrt(tilted[column] * tilted[column] +
                                       tilted[3 + column] * tilted[3 + column] +
                                       tilted[6 + column] * tilted[6 + column]);
        const float after = std::sqrt(out[column] * out[column] +
                                      out[3 + column] * out[3 + column] +
                                      out[6 + column] * out[6 + column]);
        CHECK(after == doctest::Approx(before).epsilon(0.0001));
    }
}

TEST_CASE("yaw planner rejects non-finite or degenerate inputs") {
    const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    YawState state{};
    CHECK_FALSE(prepareYaw(state, basis, {0, 1, 0}, 20.0f, YawTiming::Early));
    CHECK_FALSE(prepareYaw(state, basis, {1, 0, 0},
                           std::numeric_limits<float>::quiet_NaN(),
                           YawTiming::Early));
}
