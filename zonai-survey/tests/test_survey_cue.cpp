// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cstring>

#include "ScanBatching.hpp"
#include "SurveyCue.hpp"

using namespace zonai_survey::pure;

namespace {

ScanSegment segmentWith(SegmentOrigin origin, bool isArc, bool crestline) {
    ScanSegment segment{};
    segment.origin = origin;
    segment.isArc = isArc;
    segment.crestline = crestline;
    segment.lane = SlopeLane::Cool;
    segment.revealWave = 3.0f;
    return segment;
}

}

TEST_CASE("the survey's start sound is a name the notification bank owns") {

    CHECK(std::strcmp(kSurveyStartCueName, "AmiiboMarker_Sign") == 0);

    CHECK(kSurveyStartCueName[0] != '\0');
}

TEST_CASE("a cliff up-down line is exactly a wall segment that is not an arc") {

    CHECK(isCliffVertical(segmentWith(SegmentOrigin::WallNear, false, false)));

    CHECK(isCliffVertical(segmentWith(SegmentOrigin::WallMid, false, false)));
    CHECK(isCliffVertical(segmentWith(SegmentOrigin::WallFar, false, false)));

    CHECK_FALSE(isCliffVertical(segmentWith(SegmentOrigin::ShallowGround, false, false)));

    CHECK_FALSE(isCliffVertical(segmentWith(SegmentOrigin::WallNear, true, false)));

    CHECK_FALSE(isCliffVertical(segmentWith(SegmentOrigin::Ground, false, false)));
    CHECK_FALSE(isCliffVertical(segmentWith(SegmentOrigin::Ground, true, false)));
}

TEST_CASE("a dimmed cliff vertical never shares a draw with an undimmed rib") {
    const ScanSegment vertical = segmentWith(SegmentOrigin::WallNear, false, false);
    const ScanSegment rib = segmentWith(SegmentOrigin::Ground, false, false);

    CHECK(vertical.revealWave == rib.revealWave);
    CHECK(vertical.lane == rib.lane);
    CHECK(vertical.isArc == rib.isArc);

    CHECK_FALSE(batchKeyFor(vertical).sameAs(batchKeyFor(rib)));
    CHECK(batchBucket(vertical) != batchBucket(rib));

    CHECK(batchKindOf(false, false, true) == 3u);
    CHECK(batchKindOf(false, false, false) == 0u);
    CHECK(batchKindOf(true, false, false) == 1u);
    CHECK(batchKindOf(false, true, false) == 2u);

    CHECK(batchKindOf(false, true, true) == 2u);

    for (std::uint32_t kind = 0; kind < kBatchKindCount; ++kind)
        CHECK(kind < kBatchKindCount);
    CHECK(kBatchKindCount == 4u);
}

TEST_CASE("the batch bucket axis still bounds every segment it can be handed") {

    CHECK(kBatchBucketCount == kBatchWaveSteps * kBatchKindCount * kSlopeLaneCount);
    CHECK(kMaxSurveyGroups == kBatchBucketCount);

    const SegmentOrigin origins[] = {SegmentOrigin::Ground, SegmentOrigin::WallNear,
                                     SegmentOrigin::WallMid, SegmentOrigin::WallFar,
                                     SegmentOrigin::ShallowGround};
    const bool flags[] = {false, true};
    const float waves[] = {0.0f, 0.25f, 1.0f, 12.5f, 900.0f};
    for (SegmentOrigin origin : origins) {
        for (bool isArc : flags) {
            for (bool crest : flags) {
                ScanSegment segment = segmentWith(origin, isArc, crest);
                for (float wave : waves) {
                    segment.revealWave = wave;
                    CHECK(batchBucket(segment) < kBatchBucketCount);
                }
            }
        }
    }
}

TEST_CASE("the cliff vertical dim is the number Clay asked for") {

    CHECK(kCliffVerticalAlphaScale == doctest::Approx(0.90f));
}
