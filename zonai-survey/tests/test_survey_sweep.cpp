// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cmath>
#include <cstdlib>
#include <initializer_list>

#include "ScanBatching.hpp"
#include "SurveyPalette.hpp"
#include "SurveySweep.hpp"
#include "WallReduction.hpp"

using namespace zonai_survey::pure;

TEST_CASE("the sweep front travels forward and never reverses") {
    {
        float previous = -1.0f;
        for (std::uint32_t tick = 0; tick <= kSweepEndTicks; ++tick) {
            const float front = sweepFront(tick);
            CHECK(front >= previous);
            previous = front;
        }

        CHECK(sweepFront(0) == doctest::Approx(0.0f));
        CHECK(sweepFront(kSweepEndTicks) >=
              static_cast<float>(kRings) + kSweepBandRings - 0.001f);
    }
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

TEST_CASE("no complete lattice is ever on screen at once") {

    {
        for (std::uint32_t tick = 0; tick <= kSweepEndTicks; ++tick) {
            std::uint32_t lit = 0;
            for (std::uint32_t ring = 1; ring <= kRings; ++ring) {
                if (sweepAlpha(static_cast<float>(ring), tick) >
                    kAlphaCutoff)
                    ++lit;
            }
            CHECK(lit < kRings);
        }
    }
}

TEST_CASE("the band erases behind itself and leaves nothing at the end") {

    const float ring = 6.0f;
    std::uint32_t passed = 0;
    for (std::uint32_t tick = 0; tick <= kSweepEndTicks; ++tick) {
        const bool lit = sweepAlpha(ring, tick) > kAlphaCutoff;
        if (!lit && passed != 0) {

            for (std::uint32_t later = tick; later <= kSweepEndTicks; ++later)
                CHECK(sweepAlpha(ring, later) <= kAlphaCutoff);
            break;
        }
        if (lit) ++passed;
    }
    CHECK(passed > 0);
    for (std::uint32_t r = 1; r <= kRings; ++r)
        CHECK(sweepAlpha(static_cast<float>(r), kSweepEndTicks) <=
              kAlphaCutoff);
}
TEST_CASE("a swept scan outlives its band but not by much") {
    CHECK(sweepExpired(kSweepLifetimeTicks));
    CHECK_FALSE(sweepExpired(kSweepEndTicks));
    CHECK(kSweepLifetimeTicks < kScanLifetimeTicks);
}

TEST_CASE("the band is always inside the data the pulse already measured") {

    const std::uint32_t dataComplete = 32;
        CHECK(sweepFront(dataComplete) < 12.0f);
}
TEST_CASE("the frame budget bounds every producer") {
    CHECK(kMaxGroundSegments >= kWorstCaseArcs + kWorstCaseRibs);
    CHECK(kMaxWallSegments >= 18800u);
    CHECK(kMaxDrawnSegments < kMaxGroundSegments + kMaxWallSegments);

    CHECK(kWorstCaseArcs + kWorstCaseRibs <= kMaxDrawnSegments);

    CHECK(kSurveyRingBytes >=
          kRingFramesInFlight *
              surveyBeamRingBytesPerFrame(kMaxDrawnSegments, kMaxSurveyGroups));

    CHECK(kSurveyRingBytes <= 48u * 1024u * 1024u);
}
TEST_CASE("the afterglow holds at full while the band runs, then fades out") {

    CHECK(sweepAfterglow(-0.5f, 0u) == doctest::Approx(0.0f));
    CHECK(sweepAfterglow(-0.5f, kSweepEndTicks) == doctest::Approx(0.0f));

    CHECK(sweepAfterglow(0.0f, 1u) == doctest::Approx(kCrestHoldLevel));
    CHECK(sweepAfterglow(40.0f, kSweepEndTicks) == doctest::Approx(kCrestHoldLevel));

    float previous = kCrestHoldLevel;
    for (std::uint32_t tick = kSweepEndTicks + 3u;
         tick < kSweepEndTicks + kCrestFadeTicks; tick += 3u) {
        const float glow = sweepAfterglow(5.0f, tick);
        CHECK(glow < previous);
        CHECK(glow > 0.0f);
        previous = glow;
    }
    CHECK(sweepAfterglow(5.0f, kSweepEndTicks + kCrestFadeTicks) ==
          doctest::Approx(0.0f));
}

TEST_CASE("a crest line is brighter than any ordinary line beside it") {

    const float ring = 6.0f;
    bool everPassed = false;
    for (std::uint32_t tick = 0; tick <= kSweepEndTicks; ++tick) {
        if (sweepBehind(ring, tick) < 0.0f) continue;
        everPassed = true;
        CHECK(sweepCrestlineAlpha(ring, tick) >= sweepAlpha(ring, tick));
        CHECK(sweepCrestlineAlpha(ring, tick) ==
              doctest::Approx(kCrestHoldLevel));
    }
    CHECK(everPassed);
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

    const float ringZero = waveArrivalOf(0);
    const float toRingZero = sweepTicksToReach(ringZero);
    const float toRingFive = sweepTicksToReach(waveArrivalOf(5));
    CHECK(toRingZero > 8.0f);
    CHECK(toRingFive > 30.0f);

    CHECK(sweepFront(kSweepEndTicks) >=
          static_cast<float>(kRings) + kSweepBandRings - 0.01f);

    CHECK(toRingFive < 110.0f);
}
TEST_CASE("every crest line has finished fading before the scan expires") {

        for (std::uint32_t ring = 1; ring <= kRings; ++ring)
            CHECK(sweepCrestlineAlpha(static_cast<float>(ring),
                                      kSweepLifetimeTicks - 1u) <=
                  kAlphaCutoff);
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

TEST_CASE("the slope colour is a gradient at band joins and exact at band hearts") {

    CHECK(continuousSlopeRate(0.10f, SlopeBand::Level) == slopeRate(SlopeBand::Level));
    CHECK(continuousSlopeRate(0.30f, SlopeBand::Rolling) == slopeRate(SlopeBand::Rolling));
    CHECK(continuousSlopeRate(0.50f, SlopeBand::Inclined) == slopeRate(SlopeBand::Inclined));
    CHECK(continuousSlopeRate(0.68f, SlopeBand::Steep) == slopeRate(SlopeBand::Steep));
    CHECK(continuousSlopeRate(0.86f, SlopeBand::Severe) == slopeRate(SlopeBand::Severe));
    CHECK(continuousSlopeRate(1.00f, SlopeBand::Vertical) == slopeRate(SlopeBand::Vertical));

    CHECK(continuousSlopeRate(0.766f, SlopeBand::Steep) ==
          doctest::Approx(0.5f * (slopeRate(SlopeBand::Steep) +
                                  slopeRate(SlopeBand::Severe))));
    CHECK(continuousSlopeRate(0.940f, SlopeBand::Severe) ==
          doctest::Approx(0.5f * (slopeRate(SlopeBand::Severe) +
                                  slopeRate(SlopeBand::Vertical))));
    CHECK(continuousSlopeRate(0.208f, SlopeBand::Level) ==
          doctest::Approx(0.5f * (slopeRate(SlopeBand::Level) +
                                  slopeRate(SlopeBand::Rolling))));
    CHECK(continuousSlopeRate(0.407f, SlopeBand::Rolling) ==
          doctest::Approx(0.5f * (slopeRate(SlopeBand::Rolling) +
                                  slopeRate(SlopeBand::Inclined))));

    CHECK(continuousSlopeRate(0.588f - kSlopeBlendHalfWidth, SlopeBand::Inclined) ==
          slopeRate(SlopeBand::Inclined));
    CHECK(continuousSlopeRate(0.588f + kSlopeBlendHalfWidth, SlopeBand::Steep) ==
          slopeRate(SlopeBand::Steep));

    float previous = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        const float s = 0.588f * static_cast<float>(i) / 100.0f;
        const float rate = continuousSlopeRate(s, slopeBandFor(s));
        CHECK(rate >= previous);
        previous = rate;
    }
    previous = -1.0f;
    for (int i = 1; i <= 100; ++i) {
        const float s = 0.589f + (1.0f - 0.589f) * static_cast<float>(i) / 100.0f;
        const float rate = continuousSlopeRate(s, slopeBandFor(s));
        CHECK(rate >= previous);
        previous = rate;
    }

    for (std::uint32_t band = 0; band < kSlopeBandCount; ++band) {
        const SlopeBand b = static_cast<SlopeBand>(band);
        CHECK(continuousSlopeRate(-1.0f, b) == slopeRate(b));
    }
}
