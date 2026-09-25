// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cmath>

#include "SurveySweep.hpp"
#include "totk/render/PrimitiveGeometry.hpp"

using namespace zonai_survey::pure;
using namespace totk::render;

namespace {

constexpr float kEyeX = 2776.0f;
constexpr float kEyeY = 161.2f;
constexpr float kEyeZ = 1774.0f;

float lengthOf(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

}

TEST_CASE("clearance slides a point along its own view ray and nowhere else") {
    const DepthLift clearance =
        depthLiftFor(kSurveyDepthLiftFraction, kEyeX, kEyeY, kEyeZ);
    CHECK(clearance.active);

    const Vec3 points[] = {
        Vec3{kEyeX + 40.0f, kEyeY + 52.0f, kEyeZ - 6.0f},
        Vec3{kEyeX - 3.0f, kEyeY - 1.6f, kEyeZ + 2.0f},
        Vec3{kEyeX + 210.0f, kEyeY + 90.0f, kEyeZ + 300.0f}
    };
    for (const Vec3& point : points) {
        const Vec3 lifted = applyDepthLift(clearance, point);
        const Vec3 before{point.x - kEyeX, point.y - kEyeY, point.z - kEyeZ};
        const Vec3 after{lifted.x - kEyeX, lifted.y - kEyeY, lifted.z - kEyeZ};
        const float range = lengthOf(before);

        const float dot =
            before.x * after.x + before.y * after.y + before.z * after.z;
        CHECK(dot / (lengthOf(before) * lengthOf(after)) ==
              doctest::Approx(1.0f).epsilon(1e-5));

        CHECK(lengthOf(after) ==
              doctest::Approx(range * (1.0f - kSurveyDepthLiftFraction))
                  .epsilon(1e-4));
    }
}

TEST_CASE("the clearance clears the depth a join sinks into a bulging rock") {

    const float second = 1.0f;
    const float range = 66.0f;
    auto sag = [second](float span) { return span * span * second / 8.0f; };

    CHECK(sag(1.0f) == doctest::Approx(0.125f));
    CHECK(sag(2.0f) == doctest::Approx(0.5f));
    CHECK(sag(3.0f) == doctest::Approx(1.125f));

    CHECK(range * kSurveyDepthLiftFraction > sag(1.0f));
    CHECK(range * kSurveyDepthLiftFraction > sag(2.0f));
    CHECK(range * kSurveyDepthLiftFraction < sag(3.0f));
}

TEST_CASE("clearance refuses rather than moving the picture somewhere wrong") {
    const Vec3 point{kEyeX + 40.0f, kEyeY + 52.0f, kEyeZ - 6.0f};

    const DepthLift off = depthLiftFor(0.0f, kEyeX, kEyeY, kEyeZ);
    CHECK_FALSE(off.active);
    CHECK(applyDepthLift(off, point).x == point.x);
    CHECK(applyDepthLift(off, point).y == point.y);
    CHECK(applyDepthLift(off, point).z == point.z);

    CHECK_FALSE(depthLiftFor(1.0f, kEyeX, kEyeY, kEyeZ).active);
    CHECK_FALSE(depthLiftFor(2.0f, kEyeX, kEyeY, kEyeZ).active);
    CHECK_FALSE(depthLiftFor(-0.01f, kEyeX, kEyeY, kEyeZ).active);
    const float nan = std::nanf("");
    CHECK_FALSE(depthLiftFor(0.01f, nan, kEyeY, kEyeZ).active);
    CHECK_FALSE(depthLiftFor(nan, kEyeX, kEyeY, kEyeZ).active);
    const DepthLift untouched{};
    CHECK(applyDepthLift(untouched, point).y == point.y);
}
TEST_CASE("clearance and wave height compose without fighting each other") {

    const DepthLift clearance =
        depthLiftFor(kSurveyDepthLiftFraction, kEyeX, kEyeY, kEyeZ);
    const Vec3 measured{kEyeX + 40.0f, kEyeY + 52.0f, kEyeZ - 6.0f};
    const float wave = sweepLift(1.0f, 0u);
    CHECK(wave > 0.0f);

    const Vec3 raised{measured.x, measured.y + wave, measured.z};
    const Vec3 drawn = applyDepthLift(clearance, raised);
    const Vec3 ray{raised.x - kEyeX, raised.y - kEyeY, raised.z - kEyeZ};
    const Vec3 out{drawn.x - kEyeX, drawn.y - kEyeY, drawn.z - kEyeZ};
    const float dot = ray.x * out.x + ray.y * out.y + ray.z * out.z;
    CHECK(dot / (lengthOf(ray) * lengthOf(out)) ==
          doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(lengthOf(out) < lengthOf(ray));
}
