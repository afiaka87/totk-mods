// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis
#pragma once

#include <cmath>
#include <cstdint>

namespace totk::render {

// Primitive positions, directions and offsets share this arithmetic type.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct PrimitiveBar {
    Vec3 center{};
    Vec3 right{};
    Vec3 up{};
    Vec3 axis{};
    float radius = 0.0f;
    float halfLength = 0.0f;
    bool valid = false;
};

// Ten vertices close a square beam; two degenerate connectors join beams.
// Budget twelve vertices per segment when batching.
inline constexpr std::uint32_t kBeamStripVertices = 10;
inline constexpr std::uint32_t kBeamStripBudgetVertices = 12;

struct BeamStrip {
    Vec3 vertices[kBeamStripVertices]{};
    bool valid = false;
};

inline constexpr float kMetersPerPixelPerMetre = 0.0013f;
inline constexpr float kMinMetersPerPixel = 0.0020f;
inline constexpr float kMaxPrimitiveRadius = 0.45f;
inline constexpr float kEndCapOverlap = 0.65f;

[[nodiscard]] inline float primitiveRadius(float pixelWidth, float cameraDistance) {
    if (!(pixelWidth > 0.0f) || !std::isfinite(pixelWidth)) return 0.0f;
    if (!(cameraDistance >= 0.0f) || !std::isfinite(cameraDistance)) cameraDistance = 0.0f;
    float metresPerPixel = cameraDistance * kMetersPerPixelPerMetre;
    if (metresPerPixel < kMinMetersPerPixel) metresPerPixel = kMinMetersPerPixel;
    float radius = pixelWidth * metresPerPixel * 0.5f;
    if (radius > kMaxPrimitiveRadius) radius = kMaxPrimitiveRadius;
    return radius;
}

// Move toward the eye along the view ray to change depth without screen drift.
// p * (1-k) + eye * k costs three multiplies and three adds per vertex.
struct DepthLift {
    float keep = 1.0f;
    Vec3 anchor{};  // the eye, already scaled by the fraction
    bool active = false;
};

[[nodiscard]] inline DepthLift depthLiftFor(float fraction, float eyeX,
                                            float eyeY, float eyeZ) {
    DepthLift out{};
    // A fraction at or above 1 would fold the picture through the camera, and a
    // broken eye would move every line to nowhere. Both refuse instead.
    if (!(fraction > 0.0f) || !(fraction < 1.0f) || !std::isfinite(fraction))
        return out;
    if (!std::isfinite(eyeX) || !std::isfinite(eyeY) || !std::isfinite(eyeZ))
        return out;
    out.keep = 1.0f - fraction;
    out.anchor = Vec3{eyeX * fraction, eyeY * fraction, eyeZ * fraction};
    out.active = true;
    return out;
}

[[nodiscard]] inline Vec3 applyDepthLift(const DepthLift& lift, const Vec3& point) {
    if (!lift.active) return point;
    return Vec3{point.x * lift.keep + lift.anchor.x,
                point.y * lift.keep + lift.anchor.y,
                point.z * lift.keep + lift.anchor.z};
}

[[nodiscard]] inline PrimitiveBar buildPrimitiveBar(const Vec3& from, const Vec3& to,
                                                     float radius) {
    PrimitiveBar out{};
    if (!(radius > 0.0f) || !std::isfinite(radius)) return out;

    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    const float lengthSquared = dx * dx + dy * dy + dz * dz;
    if (!(lengthSquared > 0.0025f) || !std::isfinite(lengthSquared)) return out;

    const float length = std::sqrt(lengthSquared);
    out.axis = Vec3{dx / length, dy / length, dz / length};

    // Cross against world up except for nearly vertical bars, where world right
    // avoids an unstable near-zero basis.
    const Vec3 reference = std::fabs(out.axis.y) < 0.90f ? Vec3{0.0f, 1.0f, 0.0f}
                                                         : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 right{reference.y * out.axis.z - reference.z * out.axis.y,
               reference.z * out.axis.x - reference.x * out.axis.z,
               reference.x * out.axis.y - reference.y * out.axis.x};
    const float rightLength = std::sqrt(right.x * right.x + right.y * right.y +
                                        right.z * right.z);
    if (!(rightLength > 0.0001f) || !std::isfinite(rightLength)) return PrimitiveBar{};
    out.right = Vec3{right.x / rightLength, right.y / rightLength, right.z / rightLength};
    out.up = Vec3{out.axis.y * out.right.z - out.axis.z * out.right.y,
                  out.axis.z * out.right.x - out.axis.x * out.right.z,
                  out.axis.x * out.right.y - out.axis.y * out.right.x};
    out.center = Vec3{(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f,
                      (from.z + to.z) * 0.5f};
    out.radius = radius;
    out.halfLength = length * 0.5f + radius * kEndCapOverlap;
    out.valid = true;
    return out;
}

// Clockwise corners give outward-facing triangles along the from->to axis.
// End caps are omitted because neighbouring segments overlap.
[[nodiscard]] inline BeamStrip buildSquareBeamStrip(const Vec3& from, const Vec3& to,
                                                     float radius) {
    BeamStrip out{};
    const PrimitiveBar bar = buildPrimitiveBar(from, to, radius);
    if (!bar.valid) return out;

    constexpr float kCornerScale = 0.7071067811865475f;
    const float cornerRadius = radius * kCornerScale;
    const Vec3 corners[4] = {
        {(bar.right.x + bar.up.x) * cornerRadius,
         (bar.right.y + bar.up.y) * cornerRadius,
         (bar.right.z + bar.up.z) * cornerRadius},
        {(bar.right.x - bar.up.x) * cornerRadius,
         (bar.right.y - bar.up.y) * cornerRadius,
         (bar.right.z - bar.up.z) * cornerRadius},
        {(-bar.right.x - bar.up.x) * cornerRadius,
         (-bar.right.y - bar.up.y) * cornerRadius,
         (-bar.right.z - bar.up.z) * cornerRadius},
        {(-bar.right.x + bar.up.x) * cornerRadius,
         (-bar.right.y + bar.up.y) * cornerRadius,
         (-bar.right.z + bar.up.z) * cornerRadius},
    };
    const Vec3 start{bar.center.x - bar.axis.x * bar.halfLength,
                     bar.center.y - bar.axis.y * bar.halfLength,
                     bar.center.z - bar.axis.z * bar.halfLength};
    const Vec3 end{bar.center.x + bar.axis.x * bar.halfLength,
                   bar.center.y + bar.axis.y * bar.halfLength,
                   bar.center.z + bar.axis.z * bar.halfLength};

    for (std::uint32_t side = 0; side <= 4; ++side) {
        const Vec3& corner = corners[side & 3u];
        out.vertices[side * 2u] =
            Vec3{start.x + corner.x, start.y + corner.y, start.z + corner.z};
        out.vertices[side * 2u + 1u] =
            Vec3{end.x + corner.x, end.y + corner.y, end.z + corner.z};
    }
    out.valid = true;
    return out;
}

}  // namespace totk::render
