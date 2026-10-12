// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace remote_bomb::render_policy {
struct Camera { float view[12]{}, projection[16]{}, near{}, far{}; };
// highlight: linear colour and pass (0 none, 1 body tint, 2 outline shell); outline: the mesh centre the shell is pushed out from, and its width in screen pixels; depth: the view distances the private depth buffer spans.
struct Uniforms {
    float dimensions[4]{}, view[3][4]{}, projection[4]{}, actor[3][4]{}, lighting[4]{}, emission[4]{},
        highlight[4]{}, outline[4]{}, depth[4]{};
};
static_assert(sizeof(Uniforms) == 13 * 16);
inline bool valid(const Camera &camera) {
    for (float value : camera.view)
        if (!std::isfinite(value))
            return false;
    for (float value : camera.projection)
        if (!std::isfinite(value))
            return false;
    return camera.near > 0.f && camera.far > camera.near && std::isfinite(camera.far) &&
           std::fabs(camera.projection[0]) > .0001f && std::fabs(camera.projection[5]) > .0001f;
}
inline float determinant(const std::array<float, 9> &m) {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
}
// A usable orientation is finite and close to unit size; zero or non-finite would draw nothing.
inline bool orientable(const std::array<float, 9> &m) {
    for (float value : m)
        if (!std::isfinite(value))
            return false;
    const auto size = determinant(m);
    return size > .5f && size < 2.f;
}
// A bomb whose orientation the game has not set yet is drawn upright instead of collapsed.
inline std::array<float, 9> drawRotation(const std::array<float, 9> &m) {
    return orientable(m) ? m : std::array<float, 9>{1,0,0, 0,1,0, 0,0,1};
}
// The private depth buffer is 16-bit, so it spans the live bombs and their outline shells rather than the camera's whole range: a few metres keep each step under a millimetre. The margin covers the bomb and a shell that widens with distance.
inline std::array<float, 2> depthRange(const Camera &camera, const std::array<float, 3> *positions,
                                       unsigned count) {
    if (!count)
        return {camera.near, camera.far};
    float low = 0.f, high = 0.f;
    for (unsigned i = 0; i < count; ++i) {
        const auto &p = positions[i];
        const float distance = -(camera.view[8] * p[0] + camera.view[9] * p[1] + camera.view[10] * p[2] +
                                 camera.view[11]);
        low = i == 0 || distance < low ? distance : low;
        high = i == 0 || distance > high ? distance : high;
    }
    const float margin = 1.f + .01f * (high > 0.f ? high : 0.f);
    low = low - margin > camera.near ? low - margin : camera.near;
    high = high + margin < camera.far ? high + margin : camera.far;
    return {low, high > low + .01f ? high : low + .01f};
}
// Centre of the vertices a mesh's indices reach (rows are three vec4 per vertex, position first).
inline std::array<float, 3> centre(const float (*rows)[4], const std::uint16_t *indices, unsigned count) {
    if (!count)
        return {};
    float low[3], high[3];
    for (unsigned axis = 0; axis < 3; ++axis)
        low[axis] = high[axis] = rows[indices[0] * 3u][axis];
    for (unsigned i = 1; i < count; ++i)
        for (unsigned axis = 0; axis < 3; ++axis) {
            const float value = rows[indices[i] * 3u][axis];
            low[axis] = value < low[axis] ? value : low[axis];
            high[axis] = value > high[axis] ? value : high[axis];
        }
    return {(low[0] + high[0]) * .5f, (low[1] + high[1]) * .5f, (low[2] + high[2]) * .5f};
}
inline Uniforms uniforms(const Camera &camera, std::array<float, 3> position,
                         const std::array<float, 9> &rotation,
                         unsigned width, unsigned height) {
    Uniforms out;
    out.dimensions[0] = float(width);
    out.dimensions[1] = float(height);
    out.dimensions[2] = camera.near;
    out.dimensions[3] = camera.far;
    const float eye[]{
        -(camera.view[0]*camera.view[3]+camera.view[4]*camera.view[7]+camera.view[8]*camera.view[11]),
        -(camera.view[1]*camera.view[3]+camera.view[5]*camera.view[7]+camera.view[9]*camera.view[11]),
        -(camera.view[2]*camera.view[3]+camera.view[6]*camera.view[7]+camera.view[10]*camera.view[11]),
    };
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned col = 0; col < 3; ++col) {
            out.view[row][col] = camera.view[col*4+row];
            out.actor[row][col] = rotation[row*3+col];
        }
        out.view[row][3] = eye[row];
        out.actor[row][3] = position[row];
    }
    out.projection[0] = camera.projection[0];
    out.projection[1] = camera.projection[5];
    out.projection[2] = camera.projection[2];
    out.projection[3] = camera.projection[6];
    out.lighting[0] = -.4f;
    out.lighting[1] = .8f;
    out.lighting[2] = .3f;
    out.depth[0] = camera.near;
    out.depth[1] = camera.far;
    return out;
}
}
