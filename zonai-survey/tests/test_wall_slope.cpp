// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cmath>

#include "WallReduction.hpp"

using namespace zonai_survey::pure;

namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;

TerrainSample surfaceAt(float distance, float normalY) {
    TerrainSample sample{};
    sample.hit = true;
    sample.distance = distance;
    sample.x = distance;
    sample.normalX = -1.0f;
    sample.normalY = normalY;
    return sample;
}

}

TEST_CASE("the wall rule keeps rock faces and refuses shallow ground") {

    CHECK(kWallSteepLimit == doctest::Approx(0.85f));

    const int kept[] = {0, 20, 46, 58};
    for (const int tilt : kept) {
        CAPTURE(tilt);
        CHECK(wallNormalIsSteep(std::sin(static_cast<float>(tilt) * kDegrees)));
    }

    const int refused[] = {68, 75, 82, 86, 90};
    for (const int tilt : refused) {
        CAPTURE(tilt);
        CHECK_FALSE(
            wallNormalIsSteep(std::sin(static_cast<float>(tilt) * kDegrees)));
    }

    CHECK(wallNormalIsSteep(-std::sin(50.0f * kDegrees)));
}

TEST_CASE("the rule holds in the reducer too, not only in the fan") {

    const float normalY = std::sin(50.0f * kDegrees);
    TerrainSample a = surfaceAt(30.0f, normalY);
    TerrainSample b = surfaceAt(30.0f, normalY);
    b.z = 30.0f * wallAzimuthStep();
    a.viewCosine = 0.5f;
    b.viewCosine = 0.5f;

    CHECK(acceptedWallSample(a));
    CHECK(wallLinkable(a, b, WallEdgeAxis::Across));
}

TEST_CASE("a flat floor is refused by acceptance and by joining alike") {

    TerrainSample a = surfaceAt(30.0f, std::sin(88.0f * kDegrees));
    TerrainSample b = a;
    b.z = 30.0f * wallAzimuthStep();
    a.viewCosine = 0.5f;
    b.viewCosine = 0.5f;

    CHECK_FALSE(acceptedWallSample(a));
    CHECK_FALSE(wallLinkable(a, b, WallEdgeAxis::Across));
    CHECK(wallJoinFailure(a, b, WallEdgeAxis::Across) ==
          WallJoinFailure::MissingSample);
}
