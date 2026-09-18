// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include <initializer_list>

#include "ChainPresentation.hpp"

using namespace zonai_hookshot::pure;

TEST_CASE("helix frame is orthonormal and survives a vertical chain") {
    for (const Vec3 end : {Vec3{10.0f, 2.0f, 4.0f},
                           Vec3{0.0f, 20.0f, 0.0f}}) {
        const HelixFrame frame = makeHelixFrame({0.0f, 0.0f, 0.0f}, end);
        REQUIRE(frame.valid);
        CHECK(dot(frame.axis, frame.radialA) == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(dot(frame.axis, frame.radialB) == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(dot(frame.radialA, frame.radialB) == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(length(frame.axis) == doctest::Approx(1.0f));
        CHECK(length(frame.radialA) == doctest::Approx(1.0f));
        CHECK(length(frame.radialB) == doctest::Approx(1.0f));
    }
}

TEST_CASE("opposing helix samples stay opposite around a taut centre") {
    const Vec3 from{0.0f, 0.0f, 0.0f};
    const Vec3 to{20.0f, 0.0f, 0.0f};
    const HelixFrame frame = makeHelixFrame(from, to);
    REQUIRE(frame.valid);
    for (float t : {0.0f, 0.2f, 0.5f, 0.9f, 1.0f}) {
        const Vec3 centre = lerp(from, to, t);
        const Vec3 a = helixPoint(from, to, frame, t, 0.2f, 0.75f, false);
        const Vec3 b = helixPoint(from, to, frame, t, 0.2f, 0.75f, true);
        CHECK(distance(a, centre) == doctest::Approx(0.2f).epsilon(0.001));
        CHECK(distance(b, centre) == doctest::Approx(0.2f).epsilon(0.001));
        CHECK(distance(lerp(a, b, 0.5f), centre) == doctest::Approx(0.0f).epsilon(0.001));
    }
}

TEST_CASE("latch feedback starts strong and decays smoothly to zero") {
    CHECK(latchFeedbackEnvelope(kLatchFeedbackTicks) == doctest::Approx(1.0f));
    CHECK(latchFeedbackEnvelope(kLatchFeedbackTicks / 2) > 0.45f);
    CHECK(latchFeedbackEnvelope(1) > 0.0f);
    CHECK(latchFeedbackEnvelope(0) == doctest::Approx(0.0f));
    float previous = 1.0f;
    for (std::uint32_t remaining = kLatchFeedbackTicks; remaining > 0; --remaining) {
        const float current = latchFeedbackEnvelope(remaining);
        CHECK(current <= previous + 0.0001f);
        previous = current;
    }
}
