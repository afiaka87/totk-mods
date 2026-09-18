// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include "CaptureMath.hpp"

using namespace zonai_hookshot::pure;

TEST_CASE("capture vector moves inward with a small downward component") {
    const Vec3 velocity = captureVelocity(
        {0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
    CHECK(length(velocity) == doctest::Approx(4.0f));
    CHECK(velocity.x > 3.8f);
    CHECK(velocity.y < -0.1f);
    CHECK(velocity.z == doctest::Approx(0.0f));
}

TEST_CASE("capture vector never turns an elevated anchor into upward travel") {
    const Vec3 velocity = captureVelocity(
        {0.0f, 0.0f, 0.0f}, {3.0f, 4.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
    CHECK(length(velocity) == doctest::Approx(4.0f));
    CHECK(velocity.x > 0.0f);
    CHECK(velocity.y < 0.0f);
}

TEST_CASE("capture vector falls back to target direction for a bad normal") {
    const Vec3 velocity = captureVelocity(
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 5.0f}, {});
    CHECK(length(velocity) == doctest::Approx(4.0f));
    CHECK(velocity.z > 3.8f);
    CHECK(velocity.y < 0.0f);
}

TEST_CASE("capture vector rejects degenerate inputs") {
    CHECK(length(captureVelocity({}, {}, {-1.0f, 0.0f, 0.0f})) ==
          doctest::Approx(0.0f));
    CaptureConfig bad{};
    bad.speed = 0.0f;
    CHECK(length(captureVelocity({}, {1.0f, 0.0f, 0.0f},
                                     {-1.0f, 0.0f, 0.0f}, bad)) ==
          doctest::Approx(0.0f));
}
