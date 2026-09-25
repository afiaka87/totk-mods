// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cstring>

#include "ScanReduction.hpp"

using namespace zonai_survey::pure;

namespace {

TerrainSample hit(float x, float y, float z, float normalY = 1.0f, float distance = 10.0f) {
    return TerrainSample{true, x, y, z, normalY, distance};
}

TerrainSample miss() { return TerrainSample{}; }

}

TEST_CASE("flat ground gives a walkable rib that follows the surface") {
    ScanSegment out{};
    REQUIRE(reduceRadialStep(hit(0.0f, 10.0f, 2.5f), hit(0.0f, 10.0f, 5.0f), 1, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.surface == SegmentClass::Ground);
    CHECK(out.slopeBand == SlopeBand::Level);
    CHECK_FALSE(out.isArc);
    CHECK(out.az == doctest::Approx(2.5f));
    CHECK(out.bz == doctest::Approx(5.0f));
}

TEST_CASE("a gentle rise stays walkable, a moderate one becomes steep") {
    ScanSegment out{};
    REQUIRE(reduceRadialStep(hit(0.0f, 0.0f, 2.5f), hit(0.0f, 0.8f, 5.0f), 1, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.surface == SegmentClass::Ground);

    REQUIRE(reduceRadialStep(hit(0.0f, 0.0f, 2.5f), hit(0.0f, 1.6f, 5.0f), 1, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.surface == SegmentClass::Steep);
    CHECK(static_cast<std::uint32_t>(out.slopeBand) >=
          static_cast<std::uint32_t>(SlopeBand::Inclined));
}

TEST_CASE("the drawn segment is the REAL surface transition, not a synthetic mark") {

    ScanSegment out{};
    REQUIRE(reduceRadialStep(hit(1.0f, 6.0f, 2.0f), hit(3.0f, 0.0f, 6.0f), 1, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.ax == doctest::Approx(1.0f));
    CHECK(out.ay == doctest::Approx(6.0f));
    CHECK(out.bx == doctest::Approx(3.0f));
    CHECK(out.by == doctest::Approx(0.0f));
}

TEST_CASE("a step up a face is classified vertical, not divided by a tiny run") {

    ScanSegment out{};
    const TerrainSample lower = hit(4.0f, 10.0f, 4.0f, 0.05f);
    const TerrainSample upper = hit(4.0f, 12.5f, 4.0f, 0.05f);
    REQUIRE(reduceRadialStep(lower, upper, 3, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.surface == SegmentClass::Cliff);
    CHECK(out.slopeBand == SlopeBand::Vertical);
}

TEST_CASE("a real drop is left as a break, not a diagonal through open air") {

    ScanSegment out{};
    const TerrainSample lip = hit(0.0f, 10.0f, 5.0f);
    const TerrainSample floorBelow = hit(0.0f, -20.0f, 9.0f);
    CHECK_FALSE(linkable(lip, floorBelow));
    CHECK(reduceRadialStep(lip, floorBelow, 3, &out, GroundJoinRule::Recovered) == 0);
    CHECK(reduceArcStep(lip, floorBelow, 3, &out, GroundJoinRule::Recovered) == 0);
}

TEST_CASE("rocks, ruins and banks are crawled over rather than skipped") {

    ScanSegment out{};
    const TerrainSample ground = hit(4.0f, 0.0f, 4.0f);
    const TerrainSample onTopOfRock = hit(4.2f, 2.5f, 4.0f, 0.4f);
    CHECK(linkable(ground, onTopOfRock));
    REQUIRE(reduceArcStep(ground, onTopOfRock, 3, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.surface != SegmentClass::Ground);
}

TEST_CASE("clean continuity does not draw a tent between flat deck and ground") {
    const TerrainSample ground = hit(0.0f, 0.0f, 0.0f, 1.0f);
    const TerrainSample deck = hit(0.5f, 4.0f, 0.0f, 1.0f);
    ScanSegment out{};
    REQUIRE(reduceArcStep(ground, deck, 3, &out, GroundJoinRule::Recovered) == 1);
    CHECK(reduceArcStep(ground, deck, 3, &out, GroundJoinRule::Continuous) == 0);

    const TerrainSample closeDeck = hit(0.2f, 4.0f, 0.0f, 1.0f);
    REQUIRE(reduceArcStep(ground, closeDeck, 3, &out, GroundJoinRule::Recovered) == 1);
    CHECK(reduceArcStep(ground, closeDeck, 3, &out, GroundJoinRule::Continuous) == 0);
}

TEST_CASE("clean continuity preserves a real inclined surface") {

    const TerrainSample low = hit(0.0f, 0.0f, 0.0f, 0.8944272f);
    const TerrainSample high = hit(0.0f, 20.0f, 40.0f, 0.8944272f);
    ScanSegment out{};
    REQUIRE(reduceArcStep(low, high, kRings - 1, &out, GroundJoinRule::Continuous) == 1);
    CHECK(out.surface == SegmentClass::Steep);
}

TEST_CASE("segments reveal at their own ring, so the circles expand evenly") {

    ScanSegment out{};
    REQUIRE(reduceRadialStep(hit(0.0f, 0.0f, 0.0f), hit(1.0f, 0.0f, 0.0f), 7, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.revealWave == doctest::Approx(waveArrivalOf(7)));

    REQUIRE(reduceArcStep(hit(0.0f, 0.0f, 0.0f), hit(1.0f, 0.0f, 0.0f), 4, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.revealWave == doctest::Approx(waveArrivalOf(4)));
}

TEST_CASE("a steep bank inside the link threshold is still a cliff") {
    ScanSegment out{};
    REQUIRE(reduceRadialStep(hit(0.0f, 6.0f, 2.5f), hit(0.0f, 0.0f, 5.0f), 1, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.surface == SegmentClass::Cliff);
}

TEST_CASE("a missed probe draws nothing") {
    ScanSegment out{};
    CHECK(reduceRadialStep(hit(0.0f, 0.0f, 2.5f), miss(), 1, &out, GroundJoinRule::Recovered) == 0);
    CHECK(reduceRadialStep(miss(), hit(0.0f, 0.0f, 5.0f), 1, &out, GroundJoinRule::Recovered) == 0);
    CHECK(reduceRadialStep(miss(), miss(), 1, &out, GroundJoinRule::Recovered) == 0);
}

TEST_CASE("the surface normal can only make a rib worse, never better") {
    ScanSegment out{};
    REQUIRE(reduceRadialStep(hit(0.0f, 10.0f, 2.5f, 0.2f), hit(0.0f, 10.0f, 5.0f, 0.2f), 1,
                             &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.surface == SegmentClass::Cliff);
}

TEST_CASE("a rib is refused without somewhere to write") {
    CHECK(reduceRadialStep(hit(0.0f, 0.0f, 2.5f), hit(0.0f, 0.0f, 5.0f), 1, nullptr, GroundJoinRule::Recovered) == 0);
}

TEST_CASE("nothing is permanently blocked any more") {

    static_assert(sizeof(reduceRadialStep(hit(0, 0, 0), hit(0, 0, 1), 1, nullptr, GroundJoinRule::Recovered)) ==
                      sizeof(uint32_t),
                  "reduceRadialStep reports only how many segments it wrote");

    ScanSegment out{};
    CHECK(reduceRadialStep(hit(0.0f, 0.0f, 2.5f), hit(0.0f, 5.0f, 5.0f), 1, &out, GroundJoinRule::Recovered) == 1);
    CHECK(reduceRadialStep(hit(0.0f, 5.0f, 5.0f), hit(0.0f, 5.2f, 7.5f), 2, &out, GroundJoinRule::Recovered) == 1);
}

TEST_CASE("neighbouring directions on flat ground join into a ring arc") {
    ScanSegment out{};
    REQUIRE(reduceArcStep(hit(1.0f, 10.0f, 0.0f), hit(0.9f, 10.0f, 0.4f), 5, &out, GroundJoinRule::Recovered) == 1);
    CHECK(out.isArc);
    CHECK(out.surface == SegmentClass::Ground);
    CHECK(out.revealWave == doctest::Approx(waveArrivalOf(5)));
}

TEST_CASE("an arc across a real drop is not drawn") {

    ScanSegment out{};
    CHECK(reduceArcStep(hit(1.0f, 0.0f, 0.0f), hit(0.9f, 9.0f, 0.4f), 5, &out, GroundJoinRule::Recovered) == 0);
}

TEST_CASE("an arc with a missed end is not drawn — a gap leaves a hole") {
    ScanSegment out{};
    CHECK(reduceArcStep(hit(1.0f, 0.0f, 0.0f), miss(), 5, &out, GroundJoinRule::Recovered) == 0);
    CHECK(reduceArcStep(miss(), hit(1.0f, 0.0f, 0.0f), 5, &out, GroundJoinRule::Recovered) == 0);
}

TEST_CASE("arc classification also takes the worse signal") {
    ScanSegment out{};
    REQUIRE(reduceArcStep(hit(1.0f, 10.0f, 0.0f, 0.2f), hit(0.9f, 10.0f, 0.4f, 0.2f), 5, &out, GroundJoinRule::Recovered) ==
            1);
    CHECK(out.surface == SegmentClass::Cliff);
}

TEST_CASE("arcs are refused without somewhere to write") {
    CHECK(reduceArcStep(hit(1.0f, 0.0f, 0.0f), hit(0.9f, 0.0f, 0.4f), 5, nullptr, GroundJoinRule::Recovered) == 0);
}
TEST_CASE("everything is drawn when it fits") {
    const auto selection = selectForDraw(120);
    CHECK(selection.drawn == 120);
    CHECK(selection.dropped == 0);
}

TEST_CASE("the cap is honoured and the overflow is counted, never silent") {
    const auto selection = selectForDraw(kMaxDrawnSegments + 300);
    CHECK(selection.drawn == kMaxDrawnSegments);
    CHECK(selection.dropped == 300);
}

TEST_CASE("exactly at the cap nothing is reported as dropped") {
    const auto selection = selectForDraw(kMaxDrawnSegments);
    CHECK(selection.drawn == kMaxDrawnSegments);
    CHECK(selection.dropped == 0);
}
TEST_CASE("the buffer holds the whole picture, so nothing truncates in normal use") {

    CHECK(kWorstCaseArcs == kSpokes * kRings);
    CHECK(kWorstCaseArcs + kWorstCaseRibs <= kMaxDrawnSegments);

    CHECK(kMaxGroundSegments <= kMaxDrawnSegments);
}

TEST_CASE("the forward joins are bounded by the lattice they walk") {

    CHECK(kWorstCaseRibs == kSpokes * (kRings - 1));

    CHECK(kRings == 37);
    CHECK(kSpokes == 64);
}

TEST_CASE("every measured spoke exposes its radial history") {
    CHECK(kRadialSpokeStride == 1);
    CHECK(kWorstCaseRibs == kSpokes * (kRings - 1));
}

namespace {

void fillFlatRing(TerrainSample* ring, uint32_t count, float height = 10.0f) {
    for (uint32_t i = 0; i < count; ++i) ring[i] = hit(static_cast<float>(i), height, 0.0f);
}

}

TEST_CASE("a roof beam standing proud of the ground is dropped, not drawn as a tent") {
    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    ring[10].y = 16.0f;

    const uint32_t dropped = despikeRing(ring, kSpokes);
    CHECK(dropped == 1);
    CHECK_FALSE(ring[10].hit);
    CHECK(ring[9].hit);
    CHECK(ring[11].hit);
}

TEST_CASE("a probe that slipped between planks to the floor is also dropped") {
    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    ring[20].y = 3.0f;

    CHECK(despikeRing(ring, kSpokes) == 1);
    CHECK_FALSE(ring[20].hit);
}

TEST_CASE("a two-wide beam is still dropped") {

    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    ring[30].y = 15.0f;
    ring[31].y = 15.0f;

    CHECK(despikeRing(ring, kSpokes) == 2);
    CHECK_FALSE(ring[30].hit);
    CHECK_FALSE(ring[31].hit);
}

TEST_CASE("a real bank is NOT despiked away") {

    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    for (uint32_t i = 0; i < kSpokes / 2; ++i) ring[i].y = 16.0f;

    const uint32_t dropped = despikeRing(ring, kSpokes);
    CHECK(dropped <= 2);
    CHECK(ring[kSpokes / 4].hit);
    CHECK(ring[kSpokes - kSpokes / 4].hit);
}

TEST_CASE("despiking judges against the original heights, so spikes cannot cascade") {
    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    ring[5].y = 20.0f;

    CHECK(despikeRing(ring, kSpokes) == 1);
    for (uint32_t i = 0; i < kSpokes; ++i) {
        if (i != 5) CHECK(ring[i].hit);
    }
}

TEST_CASE("despiking refuses to run without enough context") {
    TerrainSample ring[4];
    fillFlatRing(ring, 4);
    CHECK(despikeRing(ring, 4) == 0);
    CHECK(despikeRing(nullptr, kSpokes) == 0);
}

TEST_CASE("smoothing settles jitter but leaves a real step alone") {
    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    ring[8].y = 10.6f;

    const float stepFrom = 10.0f;
    const float stepTo = 14.0f;
    for (uint32_t i = kSpokes / 2; i < kSpokes; ++i) ring[i].y = stepTo;

    smoothRing(ring, kSpokes);

    CHECK(ring[8].y < 10.6f);
    CHECK(ring[8].y > stepFrom);
    CHECK(ring[kSpokes / 2 + 4].y == doctest::Approx(stepTo));
    CHECK(ring[4].y == doctest::Approx(stepFrom));
}

TEST_CASE("smoothing never invents a sample or resurrects a dropped one") {
    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    ring[12] = TerrainSample{};

    smoothRing(ring, kSpokes);
    CHECK_FALSE(ring[12].hit);
    CHECK(ring[11].hit);
    CHECK(ring[13].hit);
}

TEST_CASE("smoothing is refused on degenerate input") {
    TerrainSample ring[2];
    fillFlatRing(ring, 2);
    smoothRing(ring, 2);
    smoothRing(nullptr, kSpokes);
    CHECK(true);
}

TEST_CASE("the median helper is a real median") {
    float five[5] = {9.0f, 1.0f, 5.0f, 3.0f, 7.0f};
    CHECK(medianOfFive(five, 5) == doctest::Approx(5.0f));
    float three[3] = {4.0f, 2.0f, 6.0f};
    CHECK(medianOfFive(three, 3) == doctest::Approx(4.0f));
}

TEST_CASE("the link rule follows the lattice out, or the far field draws nothing") {

    CHECK_FALSE(linkable(hit(0.0f, 0.0f, 0.0f), hit(0.0f, 20.0f, 2.5f)));
    CHECK(linkable(hit(0.0f, 0.0f, 0.0f), hit(0.0f, 20.0f, 40.0f)));
}

TEST_CASE("a genuine cliff is still a break however far out it is") {

    CHECK_FALSE(linkable(hit(0.0f, 0.0f, 0.0f), hit(0.0f, -140.0f, 40.0f)));
}

TEST_CASE("the near-field link tolerance is exactly what it was") {

    CHECK(linkRiseAllowance(hit(0.0f, 0.0f, 0.0f), hit(0.0f, 0.0f, 2.5f)) ==
          doctest::Approx(kMaxLinkRise));
}

TEST_CASE("far-field terrain relief is not mistaken for a roof beam") {
    TerrainSample nearRing[kSpokes];
    fillFlatRing(nearRing, kSpokes);
    nearRing[10].y = 16.0f;
    CHECK(despikeRing(nearRing, kSpokes, 0) == 1);

    TerrainSample farRing[kSpokes];
    fillFlatRing(farRing, kSpokes);
    farRing[10].y = 16.0f;
    CHECK(despikeRing(farRing, kSpokes, kRings - 1) == 0);
    CHECK(farRing[10].hit);
}

TEST_CASE("a real outlier is still dropped at the far edge") {
    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    ring[10].y = 90.0f;
    CHECK(despikeRing(ring, kSpokes, kRings - 1) == 1);
    CHECK_FALSE(ring[10].hit);
}

TEST_CASE("smoothing settles far-field jitter without flattening far-field relief") {
    TerrainSample ring[kSpokes];
    fillFlatRing(ring, kSpokes);
    ring[8].y = 14.0f;
    for (uint32_t i = kSpokes / 2; i < kSpokes; ++i) ring[i].y = 60.0f;

    smoothRing(ring, kSpokes, kRings - 1);

    CHECK(ring[8].y < 14.0f);
    CHECK(ring[8].y > 10.0f);
    CHECK(ring[kSpokes / 2 + 4].y == doctest::Approx(60.0f));
}

TEST_CASE("every scaled threshold keeps its near-field value as a floor") {
    for (uint32_t ring = 0; ring < kRings; ++ring) {
        CHECK(spikeRiseFor(ring) >= kSpikeRise);
        CHECK(smoothLimitFor(ring) >= kSmoothLimit);
    }
    CHECK(spikeRiseFor(0) == doctest::Approx(kSpikeRise));
    CHECK(smoothLimitFor(0) == doctest::Approx(kSmoothLimit));

    CHECK(spikeRiseFor(kRings - 1) > spikeRiseFor(0) * 4.0f);
}

TEST_CASE("a coherent four-sample cell recovers a track-only missing edge") {
    const TerrainSample currentLeft = hit(0.0f, 0.0f, 1.0f);
    const TerrainSample currentRight = hit(1.0f, 0.0f, 1.0f);
    const TerrainSample previousLeft = hit(0.0f, 0.0f, 0.0f);
    const TerrainSample previousRight = hit(1.0f, 0.0f, 0.0f);
    CHECK(groundCellRecoversEdge(currentLeft, currentRight, previousLeft,
                                 previousRight, GroundRefusal::Track));
}

TEST_CASE("cell plane evidence can recover a locally inconsistent normal") {
    const TerrainSample currentLeft = hit(0.0f, 0.0f, 1.0f, 1.0f);
    const TerrainSample currentRight = hit(1.0f, 2.0f, 1.0f, 1.0f);
    const TerrainSample previousLeft = hit(0.0f, 0.0f, 0.0f, 0.4f);
    const TerrainSample previousRight = hit(1.0f, 2.0f, 0.0f, 0.4f);
    REQUIRE_FALSE(surfaceContinuous(currentLeft, currentRight));
    REQUIRE(surfaceContinuous(previousLeft, previousRight));
    CHECK(groundCellRecoversEdge(currentLeft, currentRight, previousLeft,
                                 previousRight, GroundRefusal::Pairwise));
}

TEST_CASE("cell recovery refuses an edge across a real layer break") {
    const TerrainSample currentLeft = hit(0.0f, 0.0f, 1.0f);
    const TerrainSample currentRight = hit(1.0f, 20.0f, 1.0f);
    const TerrainSample previousLeft = hit(0.0f, 0.0f, 0.0f);
    const TerrainSample previousRight = hit(1.0f, 0.0f, 0.0f);
    CHECK_FALSE(groundCellRecoversEdge(currentLeft, currentRight, previousLeft,
                                       previousRight, GroundRefusal::Pairwise));
}

TEST_CASE("a ground track follows curved terrain without cutting it") {

    GroundTrack track{};
    ScanSegment out{};
    float y = 0.0f;
    float step = 0.0f;
    TerrainSample previous = hit(0.0f, y, 0.0f);
    std::uint32_t drawn = 0;
    for (std::uint32_t i = 0; i < 20; ++i) {
        step += 0.05f;
        y += step;
        const TerrainSample next = hit(0.0f, y, static_cast<float>(i + 1) * 2.5f);
        drawn += trackedRadialStep(track, previous, next, 1, &out);
        previous = next;
    }
    CHECK(drawn == 20);
}

TEST_CASE("a ground track refuses to walk off the surface it is following") {

    const float steep = 0.5f;
    GroundTrack track{};
    ScanSegment out{};
    const float allowance = groundBendAllowance(ringSpacing(1));

    const TerrainSample a = hit(0.0f, 0.0f, 0.0f, steep);
    const TerrainSample b = hit(0.0f, 0.5f, 2.5f, steep);
    REQUIRE(trackedRadialStep(track, a, b, 1, &out) == 1);
    const TerrainSample c = hit(0.0f, 1.0f, 5.0f, steep);
    REQUIRE(trackedRadialStep(track, b, c, 1, &out) == 1);

    const float wild = 1.5f + 5.0f;
    REQUIRE(5.0f > allowance);
    const TerrainSample d = hit(0.0f, wild, 7.5f, steep);
    ScanSegment pairwise{};
    REQUIRE(reduceRadialStep(c, d, 1, &pairwise, GroundJoinRule::Continuous) == 1);
    CHECK(trackedRadialStep(track, c, d, 1, &out) == 0);

    const TerrainSample e = hit(0.0f, wild + 0.4f, 10.0f, steep);
    CHECK(trackedRadialStep(track, d, e, 1, &out) == 1);
}

TEST_CASE("ground tracks never invent a join the pairwise rule rejects") {

    for (std::uint32_t ring = 1; ring < kRings; ++ring) {
        GroundTrack arcTrack{};
        GroundTrack ribTrack{};
        float y = 0.0f;
        TerrainSample previous = hit(0.0f, 0.0f, 0.0f);
        for (std::uint32_t i = 0; i < 12; ++i) {
            y += static_cast<float>((i % 5)) * 1.7f - 2.0f;
            const TerrainSample next =
                hit(static_cast<float>(i), y, static_cast<float>(i) * 3.0f,
                    i % 3 == 0 ? 0.2f : 1.0f);

            ScanSegment tracked{};
            ScanSegment pairwise{};
            const std::uint32_t arcTracked =
                trackedArcStep(arcTrack, previous, next, ring, &tracked);
            const std::uint32_t arcPair =
                reduceArcStep(previous, next, ring, &pairwise, GroundJoinRule::Continuous);
            CHECK(arcTracked <= arcPair);
            if (arcTracked != 0) {
                CHECK(tracked.ax == doctest::Approx(pairwise.ax));
                CHECK(tracked.by == doctest::Approx(pairwise.by));
                CHECK(tracked.slopeBand == pairwise.slopeBand);
            }

            ScanSegment ribTracked{};
            ScanSegment ribPair{};
            CHECK(trackedRadialStep(ribTrack, previous, next, ring, &ribTracked) <=
                  reduceRadialStep(previous, next, ring, &ribPair, GroundJoinRule::Continuous));
            previous = next;
        }
    }
}

TEST_CASE("the Baseline continuity setting leaves the ground walk untouched") {

    GroundTrack track{};
    TerrainSample previous = hit(0.0f, 0.0f, 0.0f);
    for (std::uint32_t i = 0; i < 10; ++i) {
        const TerrainSample next =
            hit(0.0f, static_cast<float>(i % 3) * 9.0f, static_cast<float>(i + 1) * 2.5f);
        ScanSegment tracked{};
        ScanSegment plain{};
        CHECK(trackedRadialStep(track, previous, next, 1, &tracked) ==
              reduceRadialStep(previous, next, 1, &plain, GroundJoinRule::Recovered));
        previous = next;
    }
}

TEST_CASE("a missed measurement breaks the ground track instead of bridging it") {
    GroundTrack track{};
    ScanSegment out{};
    const TerrainSample a = hit(0.0f, 0.0f, 0.0f);
    const TerrainSample b = hit(0.0f, 0.4f, 2.5f);
    REQUIRE(trackedRadialStep(track, a, b, 1, &out) == 1);
    CHECK(trackedRadialStep(track, b, miss(), 1, &out) == 0);
    CHECK(trackedRadialStep(track, miss(), b, 1, &out) == 0);

    const TerrainSample c = hit(0.0f, 0.8f, 5.0f);
    CHECK(trackedRadialStep(track, b, c, 1, &out) == 1);
}

TEST_CASE("the ground bend allowance scales with lattice spacing and keeps a floor") {
    for (uint32_t ring = 0; ring < kRings; ++ring) {
        CHECK(groundBendAllowance(ringSpacing(ring)) >= kGroundBendFloorMeters);
        CHECK(groundBendAllowance(ringSpokeSpacing(ring)) >= kGroundBendFloorMeters);
    }

    CHECK(groundBendAllowance(ringSpacing(kRings - 1)) >
          groundBendAllowance(ringSpacing(0)) * 4.0f);
    CHECK(groundBendAllowance(0.0f) == doctest::Approx(kGroundBendFloorMeters));
}

TEST_CASE("a refused ground join names the rule that refused it") {

    const float steep = 0.5f;
    GroundTrack track{};
    ScanSegment out{};
    GroundRefusal why = GroundRefusal::None;

    const TerrainSample a = hit(0.0f, 0.0f, 0.0f, steep);
    const TerrainSample b = hit(0.0f, 0.5f, 2.5f, steep);
    REQUIRE(trackedRadialStep(track, a, b, 1, &out, &why) == 1);
    CHECK(why == GroundRefusal::None);
    const TerrainSample c = hit(0.0f, 1.0f, 5.0f, steep);
    REQUIRE(trackedRadialStep(track, b, c, 1, &out, &why) == 1);
    CHECK(why == GroundRefusal::None);

    const TerrainSample d = hit(0.0f, 6.5f, 7.5f, steep);
    ScanSegment pairwise{};
    REQUIRE(reduceRadialStep(c, d, 1, &pairwise, GroundJoinRule::Continuous) == 1);
    REQUIRE(trackedRadialStep(track, c, d, 1, &out, &why) == 0);
    CHECK(why == GroundRefusal::Track);

    GroundTrack fresh{};
    const TerrainSample p = hit(0.0f, 0.0f, 0.0f);
    const TerrainSample q = hit(0.0f, -60.0f, 2.5f);
    REQUIRE(reduceRadialStep(p, q, 1, &pairwise, GroundJoinRule::Continuous) == 0);
    REQUIRE(trackedRadialStep(fresh, p, q, 1, &out, &why) == 0);
    CHECK(why == GroundRefusal::Pairwise);

    GroundTrack blank{};
    REQUIRE(trackedRadialStep(blank, p, miss(), 1, &out, &why) == 0);
    CHECK(why == GroundRefusal::None);

    GroundTrack arc{};
    REQUIRE(trackedArcStep(arc, p, q, 1, &out, &why) == 0);
    CHECK(why == GroundRefusal::Pairwise);
}

TEST_CASE("the bend allowance grows with slope but flat ground keeps its floor") {

    const float span = 5.0f;

    CHECK(groundBendAllowance(span, 0.0f) == doctest::Approx(span * kGroundBendSlopeChange));
    CHECK(groundBendAllowance(span, 0.3f) == doctest::Approx(span * kGroundBendSlopeChange));

    CHECK(groundBendAllowance(span, 0.9f) == doctest::Approx(span * 0.9f));
    CHECK(groundBendAllowance(span, 0.9f) > groundBendAllowance(span, 0.0f));

    CHECK(groundBendAllowance(0.1f, 0.9f) == doctest::Approx(kGroundBendFloorMeters));
}

TEST_CASE("a steep track admits a step the constant gate refused") {

    const float steep = 0.5f;
    GroundTrack track{};
    ScanSegment out{};

    const TerrainSample a = hit(0.0f, 0.0f, 0.0f, steep);
    const TerrainSample b = hit(0.0f, 2.0f, 2.5f, steep);
    REQUIRE(trackedRadialStep(track, a, b, 4, &out) == 1);
    const TerrainSample c = hit(0.0f, 4.0f, 5.0f, steep);
    REQUIRE(trackedRadialStep(track, b, c, 4, &out) == 1);

    const TerrainSample d = hit(0.0f, 7.6f, 7.5f, steep);
    REQUIRE(1.6f > groundBendAllowance(ringSpacing(4)));
    CHECK(trackedRadialStep(track, c, d, 4, &out) == 1);
}

TEST_CASE("a ridge point is a radial local maximum with real prominence") {
    const float spacing = 5.0f;
    CHECK(crestProminenceFor(spacing) == doctest::Approx(kCrestProminenceMeters));
    const TerrainSample inner = hit(0.0f, 10.0f, 20.0f);
    const TerrainSample outer = hit(0.0f, 10.0f, 30.0f);
    CHECK(groundRidgePoint(inner, hit(0.0f, 12.0f, 25.0f), outer, spacing));

    CHECK_FALSE(groundRidgePoint(inner, hit(0.0f, 11.0f, 25.0f), outer, spacing));

    CHECK_FALSE(groundRidgePoint(inner, hit(0.0f, 12.0f, 25.0f),
                                 hit(0.0f, 14.0f, 30.0f), spacing));

    CHECK_FALSE(groundRidgePoint(miss(), hit(0.0f, 12.0f, 25.0f), outer, spacing));

    CHECK(crestProminenceFor(40.0f) == doctest::Approx(4.0f));
    CHECK_FALSE(groundRidgePoint(inner, hit(0.0f, 12.0f, 25.0f), outer, 40.0f));
}
