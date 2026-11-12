// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support - the depth-correct world-draw seam.
// Copyright (C) HanSoloBlaster, afiaka87
//
// ONE place in the frame where mods draw solid geometry into the world, shared
// by every feature that needs it.
//
// The seam sits at the end of the game's solid pass, so anything drawn here is
// occluded by terrain, grass, props and Link exactly as world geometry is. That
// position is reached by hooking a single engine call site, and the drawing runs
// through a single primitive drawer under a single depth-state swap. All three
// are process-wide resources: a second copy of this machinery would install a
// second hook at the same address, open a second begin/end pair inside the
// first, and restore the engine's render state twice. So features do not each
// carry their own - they register a drawer here and the seam runs them in
// registration order inside one correctly-bracketed pass.
//
// Promoted out of porter-bridges-redux on 2026-08-21, when the Survey and the
// Porter Pack grid (which had shared this seam inside one mod) became separate
// mods that must also compose inside zonai-fieldworks.
//
// Threading: registration and configure() happen at module init on the game
// thread. Everything else runs on the RENDER thread inside the hook. A drawer
// callback therefore reads its feature's published frame and must not touch
// game-thread state directly.
#pragma once

#include <cstdint>

#include <gfx/seadColor.h>
#include <gfx/seadPrimitiveRenderer.h>

#include "totk/render/DrawOffsets.hpp"
#include "totk/render/PrimitiveGeometry.hpp"

namespace agl {
class DrawContext;
}

namespace sead {
class Camera;
class Projection;
}

namespace totk::render {

// Where the camera is this frame, recovered from the view matrix once and shared
// by every drawer. A false `valid` means the matrix was unusable; a drawer
// should fall back to distance-independent sizing rather than drawing at
// nonsense coordinates.
struct EyePoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool valid = false;
};

[[nodiscard]] float distanceTo(const EyePoint& eye, const Vec3& point);
[[nodiscard]] bool finite3(float x, float y, float z);

// Fades a colour for a settled line. The alpha goes through a square root, which
// keeps low-opacity lines readable instead of vanishing.
[[nodiscard]] sead::Color4f lineTint(const sead::Color4f& base, float alpha);

// ---------------------------------------------------------- the vertex path
//
// The primitive drawer's own vertex format: a position, an unused texture
// coordinate, and the per-vertex rate the shipped shader uses to lerp between a
// draw's two colours. Only the first rate component is read by that shader; the
// other three are written to match purely so a vertex is never partly stale.
struct PrimitiveVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float u = 0.0f, v = 0.0f;
    float rate[4]{};
};
static_assert(sizeof(PrimitiveVertex) == kPrimitiveVertexStride,
              "the drawer sizes its own buffer as 36 * count; this must match exactly");

// Everything one vertex-buffer draw needs, resolved fresh each frame exactly as
// the engine resolves its own - the manager and its ring are engine state, not
// constants, and caching them across a scene change would be a stale-pointer bug.
//
// The five entry points are stored as void* and cast at the call site inside the
// seam: their real signatures are graphics-driver detail that no consumer should
// have to name to hold this struct.
struct VertexDrawPath {
    std::uintptr_t ringHeader = 0;
    std::uint8_t* ringCpu = nullptr;
    std::uint64_t ringGpu = 0;
    void* commandBuffer = nullptr;
    std::uintptr_t widthContext = 0;
    void* alloc = nullptr;
    void* bindVertexBuffer = nullptr;
    void* bindUniformBuffer = nullptr;
    void* drawArrays = nullptr;
    void* setLineWidth = nullptr;
    bool valid = false;
};

// One pass's vertex block: where it lives in the drawer's ring, and how far into
// it the caller has written. There is no CPU-side scratch - this IS the upload
// buffer and the only copy.
struct VertexBlock {
    PrimitiveVertex* cpu = nullptr;
    std::uint32_t offset = 0;
    std::uint32_t bytes = 0;
    std::uint32_t capacity = 0;  // vertices
    std::uint32_t written = 0;
    bool valid = false;
};

// True once the ring has refused an allocation this frame. Everything after that
// point is silently missing from the picture, so a caller that cares reports it.
[[nodiscard]] bool ringExhausted(const VertexDrawPath& path);

// Reserves room for `vertexCount` vertices and binds it as THE vertex buffer for
// everything drawn until the next reservation. An invalid block means the ring
// had no room; the caller draws nothing rather than drawing wrong.
[[nodiscard]] VertexBlock reserveVertices(const VertexDrawPath& path,
                                          std::uint32_t vertexCount);

// One draw out of the bound block: however many consecutive vertices share a
// pair of colours and a line width. `lastWidth` lets a caller skip re-sending a
// width that has not changed - line width is fixed-function state and survives
// between draws.
bool submitRange(const VertexDrawPath& path, agl::DrawContext* drawContext,
                 std::uint32_t firstVertex, std::uint32_t vertexCount,
                 const sead::Color4f& startColor, const sead::Color4f& endColor,
                 float width, float& lastWidth, int primitive = kPrimitiveLines);

void writeVertex(VertexBlock& block, const Vec3& point, float rate);

// ------------------------------------------------------------- the seam
//
// Everything one registered feature gets for one frame.
struct WorldFrame {
    agl::DrawContext* drawContext = nullptr;
    sead::Camera* camera = nullptr;
    sead::Projection* projection = nullptr;
    sead::PrimitiveDrawer* drawer = nullptr;
    EyePoint eye{};
    // Null unless this feature registered with `needsVertexPath` AND the path
    // resolved this frame. A feature that can batch must always keep a working
    // immediate fallback for the frames where this is null.
    const VertexDrawPath* vertexPath = nullptr;
};

// A stretched sphere and a stretched cylinder between two world points, drawn
// through the engine's own primitive helpers. One call is one GPU draw, so these
// are the immediate path: correct everywhere, and the fallback for any feature
// whose batched path is unavailable this frame. `pixelWidth` is apparent
// thickness on screen, held roughly constant with distance.
bool drawSphereBar(const WorldFrame& frame, const Vec3& from, const Vec3& to,
                   float pixelWidth, const sead::Color4f& color);
bool drawCylinderBar(const WorldFrame& frame, const Vec3& from, const Vec3& to,
                     float pixelWidth, const sead::Color4f& color);

// Asked once per gameplay frame, BEFORE any state is touched. Returning false
// from every registered feature is what makes an idle frame free: the pass never
// opens, so there is no drawer acquisition, no depth swap and no begin/end.
//
// This also runs on every gameplay-layer frame regardless of outcome, so it is
// the right place for a feature's own once-a-second render-thread bookkeeping.
using WantsDrawFn = bool (*)();

// Draws this feature's geometry. Runs inside the seam's begin/end pair with the
// depth state already installed.
using DrawFn = void (*)(const WorldFrame& frame);

inline constexpr int kMaxWorldDrawers = 4;

// Resolves the primitive drawer symbols and the depth state, and installs the
// seam hook. Idempotent: the first caller does the work and every later caller
// gets the same answer, which is what lets composed features each call it at
// their own module init without ordering between them.
bool configure(std::uintptr_t mainBase);
[[nodiscard]] bool configured();

// Registers one feature's drawer. Registration order is draw order. `name` must
// outlive the process (a string literal); it appears in seam diagnostics. A null
// `wants` means the feature always draws, which is right only for a feature that
// is cheap to enter and wrong for anything that can be idle for minutes.
// Returns false if the seam is full or that name is already registered.
bool registerDrawer(const char* name, WantsDrawFn wants, DrawFn draw,
                    bool needsVertexPath = false);

// What the seam itself did, for a feature's own debug readout. Cleared by
// resetStats() so a caller can report per-window figures.
struct SeamStats {
    std::uint32_t gameplayFrames = 0;  // seam dispatches on the gameplay layer
    std::uint32_t openedPasses = 0;    // of those, frames a pass actually opened
    std::uint32_t idleFrames = 0;      // of those, frames nothing wanted to draw
    std::uint32_t refusedPasses = 0;   // wanted to draw, but drawer/state refused
    std::uint64_t gfxTicks = 0;        // state setup + begin/end, kernel ticks
    std::uint32_t gfxCalls = 0;
};
[[nodiscard]] SeamStats stats();
void resetStats();

}  // namespace totk::render
