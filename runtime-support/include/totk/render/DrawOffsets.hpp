// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support — world-draw offsets.
// Copyright (C) Clay Mullis
//
// TotK 1.2.1 / build 9B4E43650501A4D4 — the world-space immediate-line draw
// path, and the one string constant that path will not work without.
//
// This is the approved offset file for world drawing: raw addresses for the
// draw path belong here and nowhere else in the tree. Anything the shared
// adapters already publish comes from totk::engine::Totk121Offsets instead, so
// an address is never written twice.
//
// Promoted here from porter-bridges-redux on 2026-08-21. Its own note asked for
// exactly this: the same addresses are used identically by living-glue's
// RopeRenderer, zonai-hookshot's ChainRenderer, self-recall's RecallRenderer and
// the depth-correct seam, and the promotion was to ride with the world-line
// commonization (refactor plan 7.1/7.5). Splitting the Survey and the Porter
// Pack grid into separate mods is what forced it: they share one hook site and
// one primitive drawer, so the seam itself had to become shared code.
#pragma once

#include "totk/core/Units.hpp"

namespace totk::render {

// Call ORDER is part of the contract, not a suggestion: construct a graphics
// context, apply it to the draw context, begin the immediate pass with the
// camera and projection matrices, then issue lines. Skipping the context pair
// draws nothing.
struct DrawOffsets {
    static constexpr const char* kGameVersion = "1.2.1";
    static constexpr const char* kBuildId = "9B4E43650501A4D4";

    // sead::GraphicsContext::GraphicsContext — named in the symbol database,
    // 37 instructions, fills a 0x80 block of default render state.
    static constexpr totk::core::ImageOffset kGraphicsContextCtor{0x0074C19C};

    // sead::GraphicsContext::apply — pushes that state through the renderer.
    static constexpr totk::core::ImageOffset kGraphicsContextApply{0x00756A08};

    // Opens an immediate-mode pass against a camera/projection matrix pair.
    static constexpr totk::core::ImageOffset kBeginDrawImm{0x00DDC6AC};

    // One world-space line: two endpoints, a colour and a width.
    static constexpr totk::core::ImageOffset kDrawLineImm{0x00D977E8};

    // One world-space FILLED TRIANGLE: three corners and a colour. The symbol
    // database calls this `drawLineImm_0`; the name is wrong. Its own bytes read
    // three vertices through three pointers, upload them as a three-vertex
    // block, and issue graphics primitive 4 (triangles) — and the retail memory
    // graph uses it for filled bars with the line helper for their outlines.
    //
    // Its arguments are pointers, exactly like the line helper's, so declaring
    // it with reference parameters matches the real calling convention rather
    // than guessing at how a by-value vector would be passed.
    static constexpr totk::core::ImageOffset kDrawTriangleImm{0x021F6350};

    // ---------------------------------------------------------------------
    // The three calls `drawLineImm` is actually made of.
    //
    // `drawLineImm` is a convenience wrapper, not the
    // machinery: it stages two vertices and issues its own draw, which is why a
    // dense Survey costs one GPU draw call per line. Both helpers underneath it
    // take a COUNT, so submitting many lines in one draw needs no new engine
    // capability — only these three entry points and the two data slots below.
    //
    // Read off the disassembly of `drawLineImm` itself (0x00D977E8), in the
    // order it calls them.

    // sead uniform-block write. Called (program, drawCtx, slot, data). The line
    // helper passes slot 3 with a 16-byte colour, and the block is bound for the
    // whole draw — which is the reason one batch carries exactly one colour.
    static constexpr totk::core::ImageOffset kSetUniformBlock{0x0096D8A0};

    // The vertex upload. Called (program, drawCtx, slot, vec3Array, count),
    // 12-byte stride in, padded to vec4 on the way to the uniform block.
    //
    // ⚠ ITS STAGING BUFFER IS 4096 BYTES (`SUB SP, SP, #1,LSL#12`) AND IT DOES
    // NOT CLAMP `count`. 256 vertices is therefore a hard ceiling imposed by the
    // GAME's stack frame, not by us. `pure::kMaxBatchVertices` holds that line;
    // never raise either one without re-reading this function.
    static constexpr totk::core::ImageOffset kSetVertexBlock{0x021F4FB8};

    // Line width, in PIXELS. A five-instruction wrapper that reloads its own
    // first argument from the draw context and tail-calls the graphics driver,
    // so it is safe to call directly and keeps the driver pointer out of our
    // hands.
    static constexpr totk::core::ImageOffset kSetLineWidth{0x021F5078};

    // The engine's phase-0 opaque-pass extension point. A RenderBuffer is bound
    // across this callback, but draws here do not survive into final colour.
    //
    //   engine::graphics::ModelScenePassCallback::drawModelSceneOpa
    //   (self, agl::DrawContext* drawCtx, int phase,
    //    gsys::ModelScene* scene, gsys::ModelSceneContext* viewContext)
    //
    // gsys::ModelScene::drawOpa @ 0x00A42694 ends like this:
    //
    //   0x00A43198  draw the last opaque queue (22)
    //   0x00A431D8  call THIS through vtable +0x78, with phase in W2 = 0
    //   0x00A431F8  unbind the render target
    //   0x00A43208  release the render-buffer context
    //
    // A draw issued after the callback's own body therefore lands before this
    // particular target unbind. That establishes target lifetime only, not
    // survival into the presented picture.
    //
    // The outer per-step dispatch in agl::lyr::RenderDisplay::drawRenderStep
    // (0x00818EF0). That callback runs after the layer's draw delegate has
    // RETURNED — after drawOpa above has already unbound its targets. Sharing a
    // layer, a render step and a draw context does not make a target bound.
    static constexpr totk::core::ImageOffset kDrawModelSceneOpa{0x0248B4E4};

    // The second scene seam, and the later of the two.
    //
    //   gsys::ModelSceneContext::drawQueue(ModelSceneContext*, agl::DrawContext*, int queue)
    //
    // gsys::ModelScene::drawXlu @ 0x00D2E32C draws the translucent world like
    // this:
    //
    //   0x00D2E5EC  gsys::ModelRenderContext::bindDefaultFrameBuffer
    //   0x00D2E608  drawQueue(… , 23)
    //   0x00D2E618  drawQueue(… , 24)   <- the last translucent queue
    //   0x00D2E638  unbind the render buffer
    //
    // Queue 24 is the last thing the world itself draws before the buffer is
    // let go, and translucent geometry is drawn there with exactly the state
    // the survey wants: depth tested, depth not written. Hooking the queue
    // function and acting only on queue 24 puts our lines at the end of that
    // list.
    //
    // ⚠ ONE CLAIM ABOUT THIS SEAM IS NOT TRUE AND MUST NOT BE REPEATED: that it
    // reaches a different, "default" frame buffer than the opaque seam above.
    // bindDefaultFrameBuffer binds `context + 0x540` through the viewport at
    // `context + 0x548` — the SAME two fields gsys::ModelScene::drawOpa binds at
    // 0x00A43120 before the phase-0 callback. The seams differ in when they run,
    // not in which target they use.
    static constexpr totk::core::ImageOffset kDrawQueue{0x00A43BE4};
};

// Names carried by the version-matched SDK symbol table for TotK 1.2.1.
struct PrimitiveSymbols {
    static constexpr const char* kRendererInstance = "sead::PrimitiveRenderer::instance";
    static constexpr const char* kBegin = "sead::PrimitiveDrawer::begin";
    static constexpr const char* kEnd = "sead::PrimitiveDrawer::end";
    static constexpr const char* kSetDrawCtx = "sead::PrimitiveDrawer::setDrawCtx";
    static constexpr const char* kSetCamera = "sead::PrimitiveDrawer::setCamera";
    static constexpr const char* kSetProjection = "sead::PrimitiveDrawer::setProjection";
    static constexpr const char* kSetModelMtx = "sead::PrimitiveDrawer::setModelMtx";
    static constexpr const char* kDrawSphere8x16 = "sead::PrimitiveDrawer::drawSphere8x16";
    static constexpr const char* kDrawCylinder16 = "sead::PrimitiveDrawer::drawCylinder16";
};

// ---------------------------------------------------------------------------
// Reaching the immediate-draw shader program and the driver's draw entry.
//
// ⚠ BOTH SLOTS BELOW ARE DOUBLE-DEREFERENCE SLOTS, verified on the disassembly
// rather than on decompiler output, which renders them as if they were single:
//
//   ADRP X8, off_46381D8 ; LDR X8, [X8, #off] ; LDR X8, [X8]   <- two loads
//
// So the module is `*(uintptr*)(*(uintptr*)(mainBase + slot))`. Getting this
// depth wrong reads a pointer-shaped lie and faults inside the draw.
inline constexpr std::uintptr_t kImmDrawModuleSlot = 0x046381D8;

// Inside that module: a program count, then the program table.
// `drawLineImm` refuses to run when the count is below 0x3D, and this mod
// declines to draw rather than reproducing what the engine does next — with a
// null program the engine's own uniform write would fault on a null read.
inline constexpr int kImmModuleProgramCount = 0x08;   // u32
inline constexpr int kImmModuleProgramTable = 0x10;   // pointer
inline constexpr unsigned int kImmProgramCountMin = 0x3D;
inline constexpr int kImmLineProgram = 0x1E0;         // the line/triangle/point program

// The uniform slots the line program reads: colour, then the vertex array.
inline constexpr unsigned int kImmColourSlot = 3;
inline constexpr unsigned int kImmVertexSlot = 4;

// The first argument the engine hands its line-width wrapper. The wrapper
// ignores it — it reloads the command buffer from the draw context — but it is
// resolved and passed faithfully so a future build that starts using it cannot
// silently receive garbage.
inline constexpr std::uintptr_t kLineWidthContextSlot = 0x0462EF90;

// The graphics driver's draw-arrays entry, called (commandBuffer, mode, first,
// count). The command buffer is a field on the draw context. `mode` is the
// driver's primitive enum: the line helper passes 1, the triangle helper 4 and
// the point helper 0, which pins LINES = 1 without guessing.
inline constexpr std::uintptr_t kDrawArraysSlot = 0x04616F28;
inline constexpr int kDrawContextCommandBuffer = 0xB8;
inline constexpr int kPrimitiveLines = 1;
// `NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP` from the vendored 1.2.1 SDK headers.
inline constexpr int kPrimitiveTriangleStrip = 5;

inline constexpr int kDrawerWithinRenderer = 0x08;
inline constexpr std::uintptr_t kPrimitiveDrawMgrSlot = 0x0463CD48;
inline constexpr int kDrawMgrBlockExhausted = 1880;
inline constexpr int kDrawMgrCapacity = 1888;

// ---------------------------------------------------------------------------
// The NATIVE VERTEX-BUFFER draw path.
//
// The immediate line helper above holds its vertices in a shader uniform array
// that is exactly three entries deep, so asking it for a multi-line draw returns
// a blank screen rather than an error. `sead::PrimitiveDrawMgrNvn` is a different animal: a real
// vertex-buffer pipeline that takes an ARBITRARY vertex count. This mod already
// brings that drawer up, already ships its shader asset, and already draws the
// Porter Pack grid through it — so nothing new has to be created.
//
// `sead::PrimitiveDrawMgrNvn::beginImpl @ 0x021C2C90` binds, on drawCtx+0xB8:
//     BindProgram(cb, manager + 8, stageMask 31)
//     BindVertexAttribState(cb, 3, manager + 440)
//     BindVertexStreamState(cb, 1, manager + 456)
//     BindUniformBuffer(cb, stage 0, index 0, ring + off, 64)   <- wvp
// so one `PrimitiveDrawer::begin()` leaves a complete position/colour pipeline
// bound. `drawImpl_ @ 0x021C2DF0` then shows the whole per-draw recipe:
//     BindVertexBuffer(cb, 0, bufferAddress, 36 * vertexCount)
//     ring.alloc(256) -> model Matrix34f | constant 4th row | color0 | color1
//     BindUniformBuffer(cb, stage 0, index 1, ring + off, 96)
//     DrawElements/DrawArrays
//
// Restoring state afterwards is FREE: `drawImpl_` re-binds the vertex buffer and
// the index-1 uniform block on every single shape, so the only two things we
// disturb are the two the next shape draw sets again anyway.

// The manager's uniform-ring bump allocator, called (ringHeader, byteCount) and
// returning a byte offset into the ring. `ringHeader` is `manager + 1880`, whose
// fields are the four below.
//
// ⚠ IT APPLIES NO ALIGNMENT OF ITS OWN. The engine's own callers only ever ask
// for 256 or 512 bytes, which keeps the ring 256-aligned by construction. NVN
// uniform buffers REQUIRE 256-byte alignment, so an unrounded request would
// silently misalign every later uniform bind in the frame instead of failing.
// Round every request up to kPrimitiveUniformAlign.
inline constexpr std::uintptr_t kPrimitiveRingAlloc = 0x021C08E0;

// Byte offsets into sead::PrimitiveDrawMgrNvn, read out of beginImpl/drawImpl_.
inline constexpr int kDrawMgrRingBuffer = 1824;   // an INLINE NVNbuffer object
inline constexpr int kDrawMgrRingCpuBase = 1872;  // void*, the ring's CPU base
inline constexpr int kDrawMgrRingCursor = 1884;   // u32, the atomic bump cursor
inline constexpr int kDrawMgrRingRetire = 1892;   // u32, the retired-frame base

// The attrib and stream states `beginImpl` binds: `BindVertexAttribState(cb, 3,
// mgr + 440)` and `BindVertexStreamState(cb, 1, mgr + 456)`. Both are built at
// runtime from the shipped shader binary, so the stride is REPORTED once from
// here rather than asserted — two independent sources already say 36.
inline constexpr int kDrawMgrVertexAttribState = 440;
inline constexpr int kDrawMgrVertexStreamState = 456;

// Non-zero when the drawer is in its textured mode, in which `drawImpl_` writes
// a LARGER uniform layout (512 bytes, extra UV fields and a texture handle bound
// at stage 1). This build refuses to draw rather than guessing that layout.
inline constexpr int kDrawMgrTexturedMode = 1900;

// ⚠ DOUBLE-DEREFERENCE SLOTS, like kDrawArraysSlot above. Each holds the address
// of an entry in the binary's own named `pfnc_nvn*` dispatch table, which the
// driver fills in at boot; so the call is `**(fn**)(mainBase + slot)`. Verified
// by reading each slot's stored value and matching it against the version-matched
// symbol table rather than by trusting decompiler output:
//     0x04616F10 -> 0x04670BF8  pfnc_nvnBufferGetAddress
//     0x04616FD8 -> 0x04671398  pfnc_nvnCommandBufferBindVertexBuffer
//     0x04617088 -> 0x046713A8  pfnc_nvnCommandBufferBindUniformBuffer
//     0x04616F28 -> 0x04671460  pfnc_nvnCommandBufferDrawArrays  (kDrawArraysSlot)
inline constexpr std::uintptr_t kNvnBufferGetAddressSlot = 0x04616F10;
inline constexpr std::uintptr_t kNvnBindVertexBufferSlot = 0x04616FD8;
inline constexpr std::uintptr_t kNvnBindUniformBufferSlot = 0x04617088;

// The vertex the primitive drawer's program reads: position, an unused texture
// coordinate, then a per-vertex rate that lerps between the draw's two colours.
// Pinned twice over — the binary sizes its own buffer `36 * count`, and sead's
// published `primitive_renderer.vert` declares exactly
// `vec3 Vertex; vec2 TexCoord0; vec4 ColorRate`.
inline constexpr int kPrimitiveVertexStride = 36;

// The per-draw block: a Matrix34f promoted to 4x4, then two colours.
inline constexpr int kPrimitiveDrawUniformBytes = 96;
inline constexpr int kPrimitiveUniformAlign = 256;
inline constexpr int kPrimitiveVertexStage = 0;
inline constexpr int kPrimitiveModelUniformIndex = 1;

// The constant 4th row `drawImpl_` appends when promoting the Matrix34f, read
// out of the image at 0x037FB320: (0, 0, 0, 1). Confirming it is what lets this
// mod pass an IDENTITY model matrix with confidence — identity is identity in
// every row/column convention, so the shader's `vec4(Vertex,1) * user * wvp`
// cannot be got wrong by us. World coordinates go straight into the buffer.
inline constexpr float kPrimitiveMatrixLastRow[4] = {0.0f, 0.0f, 0.0f, 1.0f};

// The engine's extension broadcast after
// opaque queue 22 and immediately before the colour/depth target is released.
inline constexpr std::uintptr_t kOpaquePassEndBroadcast = 0x02AEFE74;

// The last translucent queue, drawn at 0x00D2E618. Every other call to
// drawQueue passes a different index, so this is the whole filter.
inline constexpr int kTranslucentQueue = 24;

// The render buffer a scene context draws into, and the viewport it uses.
// Both seams bind exactly these. A null target explains a missing draw and is
// logged once at each seam.
inline constexpr int kSceneContextRenderBuffer = 0x540;

// The phase argument the opaque-pass callback is given at the site above. The
// same function is dispatched again later in drawOpa with phase 15 for the
// engine's occlusion queries.
inline constexpr int kOpaquePassPhase = 0;

// Byte offsets into gsys::ModelSceneContext (stride 0x19F0), read out of
// gsys::ModelScene::calcFrame_ @ 0x00974D9C, which indexes exactly these two.
inline constexpr int kSceneContextViewIndex = 0x08;  // u8
inline constexpr int kSceneContextLayer = 0x5F0;     // agl::lyr::Layer*

// Byte offsets into agl::lyr::Layer, and the rule for choosing between them.
//
// These are not guessed: agl::lyr::RenderDisplay::drawRenderStep fills the
// camera and projection fields of every RenderInfo it hands out with exactly
// this selection, and those are the two pointers this mod's draw path has
// always consumed. Reproducing it here is what lets the draw move to a seam
// that has no RenderInfo of its own.
//
//   flags = *(u16*)(layer + 0x8A)
//   if ((flags & 0x21) && (flags & 0x02) && (override = *(void**)(layer + 0x1F0)))
//       camera = override + 0x08;  projection = *(void**)(override + 0x160)
//   else
//       camera = *(void**)(layer + 0x78);  projection = *(void**)(layer + 0x80)
//
// The engine substitutes two module globals when the layer's own pointers are
// null. This mod declines to draw instead, and says so in the log once — a
// gameplay layer with no camera is a situation we would want to hear about
// rather than paper over.
inline constexpr int kLayerCamera = 0x78;
inline constexpr int kLayerProjection = 0x80;
inline constexpr int kLayerViewSelectFlags = 0x8A;
inline constexpr int kLayerViewOverride = 0x1F0;
inline constexpr int kViewOverrideCamera = 0x08;
inline constexpr int kViewOverrideProjection = 0x160;
inline constexpr unsigned short kViewOverrideAvailable = 0x0021;
inline constexpr unsigned short kViewOverrideEnabled = 0x0002;

// Byte offsets into the sead::GraphicsContext block the draw path constructs.
//
// Read out of `sead::GraphicsContext::apply` itself rather than from a header:
// apply indexes the block as raw bytes, and the indices it uses pin the layout
// exactly. It passes byte 0 to the "enable depth test" setter, byte 1 to
// "enable depth write", byte 100 to "set depth comparison", and byte 109 to
// "enable polygon offset". Those match the field order in sead's own Gfx
// header, so both sources agree.
//
// The comparison value is an ordinary graphics-API enum and its meaning IS
// knowable. `apply` passes byte 100 through UNCHANGED to the depth-comparison
// setter — no table, no remap — so the value the game stores is the value the
// graphics API receives, and the local API header defines that enum as
// 1 never, 2 less, 3 equal, 4 less-or-equal, 5 greater, 6 not-equal,
// 7 greater-or-equal, 8 always. The constructor's default of 4 is therefore
// "nearer or equal wins", which is what the survey wants.
inline constexpr int kGcDepthTestEnable = 0x00;
inline constexpr int kGcDepthWriteEnable = 0x01;
inline constexpr int kGcDepthFunc = 0x64;
inline constexpr int kGcPolygonOffsetEnable = 0x6D;
inline constexpr int kGraphicsContextSize = 128;

// "Nearer or equal wins" — the constructor's own default, restated here so the
// renderer names what it is asking for instead of relying on the default.
inline constexpr unsigned char kGcDepthFuncNearerOrEqual = 4;

// Two diagnostic comparisons, from the same enum documented above. "Farther"
// draws only what is currently BEHIND something, which is how a boot can tell a
// missing line from a hidden one.
inline constexpr unsigned char kGcDepthFuncFarther = 5;
inline constexpr unsigned char kGcDepthFuncAlways = 8;

// The gameplay 3D layer. There is NO layer named "3D" — drawing against that
// name silently produces nothing at all, which cost Living Glue an entire
// version. Treat this string as load-bearing as the addresses above.
inline constexpr const char* kGameplayLayer = "Main_3D_0";

}  // namespace totk::render
