// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "ScanBatching.hpp"
#include "SurveyPalette.hpp"

using namespace zonai_survey::pure;

namespace {

struct EmittedGroup {
    BatchKey key{};
    std::vector<std::uint32_t> indices{};
};

std::vector<ScanSegment> composed(bool coherent) {
    std::vector<ScanSegment> segments{};
    segments.reserve(kWorstCaseArcs + kWorstCaseRibs);
    for (std::uint32_t ring = 0; ring < kRings; ++ring) {
        const float wave = waveArrivalOf(ring);
        const float radius = ringRadius(ring);
        for (int pass = 0; pass < 2; ++pass) {
            const bool arcs = pass == 1;
            if (!arcs && ring == 0) continue;
            const std::uint32_t spokes = arcs ? kSpokes - 1 : kSpokes;
            for (std::uint32_t spoke = 0; spoke < spokes; ++spoke) {
                ScanSegment segment{};
                segment.revealWave = wave;
                segment.isArc = arcs;
                segment.surface = static_cast<SegmentClass>(
                    coherent ? (ring / 4u) % 3u : (spoke + ring) % 3u);
                segment.slopeBand = static_cast<SlopeBand>(
                    coherent ? (ring / 4u) % kSlopeBandCount
                             : (spoke + ring) % kSlopeBandCount);

                segment.lane = surveyLaneFor(segment);
                segment.ax = radius + static_cast<float>(spoke);
                segment.ay = 0.0f;
                segment.az = static_cast<float>(ring);
                segment.bx = segment.ax + 1.0f;
                segment.by = 0.25f;
                segment.bz = segment.az + 1.0f;
                segments.push_back(segment);
            }
        }
    }

    std::vector<ScanSegment> packed{};
    packed.reserve(segments.size());
    for (std::uint32_t bucket = 0; bucket < kBatchBucketCount; ++bucket)
        for (const auto& segment : segments)
            if (batchBucket(segment) == bucket) packed.push_back(segment);
    return packed;
}

std::vector<EmittedGroup> collect(const std::vector<ScanSegment>& segments,
                                  std::uint32_t tick) {
    std::vector<EmittedGroup> groups{};
    planGroups(segments.data(), static_cast<std::uint32_t>(segments.size()), tick,
               [&](const SegmentGroup& group) {
                   EmittedGroup copy{};
                   copy.key = group.key;

                   for (std::uint32_t i = group.first;
                        i < group.first + group.count; ++i) {
                       if (segmentPasses(segments[i], tick))
                           copy.indices.push_back(i);
                   }
                   CHECK(copy.indices.size() == group.drawn);
                   groups.push_back(copy);
               });
    return groups;
}

std::uint32_t drawsOnVertexPath(const std::vector<EmittedGroup>& groups) {
    return static_cast<std::uint32_t>(groups.size());
}

std::vector<std::uint32_t> perLineOrder(const std::vector<ScanSegment>& segments,
                                        std::uint32_t tick) {
    std::vector<std::uint32_t> drawn{};
    for (std::uint32_t i = 0; i < segments.size(); ++i) {
        if (segmentPasses(segments[i], tick)) drawn.push_back(i);
    }
    return drawn;
}

constexpr std::uint32_t kSettledTick = kSweepTicks / 2;
constexpr std::uint32_t kSweepingTick = 15;

std::uint32_t busiestTick(const std::vector<ScanSegment>& segments,
                          std::uint32_t& outLines) {
    std::uint32_t best = 0;
    outLines = 0;
    for (std::uint32_t tick = 0; tick <= kSweepEndTicks; ++tick) {
        const auto lines =
            static_cast<std::uint32_t>(perLineOrder(segments, tick).size());
        if (lines > outLines) {
            outLines = lines;
            best = tick;
        }
    }
    return best;
}

}
TEST_CASE("every group carries exactly one appearance") {
    const auto segments = composed(false);
    const auto groups = collect(segments, kSettledTick);
    REQUIRE(!groups.empty());
    for (const auto& group : groups) {
        REQUIRE(!group.indices.empty());
        CHECK(group.indices.size() <= kSpokes);
        for (const std::uint32_t index : group.indices) {
            const ScanSegment& segment = segments[index];

            CHECK(segment.revealWave == group.key.revealWave);
            CHECK(slopeLane(segment.slopeBand) == group.key.lane);
            CHECK(segment.isArc == group.key.isArc);
            CHECK(segment.crestline == group.key.crestline);

            CHECK(surveyPassAlpha(segment.crestline, segment.revealWave,
                                  kSettledTick) ==
                  surveyPassAlpha(group.key.crestline, group.key.revealWave,
                                  kSettledTick));
            CHECK(segmentWidth(segment.isArc) == segmentWidth(group.key.isArc));
            CHECK(sweepLift(segment.revealWave, kSettledTick) ==
                  sweepLift(group.key.revealWave, kSettledTick));
        }
    }
}

TEST_CASE("grouping draws every line the per-line path draws, exactly once") {
    for (const bool coherent : {true, false}) {
        const auto segments = composed(coherent);
        for (const std::uint32_t tick : {kSweepingTick, kSettledTick}) {
            const auto expected = perLineOrder(segments, tick);
            REQUIRE(!expected.empty());
            const auto groups = collect(segments, tick);
            std::vector<std::uint32_t> seen{};
            for (const auto& group : groups)
                for (const std::uint32_t index : group.indices) seen.push_back(index);
            std::vector<std::uint32_t> sorted = seen;
            std::sort(sorted.begin(), sorted.end());
            CHECK(sorted == expected);
            CHECK(seen.size() == expected.size());
        }
    }
}

TEST_CASE("blend order survives: a ring's ribs are all submitted before its arcs") {
    const auto segments = composed(false);
    const auto groups = collect(segments, kSettledTick);

    std::vector<std::pair<float, bool>> order{};
    for (const auto& group : groups) {
        const std::pair<float, bool> band{group.key.revealWave, group.key.isArc};
        if (order.empty() || order.back() != band) {
            for (const auto& seen : order) CHECK(seen != band);
            order.push_back(band);
        }
    }
    REQUIRE(order.size() >= 2);

    std::vector<std::pair<float, bool>> produced{};
    for (const auto& segment : segments) {
        if (!segmentPasses(segment, kSettledTick)) continue;
        const std::pair<float, bool> band{segment.revealWave, segment.isArc};
        if (produced.empty() || produced.back() != band) produced.push_back(band);
    }
    CHECK(order == produced);
}

TEST_CASE("the vertex path collapses the dense lattice by more than an order of magnitude") {
    const auto segments = composed(true);
    std::uint32_t busiestLines = 0;
    const std::uint32_t tick = busiestTick(segments, busiestLines);
    REQUIRE(busiestLines > 500);
    const auto perLine = perLineOrder(segments, tick);
    const auto groups = collect(segments, tick);

    const std::uint32_t draws = drawsOnVertexPath(groups);
    std::uint32_t lines = 0;
    for (const auto& group : groups) lines += static_cast<std::uint32_t>(group.indices.size());
    CHECK(lines == perLine.size());

    CHECK(draws * 10 < perLine.size());
}

TEST_CASE("one key is one draw, however many lines it holds") {

    for (const bool coherent : {true, false}) {
        const auto segments = composed(coherent);
        for (const std::uint32_t tick : {kSweepingTick, kSettledTick}) {
            const auto groups = collect(segments, tick);
            std::vector<BatchKey> seen{};
            for (const auto& group : groups) {
                for (const auto& other : seen) CHECK_FALSE(other.sameAs(group.key));
                seen.push_back(group.key);
            }
            CHECK(groups.size() <= kMaxSurveyGroups);
        }
    }
}

TEST_CASE("the six slope bands cost two draws, not six") {

    const auto segments = composed(false);
    const auto groups = collect(segments, kSettledTick);
    REQUIRE(!groups.empty());
    std::vector<std::pair<float, bool>> bands{};
    std::vector<std::uint32_t> counts{};
    for (const auto& group : groups) {
        const std::pair<float, bool> band{group.key.revealWave, group.key.isArc};
        const auto found = std::find(bands.begin(), bands.end(), band);
        if (found == bands.end()) {
            bands.push_back(band);
            counts.push_back(1);
        } else {
            ++counts[static_cast<std::size_t>(found - bands.begin())];
        }
    }
    for (const std::uint32_t count : counts) CHECK(count <= kSlopeLaneCount);

    for (std::uint32_t band = 0; band < kSlopeBandCount; ++band) {
        for (std::uint32_t other = band + 1u; other < kSlopeBandCount; ++other) {
            const SurveyRgb a = surveyCoreColor(static_cast<SlopeBand>(band), 6.0f);
            const SurveyRgb b = surveyCoreColor(static_cast<SlopeBand>(other), 6.0f);
            CHECK((a.r != b.r || a.g != b.g || a.b != b.b));
        }
    }
}

TEST_CASE("the published frame is ordered the way the planner needs it") {
    for (const bool coherent : {true, false}) {
        const auto segments = composed(coherent);
        CHECK(framePackedForBatching(segments.data(),
                                     static_cast<std::uint32_t>(segments.size())));

        for (std::uint32_t i = 1; i < segments.size(); ++i) {
            CHECK(batchBucket(segments[i]) >= batchBucket(segments[i - 1u]));
            const bool sameBucket =
                batchBucket(segments[i]) == batchBucket(segments[i - 1u]);
            CHECK(sameBucket ==
                  batchKeyFor(segments[i]).sameAs(batchKeyFor(segments[i - 1u])));
        }
    }

    auto broken = composed(true);
    REQUIRE(broken.size() > 2000);
    std::swap(broken.front(), broken.back());
    CHECK_FALSE(framePackedForBatching(
        broken.data(), static_cast<std::uint32_t>(broken.size())));
}

TEST_CASE("segments the picture rejects never reach a group") {
    auto segments = composed(true);

    segments[10].bx = segments[10].ax;
    segments[10].by = segments[10].ay;
    segments[10].bz = segments[10].az;
    segments[20].ay = std::numeric_limits<float>::infinity();

    const auto groups = collect(segments, kSettledTick);
    for (const auto& group : groups) {
        for (const std::uint32_t index : group.indices) {
            CHECK(index != 10u);
            CHECK(index != 20u);
            CHECK(drawableSegment(segments[index]));
        }
    }
}

TEST_CASE("an empty or absent frame plans nothing") {
    std::uint32_t emitted = 0;
    const auto count = [&](const SegmentGroup&) { ++emitted; };
    CHECK(planGroups(nullptr, 100, kSettledTick, count) == 0);
    const auto segments = composed(true);
    CHECK(planGroups(segments.data(), 0, kSettledTick, count) == 0);
    CHECK(emitted == 0);
}

TEST_CASE("the ring we ask for covers the worst frame this lattice can produce") {

    std::uint32_t worstBytes = 0;
    for (const bool coherent : {true, false}) {
        const auto segments = composed(coherent);
        std::uint32_t busiestLines = 0;
        const std::uint32_t busiest = busiestTick(segments, busiestLines);
        for (const std::uint32_t tick : {kSweepingTick, busiest}) {
            std::uint32_t frameBytes = 0;
            {
                const auto groups = collect(segments, tick);
                std::uint32_t lines = 0;
                for (const auto& g : groups) lines += static_cast<std::uint32_t>(g.indices.size());
                frameBytes += surveyRingBytesPerPass(
                    lines, static_cast<std::uint32_t>(groups.size()));

                CHECK(groups.size() <= kMaxSurveyGroups);
                CHECK(lines <= kMaxDrawnSegments);
            }
            if (frameBytes > worstBytes) worstBytes = frameBytes;
        }
    }
    REQUIRE(worstBytes > 0);

    CHECK(kSurveyRingBytes >= worstBytes * kRingFramesInFlight);
    CHECK(kSurveyRingBytes >=
          surveyBeamRingBytesPerFrame(kMaxDrawnSegments, kMaxSurveyGroups) *
              kRingFramesInFlight);
    CHECK(kSurveyRingBytes >= kSurveyRequiredRingBytes);
    CHECK(kSurveySharedSlackBytes == 4u * 1024u * 1024u);
}

TEST_CASE("a lane's three bands ride one draw at their own exact colours") {

    const float waves[] = {1.0f, 4.0f, 12.0f, 24.0f,
                           static_cast<float>(kRings)};
    for (const float wave : waves) {
        for (std::uint32_t band = 0; band < kSlopeBandCount; ++band) {
            const auto slope = static_cast<SlopeBand>(band);
            const SlopeLane lane = slopeLane(slope);
            const float rate = slopeRate(slope);
            const SurveyRgba start = surveyLaneColor(lane, false, wave);
            const SurveyRgba end = surveyLaneColor(lane, true, wave);
            const SurveyRgba mixed = mixColor(start, end, rate);
            const SurveyRgba expected = surveyBandColor(slope, wave);
            CHECK(mixed.r == doctest::Approx(expected.r).epsilon(1e-6));
            CHECK(mixed.g == doctest::Approx(expected.g).epsilon(1e-6));
            CHECK(mixed.b == doctest::Approx(expected.b).epsilon(1e-6));
            CHECK(mixed.a == doctest::Approx(expected.a).epsilon(1e-6));
            CHECK(rate >= 0.0f);
            CHECK(rate <= 1.0f);
        }
    }

    CHECK(kSlopeLaneCount == 2);
    for (std::uint32_t band = 0; band < kSlopeBandCount; ++band) {
        const SlopeLane lane = slopeLane(static_cast<SlopeBand>(band));
        CHECK((lane == SlopeLane::Cool || lane == SlopeLane::Warm));
    }
    CHECK(slopeLane(SlopeBand::Level) == SlopeLane::Cool);
    CHECK(slopeLane(SlopeBand::Rolling) == SlopeLane::Cool);
    CHECK(slopeLane(SlopeBand::Inclined) == SlopeLane::Cool);
    CHECK(slopeLane(SlopeBand::Steep) == SlopeLane::Warm);
    CHECK(slopeLane(SlopeBand::Severe) == SlopeLane::Warm);
    CHECK(slopeLane(SlopeBand::Vertical) == SlopeLane::Warm);
}

TEST_CASE("Survey palette keeps six distinguishable bands, cool to warm") {
    for (std::uint32_t band = 0; band < kSlopeBandCount; ++band) {
        const auto slope = static_cast<SlopeBand>(band);
        for (const float wave : {1.0f, static_cast<float>(kRings) * 0.66f,
                                 static_cast<float>(kRings)}) {
            const SurveyRgba color = surveyBandColor(slope, wave);
            for (const float channel : {color.r, color.g, color.b, color.a}) {
                CHECK(channel >= 0.0f);
                CHECK(channel <= 1.0f);
            }
        }
    }

    CHECK(slopeColor(SlopeBand::Level).b > slopeColor(SlopeBand::Level).r);
    CHECK(slopeColor(SlopeBand::Vertical).r > slopeColor(SlopeBand::Vertical).b);
    CHECK(slopeColor(SlopeBand::Level).r < 0.6f);
    for (const SlopeBand warm :
         {SlopeBand::Steep, SlopeBand::Severe, SlopeBand::Vertical})
        CHECK(slopeColor(warm).r > 0.9f);

    for (std::uint32_t band = 1; band < kSlopeBandCount; ++band) {
        const SurveyRgb previous = slopeColor(static_cast<SlopeBand>(band - 1u));
        const SurveyRgb current = slopeColor(static_cast<SlopeBand>(band));
        CHECK(current.g <= previous.g);
    }
    CHECK(slopeColor(SlopeBand::Vertical).g < slopeColor(SlopeBand::Steep).g);
    CHECK(slopeColor(SlopeBand::Level).b == doctest::Approx(1.0f));
    for (std::uint32_t band = 1; band < kSlopeBandCount; ++band)
        CHECK(slopeColor(static_cast<SlopeBand>(band)).b <
              slopeColor(SlopeBand::Level).b);
}

TEST_CASE("vertical terrain is quieted rather than shouted") {

    const SurveyRgba vertical = surveyBandColor(SlopeBand::Vertical, 8.0f);
    const SurveyRgba steep = surveyBandColor(SlopeBand::Steep, 8.0f);

    CHECK(vertical.g > 0.15f);
    CHECK(vertical.b > 0.15f);
    CHECK(vertical.g == doctest::Approx(vertical.b).epsilon(0.05));

    CHECK(vertical.a < 0.75f);
    CHECK(vertical.a == doctest::Approx(steep.a));

    CHECK(kCliffVerticalAlphaScale < 1.0f);
    CHECK(kCliffVerticalAlphaScale > 0.5f);
}

TEST_CASE("distance does not fade the Survey") {

    for (std::uint32_t band = 0; band < kSlopeBandCount; ++band) {
        const auto slope = static_cast<SlopeBand>(band);
        const SurveyRgba near = surveyBandColor(slope, 4.0f);
        const SurveyRgba mid = surveyBandColor(slope, 12.0f);
        const SurveyRgba far = surveyBandColor(slope, static_cast<float>(kRings));
        CHECK(far.a == doctest::Approx(near.a));
        CHECK(far.r == doctest::Approx(mid.r));
        CHECK(far.g == doctest::Approx(mid.g));
        CHECK(far.b == doctest::Approx(mid.b));
    }

    CHECK(surveyRangePearl(0.0f) == doctest::Approx(kSurveyNearPearl));
    float previous = surveyRangePearl(0.0f);
    for (std::uint32_t ring = 1; ring <= kRings; ++ring) {
        const float pearl = surveyRangePearl(static_cast<float>(ring));
        CHECK(pearl <= previous);
        previous = pearl;
    }
    CHECK(surveyRangePearl(static_cast<float>(kRings)) == doctest::Approx(0.0f));
}

TEST_CASE("a pass packs into one allocation with no rounding waste") {

    CHECK(surveyRingBytesPerPass(1, 1) == 256u + 256u);
    CHECK(surveyRingBytesPerPass(64, 1) == roundToRingBlock(64u * 2u * 36u) + 256u);

    const auto perGroupTotal = [](std::uint32_t groups, std::uint32_t linesEach) {
        return groups * (roundToRingBlock(linesEach * 2u * kRingVertexBytes) + 256u);
    };
    for (std::uint32_t linesEach = 1; linesEach <= kSpokes; ++linesEach) {
        const std::uint32_t groups = 100;
        CHECK(surveyRingBytesPerPass(groups * linesEach, groups) <=
              perGroupTotal(groups, linesEach));
    }

    CHECK(surveyRingBytesPerPass(100u * 31u, 100u) < perGroupTotal(100u, 31u));
}

TEST_CASE("the split predicate equals the original, everywhere") {

    ScanSegment segment{};
    segment.ax = 0.0f; segment.ay = 5.0f; segment.az = 0.0f;
    segment.bx = 2.0f; segment.by = 5.0f; segment.bz = 1.0f;
    for (std::uint32_t wave = 1; wave <= kRings; wave += 3) {
        segment.revealWave = static_cast<float>(wave);
        for (const SegmentOrigin origin :
             {SegmentOrigin::Ground, SegmentOrigin::WallMid,
              SegmentOrigin::ShallowGround}) {
            segment.origin = origin;
            for (const bool isArc : {false, true}) {
                segment.isArc = isArc;
                for (std::uint32_t tick = 0; tick < 700; tick += 7) {
                    const bool joint =
                        segmentStaticVisible(segment) &&
                        groupTickPasses(segment.revealWave, tick,
                                        segment.crestline);
                    REQUIRE(joint == segmentPasses(segment, tick));
                }
            }
        }
    }
    ScanSegment broken = segment;
    broken.bx = broken.ax; broken.by = broken.ay; broken.bz = broken.az;
    CHECK_FALSE(segmentStaticVisible(broken));
}

TEST_CASE("published group spans reproduce planGroups' grouping") {

    ScanSegment frame[64]{};
    std::uint32_t count = 0;
    for (std::uint32_t ring = 1; ring <= 4; ++ring) {
        for (const bool isArc : {false, true}) {
            for (std::uint32_t n = 0; n < 3; ++n) {
                ScanSegment& segment = frame[count++];
                segment.revealWave = static_cast<float>(ring);
                segment.isArc = isArc;
                segment.lane = n == 2 ? SlopeLane::Warm : SlopeLane::Cool;
                segment.ax = static_cast<float>(count);
                segment.ay = 1.0f;
                segment.az = 0.0f;
                segment.bx = static_cast<float>(count) + 1.0f;
                segment.by = 1.0f;
                segment.bz = 1.0f;
            }
        }
    }
    REQUIRE(framePackedForBatching(frame, count));

    GroupSpan spans[kMaxSurveyGroups]{};
    const std::uint32_t spanCount = buildGroupSpans(frame, count, spans, kMaxSurveyGroups);

    const auto lit = static_cast<std::uint32_t>(sweepTicksToReach(5.0f));
    std::uint32_t planned = 0;
    std::uint32_t matched = 0;
    planGroups(frame, count, lit, [&](const SegmentGroup& group) {
        for (std::uint32_t i = 0; i < spanCount; ++i) {
            if (spans[i].first == group.first && spans[i].count == group.count)
                ++matched;
        }
        ++planned;
    });
    CHECK(planned != 0);
    CHECK(matched == planned);
    std::uint32_t cursor = 0;
    for (std::uint32_t i = 0; i < spanCount; ++i) {
        CHECK(spans[i].first == cursor);
        cursor += spans[i].count;
    }
    CHECK(cursor == count);
}

TEST_CASE("feet arcs, base arcs and crest overlays never share a bucket") {

    ScanSegment feet{};
    feet.isArc = true;
    feet.lane = SlopeLane::Cool;
    ScanSegment ringZero = feet;
    ringZero.revealWave = 1.0f;
    std::uint32_t previous = 0;
    for (const float fraction : {0.25f, 0.5f, 0.75f}) {
        feet.revealWave = fraction;
        const std::uint32_t bucket = batchBucket(feet);
        CHECK(bucket < batchBucket(ringZero));
        if (fraction > 0.25f) CHECK(bucket > previous);
        previous = bucket;
    }
    ScanSegment crest = ringZero;
    crest.crestline = true;
    CHECK(batchBucket(crest) != batchBucket(ringZero));

    CHECK(batchKindOf(true, true, false) == 2u);
    CHECK(batchKindOf(true, false, false) == 1u);
    CHECK(batchKindOf(false, false, false) == 0u);
    CHECK(batchKindOf(false, false, true) == 3u);

    CHECK(kBatchBucketCount == kBatchWaveSteps * kBatchKindCount * kSlopeLaneCount);
    CHECK(kMaxSurveyGroups >= kBatchBucketCount);
}

TEST_CASE("width depends on the line's kind and nothing else") {

    CHECK(segmentWidth(true) > segmentWidth(false));

    CHECK(segmentWidth(true) == doctest::Approx(kArcWidth * kSurveyWidthGain));
    CHECK(segmentWidth(false) == doctest::Approx(kRibWidth * kSurveyWidthGain));
}

TEST_CASE("every Survey line follows the travelling envelope") {
    const float wave = 20.0f;
    bool bodyAppeared = false;
    bool crestAppeared = false;
    for (std::uint32_t tick = 0; tick <= kSweepLifetimeTicks; ++tick) {
        const float body = sweepAlpha(wave, tick);
        const float crest = sweepCrestlineAlpha(wave, tick);

        CHECK(surveyPassAlpha(false, wave, tick) == doctest::Approx(body));
        CHECK(surveyPassAlpha(true, wave, tick) == doctest::Approx(crest));
        CHECK(groupTickPasses(wave, tick, false) == (body > kAlphaCutoff));
        CHECK(groupTickPasses(wave, tick, true) == (crest > kAlphaCutoff));
        bodyAppeared = bodyAppeared || body > kAlphaCutoff;
        crestAppeared = crestAppeared || crest > kAlphaCutoff;
    }
    CHECK(bodyAppeared);
    CHECK(crestAppeared);
}
