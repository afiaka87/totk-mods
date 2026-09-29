// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cstdint>
#include <initializer_list>

#include "SurveySweep.hpp"

using namespace zonai_survey::pure;

TEST_CASE("the sweep front travels forward and never reverses") {
    float previous = -1.0f;
    for (std::uint32_t tick = 0; tick <= kSweepTicks; ++tick) {
        const float front = sweepFrontAt(static_cast<float>(tick));
        CHECK(front >= previous);
        previous = front;
    }
    CHECK(sweepFrontAt(0.0f) == doctest::Approx(0.0f));
    CHECK(sweepFrontAt(static_cast<float>(kSweepTicks)) >=
          static_cast<float>(kRings) + kSweepBandRings - 0.001f);
}

TEST_CASE("the sweep slows through the middle and hurries at both ends") {
    const auto speed = [](float u) {
        const float step = 0.002f;
        return (sweepEase(u + step) - sweepEase(u - step)) / (2.0f * step);
    };
    const float early = speed(0.10f);
    const float middle = speed(0.50f);
    const float late = speed(0.90f);
    CHECK(middle < early);
    CHECK(middle < late);
    CHECK(middle > 0.0f);
}

TEST_CASE("the birth warp is monotone, fixes both ends, and inverts") {
    CHECK(sweepBirthWarp(0.0f) == doctest::Approx(0.0f));
    CHECK(sweepBirthWarp(1.0f) == doctest::Approx(1.0f));
    float previous = -1.0f;
    for (float u = 0.0f; u <= 1.5f; u += 0.02f) {
        const float w = sweepBirthWarp(u);
        CHECK(w > previous);
        CHECK(sweepBirthWarpInverse(w) == doctest::Approx(u).epsilon(0.001));
        previous = w;
    }
}

TEST_CASE("the wave is born slowly enough at Link's feet to be seen") {
    const float toRingZero = sweepTicksToReach(waveArrivalOf(0));
    const float toRingFive = sweepTicksToReach(waveArrivalOf(5));
    CHECK(toRingZero > 8.0f);
    CHECK(toRingFive > 30.0f);
    CHECK(toRingFive < 110.0f);
}

TEST_CASE("the icon clock lands where the band actually is") {
    {
        for (const float wave :
             {0.25f, 0.5f, 0.75f, 1.0f, 8.5f, 17.0f, 30.0f, 34.0f}) {
            const float tick = sweepTicksToReach(wave);
            CHECK(sweepFrontAt(tick) ==
                  doctest::Approx(wave).epsilon(0.001f));
        }
    }
    CHECK(sweepTicksToReach(0.0f) == doctest::Approx(0.0f));
    float previous = 0.0f;
    for (const float wave : {1.0f, 8.5f, 17.0f, 30.0f, 34.0f}) {
        const float tick = sweepTicksToReach(wave);
        CHECK(tick > previous);
        previous = tick;
    }
}

TEST_CASE("the ease curve extends linearly past its end instead of clamping") {
    CHECK(sweepEase(1.0f) == doctest::Approx(1.0f));
    const float slope = 1.0f + kSweepEase;
    CHECK(sweepEase(1.2f) == doctest::Approx(1.0f + 0.2f * slope));
    CHECK(sweepEase(1.5f) > sweepEase(1.2f));
}
