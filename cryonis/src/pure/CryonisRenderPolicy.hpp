// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisPresentation.hpp"
#include "CryonisSelection.hpp"
#include <algorithm>
namespace cryonis::render_policy {
struct Uniforms {
    float dimensions[4]{}, world[3][4]{}, projection[4]{}, base[4]{}, flags[4]{}, appearance[4]{};
    float bones[7][4]{}, waterOrigin[4]{}, transform[4]{1, 0, kSizeScale, 0};
};
static_assert(sizeof(Uniforms) == 17 * 16);
struct Camera {
    float view[12], projection[16], near{}, far{};
};
inline Ray ray(const Camera &c) {
    const Vec3 eye{-(c.view[0] * c.view[3] + c.view[4] * c.view[7] + c.view[8] * c.view[11]),
                   -(c.view[1] * c.view[3] + c.view[5] * c.view[7] + c.view[9] * c.view[11]),
                   -(c.view[2] * c.view[3] + c.view[6] * c.view[7] + c.view[10] * c.view[11])};
    const float x = c.projection[2] / c.projection[0], y = c.projection[6] / c.projection[5];
    const Vec3 direction{c.view[0] * x + c.view[4] * y - c.view[8],
                         c.view[1] * x + c.view[5] * y - c.view[9],
                         c.view[2] * x + c.view[6] * y - c.view[10]};
    const float magnitude = length(direction);
    if (!finite(eye) || !std::isfinite(magnitude) || magnitude < .001f)
        return {};
    return {eye, mul(direction, 1 / magnitude), true};
}
inline Uniforms uniforms(const Camera &c, const Presentation &p, unsigned width, unsigned height) {
    Uniforms u;
    const auto r = ray(c);
    u.dimensions[0] = float(width);
    u.dimensions[1] = float(height);
    u.dimensions[2] = c.near;
    u.dimensions[3] = c.far;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col)
            u.world[row][col] = c.view[col * 4 + row];
    u.world[0][3] = r.eye.x;
    u.world[1][3] = r.eye.y;
    u.world[2][3] = r.eye.z;
    u.projection[0] = c.projection[0];
    u.projection[1] = c.projection[5];
    u.projection[2] = c.projection[2];
    u.projection[3] = c.projection[6];
    u.flags[0] = p.elapsedFrames;
    u.flags[2] = p.cooldown;
    u.waterOrigin[0] = p.waterOrigin.x;
    u.waterOrigin[1] = p.waterOrigin.z;
    u.waterOrigin[2] = p.waterOrigin.y;
    u.waterOrigin[3] = kWaterSide;
    return u;
}
inline Uniforms solid(const Uniforms &common, const Pillar &pillar, unsigned slot,
                      const Presentation &snapshot) {
    const auto base = visualBase(pillar.base, pillar.growth.frame);
    auto u = common;
    u.base[0] = base.x;
    u.base[1] = base.y;
    u.base[2] = base.z;
    u.base[3] = 0;
    u.flags[3] = target::selected(slot, snapshot.target, snapshot.aiming) ? 1.0f : 0.0f;
    u.flags[1] = pillar.growth.frame;
    const auto pose = originalPose(pillar.growth.frame);
    u.transform[0] = std::cos(pillar.yaw);
    u.transform[1] = std::sin(pillar.yaw);
    u.transform[3] = pillar.base.y;
    for (auto &b : u.bones) {
        b[0] = pose.width;
        b[1] = pose.height;
        b[2] = pose.translation;
    }
    return u;
}
inline Uniforms preview(const Uniforms &common, const Presentation &snapshot) {
    auto u = common;
    u.base[0] = snapshot.target.base.x;
    u.base[1] = snapshot.target.base.y - kSettledSubmerge;
    u.base[2] = snapshot.target.base.z;
    u.base[3] = 1;
    u.transform[0] = std::cos(snapshot.target.yaw);
    u.transform[1] = std::sin(snapshot.target.yaw);
    u.transform[3] = snapshot.target.base.y;
    const float heightMetres = kHeight * std::fmod(snapshot.previewFrames, 60.0f) / 60.0f;
    for (auto &b : u.bones) {
        b[0] = b[1] = 1;
        b[2] = heightMetres / kSizeScale - kOriginalHeight;
    }
    return u;
}
struct PillarBatch {
    Uniforms values[4];
    unsigned count{};
};
inline PillarBatch pillars(const Uniforms &common, const Presentation &snapshot, const Camera &c) {
    PillarBatch batch;
    auto &values = batch.values;
    auto &counts = batch.count;
    for (unsigned slot = 0; slot < kPillarLimit; ++slot)
        if (snapshot.pillars[slot].occupied)
            values[counts++] = solid(common, snapshot.pillars[slot], slot, snapshot);
    if (snapshot.aiming && snapshot.target.valid && snapshot.target.pillar < 0)
        values[counts++] = preview(common, snapshot);
    // Scene depth is borrowed, so translucent convex pillars are sorted far to near.
    const auto depth = [&](const Uniforms &u) {
        return -(c.view[8] * u.base[0] + c.view[9] * u.base[1] + c.view[10] * u.base[2] +
                 c.view[11]);
    };
    for (unsigned i = 1; i < counts; ++i)
        for (unsigned j = i; j > 0 && depth(values[j]) > depth(values[j - 1]); --j)
            std::swap(values[j], values[j - 1]);
    return batch;
}
}
