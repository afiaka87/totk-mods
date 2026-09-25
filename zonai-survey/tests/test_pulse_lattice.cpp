// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <string>

#include "PulseLattice.hpp"

using namespace zonai_survey::pure;

TEST_CASE("the lattice reaches the required long range") {

    CHECK(kMaxRange >= 4.0f * kAcceptedGroundRange);
    CHECK(kMaxRange <= 8.0f * kAcceptedGroundRange);
    CHECK(kProbeCount == kSpokes * kRings);
    CHECK(ringRadius(kRings - 1) == doctest::Approx(kMaxRange));
}

TEST_CASE("the near field retains its accepted geometry") {

    for (uint32_t ring = 0; ring <= kInnerRings; ++ring) {
        CHECK(ringRadius(ring) ==
              doctest::Approx(kInnerSpacing * static_cast<float>(ring + 1)));
        CHECK(ringSpacing(ring) == doctest::Approx(kInnerSpacing));
    }
    CHECK(ringRadius(kInnerRings) == doctest::Approx(kNearSpacing));
    for (uint32_t ring = kInnerRings; ring < kInnerRings + kNearRings; ++ring) {
        CHECK(ringRadius(ring) ==
              doctest::Approx(kNearSpacing *
                              static_cast<float>(ring - kInnerRings + 1)));
    }
    for (uint32_t ring = kInnerRings + 1; ring < kInnerRings + kNearRings;
         ++ring) {
        CHECK(ringSpacing(ring) == doctest::Approx(kNearSpacing));
    }
}

TEST_CASE("rings only ever get further apart, never closer") {
    for (uint32_t ring = 1; ring < kRings; ++ring) {
        CHECK(ringSpacing(ring) >= ringSpacing(ring - 1));
        CHECK(ringRadius(ring) > ringRadius(ring - 1));
    }
}

TEST_CASE("the far lattice is self-similar, so rings read as evenly spaced on screen") {

    for (uint32_t ring = kInnerRings + kNearRings; ring < kRings; ++ring) {
        const float aspect = ringSpacing(ring) / ringArcChord(ring);
        CHECK(aspect > 0.8f);
        CHECK(aspect < 1.6f);
    }
}
TEST_CASE("probe indices decompose to a unique ring and spoke") {
    for (uint32_t ring = 0; ring < kRings; ++ring) {
        for (uint32_t spoke = 0; spoke < kSpokes; ++spoke) {
            const uint32_t index = probeIndex(ring, spoke);
            CHECK(index < kProbeCount);
            CHECK(probeRing(index) == ring);
            CHECK(probeSpoke(index) == spoke);
        }
    }
}

TEST_CASE("probe ordering is ring-major, so a partial pulse is a complete disc") {

    for (uint32_t index = 1; index < kProbeCount; ++index) {
        CHECK(probeRing(index) >= probeRing(index - 1));
    }
}

TEST_CASE("a row is a straight crosswise band of the survey's own width") {

    const float halfSpan = std::tan(0.5f * kSurveyWidthRadians);
    for (uint32_t ring = 0; ring < kRings; ring += 6) {
        float leftX = 0.0f, leftZ = 0.0f, rightX = 0.0f, rightZ = 0.0f;
        sampleOffset(ring, 0, 0.0f, 1.0f, leftX, leftZ);
        sampleOffset(ring, kSpokes - 1, 0.0f, 1.0f, rightX, rightZ);

        CHECK(leftZ == doctest::Approx(ringRadius(ring)));
        CHECK(rightZ == doctest::Approx(ringRadius(ring)));

        CHECK(leftX == doctest::Approx(-rightX));
        CHECK(rightX == doctest::Approx(ringRadius(ring) * halfSpan));

        float midLeftX = 0.0f, midRightX = 0.0f, ignored = 0.0f;
        sampleOffset(ring, kSpokes / 2 - 1, 0.0f, 1.0f, midLeftX, ignored);
        sampleOffset(ring, kSpokes / 2, 0.0f, 1.0f, midRightX, ignored);
        CHECK(midLeftX == doctest::Approx(-midRightX));
        CHECK(std::abs(midRightX) < 0.07f * ringRadius(ring));
    }
}

TEST_CASE("the row turns rigidly with the heading") {

    for (uint32_t spoke = 0; spoke < kSpokes; spoke += 5) {
        float ax = 0.0f, az = 0.0f, bx = 0.0f, bz = 0.0f;
        sampleOffset(20, spoke, 0.0f, 1.0f, ax, az);
        sampleOffset(20, spoke, 1.0f, 0.0f, bx, bz);

        CHECK(bx == doctest::Approx(az));
        CHECK(bz == doctest::Approx(-ax));
    }

    float midX = 0.0f, midZ = 0.0f;
    sampleOffset(20, kSpokes / 2, 1.0f, 0.0f, midX, midZ);
    CHECK(midX == doctest::Approx(ringRadius(20)));
}

TEST_CASE("the cone resolves the ground far more finely than the full circle did") {

    CHECK(ringSpokeSpacing(kRings - 1) < 12.0f);
    CHECK(ringSpokeSpacing(0) < 0.5f);

    CHECK(ringArcChord(kRings - 1) > ringSpokeSpacing(kRings - 1) * 4.0f);
}

TEST_CASE("the wavefront starts at Link and reaches maximum range") {
    CHECK(wavefrontRadius(0) == doctest::Approx(0.0f));
    CHECK(wavefrontRadius(kPulseTicks) == doctest::Approx(kMaxRange));
    CHECK(wavefrontRadius(kPulseTicks + 500) == doctest::Approx(kMaxRange));
}

TEST_CASE("the wavefront never moves backward") {
    float previous = -1.0f;
    for (uint32_t tick = 0; tick <= kPulseTicks + 10; ++tick) {
        const float radius = wavefrontRadius(tick);
        CHECK(radius >= previous);
        previous = radius;
    }
}

TEST_CASE("the wave crosses rings at a constant rate") {

    const float perTick = static_cast<float>(kRings) / static_cast<float>(kPulseTicks);
    for (uint32_t tick = 1; tick <= kPulseTicks; ++tick) {
        const float step = wavePosition(tick) - wavePosition(tick - 1);
        CHECK(step == doctest::Approx(perTick));
    }
}

TEST_CASE("constant ring rate means the wave accelerates in metres") {

    const float early = wavefrontRadius(10) - wavefrontRadius(9);
    const float late = wavefrontRadius(kPulseTicks) - wavefrontRadius(kPulseTicks - 1);
    CHECK(late > early * 4.0f);
}

TEST_CASE("the crest can leave the picture instead of parking on the last ring") {

    CHECK(wavePosition(kPulseTicks * 2) > static_cast<float>(kRings));
    CHECK(ringsReached(kPulseTicks * 2) == kRings);
}

TEST_CASE("rings are only reached once the wave has passed their radius") {
    CHECK(ringsReached(0) == 0);
    for (uint32_t tick = 0; tick <= kPulseTicks; ++tick) {
        const uint32_t reached = ringsReached(tick);
        CHECK(reached <= kRings);
        if (reached > 0) CHECK(ringRadius(reached - 1) <= wavefrontRadius(tick));
        if (reached < kRings) CHECK(ringRadius(reached) > wavefrontRadius(tick));
    }
    CHECK(ringsReached(kPulseTicks) == kRings);
}

TEST_CASE("a ring's arrival coordinate is the wave position that crosses it") {

    for (uint32_t ring = 0; ring < kRings; ++ring) {
        CHECK(waveArrivalOf(ring) == doctest::Approx(static_cast<float>(ring + 1)));
        const uint32_t arrivalTick = ((ring + 1) * kPulseTicks + kRings - 1) / kRings;
        CHECK(ringsReached(arrivalTick) >= ring + 1);
        if (arrivalTick > 0) CHECK(ringsReached(arrivalTick - 1) <= ring + 1);
    }
}

TEST_CASE("a world distance converts to the wave coordinate that sweeps it") {

    CHECK(waveCoordForRadius(0.0f) == doctest::Approx(0.0f));
    CHECK(waveCoordForRadius(kMaxRange) == doctest::Approx(static_cast<float>(kRings)));
    CHECK(waveCoordForRadius(kMaxRange * 10.0f) == doctest::Approx(static_cast<float>(kRings)));
    for (uint32_t ring = 0; ring < kRings; ++ring) {
        CHECK(waveCoordForRadius(ringRadius(ring)) == doctest::Approx(waveArrivalOf(ring)));
    }

    float previous = -1.0f;
    for (uint32_t step = 0; step <= 100; ++step) {
        const float coord = waveCoordForRadius(kMaxRange * static_cast<float>(step) / 100.0f);
        CHECK(coord >= previous);
        previous = coord;
    }
}

TEST_CASE("the budget can actually walk the lattice inside the wave's own window") {

    CHECK(kProbeCount <= kProbesPerTick * kPulseTicks);
}

TEST_CASE("no probe is ever cast ahead of the wave") {
    uint32_t completed = 0;
    for (uint32_t tick = 0; tick <= kPulseTicks; ++tick) {
        const uint32_t authorised = authorisedProbeCount(tick);
        const uint32_t batch = probeBatchSize(authorised, completed);

        CHECK(completed + batch <= ringsReached(tick) * kSpokes);
        completed += batch;
    }
}

TEST_CASE("one service never exceeds the per-tick budget") {
    uint32_t completed = 0;
    for (uint32_t tick = 0; tick <= kPulseTicks + 20; ++tick) {
        const uint32_t batch = probeBatchSize(authorisedProbeCount(tick), completed);
        CHECK(batch <= kProbesPerTick);
        completed += batch;
    }
}

TEST_CASE("a full pulse casts every probe exactly once, and never past the end") {
    bool seen[kProbeCount] = {};
    uint32_t completed = 0;

    const uint32_t generousBound = kPulseTicks + kProbeCount / kProbesPerTick + 10;
    for (uint32_t tick = 0; tick <= generousBound && completed < kProbeCount; ++tick) {
        const uint32_t batch = probeBatchSize(authorisedProbeCount(tick), completed);
        for (uint32_t i = 0; i < batch; ++i) {
            const uint32_t index = completed + i;
            REQUIRE(index < kProbeCount);
            CHECK_FALSE(seen[index]);
            seen[index] = true;
        }
        completed += batch;
    }

    CHECK(completed == kProbeCount);
    for (uint32_t i = 0; i < kProbeCount; ++i) CHECK(seen[i]);
}

TEST_CASE("a starved physics worker slows the pulse instead of dropping probes") {

    uint32_t completed = 0;
    constexpr uint32_t starvedRate = 2;
    const uint32_t generousBound = kPulseTicks + kProbeCount / starvedRate + 10;
    for (uint32_t tick = 0; tick <= generousBound && completed < kProbeCount; ++tick) {
        const uint32_t offered = probeBatchSize(authorisedProbeCount(tick), completed);
        const uint32_t cast = offered > starvedRate ? starvedRate : offered;
        completed += cast;
    }
    CHECK(completed == kProbeCount);
}

TEST_CASE("catching up with the wave asks for nothing, which is not a failure") {

    CHECK(probeBatchSize(authorisedProbeCount(0), 0) == 0);
    CHECK(probeBatchSize(kSpokes, kSpokes) == 0);

    CHECK(probeBatchSize(kProbeCount, kProbeCount) == 0);
    CHECK(probeBatchSize(kProbeCount * 4, kProbeCount) == 0);

    CHECK(probeBatchSize(kProbeCount * 4, kProbeCount - 1) == 1);
}

TEST_CASE("re-triggering mid-pulse begins a clean scan, not a resumed one") {

    const uint32_t midPulse = authorisedProbeCount(kPulseTicks / 2);
    REQUIRE(midPulse > 0);

    CHECK(authorisedProbeCount(0) == 0);
    CHECK(probeBatchSize(authorisedProbeCount(0), 0) == 0);

    uint32_t firstOpenTick = 0;
    for (uint32_t tick = 1; tick <= kPulseTicks; ++tick) {
        if (authorisedProbeCount(tick) > 0) {
            firstOpenTick = tick;
            break;
        }
    }
    REQUIRE(firstOpenTick > 0);
    CHECK(authorisedProbeCount(firstOpenTick) == kSpokes);
    CHECK(wavePosition(firstOpenTick) >= 1.0f);
    CHECK(wavePosition(firstOpenTick - 1) < 1.0f);
}

TEST_CASE("every probe is exactly vertical") {

    for (uint32_t ring = 0; ring < kRings; ++ring) {
        for (uint32_t spoke = 0; spoke < kSpokes; spoke += 7) {
            const FanRay ray = fanRayFor(probeIndex(ring, spoke), 12.0f, -4.0f, 30.0f, 0.0f, 1.0f);
            CHECK(ray.fromX == doctest::Approx(ray.toX));
            CHECK(ray.fromZ == doctest::Approx(ray.toZ));
            CHECK(ray.toY < ray.fromY);
        }
    }
}

TEST_CASE("probes land at exactly their row's forward distance, whatever the terrain") {

    for (uint32_t ring = 0; ring < kRings; ++ring) {
        for (uint32_t spoke = 0; spoke < kSpokes; spoke += 21) {
            const FanRay ray =
                fanRayFor(probeIndex(ring, spoke), 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
            CHECK(ray.fromZ == doctest::Approx(ringRadius(ring)));
        }
    }
}

TEST_CASE("the drop is centred on the height that spoke last found ground") {

    const uint32_t index = probeIndex(kRings - 1, 3);
    const FanRay low = fanRayFor(index, 0.0f, 0.0f, -120.0f, 0.0f, 1.0f);
    const FanRay high = fanRayFor(index, 0.0f, 0.0f, 260.0f, 0.0f, 1.0f);
    CHECK(high.fromY - low.fromY == doctest::Approx(380.0f));
    CHECK(high.toY - low.toY == doctest::Approx(380.0f));

    CHECK(low.fromX == doctest::Approx(high.fromX));
    CHECK(low.fromZ == doctest::Approx(high.fromZ));
}

TEST_CASE("the drop starts high enough to clear terrain above the reference") {

    CHECK(kMinProbeCeiling >= 40.0f);
    const FanRay ray = fanRayFor(probeIndex(5, 5), 0.0f, 0.0f, 100.0f, 0.0f, 1.0f);
    CHECK(ray.fromY == doctest::Approx(100.0f + probeCeiling(5)));
}

TEST_CASE("the drop reaches far enough below for the plane beneath a cliff") {
    CHECK(kMinProbeFloor >= 60.0f);
    const FanRay ray = fanRayFor(probeIndex(5, 5), 0.0f, 0.0f, 100.0f, 0.0f, 1.0f);
    CHECK(ray.toY == doctest::Approx(100.0f - probeFloor(5)));
}

TEST_CASE("the drop window covers a ring gap however wide that gap becomes") {

    for (uint32_t ring = 0; ring < kRings; ++ring) {
        CHECK(probeCeiling(ring) >= kMinProbeCeiling);
        CHECK(probeFloor(ring) >= kMinProbeFloor);
        CHECK(probeCeiling(ring) >= ringSpacing(ring));
        CHECK(probeFloor(ring) >= ringSpacing(ring) * 2.0f);
    }
}

TEST_CASE("the whole survey is in front of the player, and evenly to both sides") {

    const uint32_t outer = kRings - 1;
    const FanRay left =
        fanRayFor(probeIndex(outer, 0), 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    const FanRay right =
        fanRayFor(probeIndex(outer, kSpokes - 1), 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(left.fromX == doctest::Approx(-right.fromX));
    CHECK(left.fromZ == doctest::Approx(right.fromZ));
    for (uint32_t ring = 0; ring < kRings; ++ring) {
        for (uint32_t spoke = 0; spoke < kSpokes; spoke += 9) {
            const FanRay ray =
                fanRayFor(probeIndex(ring, spoke), 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
            CHECK(ray.fromZ > 0.0f);
        }
    }
}

TEST_CASE("a missed lattice point is retried through a much wider window") {
    for (uint32_t ring = 0; ring < kRings; ++ring) {

        CHECK(recoveryCeiling(ring) > probeCeiling(ring));
        CHECK(recoveryFloor(ring) > probeFloor(ring));

        CHECK(recoveryCeiling(ring) >= kMinRecoveryCeiling);
        CHECK(recoveryFloor(ring) >= kMinRecoveryFloor);
    }

    CHECK(recoveryCeiling(kRings - 1) > recoveryCeiling(0));
    CHECK(recoveryFloor(kRings - 1) > recoveryFloor(0));
}

TEST_CASE("the retry asks the same question, only louder") {

    for (uint32_t index = 0; index < kProbeCount; index += 137) {
        const FanRay narrow = fanRayFor(index, 10.0f, -4.0f, 25.0f, 0.0f, 1.0f);
        const FanRay wide = recoveryRayFor(index, 10.0f, -4.0f, 25.0f, 0.0f, 1.0f);
        CHECK(wide.fromX == doctest::Approx(narrow.fromX));
        CHECK(wide.fromZ == doctest::Approx(narrow.fromZ));
        CHECK(wide.toX == doctest::Approx(narrow.fromX));
        CHECK(wide.toZ == doctest::Approx(narrow.fromZ));

        CHECK(wide.fromX == doctest::Approx(wide.toX));
        CHECK(wide.fromZ == doctest::Approx(wide.toZ));
        CHECK(wide.fromY > narrow.fromY);
        CHECK(wide.toY < narrow.toY);
    }
}

TEST_CASE("the band is 100 degrees wide, and the near rows are real bands") {

    CHECK(kSurveyWidthDegrees == doctest::Approx(100.0f));
    CHECK(kProbeCount == kSpokes * kRings);

    float leftX = 0.0f, leftZ = 0.0f, rightX = 0.0f, rightZ = 0.0f;
    sampleOffset(0, 0, 0.0f, 1.0f, leftX, leftZ);
    sampleOffset(0, kSpokes - 1, 0.0f, 1.0f, rightX, rightZ);
    CHECK(rightX - leftX == doctest::Approx(1.489692f).epsilon(0.001));

    CHECK(sampleRowSpacing(0) * static_cast<float>(kSpokes - 1) ==
          doctest::Approx(1.489692f).epsilon(0.001));
}

TEST_CASE("a spoke keeps one bearing, so the march stays valid") {

    for (uint32_t spoke = 0; spoke < kSpokes; spoke += 7) {
        float nearX = 0.0f, nearZ = 0.0f, farX = 0.0f, farZ = 0.0f;
        sampleOffset(0, spoke, 0.0f, 1.0f, nearX, nearZ);
        sampleOffset(30, spoke, 0.0f, 1.0f, farX, farZ);
        const float nearLen = std::sqrt(nearX * nearX + nearZ * nearZ);
        const float farLen = std::sqrt(farX * farX + farZ * farZ);
        CHECK(nearX / nearLen == doctest::Approx(farX / farLen).epsilon(1e-4));
        CHECK(nearZ / nearLen == doctest::Approx(farZ / farLen).epsilon(1e-4));
    }
}

TEST_CASE("the band's edge widens its window for the ground it skips over") {

    CHECK(sampleRadialScale(0) == doctest::Approx(1.555724f).epsilon(0.001));
    CHECK(sampleRadialScale(kSpokes - 1) ==
          doctest::Approx(1.555724f).epsilon(0.001));
    CHECK(sampleRadialScale(kSpokes / 2 - 1) < 1.01f);

    const FanRay edge =
        fanRayFor(probeIndex(0, 0), 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    CHECK(edge.fromY == doctest::Approx(10.0f + kMinProbeCeiling));
    CHECK(edge.toY == doctest::Approx(10.0f - kMinProbeFloor));
}

TEST_CASE("the accepted maximum range clips the band") {

    CHECK_FALSE(sampleWithinRange(kRings - 1, 0));
    CHECK_FALSE(sampleWithinRange(kRings - 1, kSpokes - 1));
    CHECK(sampleWithinRange(kRings - 1, kSpokes / 2 - 1));
    CHECK(sampleWithinRange(kRings - 1, kSpokes / 2));

    for (uint32_t ring = 0; ring < kInnerRings + kNearRings; ++ring) {
        CHECK(sampleWithinRange(ring, 0));
        CHECK(sampleWithinRange(ring, kSpokes - 1));
    }
}

TEST_CASE("the row spacing feeds the bend gate honestly") {

    const float halfSpan = std::tan(0.5f * kSurveyWidthRadians);
    for (uint32_t ring = 0; ring < kRings; ring += 8) {
        CHECK(sampleRowSpacing(ring) ==
              doctest::Approx(2.0f * ringRadius(ring) * halfSpan /
                              static_cast<float>(kSpokes - 1)));

        CHECK(sampleRowSpacing(ring) < ringArcChord(ring));
    }
}
