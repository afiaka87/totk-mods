// SPDX-License-Identifier: MIT
#include <doctest.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "WallReduction.hpp"
#include "WallSurvey.hpp"

using namespace zonai_survey::pure;

namespace {

TerrainSample wallHit(float x, float y, float z, float distance,
                      float normalY = 0.1f) {
    TerrainSample sample{true, x, y, z, normalY, distance};
    const float horizontalSquared = 1.0f - normalY * normalY;
    sample.normalZ = std::sqrt(horizontalSquared > 0.0f
                                   ? horizontalSquared
                                   : 0.0f);
    return sample;
}

}

TEST_CASE("the cliff fan's shipped geometry is what the reducers assume") {

    CHECK(kWallColumns == 64);
    CHECK(kWallRows == 64);
    CHECK(kWallProbes == kWallColumns * kWallRows);
    CHECK(kWallDepthLayers == 3);
    CHECK(kWallSamples == kWallProbes * kWallDepthLayers);
    CHECK(kWallMaxRaycastsPerProbe == 7);
    CHECK(kWallJoinScale == doctest::Approx(1.75f));

    CHECK(kWallMinElevation < 0.0f);
    CHECK(kWallMaxElevation > 0.0f);
    CHECK(wallAzimuthStep() > 0.0f);
    CHECK(wallElevationStep() > 0.0f);
}

TEST_CASE("wall rays fill the forward cone above and below Link") {
    const WallRay lower = wallRayFor(0, 10.0f, 20.0f, 30.0f,
                                     0.0f, 1.0f);
    const WallRay upper = wallRayFor(kWallProbes - 1u,
                                     10.0f, 20.0f, 30.0f, 0.0f, 1.0f);
    CHECK(lower.fromX == doctest::Approx(10.0f));
    CHECK(lower.fromY == doctest::Approx(20.0f + kWallEyeHeight));
    CHECK(lower.toY < lower.fromY);
    CHECK(upper.toY > upper.fromY);
    CHECK(lower.toZ > lower.fromZ);
    CHECK(upper.toZ > upper.fromZ);
}

TEST_CASE("only steep finite hits enter the wall mesh") {
    CHECK(acceptedWallSample(wallHit(0, 0, 10, 10, 0.2f)));
    CHECK_FALSE(acceptedWallSample(wallHit(0, 0, 10, 10, 0.9f)));
    CHECK_FALSE(acceptedWallSample(TerrainSample{}));
}

TEST_CASE("wall joining rejects a different depth layer") {
    const TerrainSample a = wallHit(0.0f, 0.0f, 100.0f, 100.0f);
    const TerrainSample sameSurface = wallHit(2.0f, 0.0f, 100.0f, 101.0f);
    const TerrainSample otherDepth = wallHit(2.0f, 0.0f, 104.0f, 104.0f);
    TerrainSample sharpCorner = sameSurface;
    sharpCorner.normalX = std::sqrt(1.0f - sharpCorner.normalY *
                                             sharpCorner.normalY);
    sharpCorner.normalZ = 0.0f;
    CHECK(wallLinkable(a, sameSurface, WallEdgeAxis::Across));
    CHECK_FALSE(wallLinkable(a, otherDepth, WallEdgeAxis::Across));
    CHECK_FALSE(wallLinkable(a, sharpCorner, WallEdgeAxis::Across));
}

TEST_CASE("wall joining requires a surface-supported chord") {
    const TerrainSample a = wallHit(0.0f, 0.0f, 100.0f, 100.0f);
    const TerrainSample alongWall = wallHit(2.0f, 0.0f, 100.0f, 101.0f);
    const TerrainSample throughParallelFaces =
        wallHit(0.0f, 0.0f, 102.0f, 102.0f);
    CHECK(wallChordFollowsSurface(a, alongWall));
    CHECK_FALSE(wallChordFollowsSurface(a, throughParallelFaces));
    CHECK_FALSE(wallLinkable(a, throughParallelFaces, WallEdgeAxis::Across));
}

TEST_CASE("wall join diagnosis names the same gate wallLinkable applies") {
    const TerrainSample a = wallHit(0.0f, 0.0f, 100.0f, 100.0f);
    const TerrainSample same = wallHit(2.0f, 0.0f, 100.0f, 101.0f);
    const TerrainSample separated = wallHit(20.0f, 0.0f, 100.0f, 101.0f);
    TerrainSample otherDepth = same;
    otherDepth.distance = 104.0f;
    TerrainSample changedNormal = same;
    changedNormal.normalX = std::sqrt(1.0f - changedNormal.normalY *
                                               changedNormal.normalY);
    changedNormal.normalZ = 0.0f;
    const TerrainSample throughFace = wallHit(0.0f, 0.0f, 102.0f, 102.0f);

    CHECK(wallJoinFailure(TerrainSample{}, same, WallEdgeAxis::Across) ==
          WallJoinFailure::MissingSample);
    CHECK(wallJoinFailure(a, separated, WallEdgeAxis::Across) ==
          WallJoinFailure::Separation);
    CHECK(wallJoinFailure(a, otherDepth, WallEdgeAxis::Across) ==
          WallJoinFailure::Depth);
    CHECK(wallJoinFailure(a, changedNormal, WallEdgeAxis::Across) ==
          WallJoinFailure::Normal);
    CHECK(wallJoinFailure(a, throughFace, WallEdgeAxis::Across) ==
          WallJoinFailure::Chord);
    CHECK(wallJoinFailure(a, same, WallEdgeAxis::Across) ==
          WallJoinFailure::None);
    CHECK(wallLinkable(a, same, WallEdgeAxis::Across));
}
TEST_CASE("wall edges become cliff segments on existing wave rings") {
    ScanSegment out{};
    REQUIRE(reduceWallEdge(wallHit(0, 2, 40, 40), wallHit(1, 2, 40, 40), WallEdgeAxis::Across, &out) == 1);
    CHECK(out.surface == SegmentClass::Cliff);
    CHECK(out.isArc);
    CHECK(out.revealWave >= 1.0f);
    CHECK(out.revealWave <= static_cast<float>(kRings));
    CHECK(std::floor(out.revealWave) == out.revealWave);

    REQUIRE(reduceWallEdge(wallHit(0, 2, 40, 40), wallHit(0, 3, 40, 40), WallEdgeAxis::Vertical, &out) == 1);
    CHECK_FALSE(out.isArc);
}

TEST_CASE("wall reveal follows spherical travel instead of flashing one column") {
    const TerrainSample near = wallHit(0.0f, 0.0f, 40.0f, 40.0f);
    const TerrainSample high = wallHit(0.0f, 60.0f, 40.0f, 72.0f);
    CHECK(wallRevealWave(near, high) > wallRevealWave(near, near));
}

TEST_CASE("layer matching follows depth rather than ordinal storage") {
    const TerrainSample a[] = {
        wallHit(0.0f, 0.0f, 20.0f, 20.0f),
        wallHit(0.0f, 0.0f, 100.0f, 100.0f),
    };
    const TerrainSample b[] = {
        wallHit(1.0f, 0.0f, 100.0f, 99.0f),
        wallHit(1.0f, 0.0f, 20.0f, 21.0f),
    };
    ScanSegment out[kWallDepthLayers]{};
    REQUIRE(reduceWallLayers(a, 2, b, 2, WallEdgeAxis::Across,
                             out, kWallDepthLayers) == 2);
    CHECK((out[0].az < 50.0f) == (out[0].bz < 50.0f));
    CHECK((out[1].az < 50.0f) == (out[1].bz < 50.0f));
}

TEST_CASE("layer matching preserves front-to-back order instead of braiding") {
    const TerrainSample a[] = {
        wallHit(0.0f, 0.0f, 100.0f, 300.0f),
        wallHit(0.0f, 1.0f, 100.0f, 304.0f),
    };
    const TerrainSample b[] = {
        wallHit(1.0f, 0.0f, 100.0f, 303.0f),
        wallHit(1.0f, 1.0f, 100.0f, 307.0f),
    };
    ScanSegment out[kWallDepthLayers]{};
    REQUIRE(reduceWallLayers(a, 2, b, 2, WallEdgeAxis::Across,
                             out, kWallDepthLayers) == 2);
    CHECK(out[0].ay == doctest::Approx(out[0].by));
    CHECK(out[1].ay == doctest::Approx(out[1].by));
}

TEST_CASE("layer matching refuses to bridge foreground to background") {
    const TerrainSample foreground[] = {wallHit(0, 0, 20, 20)};
    const TerrainSample background[] = {wallHit(1, 0, 100, 100)};
    ScanSegment out[kWallDepthLayers]{};
    CHECK(reduceWallLayers(foreground, 1, background, 1,
                           WallEdgeAxis::Across, out, kWallDepthLayers) == 0);
}

namespace {

struct WallCell {
    TerrainSample layers[kWallDepthLayers]{};
    std::uint32_t count = 0;
};

std::vector<TerrainSample> surfaceRun(const std::vector<float>& depths) {
    const std::size_t count = depths.size();
    std::vector<float> across(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i)
        across[i] = static_cast<float>(i) * depths[i] * wallAzimuthStep();

    std::vector<TerrainSample> run{};
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t before = i == 0 ? 0 : i - 1;
        const std::size_t after = i + 1 < count ? i + 1 : i;
        float tangentX = across[after] - across[before];
        float tangentZ = depths[after] - depths[before];
        const float length =
            std::sqrt(tangentX * tangentX + tangentZ * tangentZ);
        tangentX /= length;
        tangentZ /= length;

        TerrainSample sample{};
        sample.hit = true;
        sample.x = across[i];
        sample.y = 0.0f;
        sample.z = depths[i];
        sample.distance = depths[i];

        sample.normalX = tangentZ;
        sample.normalY = 0.0f;
        sample.normalZ = -tangentX;
        run.push_back(sample);
    }
    return run;
}

std::vector<WallCell> cellsOf(const std::vector<TerrainSample>& run) {
    std::vector<WallCell> cells{};
    for (const auto& sample : run) {
        WallCell cell{};
        cell.layers[0] = sample;
        cell.count = 1;
        cells.push_back(cell);
    }
    return cells;
}

void addBehind(std::vector<WallCell>& cells,
               const std::vector<TerrainSample>& back, std::size_t from,
               std::size_t stride) {
    for (std::size_t i = from; i < cells.size(); i += stride) {
        cells[i].layers[cells[i].count] = back[i];
        ++cells[i].count;
    }
}

std::vector<bool> walkRun(const std::vector<WallCell>& run, WallEdgeAxis axis) {
    std::vector<bool> drawn{};
    WallTrackSet tracks{};
    ScanSegment produced[kWallDepthLayers]{};
    for (std::size_t i = 0; i < run.size(); ++i) {
        const std::uint32_t count =
            advanceWallTracks(tracks, run[i].layers, run[i].count, axis,
                              produced, kWallDepthLayers);
        if (i > 0) drawn.push_back(count > 0);
    }
    return drawn;
}

std::vector<float> driftingProfile(std::size_t cells, float base, float step,
                                   std::size_t onset) {
    std::vector<float> depths{};
    for (std::size_t i = 0; i < cells; ++i)
        depths.push_back(i < onset
                             ? base
                             : base + step * static_cast<float>(i - onset + 1));
    return depths;
}

}

TEST_CASE("a track follows a straight wall and a curved one without holes") {

    const std::vector<float> flat(24, 100.0f);
    for (const bool joined :
         walkRun(cellsOf(surfaceRun(flat)), WallEdgeAxis::Across))
        CHECK(joined);

    const float spread = 100.0f * wallAzimuthStep();
    std::vector<float> curved{};
    for (std::uint32_t step = 0; step < 24; ++step) {
        const float lateral = static_cast<float>(step) * spread;
        curved.push_back(100.0f + lateral * lateral / (2.0f * 60.0f));
    }
    std::uint32_t curvedHoles = 0;
    for (const bool joined : walkRun(cellsOf(surfaceRun(curved)),
                                     WallEdgeAxis::Across))
        if (!joined) ++curvedHoles;
    CHECK(curvedHoles == 0);
}

TEST_CASE("a track refuses to walk off the face it is following") {

    const std::size_t onset = 6;
    const auto run = surfaceRun(driftingProfile(24, 100.0f, 2.2f, onset));

    for (std::size_t i = 1; i < run.size(); ++i)
        CHECK(wallLinkable(run[i - 1u], run[i], WallEdgeAxis::Across));

    const auto joins = walkRun(cellsOf(run), WallEdgeAxis::Across);
    REQUIRE(joins.size() == run.size() - 1u);
    for (std::size_t i = 0; i + 1 < onset; ++i) CHECK(joins[i]);
    CHECK_FALSE(joins[onset - 1u]);

    const auto gentle =
        cellsOf(surfaceRun(driftingProfile(24, 100.0f, 0.9f, onset)));
    for (const bool joined : walkRun(gentle, WallEdgeAxis::Across))
        CHECK(joined);
}

TEST_CASE("the track observer reports the actual bend cut") {
    const auto cells = cellsOf(surfaceRun(
        driftingProfile(24, 100.0f, 2.2f, 6)));
    struct Observation {
        std::uint32_t bends = 0;
        std::uint32_t assignments = 0;
    } observation{};
    const WallTrackObserver observer{
        &observation,
        [](void* context, const TerrainSample&, const TerrainSample&,
           WallTrackRejection reason) {
            auto& value = *static_cast<Observation*>(context);
            if (reason == WallTrackRejection::Bend)
                ++value.bends;
            else if (reason == WallTrackRejection::Assignment)
                ++value.assignments;
        }};
    WallTrackSet tracks{};
    for (const auto& cell : cells)
        advanceWallTracks(tracks, cell.layers, cell.count,
                          WallEdgeAxis::Across, nullptr, 0, observer);
    CHECK(observation.bends >= 1);
}

TEST_CASE("the track observer reports a tail assignment after samples run out") {
    WallTrackSet tracks{};
    tracks.count = 2;
    tracks.tracks[0].head = wallHit(0.0f, 0.0f, 100.0f, 100.0f);
    tracks.tracks[0].active = true;
    tracks.tracks[1].head = wallHit(0.0f, 0.0f, 101.0f, 101.0f);
    tracks.tracks[1].active = true;
    const TerrainSample next = wallHit(1.0f, 0.0f, 100.5f, 100.5f);
    std::uint32_t assignments = 0;
    const WallTrackObserver observer{
        &assignments,
        [](void* context, const TerrainSample&, const TerrainSample&,
           WallTrackRejection reason) {
            if (reason == WallTrackRejection::Assignment)
                ++*static_cast<std::uint32_t*>(context);
        }};

    advanceWallTracks(tracks, &next, 1, WallEdgeAxis::Across,
                      nullptr, 0, observer);

    CHECK(assignments == 1);
}

TEST_CASE("a track keeps its own layer when a neighbour offers a closer one") {

    const std::vector<float> deep(8, 100.0f);
    const std::vector<float> ledge(8, 98.6f);
    auto cells = cellsOf(surfaceRun(ledge));
    addBehind(cells, surfaceRun(deep), 4, 1);

    WallTrackSet tracks{};
    ScanSegment produced[kWallDepthLayers]{};
    for (const auto& cell : cells)
        advanceWallTracks(tracks, cell.layers, cell.count,
                          WallEdgeAxis::Across, produced, kWallDepthLayers);
    REQUIRE(tracks.count == 2);

    CHECK(tracks.tracks[0].head.distance < tracks.tracks[1].head.distance);
    CHECK(tracks.tracks[1].head.distance == doctest::Approx(100.0f));
}
TEST_CASE("advancing tracks never invents a join the pairwise rule rejects") {

    std::vector<float> receding{};
    for (std::uint32_t step = 0; step < 20; ++step)
        receding.push_back(40.0f + static_cast<float>(step));
    std::vector<float> behind{};
    for (const float depth : receding) behind.push_back(depth + 9.0f);
    auto cells = cellsOf(surfaceRun(receding));
    addBehind(cells, surfaceRun(behind), 0, 3);

    WallTrackSet tracks{};
    ScanSegment produced[kWallDepthLayers]{};
    WallTrackSet previous{};
    for (const auto& cell : cells) {
        previous = tracks;
        const std::uint32_t count =
            advanceWallTracks(tracks, cell.layers, cell.count,
                              WallEdgeAxis::Across, produced, kWallDepthLayers);

        for (std::uint32_t i = 0; i < count; ++i) {
            bool justified = false;
            for (std::uint32_t t = 0; t < previous.count && !justified; ++t)
                for (std::uint32_t layer = 0; layer < cell.count; ++layer)
                    if (wallLinkable(previous.tracks[t].head, cell.layers[layer], WallEdgeAxis::Across)) {
                        justified = true;
                        break;
                    }
            CHECK(justified);
        }
        CHECK(tracks.count <= kWallDepthLayers);
    }
}

TEST_CASE("the buried judge surfaces a deep sample only when a nearby ray sees the same surface") {

    std::vector<TerrainSample> samples(kWallSamples);
    std::vector<std::uint8_t> counts(kWallProbes, 0);

    const std::uint32_t row = 10;
    const std::uint32_t probeA = row * kWallColumns + 10;
    const std::uint32_t probeB = row * kWallColumns + 11;
    const std::uint32_t probeC = row * kWallColumns + 40;

    samples[wallSampleIndex(probeA, 0)] = wallHit(0.0f, 0.0f, 20.0f, 20.0f);
    samples[wallSampleIndex(probeA, 1)] = wallHit(0.0f, 0.0f, 25.0f, 25.0f);
    counts[probeA] = 2;

    samples[wallSampleIndex(probeB, 0)] = wallHit(0.5f, 0.0f, 25.0f, 25.0f);
    counts[probeB] = 1;

    samples[wallSampleIndex(probeC, 0)] = wallHit(30.0f, 0.0f, 20.0f, 20.0f);
    samples[wallSampleIndex(probeC, 1)] = wallHit(30.0f, 0.0f, 25.0f, 25.0f);
    counts[probeC] = 2;

    std::uint32_t surfaced[kWallDepthLayers]{};
    std::uint32_t buried[kWallDepthLayers]{};
    judgeBuriedSamples(samples.data(), counts.data(), surfaced, buried);

    CHECK(surfaced[0] == 3);
    CHECK(buried[0] == 0);
    CHECK_FALSE(samples[wallSampleIndex(probeA, 0)].buried);

    CHECK_FALSE(samples[wallSampleIndex(probeA, 1)].buried);
    CHECK(samples[wallSampleIndex(probeC, 1)].buried);
    CHECK(surfaced[1] == 1);
    CHECK(buried[1] == 1);
}

TEST_CASE("a parallel face in front does not surface a deep sample") {

    std::vector<TerrainSample> samples(kWallSamples);
    std::vector<std::uint8_t> counts(kWallProbes, 0);

    const std::uint32_t row = 10;
    const std::uint32_t probeA = row * kWallColumns + 10;
    const std::uint32_t probeB = row * kWallColumns + 11;

    samples[wallSampleIndex(probeA, 0)] = wallHit(0.0f, 0.0f, 23.0f, 23.0f);
    samples[wallSampleIndex(probeA, 1)] = wallHit(0.0f, 0.0f, 25.0f, 25.0f);
    counts[probeA] = 2;

    samples[wallSampleIndex(probeB, 0)] = wallHit(0.5f, 0.0f, 23.0f, 23.0f);
    counts[probeB] = 1;

    std::uint32_t surfaced[kWallDepthLayers]{};
    std::uint32_t buried[kWallDepthLayers]{};
    judgeBuriedSamples(samples.data(), counts.data(), surfaced, buried);
    CHECK(samples[wallSampleIndex(probeA, 1)].buried);
    CHECK(buried[1] == 1);
    CHECK(surfaced[1] == 0);
}

TEST_CASE("a two-cell heal chord needs the two-cell allowance it claims") {

    TerrainSample a = wallHit(0.0f, 5.0f, 120.0f, 120.0f);
    const float allowance1 = wallJoinAllowance(a, a,
                                               WallEdgeAxis::Across, 1.0f);
    const float allowance2 = wallJoinAllowance(a, a,
                                               WallEdgeAxis::Across, 2.0f);
    REQUIRE(allowance2 > allowance1);
    const float separation = 0.5f * (allowance1 + allowance2);
    const TerrainSample b = wallHit(separation, 5.0f, 120.0f, 120.0f);
    CHECK_FALSE(wallLinkable(a, b, WallEdgeAxis::Across, 1.0f));
    CHECK(wallLinkable(a, b, WallEdgeAxis::Across, 2.0f));
    ScanSegment healed{};
    CHECK(reduceWallEdge(a, b, WallEdgeAxis::Across, &healed, 2.0f) == 1);
    CHECK(healed.isArc);
}

TEST_CASE("shallow rows join real gentle surfaces and carry their own origin") {

    TerrainSample a{true, 0.0f, 5.0f, 12.0f, 0.9f, 12.0f};
    TerrainSample b{true, 1.0f, 5.0f, 12.0f, 0.9f, 12.0f};
    ScanSegment out{};
    REQUIRE(reduceShallowAcross(a, b, &out) == 1);
    CHECK(out.origin == SegmentOrigin::ShallowGround);
    CHECK(out.isArc);
    CHECK(out.revealWave >= 1.0f);

    TerrainSample dropped = b;
    dropped.y = 0.0f;
    CHECK(reduceShallowAcross(a, dropped, &out) == 0);

    TerrainSample far = b;
    far.x = 200.0f;
    CHECK(reduceShallowAcross(a, far, &out) == 0);
    CHECK(reduceShallowAcross(TerrainSample{}, b, &out) == 0);
}

TEST_CASE("wallRayFor is exactly wallRayForAngles at the base angles") {

    for (const std::uint32_t index : {0u, 100u, 2048u, 4095u}) {
        const WallRay a = wallRayFor(index, 12.0f, -30.0f, 250.0f,
                                     0.6f, 0.8f);
        const WallRay b = wallRayForAngles(
            wallAzimuth(wallProbeColumn(index)),
            wallElevation(wallProbeRow(index)), 12.0f, -30.0f,
            250.0f, 0.6f, 0.8f);
        CHECK(a.fromX == b.fromX);
        CHECK(a.fromY == b.fromY);
        CHECK(a.fromZ == b.fromZ);
        CHECK(a.toX == b.toX);
        CHECK(a.toY == b.toY);
        CHECK(a.toZ == b.toZ);
    }
}
