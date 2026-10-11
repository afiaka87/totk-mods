// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StasisPresentation.hpp"
#include <cmath>
namespace stasis::render {
struct Camera {
    float view[12]{}, proj[16]{};
    float nearMetres{}, farMetres{}, width{}, height{};
};
struct Point {
    float x{}, y{}, z{};
    bool valid{};
};
// The shader block keeps its availableMin/Max names: slots 0..3 carry the arrow basis and
// availableMax[0].w the target's near depth.
inline constexpr unsigned kUniformSlots = 16;
struct Uniforms {
    float dimensions[4]{}, target[4]{}, tail[4]{}, tip[4]{}, flags[4]{}, timing[4]{}, tint[4]{},
        extra[4]{};
    float world[3][4]{}, projection[4]{}, selectedMin[4]{}, selectedMax[4]{};
    float availableMin[kUniformSlots][4]{}, availableMax[kUniformSlots][4]{};
    float chains[6][4]{};
    float maskDimensions[4]{1, 1, 0, 0};
    float effects[4]{-1, -1, 0, 0}, pipeline[4]{}, resources[4]{}, trailParams[4]{};
    float trail[kTrailSamples][4]{};
};
static_assert(sizeof(Uniforms) == 153 * 16);
inline Ray cameraRay(const Camera &c) {
    if (std::fabs(c.proj[0]) < 0.0001f || std::fabs(c.proj[5]) < 0.0001f)
        return {};
    const Vec3 eye{-(c.view[0] * c.view[3] + c.view[4] * c.view[7] + c.view[8] * c.view[11]),
                   -(c.view[1] * c.view[3] + c.view[5] * c.view[7] + c.view[9] * c.view[11]),
                   -(c.view[2] * c.view[3] + c.view[6] * c.view[7] + c.view[10] * c.view[11])};
    const float x = c.proj[2] / c.proj[0], y = c.proj[6] / c.proj[5];
    Vec3 direction{c.view[0] * x + c.view[4] * y - c.view[8],
                   c.view[1] * x + c.view[5] * y - c.view[9],
                   c.view[2] * x + c.view[6] * y - c.view[10]};
    const auto magnitude = length(direction);
    if (!std::isfinite(length(eye)) || !std::isfinite(magnitude) || magnitude < 0.5f)
        return {};
    return {eye, scale(direction, 1.0f / magnitude), true};
}
// PFX's borrowed depth texture already stores normalized linear view depth.
inline float sceneDepthMetres(const Camera &c, float value) {
    return c.nearMetres + value * (c.farMetres - c.nearMetres);
}
inline Point project(const Camera &c, Vec3 world) {
    const float x = c.view[0] * world.x + c.view[1] * world.y + c.view[2] * world.z + c.view[3];
    const float y = c.view[4] * world.x + c.view[5] * world.y + c.view[6] * world.z + c.view[7];
    const float z = c.view[8] * world.x + c.view[9] * world.y + c.view[10] * world.z + c.view[11];
    const float w = c.proj[12] * x + c.proj[13] * y + c.proj[14] * z + c.proj[15];
    if (!std::isfinite(w) || w <= std::fmax(c.nearMetres, 0.0001f))
        return {};
    const float px =
        ((c.proj[0] * x + c.proj[1] * y + c.proj[2] * z + c.proj[3]) / w * 0.5f + 0.5f) * c.width;
    const float py =
        (0.5f - (c.proj[4] * x + c.proj[5] * y + c.proj[6] * z + c.proj[7]) / w * 0.5f) * c.height;
    return {px, py, -z, std::isfinite(px) && std::isfinite(py) && z < 0.0f};
}
inline void buildUniforms(const Camera &c, const Presentation &s, Uniforms &u) {
    u.dimensions[0] = c.width;
    u.dimensions[1] = c.height;
    u.dimensions[2] = c.nearMetres;
    u.dimensions[3] = c.farMetres;
    const auto p = project(c, s.position);
    const float pxPerMetre = std::fabs(c.proj[0]) * c.width * 0.5f / std::fmax(p.z, 0.1f);
    u.target[0] = p.x;
    u.target[1] = p.y;
    u.target[2] = p.z;
    u.target[3] = std::fmax(pxPerMetre * 0.85f, 12.0f);
    if (const float half = radius(s.bounds); half > 0)
        u.target[3] = std::fmax(pxPerMetre * half, 12.0f);
    // Fit activation to the projected object, including flat objects.
    float extentX = 0, extentY = 0;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const auto q = project(c, {corner & 1 ? s.bounds.max.x : s.bounds.min.x,
                                   corner & 2 ? s.bounds.max.y : s.bounds.min.y,
                                   corner & 4 ? s.bounds.max.z : s.bounds.min.z});
        if (q.valid && p.valid) {
            extentX = std::fmax(extentX, std::fabs(q.x - p.x));
            extentY = std::fmax(extentY, std::fabs(q.y - p.y));
        }
    }
    u.resources[0] = std::fmax(12.0f, std::fmin(extentX, c.width * .4f));
    u.resources[1] = std::fmax(12.0f, std::fmin(extentY, c.height * .4f));
    u.effects[0] = s.activationAge;
    u.effects[1] = s.trail.age;
    u.effects[2] = float(s.trail.count);
    u.trailParams[0] = s.trailRadius;
    for (unsigned i = 0; i < s.trail.count && i < kTrailSamples; ++i) {
        u.trail[i][0] = s.trail.samples[i].position.x;
        u.trail[i][1] = s.trail.samples[i].position.y;
        u.trail[i][2] = s.trail.samples[i].position.z;
        u.trail[i][3] = s.trail.samples[i].birth;
    }
    // Arrow size in metres from the approved appearance study; the launch force is unchanged.
    const float charge = std::fmax(0.0f, std::fmin(s.ratio, 1.0f));
    const float arrowLength = .90f + 5.3f * charge, arrowWidth = .32f + .24f * charge;
    const Vec3 arrow{arrowWidth / .33875746f, 0, arrowLength / 2.9643395f};
    const Vec3 begin = s.arrowOrigin;
    const float magnitude = length(s.direction);
    Vec3 end = begin;
    if (magnitude > 0.0001f) {
        const auto direction = scale(s.direction, 1.0f / magnitude);
        end = add(begin, scale(direction, arrowLength));
        Vec3 right{direction.z, 0.0f, -direction.x};
        if (std::fabs(direction.y) > 0.999f)
            right = {0.0f, -direction.z, direction.y};
        right = scale(right, 1.0f / std::fmax(length(right), 0.0001f));
        const Vec3 basis[]{begin, right, cross(direction, right), direction};
        for (unsigned i = 0; i < 4; ++i) {
            u.availableMin[i][0] = basis[i].x;
            u.availableMin[i][1] = basis[i].y;
            u.availableMin[i][2] = basis[i].z;
        }
        u.availableMin[0][3] = arrow.x;
        u.availableMin[1][3] = arrow.z;
    }
    const auto a = project(c, begin), b = project(c, end);
    u.tail[0] = a.x;
    u.tail[1] = a.y;
    u.tail[2] = a.z;
    u.tail[3] = std::fmax(pxPerMetre * 0.33875746f * arrow.x, 2.0f);
    u.tip[0] = b.x;
    u.tip[1] = b.y;
    u.tip[2] = b.z;
    u.tip[3] = s.frozen && a.valid && b.valid && magnitude > 0.0001f && !s.enemy ? 1.0f : 0.0f;
    const bool selected = s.aiming && s.selected;
    u.flags[0] = s.aiming ? 1.0f : 0.0f;
    u.flags[1] = s.frozen ? 1.0f : 0.0f;
    u.flags[2] = s.ratio;
    u.flags[3] = p.valid && (selected || s.frozen || s.releaseFlash > 0) ? 1.0f : 0.0f;
    u.timing[0] = s.frozen && s.durationFrames > 0
                      ? std::fmax(s.remainingFrames / s.durationFrames, 0.0f)
                      : 0.0f;
    u.timing[1] = s.elapsedFrames / 30.0f;
    u.timing[2] = s.hitFlash;
    u.timing[3] = s.releaseFlash;
    u.tint[0] = 1.0f;
    u.tint[1] = std::fmax(1.0f - s.ratio * 0.9f, 0.1f);
    u.tint[2] = 0.03f;
    u.extra[0] = s.remainingFrames;
    u.extra[1] = std::fmin(s.cooldownFrames / 300.0f, 1.0f);
    u.extra[2] = pxPerMetre;
    u.extra[3] = s.enemy ? 1.0f : 0.0f;
    const auto ray = cameraRay(c);
    for (unsigned row = 0; row < 3; ++row) {
        u.world[row][0] = c.view[row];
        u.world[row][1] = c.view[4 + row];
        u.world[row][2] = c.view[8 + row];
    }
    u.world[0][3] = ray.eye.x;
    u.world[1][3] = ray.eye.y;
    u.world[2][3] = ray.eye.z;
    u.projection[0] = c.proj[0];
    u.projection[1] = c.proj[5];
    u.projection[2] = c.proj[2];
    u.projection[3] = c.proj[6];
    const auto bounds = [](float *min, float *max, Bounds b, float active) {
        min[0] = b.min.x;
        min[1] = b.min.y;
        min[2] = b.min.z;
        min[3] = active;
        max[0] = b.max.x;
        max[1] = b.max.y;
        max[2] = b.max.z;
    };
    bounds(u.selectedMin, u.selectedMax, s.bounds, (selected || s.frozen) ? 1.0f : 0.0f);
    const float centreDepth = -(c.view[8] * (s.bounds.min.x + s.bounds.max.x) * 0.5f +
                                c.view[9] * (s.bounds.min.y + s.bounds.max.y) * 0.5f +
                                c.view[10] * (s.bounds.min.z + s.bounds.max.z) * 0.5f + c.view[11]);
    const float depthRadius = std::fabs(c.view[8]) * (s.bounds.max.x - s.bounds.min.x) * 0.5f +
                              std::fabs(c.view[9]) * (s.bounds.max.y - s.bounds.min.y) * 0.5f +
                              std::fabs(c.view[10]) * (s.bounds.max.z - s.bounds.min.z) * 0.5f;
    u.availableMax[0][3] = std::fmax(centreDepth - depthRadius, c.nearMetres);
    constexpr Vec3 directions[]{{1, .05f, .38f},   {-1, -.08f, -.35f}, {.34f, 1, .42f},
                                {-.70f, -.08f, 1}, {-.9f, .66f, .12f}, {.9f, -.66f, -.12f}};
    for (unsigned i = 0; i < 6; ++i) {
        auto d = scale(directions[i], 12.0f / length(directions[i]));
        const float dz = c.view[8] * d.x + c.view[9] * d.y + c.view[10] * d.z;
        if (dz > 0.0f && p.z - dz < c.nearMetres * 2.0f)
            d = scale(d, std::fmax((p.z - c.nearMetres * 2.0f) / dz, 0.0f));
        const auto endpoint = project(c, add(s.position, d));
        u.chains[i][0] = endpoint.x;
        u.chains[i][1] = endpoint.y;
        u.chains[i][2] = endpoint.z;
        u.chains[i][3] = endpoint.valid && s.frozen ? length(d) : 0.0f;
    }
}
} // namespace stasis::render
