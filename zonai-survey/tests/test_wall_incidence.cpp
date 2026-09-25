// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cmath>
#include <limits>

#include "WallReduction.hpp"

using namespace zonai_survey::pure;

namespace {

TerrainSample planarHit(float depth, float phi, float theta) {
    const float directionX = std::cos(theta);
    const float directionZ = std::sin(theta);
    const float normalX = -std::cos(phi);
    const float normalZ = std::sin(phi);

    TerrainSample sample{};
    sample.hit = true;
    sample.distance = depth * std::cos(phi) / std::cos(phi + theta);
    sample.x = sample.distance * directionX;
    sample.y = 0.0f;
    sample.z = sample.distance * directionZ;
    sample.normalX = normalX;
    sample.normalY = 0.0f;
    sample.normalZ = normalZ;
    sample.viewCosine =
        rayViewCosine(normalX, 0.0f, normalZ, directionX, 0.0f, directionZ);
    return sample;
}

float degrees(float value) { return value * kPi / 180.0f; }

TerrainSample squareOnReading(TerrainSample sample) {
    sample.viewCosine = 1.0f;
    return sample;
}

}

TEST_CASE("the lean measurement is the angle, not the old facing flag") {

    CHECK(rayViewCosine(-1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) ==
          doctest::Approx(1.0f));

    CHECK(rayViewCosine(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) ==
          doctest::Approx(1.0f));

    CHECK(rayViewCosine(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f) ==
          doctest::Approx(0.0f));

    const float root = 1.0f / std::sqrt(2.0f);
    CHECK(rayViewCosine(-root, 0.0f, root, 1.0f, 0.0f, 0.0f) ==
          doctest::Approx(root));
    CHECK(rayViewCosine(-7.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) ==
          doctest::Approx(1.0f));

    CHECK(rayViewCosine(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) ==
          doctest::Approx(1.0f));
    CHECK(rayViewCosine(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f,
                        1.0f, 0.0f, 0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("a square-on reading reproduces the plain distance-by-angle allowances") {
    const float step = wallAzimuthStep();

    const float ranges[] = {80.0f, 300.0f};
    for (float range : ranges) {
        const auto a = squareOnReading(planarHit(range, 0.0f, 0.0f));
        const auto b = squareOnReading(planarHit(range, 0.0f, step));
        const float cell = 0.5f * (a.distance + b.distance) * step;

        const float join = cell * kWallJoinScale;
        CHECK(wallJoinAllowance(a, b, WallEdgeAxis::Across) ==
              doctest::Approx(join > kWallMinJoinMeters ? join
                                                        : kWallMinJoinMeters));

        const float depth = cell * kCleanWallDepthScale;
        CHECK(wallDepthAllowance(a, b, WallEdgeAxis::Across) ==
              doctest::Approx(depth > kCleanWallMinDepthAllowance
                                  ? depth
                                  : kCleanWallMinDepthAllowance));
    }

    TerrainSample fresh{};
    CHECK(fresh.viewCosine == doctest::Approx(1.0f));
    CHECK(wallViewCosine(fresh, fresh) == doctest::Approx(1.0f));
    CHECK(wallViewTangent(fresh, fresh) == doctest::Approx(0.0f));
}

TEST_CASE("the old ceiling tightened with range; the fix removes it") {
    const float step = wallAzimuthStep();
    const float lean = degrees(60.0f);
    const float ranges[] = {44.0f, 150.0f, 300.0f};

    for (float range : ranges) {
        const auto a = planarHit(range, lean, 0.0f);
        const auto b = planarHit(range, lean, step);

        CHECK(wallJoinFailure(a, b, WallEdgeAxis::Across) ==
              WallJoinFailure::None);
    }

    const auto nearA = squareOnReading(planarHit(44.0f, lean, 0.0f));
    const auto nearB = squareOnReading(planarHit(44.0f, lean, step));
    CHECK(wallJoinFailure(nearA, nearB, WallEdgeAxis::Across) ==
          WallJoinFailure::None);

    const float farRanges[] = {150.0f, 300.0f};
    for (float range : farRanges) {
        const auto a = squareOnReading(planarHit(range, lean, 0.0f));
        const auto b = squareOnReading(planarHit(range, lean, step));
        CHECK(wallJoinFailure(a, b, WallEdgeAxis::Across) !=
              WallJoinFailure::None);
    }
}

TEST_CASE("the same holds stepping down a column, not just across a row") {
    const float step = wallElevationStep();
    const float lean = degrees(60.0f);

    const auto a = planarHit(150.0f, lean, 0.0f);
    const auto b = planarHit(150.0f, lean, step);
    CHECK(wallJoinFailure(a, b, WallEdgeAxis::Vertical) ==
          WallJoinFailure::None);
    CHECK(wallJoinFailure(squareOnReading(a), squareOnReading(b),
                          WallEdgeAxis::Vertical) != WallJoinFailure::None);
}

TEST_CASE("a lean cannot buy a join across a silhouette") {
    const float step = wallAzimuthStep();
    const float lean = degrees(60.0f);

    const auto a = planarHit(150.0f, lean, 0.0f);
    const auto onSurface = planarHit(150.0f, lean, step);

    const float behindMeters[] = {6.0f, 20.0f, 60.0f};
    for (float behind : behindMeters) {
        TerrainSample background = onSurface;
        background.distance += behind;
        const float scale = background.distance / onSurface.distance;
        background.x = onSurface.x * scale;
        background.z = onSurface.z * scale;
        CHECK(wallJoinFailure(a, background, WallEdgeAxis::Across) !=
              WallJoinFailure::None);
    }
}

TEST_CASE("a near edge-on reading is clamped, not trusted") {
    TerrainSample a = planarHit(150.0f, degrees(60.0f), 0.0f);
    TerrainSample b = planarHit(150.0f, degrees(60.0f), 0.02f);

    a.viewCosine = 0.001f;
    b.viewCosine = 0.001f;
    CHECK(wallViewCosine(a, b) == doctest::Approx(kWallMinViewCosine));

    a.viewCosine = kWallMinViewCosine;
    b.viewCosine = kWallMinViewCosine;
    const float clamped =
        wallDepthAllowance(a, b, WallEdgeAxis::Across);
    a.viewCosine = 0.0f;
    b.viewCosine = 0.0f;
    CHECK(wallDepthAllowance(a, b, WallEdgeAxis::Across) ==
          doctest::Approx(clamped));

    a.viewCosine = 1.0f;
    b.viewCosine = 0.5f;
    CHECK(wallViewCosine(a, b) == doctest::Approx(0.5f));
    CHECK(wallViewTangent(a, b) == doctest::Approx(std::sqrt(3.0f)));
}

TEST_CASE("the allowances only ever widen, so no join can be lost") {
    const float step = wallAzimuthStep();

    const float ranges[] = {30.0f, 90.0f, 250.0f};
    for (float range : ranges) {
        for (int index = 0; index <= 89; ++index) {
            const auto a = planarHit(range, degrees(index * 1.0f), 0.0f);
            const auto b = planarHit(range, degrees(index * 1.0f), step);
            CHECK(wallJoinAllowance(a, b, WallEdgeAxis::Across) >=
                  wallJoinAllowance(squareOnReading(a), squareOnReading(b), WallEdgeAxis::Across));
            CHECK(wallDepthAllowance(a, b, WallEdgeAxis::Across) >=
                  wallDepthAllowance(squareOnReading(a), squareOnReading(b), WallEdgeAxis::Across));
        }
    }
}
