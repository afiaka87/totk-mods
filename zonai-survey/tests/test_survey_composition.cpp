// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "ScanBatching.hpp"
#include "SurveyPalette.hpp"
#include "WallReduction.hpp"

using namespace zonai_survey::pure;

namespace {

std::uint32_t busiestTick(const std::vector<ScanSegment>& frame);

ScanSegment segmentOf(SegmentOrigin origin, SlopeBand band, bool isArc, float wave) {
    ScanSegment segment{};
    segment.origin = origin;
    segment.slopeBand = band;
    segment.isArc = isArc;
    segment.revealWave = wave;
    segment.surface = originIsWall(origin) ? SegmentClass::Cliff : SegmentClass::Ground;
    segment.ax = 1.0f;
    segment.bx = 2.0f;
    segment.by = 0.5f;
    return segment;
}

std::vector<ScanSegment> compose(std::vector<ScanSegment> segments) {
    for (auto& segment : segments) segment.lane = surveyLaneFor(segment);
    std::vector<ScanSegment> packed{};
    packed.reserve(segments.size());
    for (std::uint32_t bucket = 0; bucket < kBatchBucketCount; ++bucket)
        for (const auto& segment : segments)
            if (batchBucket(segment) == bucket) packed.push_back(segment);
    return packed;
}

std::vector<ScanSegment> mixedFrame() {
    std::vector<ScanSegment> segments{};
    for (std::uint32_t ring = 1; ring < kRings; ++ring) {
        const float wave = waveArrivalOf(ring);
        for (int arc = 0; arc < 2; ++arc) {

            for (std::uint32_t spoke = 0; spoke < 12; ++spoke) {
                const SlopeBand band =
                    static_cast<SlopeBand>((spoke * 5u + ring) % kSlopeBandCount);
                segments.push_back(segmentOf(SegmentOrigin::Ground, band, arc == 1, wave));
            }

            const SegmentOrigin layers[] = {SegmentOrigin::WallNear, SegmentOrigin::WallMid,
                                            SegmentOrigin::WallFar};
            for (std::uint32_t i = 0; i < 3; ++i) {
                const SlopeBand band = static_cast<SlopeBand>(
                    static_cast<std::uint32_t>(SlopeBand::Steep) + (ring + i) % 3u);
                segments.push_back(segmentOf(layers[i], band, arc == 1, wave));
            }
        }
    }
    return segments;
}

std::uint32_t groupsIn(const std::vector<ScanSegment>& frame, std::uint32_t tick) {
    return planGroups(frame.data(), static_cast<std::uint32_t>(frame.size()), tick,
                      [](const SegmentGroup&) {});
}

std::uint32_t linesIn(const std::vector<ScanSegment>& frame, std::uint32_t tick) {
    std::uint32_t drawn = 0;
    for (const auto& segment : frame)
        if (segmentPasses(segment, tick)) ++drawn;
    return drawn;
}

std::uint32_t busiestTick(const std::vector<ScanSegment>& frame) {
    std::uint32_t best = 0;
    std::uint32_t bestLines = 0;
    for (std::uint32_t tick = 0; tick <= kSweepEndTicks; ++tick) {
        const std::uint32_t lines = linesIn(frame, tick);
        if (lines > bestLines) {
            bestLines = lines;
            best = tick;
        }
    }
    return best;
}

float colorGap(const SurveyRgba& a, const SurveyRgba& b) {
    const float dr = a.r - b.r;
    const float dg = a.g - b.g;
    const float db = a.b - b.b;
    return std::sqrt(dr * dr + dg * dg + db * db);
}

}

TEST_CASE("the composed picture is across lines on the ground, a mesh on cliffs") {

    const SegmentOrigin ground[] = {SegmentOrigin::Ground,
                                    SegmentOrigin::ShallowGround};
    const SegmentOrigin cliff[] = {SegmentOrigin::WallNear, SegmentOrigin::WallMid,
                                   SegmentOrigin::WallFar};
    for (const SegmentOrigin origin : ground) {
        CHECK(surveyShows(segmentOf(origin, SlopeBand::Level, true, 4.0f)));
        CHECK_FALSE(surveyShows(segmentOf(origin, SlopeBand::Level, false, 4.0f)));
    }
    for (const SegmentOrigin origin : cliff) {
        CHECK(surveyShows(segmentOf(origin, SlopeBand::Level, true, 4.0f)));
        CHECK(surveyShows(segmentOf(origin, SlopeBand::Level, false, 4.0f)));
    }
}

TEST_CASE("a cliff line is tagged with the surface it was found on") {

    CHECK(wallOriginFor(0) == SegmentOrigin::WallNear);
    CHECK(wallOriginFor(1) == SegmentOrigin::WallMid);
    CHECK(wallOriginFor(2) == SegmentOrigin::WallFar);

    CHECK(wallOriginFor(9) == SegmentOrigin::WallFar);

    CHECK_FALSE(originIsWall(SegmentOrigin::Ground));
    CHECK_FALSE(originIsWall(SegmentOrigin::ShallowGround));
    CHECK(originIsWall(SegmentOrigin::WallNear));
    CHECK(originIsWall(SegmentOrigin::WallMid));
    CHECK(originIsWall(SegmentOrigin::WallFar));
}

TEST_CASE("the buried filter removes exactly the buried cliff lines") {

    const SegmentOrigin origins[] = {
        SegmentOrigin::Ground,   SegmentOrigin::ShallowGround,
        SegmentOrigin::WallNear, SegmentOrigin::WallMid,
        SegmentOrigin::WallFar,
    };
    for (const SegmentOrigin origin : origins) {
        ScanSegment plain = segmentOf(origin, SlopeBand::Level, false, 4.0f);
        ScanSegment marked = plain;
        marked.buried = true;
        CHECK(surveyBuriedShows(plain));
        CHECK(surveyBuriedShows(marked) == !originIsWall(origin));
    }
}

TEST_CASE("cliff lines are marked buried only when both ends are buried") {

    const auto sampleFacing = [](float x, bool buried) {
        TerrainSample sample{};
        sample.hit = true;
        sample.x = x;
        sample.y = 1.0f;
        sample.z = 30.0f;
        sample.normalY = 0.1f;
        sample.normalZ = -1.0f;
        sample.distance = 30.0f;
        sample.buried = buried;
        return sample;
    };
    ScanSegment out[kWallDepthLayers]{};

    const auto reduceBothCells = [&](bool aAway, bool bAway) {
        const TerrainSample a = sampleFacing(0.0f, aAway);
        const TerrainSample b = sampleFacing(0.4f, bAway);
        REQUIRE(reduceWallLayers(&a, 1, &b, 1, WallEdgeAxis::Across, out,
                                 kWallDepthLayers) == 1);
        return out[0].buried;
    };
    CHECK(reduceBothCells(true, true));
    CHECK_FALSE(reduceBothCells(true, false));
    CHECK_FALSE(reduceBothCells(false, true));
    CHECK_FALSE(reduceBothCells(false, false));
}

TEST_CASE("shallow-ground lines route as ground, not as cliff") {

    ScanSegment shallow{};
    shallow.isArc = true;
    shallow.origin = SegmentOrigin::ShallowGround;
    shallow.slopeBand = SlopeBand::Rolling;
    shallow.revealWave = 3.0f;
    CHECK_FALSE(originIsWall(SegmentOrigin::ShallowGround));
    CHECK(surveyShows(shallow));
    CHECK(surveyLaneFor(shallow) == SlopeLane::Cool);
}

TEST_CASE("a cliff row and a terrain row of the same slope look identical") {

    ScanSegment wall{};
    wall.slopeBand = SlopeBand::Vertical;
    for (const SegmentOrigin origin : {SegmentOrigin::WallNear, SegmentOrigin::WallMid,
                                       SegmentOrigin::WallFar}) {
        wall.origin = origin;
        CHECK(surveyLaneFor(wall) == SlopeLane::Warm);

        wall.buried = true;
        CHECK(surveyLaneFor(wall) == SlopeLane::Warm);
        wall.buried = false;
    }

    ScanSegment steepGround{};
    steepGround.origin = SegmentOrigin::Ground;
    steepGround.slopeBand = SlopeBand::Severe;
    CHECK(surveyLaneFor(steepGround) == SlopeLane::Warm);

    steepGround.steepness = 0.86f;
    const SurveyRgba direct = surveySegmentColor(steepGround, 6.0f);
    const SurveyRgba expected =
        mixColor(surveyLaneColor(SlopeLane::Warm, false, 6.0f),
                 surveyLaneColor(SlopeLane::Warm, true, 6.0f),
                 continuousSlopeRate(0.86f, SlopeBand::Severe));
    CHECK(direct.r == doctest::Approx(expected.r));
    CHECK(direct.g == doctest::Approx(expected.g));
    CHECK(direct.b == doctest::Approx(expected.b));
    CHECK(direct.a == doctest::Approx(expected.a));
}

TEST_CASE("the slope ramp runs one way, and its ends are unmistakable") {

    const SurveyRgba flattest = laneStartColor(laneIndex(SlopeLane::Cool));
    const SurveyRgba steepest = laneEndColor(laneIndex(SlopeLane::Warm));
    const SurveyRgba coolEnd = laneEndColor(laneIndex(SlopeLane::Cool));
    const SurveyRgba warmStart = laneStartColor(laneIndex(SlopeLane::Warm));

    CHECK(colorGap(flattest, steepest) > 0.35f);

    CHECK(colorGap(coolEnd, warmStart) < 0.25f);
}

TEST_CASE("composing the frame is what makes a lane cost one draw") {

    const std::vector<ScanSegment> composed = compose(mixedFrame());
    REQUIRE(framePackedForBatching(composed.data(),
                                   static_cast<std::uint32_t>(composed.size())));
    const std::uint32_t tick = busiestTick(composed);
    const std::uint32_t groups = groupsIn(composed, tick);
    const std::uint32_t lines = linesIn(composed, tick);
    REQUIRE(lines > 0);
    CHECK(groups > 0);

    CHECK(groups < lines);
}
