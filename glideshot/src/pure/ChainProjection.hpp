// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Screen-space projection for the shader chain, in flipped pixel space (x right, y down); the
// shader never touches a matrix.
#pragma once

#include <cmath>
#include <cstdint>

#include "ChainPresentation.hpp"
#include "Vec3.hpp"

namespace zonai_hookshot::pure {
// Shared GLSL/C++ definitions; chAbs is the one primitive the including language supplies.
inline float chAbs(float x) { return std::fabs(x); }
#define CH_INLINE inline
#include "../../shaders/chain_math.inl"
#undef CH_INLINE

constexpr float kChainTau = 6.2831853071795864769f;

// Below this the projected axis is too short to define a direction; the body is hidden rather than
// drawn wrong.
constexpr float kMinAxisPixelsSquared = 4.0f;

// Antialiasing band in pixels on each side of every stripe edge (no derivative built-ins).
constexpr float kFeatherPixels = 1.0f;

// Render camera copied by value: 3x4 row-major world->view and 4x4 row-major view->clip.
struct CameraFrame {
    float view[12]{};
    float proj[16]{};
    float nearMetres = 0.0f;
    float farMetres = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ViewPoint {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct ProjectedPoint {
    float px = 0.0f;      // flipped pixel space, x rightward
    float py = 0.0f;      // flipped pixel space, y downward
    float viewZ = 0.0f;   // positive metres in front of the camera
    bool valid = false;
};

inline ViewPoint viewTransformPoint(const float* view, Vec3 world) {
    return {view[0] * world.x + view[1] * world.y + view[2] * world.z + view[3],
            view[4] * world.x + view[5] * world.y + view[6] * world.z + view[7],
            view[8] * world.x + view[9] * world.y + view[10] * world.z + view[11]};
}

inline ViewPoint viewTransformDirection(const float* view, Vec3 world) {
    return {view[0] * world.x + view[1] * world.y + view[2] * world.z,
            view[4] * world.x + view[5] * world.y + view[6] * world.z,
            view[8] * world.x + view[9] * world.y + view[10] * world.z};
}

inline bool finiteView(ViewPoint p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// View space -> flipped pixel space; invalid at or behind the near plane, so clip first.
inline ProjectedPoint projectViewPoint(const CameraFrame& camera, ViewPoint p) {
    ProjectedPoint out{};
    if (!finiteView(p)) return out;
    const float clipX = camera.proj[0] * p.x + camera.proj[1] * p.y +
                        camera.proj[2] * p.z + camera.proj[3];
    const float clipY = camera.proj[4] * p.x + camera.proj[5] * p.y +
                        camera.proj[6] * p.z + camera.proj[7];
    const float clipW = camera.proj[12] * p.x + camera.proj[13] * p.y +
                        camera.proj[14] * p.z + camera.proj[15];
    const float minimumW = camera.nearMetres > 0.0001f ? camera.nearMetres : 0.0001f;
    if (!std::isfinite(clipX) || !std::isfinite(clipY) ||
        !std::isfinite(clipW) || clipW <= minimumW)
        return out;
    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;
    out.px = (ndcX * 0.5f + 0.5f) * camera.width;
    out.py = (0.5f - ndcY * 0.5f) * camera.height;   // pixel y runs downward
    out.viewZ = -p.z;
    out.valid = std::isfinite(out.px) && std::isfinite(out.py) &&
                std::isfinite(out.viewZ) && out.viewZ > 0.0f;
    return out;
}

// Pixels per metre of offset at one metre of depth, measured by finite difference so a transposed
// or mirrored projection cannot fool it.
struct PixelScale {
    float x = 0.0f, y = 0.0f;
    bool valid = false;
};

inline PixelScale pixelScaleAtUnitDepth(const CameraFrame& camera) {
    PixelScale scale{};
    const float probeDepth = camera.nearMetres > 0.0f
        ? camera.nearMetres * 8.0f + 1.0f
        : 10.0f;
    const ProjectedPoint origin = projectViewPoint(camera, {0.0f, 0.0f, -probeDepth});
    const ProjectedPoint alongX = projectViewPoint(camera, {1.0f, 0.0f, -probeDepth});
    const ProjectedPoint alongY = projectViewPoint(camera, {0.0f, 1.0f, -probeDepth});
    if (!origin.valid || !alongX.valid || !alongY.valid) return scale;
    scale.x = (alongX.px - origin.px) * probeDepth;
    scale.y = (alongY.py - origin.py) * probeDepth;
    scale.valid = std::isfinite(scale.x) && std::isfinite(scale.y) &&
                  std::fabs(scale.x) > 1.0f;
    return scale;
}

// Drawn fractions of the world segment: the near end is clipped, not dropped, when Link zips past
// the anchor.
struct SegmentClip {
    float from = 0.0f;   // fraction of the original segment where drawing starts
    float to = 1.0f;     // ...and where it ends
    bool valid = false;
};

inline SegmentClip clipSegmentToNear(ViewPoint a, ViewPoint b, float nearMetres) {
    SegmentClip clip{};
    if (!finiteView(a) || !finiteView(b) || !std::isfinite(nearMetres)) return clip;
    // In front when -z >= nearPlane; bias past the plane so the clipped end projects with finite
    // w.
    const float plane = -(nearMetres > 0.0f ? nearMetres : 0.0001f) * 1.01f;
    const bool aFront = a.z <= plane;
    const bool bFront = b.z <= plane;
    if (!aFront && !bFront) return clip;
    clip.valid = true;
    if (aFront && bFront) return clip;
    const float span = b.z - a.z;
    if (std::fabs(span) < 1e-6f) { clip.valid = false; return clip; }
    const float crossing = (plane - a.z) / span;
    if (!std::isfinite(crossing) || crossing <= 0.0f || crossing >= 1.0f) {
        clip.valid = false;
        return clip;
    }
    if (aFront) clip.to = crossing; else clip.from = crossing;
    return clip;
}

inline ViewPoint lerpView(ViewPoint a, ViewPoint b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Exactly 14 vec4 (224 B): the size the shared shader binding allowlist accepts.
struct alignas(16) ChainUniforms {
    float dimensions[4]{};     // x width px, y height px, z near m, w far m
    float flags[4]{};          // x valid, y bodyVisible, z reticleVisible, w occludedGain
    float nearPoint[4]{};      // xy pixel, z viewZ, w radialA view-space z
    float farPoint[4]{};       // xy pixel, z viewZ, w drawn span in metres
    float radial[4]{};         // xy radialA view xy, zw radialB view xy
    float coil[4]{};           // x radius m, y wavelength m, z phase rad, w strand half px
    float pxScale[4]{};        // xy world->px at unit depth, z spine half px, w feather px
    float coreA[4]{};          // rgb + coverage
    float coreB[4]{};
    float spineColor[4]{};
    float reticle[4]{};        // xy pixel centre, z viewZ, w outer radius px
    float reticleShape[4]{};   // x inner scale, y ring half px, z underlay half px, w radialB view z
    float reticleColor[4]{};
    float reticleUnder[4]{};
};
static_assert(sizeof(ChainUniforms) == 224,
              "The 14-vec4 layout is what keeps the shader inside the shared "
              "binding allowlist; changing it needs a matching gate update");

// Style resolved on the CPU so the shader never learns which state it draws.
struct ChainStyleResolved {
    float spineRgb[3]{};
    float coreARgb[3]{};
    float coreBRgb[3]{};
    float reticleRgb[3]{};
    float reticleUnderRgb[3]{};
    float spineCoverage = 1.0f;
    float strandCoverage = 1.0f;
    float reticleCoverage = 1.0f;
    float reticleUnderCoverage = 1.0f;
    float spineHalfPx = 1.2f;
    float strandHalfPx = 0.6f;
    float ringHalfPx = 0.85f;
    float underlayHalfPx = 1.7f;
    float reticleInnerScale = 0.46f;
    float helixRadius = 0.105f;
    float wavelength = kHelixWavelength;
    float phaseRadians = 0.0f;
};

struct ChainGeometry {
    Vec3 nearPoint{};
    Vec3 farPoint{};
    Vec3 reticlePoint{};
    bool bodyVisible = false;
    bool reticleVisible = false;
    float reticleOuterPx = 0.0f;   // computed from reticleWorldSize by the caller
    float reticleWorldSize = 0.0f;
};

inline void copyRgb(float* out, const float* rgb, float coverage) {
    out[0] = rgb[0];
    out[1] = rgb[1];
    out[2] = rgb[2];
    out[3] = coverage;
}

inline bool finiteBlock(const float* values, int count) {
    for (int i = 0; i < count; ++i)
        if (!std::isfinite(values[i])) return false;
    return true;
}

// False when there is nothing to draw; the caller skips the draw call.
inline bool buildChainUniforms(const CameraFrame& camera,
                               const ChainGeometry& geometry,
                               const ChainStyleResolved& style,
                               ChainUniforms& out) {
    out = ChainUniforms{};
    if (!(camera.width >= 16.0f && camera.height >= 16.0f) ||
        !std::isfinite(camera.nearMetres) || !std::isfinite(camera.farMetres) ||
        camera.nearMetres <= 0.0f || camera.farMetres <= camera.nearMetres)
        return false;

    out.dimensions[0] = camera.width;
    out.dimensions[1] = camera.height;
    out.dimensions[2] = camera.nearMetres;
    out.dimensions[3] = camera.farMetres;
    out.flags[3] = 0.0f;   // occludedGain: 0 hides the chain behind geometry

    const PixelScale scale = pixelScaleAtUnitDepth(camera);
    if (!scale.valid) return false;
    out.pxScale[0] = scale.x;
    out.pxScale[1] = scale.y;
    out.pxScale[2] = style.spineHalfPx;
    out.pxScale[3] = kFeatherPixels;

    out.coil[0] = style.helixRadius;
    out.coil[1] = style.wavelength > 0.01f ? style.wavelength : kHelixWavelength;
    out.coil[2] = style.phaseRadians;
    out.coil[3] = style.strandHalfPx;

    copyRgb(out.coreA, style.coreARgb, style.strandCoverage);
    copyRgb(out.coreB, style.coreBRgb, style.strandCoverage);
    copyRgb(out.spineColor, style.spineRgb, style.spineCoverage);
    copyRgb(out.reticleColor, style.reticleRgb, style.reticleCoverage);
    copyRgb(out.reticleUnder, style.reticleUnderRgb, style.reticleUnderCoverage);

    bool bodyVisible = false;
    if (geometry.bodyVisible && finite3(geometry.nearPoint) &&
        finite3(geometry.farPoint)) {
        const HelixFrame frame =
            makeHelixFrame(geometry.nearPoint, geometry.farPoint);
        const ViewPoint viewNear = viewTransformPoint(camera.view, geometry.nearPoint);
        const ViewPoint viewFar = viewTransformPoint(camera.view, geometry.farPoint);
        const SegmentClip clip =
            clipSegmentToNear(viewNear, viewFar, camera.nearMetres);
        if (frame.valid && clip.valid) {
            const ProjectedPoint a =
                projectViewPoint(camera, lerpView(viewNear, viewFar, clip.from));
            const ProjectedPoint b =
                projectViewPoint(camera, lerpView(viewNear, viewFar, clip.to));
            const float axisX = b.px - a.px;
            const float axisY = b.py - a.py;
            const float axisLengthSquared = axisX * axisX + axisY * axisY;
            if (a.valid && b.valid && axisLengthSquared >= kMinAxisPixelsSquared) {
                const ViewPoint radialA =
                    viewTransformDirection(camera.view, frame.radialA);
                const ViewPoint radialB =
                    viewTransformDirection(camera.view, frame.radialB);
                if (finiteView(radialA) && finiteView(radialB)) {
                    out.nearPoint[0] = a.px;
                    out.nearPoint[1] = a.py;
                    out.nearPoint[2] = a.viewZ;
                    out.nearPoint[3] = radialA.z;
                    out.farPoint[0] = b.px;
                    out.farPoint[1] = b.py;
                    out.farPoint[2] = b.viewZ;
                    // Arc-length parameter stays in metres after a near-plane clip.
                    out.farPoint[3] = frame.span * (clip.to - clip.from);
                    out.radial[0] = radialA.x;
                    out.radial[1] = radialA.y;
                    out.radial[2] = radialB.x;
                    out.radial[3] = radialB.y;
                    out.reticleShape[3] = radialB.z;
                    // Re-base the phase onto the clipped start so the coil stays pinned to the
                    // world.
                    out.coil[2] = style.phaseRadians +
                                  kChainTau * (clip.from * frame.span) / out.coil[1];
                    bodyVisible = out.farPoint[3] > 0.01f;
                }
            }
        }
    }
    out.flags[1] = bodyVisible ? 1.0f : 0.0f;

    bool reticleVisible = false;
    if (geometry.reticleVisible && finite3(geometry.reticlePoint)) {
        const ProjectedPoint r = projectViewPoint(
            camera, viewTransformPoint(camera.view, geometry.reticlePoint));
        if (r.valid) {
            // Constant apparent reticle size: a world radius carried through the same projection.
            const float radiusPx =
                geometry.reticleWorldSize * std::fabs(scale.y) / r.viewZ;
            if (std::isfinite(radiusPx) && radiusPx >= 1.0f && radiusPx < 4096.0f) {
                out.reticle[0] = r.px;
                out.reticle[1] = r.py;
                out.reticle[2] = r.viewZ;
                out.reticle[3] = radiusPx;
                out.reticleShape[0] = style.reticleInnerScale;
                out.reticleShape[1] = style.ringHalfPx;
                out.reticleShape[2] = style.underlayHalfPx;
                reticleVisible = true;
            }
        }
    }
    out.flags[2] = reticleVisible ? 1.0f : 0.0f;

    if (!bodyVisible && !reticleVisible) return false;

    const bool finiteAll = finiteBlock(reinterpret_cast<const float*>(&out), 56);
    out.flags[0] = finiteAll ? 1.0f : 0.0f;
    return finiteAll;
}

// Scissor box in framebuffer pixels so only the chain's slice of the screen pays.
struct ScissorBox {
    int x = 0, y = 0, width = 0, height = 0;
};

inline ScissorBox chainScissorBox(const ChainUniforms& uniforms) {
    const float width = uniforms.dimensions[0];
    const float height = uniforms.dimensions[1];
    float lowX = width, lowY = height, highX = 0.0f, highY = 0.0f;
    const bool body = uniforms.flags[1] > 0.5f;
    const bool reticle = uniforms.flags[2] > 0.5f;

    if (body) {
        // The coil bulges off the axis by at most the projected radius at the nearest point.
        const float nearestZ = uniforms.nearPoint[2] < uniforms.farPoint[2]
            ? uniforms.nearPoint[2]
            : uniforms.farPoint[2];
        const float scaleMax = std::fabs(uniforms.pxScale[0]) >
                                       std::fabs(uniforms.pxScale[1])
            ? std::fabs(uniforms.pxScale[0])
            : std::fabs(uniforms.pxScale[1]);
        const float bulge = nearestZ > 0.01f
            ? uniforms.coil[0] / nearestZ * scaleMax
            : 0.0f;
        const float pad = bulge + uniforms.coil[3] + uniforms.pxScale[2] +
                          uniforms.pxScale[3] + 2.0f;
        for (int i = 0; i < 2; ++i) {
            const float* point = i == 0 ? uniforms.nearPoint : uniforms.farPoint;
            if (point[0] - pad < lowX) lowX = point[0] - pad;
            if (point[1] - pad < lowY) lowY = point[1] - pad;
            if (point[0] + pad > highX) highX = point[0] + pad;
            if (point[1] + pad > highY) highY = point[1] + pad;
        }
    }
    if (reticle) {
        const float pad = uniforms.reticle[3] + uniforms.reticleShape[1] +
                          uniforms.reticleShape[2] + uniforms.pxScale[3] + 2.0f;
        if (uniforms.reticle[0] - pad < lowX) lowX = uniforms.reticle[0] - pad;
        if (uniforms.reticle[1] - pad < lowY) lowY = uniforms.reticle[1] - pad;
        if (uniforms.reticle[0] + pad > highX) highX = uniforms.reticle[0] + pad;
        if (uniforms.reticle[1] + pad > highY) highY = uniforms.reticle[1] + pad;
    }

    if (lowX < 0.0f) lowX = 0.0f;
    if (lowY < 0.0f) lowY = 0.0f;
    if (highX > width) highX = width;
    if (highY > height) highY = height;

    ScissorBox box{};
    if (!(highX > lowX) || !(highY > lowY)) return box;
    box.x = static_cast<int>(lowX);
    box.y = static_cast<int>(lowY);
    box.width = static_cast<int>(highX) - box.x + 1;
    box.height = static_cast<int>(highY) - box.y + 1;
    if (box.x + box.width > static_cast<int>(width))
        box.width = static_cast<int>(width) - box.x;
    if (box.y + box.height > static_cast<int>(height))
        box.height = static_cast<int>(height) - box.y;
    return box;
}

// Screen-linear t is not world-linear; these identities stop the coil crawling and are defined
// once in chain_math.inl.
inline float worldParameterAt(float t, float nearViewZ, float farViewZ) {
    return chWorldParameter(t, nearViewZ, farViewZ);
}

inline float viewDepthAt(float t, float nearViewZ, float farViewZ) {
    return chViewDepth(t, nearViewZ, farViewZ);
}

inline float helixAngleAt(float worldParameter, float spanMetres,
                          float wavelength, float phaseRadians) {
    return chHelixAngle(worldParameter, spanMetres, wavelength, phaseRadians);
}

}  // namespace zonai_hookshot::pure
