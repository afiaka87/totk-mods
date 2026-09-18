// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>

#include <cmath>
#include <initializer_list>

#include "ChainProjection.hpp"

using namespace zonai_hookshot::pure;

namespace {

constexpr float kWidth = 1280.0f;
constexpr float kHeight = 720.0f;
constexpr float kNear = 0.2f;
constexpr float kFar = 8000.0f;

CameraFrame makeCamera() {
    CameraFrame camera{};
    camera.width = kWidth;
    camera.height = kHeight;
    camera.nearMetres = kNear;
    camera.farMetres = kFar;

    camera.view[0] = 1.0f;
    camera.view[5] = 1.0f;
    camera.view[10] = 1.0f;

    const float aspect = kWidth / kHeight;
    const float focal = 1.0f / std::tan(0.5f * 1.0472f);  // 60 degrees vertical
    camera.proj[0] = focal / aspect;
    camera.proj[5] = focal;
    camera.proj[10] = (kFar + kNear) / (kNear - kFar);
    camera.proj[11] = 2.0f * kFar * kNear / (kNear - kFar);
    camera.proj[14] = -1.0f;
    return camera;
}

CameraFrame makeOffsetCamera() {
    CameraFrame camera = makeCamera();
    const float angle = 0.6f;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float eye[3]{3.0f, 2.0f, 5.0f};
    const float rows[9]{c, 0.0f, s, 0.0f, 1.0f, 0.0f, -s, 0.0f, c};
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) camera.view[r * 4 + k] = rows[r * 3 + k];
        camera.view[r * 4 + 3] = -(rows[r * 3 + 0] * eye[0] +
                                   rows[r * 3 + 1] * eye[1] +
                                   rows[r * 3 + 2] * eye[2]);
    }
    return camera;
}

ChainStyleResolved makeStyle() {
    ChainStyleResolved style{};
    style.spineRgb[0] = 0.8f;
    style.spineRgb[1] = 0.9f;
    style.spineRgb[2] = 1.0f;
    style.coreARgb[1] = 1.0f;
    style.coreBRgb[2] = 1.0f;
    style.reticleRgb[0] = 1.0f;
    style.reticleUnderRgb[0] = 0.1f;
    style.phaseRadians = 0.9f;
    return style;
}

float screenParameterOf(const CameraFrame& camera, const ChainUniforms& u,
                        Vec3 world) {
    const ProjectedPoint p =
        projectViewPoint(camera, viewTransformPoint(camera.view, world));
    REQUIRE(p.valid);
    const float axisX = u.farPoint[0] - u.nearPoint[0];
    const float axisY = u.farPoint[1] - u.nearPoint[1];
    const float lengthSquared = axisX * axisX + axisY * axisY;
    REQUIRE(lengthSquared > 0.0f);
    return ((p.px - u.nearPoint[0]) * axisX + (p.py - u.nearPoint[1]) * axisY) /
           lengthSquared;
}

Vec3 shaderStrandPoint(const ChainUniforms& u, const HelixFrame& frame,
                       Vec3 from, Vec3 to, float t) {
    const float world = worldParameterAt(t, u.nearPoint[2], u.farPoint[2]);
    const float angle = helixAngleAt(world, u.farPoint[3], u.coil[1], u.coil[2]);
    const Vec3 axisPoint = lerp(from, to, world);
    return add(axisPoint, add(mul(frame.radialA, u.coil[0] * std::cos(angle)),
                              mul(frame.radialB, u.coil[0] * std::sin(angle))));
}

}  // namespace

TEST_CASE("projection puts the view axis at the screen centre and y downward") {
    const CameraFrame camera = makeCamera();
    const ProjectedPoint centre = projectViewPoint(camera, {0.0f, 0.0f, -20.0f});
    REQUIRE(centre.valid);
    CHECK(centre.px == doctest::Approx(kWidth * 0.5f));
    CHECK(centre.py == doctest::Approx(kHeight * 0.5f));
    CHECK(centre.viewZ == doctest::Approx(20.0f));

    const ProjectedPoint right = projectViewPoint(camera, {2.0f, 0.0f, -20.0f});
    const ProjectedPoint above = projectViewPoint(camera, {0.0f, 2.0f, -20.0f});
    REQUIRE(right.valid);
    REQUIRE(above.valid);
    CHECK(right.px > centre.px);
    CHECK(above.py < centre.py);

    CHECK_FALSE(projectViewPoint(camera, {0.0f, 0.0f, 5.0f}).valid);
    CHECK_FALSE(projectViewPoint(camera, {0.0f, 0.0f, 0.0f}).valid);
}

TEST_CASE("near-plane clipping keeps the part of the chain in front") {
    const float nearMetres = 0.2f;
    SUBCASE("wholly in front") {
        const SegmentClip clip = clipSegmentToNear({0.0f, 0.0f, -2.0f},
                                                   {0.0f, 0.0f, -30.0f},
                                                   nearMetres);
        REQUIRE(clip.valid);
        CHECK(clip.from == doctest::Approx(0.0f));
        CHECK(clip.to == doctest::Approx(1.0f));
    }
    SUBCASE("near endpoint behind the camera") {
        const SegmentClip clip = clipSegmentToNear({0.0f, 0.0f, 3.0f},
                                                   {0.0f, 0.0f, -30.0f},
                                                   nearMetres);
        REQUIRE(clip.valid);
        CHECK(clip.from > 0.0f);
        CHECK(clip.from < 0.2f);
        CHECK(clip.to == doctest::Approx(1.0f));
    }
    SUBCASE("wholly behind the camera") {
        CHECK_FALSE(clipSegmentToNear({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 1.0f},
                                      nearMetres).valid);
    }
}

TEST_CASE("the screen-to-world parameter is perspective correct") {
    const CameraFrame camera = makeOffsetCamera();
    ChainGeometry geometry{};
    geometry.nearPoint = {2.0f, 1.4f, -1.0f};
    geometry.farPoint = {26.0f, 9.0f, -44.0f};
    geometry.bodyVisible = true;
    ChainUniforms uniforms{};
    REQUIRE(buildChainUniforms(camera, geometry, makeStyle(), uniforms));
    REQUIRE(uniforms.flags[1] == doctest::Approx(1.0f));

    for (const float world : {0.0f, 0.15f, 0.4f, 0.75f, 1.0f}) {
        const Vec3 point = lerp(geometry.nearPoint, geometry.farPoint, world);
        const float t = screenParameterOf(camera, uniforms, point);
        CHECK(worldParameterAt(t, uniforms.nearPoint[2], uniforms.farPoint[2]) ==
              doctest::Approx(world).epsilon(0.002));
        const ProjectedPoint projected =
            projectViewPoint(camera, viewTransformPoint(camera.view, point));
        CHECK(viewDepthAt(t, uniforms.nearPoint[2], uniforms.farPoint[2]) ==
              doctest::Approx(projected.viewZ).epsilon(0.002));
    }
}

TEST_CASE("the world parameter's rate of change matches the closed form") {
    for (const float nearZ : {0.8f, 3.0f, 11.0f}) {
        for (const float farZ : {4.0f, 25.0f, 90.0f}) {
            if (farZ <= nearZ) {
                continue;
            }
            for (const float t : {0.05f, 0.3f, 0.62f, 0.95f}) {
                const float step = 1.0e-4f;
                const float numeric =
                    (worldParameterAt(t + step, nearZ, farZ) -
                     worldParameterAt(t - step, nearZ, farZ)) / (2.0f * step);
                const float depth = viewDepthAt(t, nearZ, farZ);
                const float closedForm = (depth * depth) / (nearZ * farZ);
                CHECK(closedForm == doctest::Approx(numeric).epsilon(0.001));
            }
        }
    }
}

TEST_CASE("the shader's closed-form strand matches the reference helix") {
    const CameraFrame camera = makeOffsetCamera();
    const ChainStyleResolved style = makeStyle();
    ChainGeometry geometry{};
    geometry.nearPoint = {1.0f, 1.2f, -2.0f};
    geometry.farPoint = {14.0f, 6.5f, -31.0f};
    geometry.bodyVisible = true;
    ChainUniforms uniforms{};
    REQUIRE(buildChainUniforms(camera, geometry, style, uniforms));

    const HelixFrame frame =
        makeHelixFrame(geometry.nearPoint, geometry.farPoint);
    REQUIRE(frame.valid);
    CHECK(uniforms.farPoint[3] == doctest::Approx(frame.span));
    CHECK(uniforms.coil[2] == doctest::Approx(style.phaseRadians));

    for (const float world : {0.0f, 0.23f, 0.5f, 0.81f, 1.0f}) {
        const Vec3 point = lerp(geometry.nearPoint, geometry.farPoint, world);
        const float t = screenParameterOf(camera, uniforms, point);
        const Vec3 fromShader =
            shaderStrandPoint(uniforms, frame, geometry.nearPoint,
                              geometry.farPoint, t);
        const Vec3 reference =
            helixPoint(geometry.nearPoint, geometry.farPoint, frame, world,
                       uniforms.coil[0], style.phaseRadians, false);
        CHECK(distance(fromShader, reference) < 0.002f);
    }
}

TEST_CASE("clipping re-bases the phase so the coil stays pinned to the world") {
    const CameraFrame camera = makeCamera();
    const ChainStyleResolved style = makeStyle();
    ChainGeometry geometry{};
    geometry.nearPoint = {0.4f, 0.0f, 6.0f};
    geometry.farPoint = {0.4f, 3.0f, -52.0f};
    geometry.bodyVisible = true;
    ChainUniforms uniforms{};
    REQUIRE(buildChainUniforms(camera, geometry, style, uniforms));
    REQUIRE(uniforms.flags[1] == doctest::Approx(1.0f));

    const HelixFrame frame =
        makeHelixFrame(geometry.nearPoint, geometry.farPoint);
    REQUIRE(frame.valid);
    CHECK(uniforms.farPoint[3] < frame.span);
    CHECK(uniforms.farPoint[3] > 0.5f * frame.span);
    CHECK(uniforms.coil[2] != doctest::Approx(style.phaseRadians));

    for (const float world : {0.2f, 0.45f, 0.7f, 1.0f}) {
        const Vec3 point = lerp(geometry.nearPoint, geometry.farPoint, world);
        const ProjectedPoint projected =
            projectViewPoint(camera, viewTransformPoint(camera.view, point));
        if (!projected.valid) continue;
        const float t = screenParameterOf(camera, uniforms, point);
        if (t < 0.0f || t > 1.0f) continue;
        const float localWorld =
            worldParameterAt(t, uniforms.nearPoint[2], uniforms.farPoint[2]);
        const float angle = helixAngleAt(localWorld, uniforms.farPoint[3],
                                         uniforms.coil[1], uniforms.coil[2]);
        const Vec3 fromShader =
            add(point, add(mul(frame.radialA, uniforms.coil[0] * std::cos(angle)),
                           mul(frame.radialB, uniforms.coil[0] * std::sin(angle))));
        const Vec3 reference =
            helixPoint(geometry.nearPoint, geometry.farPoint, frame, world,
                       uniforms.coil[0], style.phaseRadians, false);
        CHECK(distance(fromShader, reference) < 0.004f);
    }
}

TEST_CASE("a chain pointed straight at the camera hides its body") {
    const CameraFrame camera = makeCamera();
    ChainGeometry geometry{};
    geometry.nearPoint = {0.0f, 0.0f, -3.0f};
    geometry.farPoint = {0.0f, 0.0f, -40.0f};
    geometry.bodyVisible = true;
    ChainUniforms uniforms{};
    CHECK_FALSE(buildChainUniforms(camera, geometry, makeStyle(), uniforms));
    CHECK(uniforms.flags[1] == doctest::Approx(0.0f));
}

TEST_CASE("the reticle shrinks with distance and survives without a body") {
    const CameraFrame camera = makeCamera();
    ChainGeometry geometry{};
    geometry.reticleVisible = true;
    geometry.reticlePoint = {0.0f, 0.0f, -12.0f};
    geometry.reticleWorldSize = reticleWorldSize(12.0f);
    ChainUniforms near{};
    REQUIRE(buildChainUniforms(camera, geometry, makeStyle(), near));
    CHECK(near.flags[1] == doctest::Approx(0.0f));
    CHECK(near.flags[2] == doctest::Approx(1.0f));
    CHECK(near.reticle[0] == doctest::Approx(kWidth * 0.5f));
    CHECK(near.reticle[1] == doctest::Approx(kHeight * 0.5f));

    geometry.reticlePoint = {0.0f, 0.0f, -90.0f};
    geometry.reticleWorldSize = reticleWorldSize(90.0f);
    ChainUniforms far{};
    REQUIRE(buildChainUniforms(camera, geometry, makeStyle(), far));
    CHECK(far.reticle[3] < near.reticle[3]);
    CHECK(far.reticle[3] > 0.0f);
}

TEST_CASE("nothing visible means no uniform block at all") {
    const CameraFrame camera = makeCamera();
    ChainUniforms uniforms{};
    CHECK_FALSE(buildChainUniforms(camera, ChainGeometry{}, makeStyle(), uniforms));

    CameraFrame broken = makeCamera();
    broken.width = 0.0f;
    ChainGeometry geometry{};
    geometry.reticleVisible = true;
    geometry.reticlePoint = {0.0f, 0.0f, -12.0f};
    geometry.reticleWorldSize = 0.4f;
    CHECK_FALSE(buildChainUniforms(broken, geometry, makeStyle(), uniforms));
}

TEST_CASE("the scissor box contains everything drawn and stays on screen") {
    const CameraFrame camera = makeOffsetCamera();
    ChainGeometry geometry{};
    geometry.nearPoint = {2.0f, 1.4f, -1.0f};
    geometry.farPoint = {18.0f, 7.0f, -30.0f};
    geometry.bodyVisible = true;
    geometry.reticleVisible = true;
    geometry.reticlePoint = geometry.farPoint;
    geometry.reticleWorldSize = reticleWorldSize(30.0f);
    ChainUniforms uniforms{};
    REQUIRE(buildChainUniforms(camera, geometry, makeStyle(), uniforms));

    const ScissorBox box = chainScissorBox(uniforms);
    CHECK(box.width > 0);
    CHECK(box.height > 0);
    CHECK(box.x >= 0);
    CHECK(box.y >= 0);
    CHECK(box.x + box.width <= static_cast<int>(kWidth));
    CHECK(box.y + box.height <= static_cast<int>(kHeight));

    const auto contains = [&](float px, float py) {
        return px >= static_cast<float>(box.x) &&
               px <= static_cast<float>(box.x + box.width) &&
               py >= static_cast<float>(box.y) &&
               py <= static_cast<float>(box.y + box.height);
    };
    CHECK(contains(uniforms.nearPoint[0], uniforms.nearPoint[1]));
    CHECK(contains(uniforms.farPoint[0], uniforms.farPoint[1]));
    CHECK(contains(uniforms.reticle[0], uniforms.reticle[1]));
    CHECK(contains(uniforms.nearPoint[0],
                   uniforms.nearPoint[1] +
                       uniforms.coil[0] / uniforms.nearPoint[2] *
                           std::fabs(uniforms.pxScale[1])));
}
