#include "RecallMotionContinuity.hpp"
#include "doctest.h"
#include <limits>
using namespace self_recall::pure;

TEST_CASE("fast falling and launches preserve motion but unrelated warps reset history") {
    const float zero[3]{}, falling[3]{0, -150, 0}, fling[3]{2000, 0, 0};
    CHECK_FALSE(motionDiscontinuity({}, {0, -50, 0}, falling, falling, 1.0 / 3, 30));
    CHECK_FALSE(motionDiscontinuity({}, {66, 0, 0}, zero, fling, 1.0 / 30, 30));
    CHECK(motionDiscontinuity({}, {0, 50, 0}, falling, falling, 1.0 / 3, 30));
    CHECK(motionDiscontinuity({}, {0, 0, 50}, zero, fling, 1.0 / 30, 30));
    CHECK(motionDiscontinuity({}, {0, -50, 0}, zero, zero, 1.0 / 3, 30));
    CHECK_FALSE(motionDiscontinuity({}, {30, 0, 0}, nullptr, nullptr, 0, 30));
}
TEST_CASE("paused stale and corrupt time cannot excuse a teleport") {
    const float falling[3]{0, -150, 0};
    const float invalid[3]{0, std::numeric_limits<float>::quiet_NaN(), 0};
    for (double seconds : {0.0, -1.0, 1.1, std::numeric_limits<double>::quiet_NaN()})
        CHECK(motionDiscontinuity({}, {0, -50, 0}, falling, falling, seconds, 30));
    CHECK(motionDiscontinuity({}, {0, -50, 0}, nullptr, invalid, 0.5, 30));
}
