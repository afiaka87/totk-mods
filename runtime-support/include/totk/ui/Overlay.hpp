// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support — the pluggable on-screen overlay.
// Banner/HUD logic Copyright (C) Clay Mullis
// StealHeapHook derived from totk-lotuskit (aquacluck and contributors), GPLv2.
//
// A drop-in module any exlaunch/subsdk9 mod uses to draw its own text on-screen
// in gameplay on Eden. It sits on top of the low-level sead::TextWriter draw path
// (totk/ui/TextWriter.hpp, GPLv2) and hides the plumbing behind a few calls:
//
//     overlay::installHooks();                              // once, from exl_main
//     overlay::tick();                                      // once per frame (npad tick)
//     overlay::showBanner(l1, l2, ttlTicks, color);         // top-right TTL'd notice
//     overlay::showChargeMeter(name, steps, thresh, full);  // persistent meter
//     overlay::hud::begin(); overlay::hud::linef(...);      // bottom-left DEBUG HUD (gated)
//
// Two boot-proven properties:
//   * NO FLICKER — every visible element is ONE long-lived multi-line drawlist node
//     re-queued each npad tick; the render hook paints the queue on whichever layer
//     composites last, so nothing depends on a sub-framerate debug pass.
//   * self-contained — the mod owns no heap; the overlay yoinks a live game heap at
//     boot (StealHeapHook) and lazily builds its frame heap on the first tick.
//
// The DEBUG HUD is the debug panel: gated behind OVERLAY_DEBUG_HUD (default 0).
// When off it compiles to nothing, so a ship build never needs debug code stripped
// out — flip the flag in the mod's CMake to bring it back for a calibration session.
//
// Pure layout policy (measurement, anchors, pips, HUD assembly) lives in
// totk/ui/OverlayLayout.hpp and is host-tested; this module owns hooks, heaps,
// and per-tick sequencing only.

#pragma once

#include "totk/ui/TextWriter.hpp"  // low-level sead::TextWriter draw path (GPLv2)
#include "totk/ui/OverlayLayout.hpp"
#include <gfx/seadColor.h>

// Default OFF: a ship build carries no debug HUD. Define OVERLAY_DEBUG_HUD=1 (via the
// mod's CMake) to compile + render the HUD for a calibration session.
#ifndef OVERLAY_DEBUG_HUD
#define OVERLAY_DEBUG_HUD 0
#endif

namespace overlay {

    // Override agl's primitive-drawer uniform ring before its backing allocation
    // is created. Values must satisfy the pure alignment/bounds policy. Valid
    // only before installHooks()/the first tick; zero never means default.
    bool configurePrimitiveUniformBuffer(std::uint32_t bytes);

    // Substitute a consumer's calibrated banner/HUD appearance for the canonical one
    // (finishing-touches addendum §3). Valid ONLY before installHooks()/the first
    // tick(); thereafter the presentation is immutable. Fail-closed: an insane
    // configuration (per totk::ui::isSanePresentation) or a late call leaves the
    // canonical presentation active, logs one bounded diagnostic, and returns false.
    // Scalar data is copied into static storage — no caller pointer is retained.
    // Buffer values are logical caps clamped by the canonical compile-time storage.
    bool configurePresentation(const totk::ui::OverlayPresentation& presentation);

    // Install every overlay hook: the boot-time heap steal + agl debug-drawer init, and the
    // two per-frame render hooks (which no-op until the frame heap exists). Call once from exl_main.
    // Mod-specific world-draw consumers register AFTER this via
    // lotuskit::DebugDrawHooks::setWorldDrawCallback / setNativeWorldDrawCallback.
    void installHooks();

    // Per-frame service — call once per npad tick, BEFORE any early-return in the mod's tick, so
    // toasts always age and the frame heap is always reclaimed. Lazily creates the frame heap on the
    // first tick after the heap steal, re-queues the live banner, and (if OVERLAY_DEBUG_HUD) the HUD.
    void tick();

    // Hide or show all overlay text without disabling native/world draw callbacks.
    // The tick service continues reclaiming frame memory and aging notices while
    // hidden, so visibility can be toggled safely during gameplay.
    void setTextVisible(bool visible);
    bool textVisible();

    // Arm a TOP-RIGHT corner notice for `ttlTicks` npad ticks (~40 Hz on Eden, so ttl 200 ≈ 5 s). The text
    // block is right-aligned to the screen's right edge (Skyrim-style). `line2` may be null for a single line.
    // Re-arming replaces the current notice.
    void showBanner(const char* line1, const char* line2, int ttlTicks, const sead::Color4f& color);

    // Same notice using the presentation's configured default color (canonical: pure
    // white). Call sites that historically relied on a mod-local default color use
    // this overload instead of restating the color at every call.
    void showBanner(const char* line1, const char* line2, int ttlTicks);

    // Persistent charge meter on its own drawlist, so charging cannot replace the
    // notice or the debug HUD. The input is plain text only; no caller-controlled
    // printf string reaches TextWriter.
    void showChargeMeter(const char* friendlyName, unsigned steps, unsigned threshold, bool full);
    void hideChargeMeter();

#if OVERLAY_DEBUG_HUD
    // DEBUG HUD (bottom-left): refill it each tick with begin() then one linef() per line. The
    // buffer is rendered (one frame lagged — harmless) by the next tick(). Compiles to nothing when
    // OVERLAY_DEBUG_HUD is 0, so HUD call sites can stay in the mod source year-round behind the same
    // #if and never need stripping for a ship build.
    // Contract: begin(), the linef()s, and end() must ALL run on the tick thread, in that order. end()
    // publishes every line's screen position so the render thread never measures a half-built HUD.
    namespace hud {
        void begin();
        void linef(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
        void end();
    }
#endif

} // namespace overlay
