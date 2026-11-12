// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) HanSoloBlaster, afiaka87
// Engine-free overlay layout from boot-proven logic; change no constant without a new in-game observation.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace totk::ui {

// ── calibrated tuning (current-truth values; provenance in the behavior matrix) ──
namespace tuning {
    // Overlay space is a fixed 1280x720; the engine upscales for 1080p.
    inline constexpr float kScreenW = 1280.0f;
    inline constexpr float kScreenH = 720.0f;

    // Banner: scale 1.40, glyph advance 8.5 and margin 0 are boot-calibrated; change only on a new observation.
    inline constexpr float  kBannerScale   = 1.40f;
    inline constexpr float  kBannerCharW   = 8.5f;
    inline constexpr float  kBannerTopY    = 16.0f;
    inline constexpr float  kBannerRightMargin = 0.0f;
    inline constexpr std::size_t kBannerBuf = 384;   // 3-line White Whale test cards

    // Debug HUD (bottom-left, compile-gated). 15.7 is the measured per-line height
    // in 720-space at scale 1.0 (screenshot-confirmed 2026-07-09); 2048 is ~2x the
    // largest menu so tail lines are never silently squeezed.
    inline constexpr float  kHudScale        = 1.10f;
    inline constexpr float  kHudLineH        = 15.7f;
    inline constexpr float  kHudX            = 8.0f;
    inline constexpr float  kHudBottomMargin = 14.0f;
    inline constexpr std::size_t kHudBuf      = 2048;
    inline constexpr std::size_t kHudMaxLines = 24;
    inline constexpr std::size_t kHudLineBuf  = 224;  // per-node line clip in the split loop

    // Charge meter (independent drawlist below the notice).
    inline constexpr float  kChargeScale = 0.95f;
    inline constexpr float  kChargeX     = 810.0f;
    inline constexpr float  kChargeY     = 63.0f;
    inline constexpr std::size_t kChargeBuf = 128;

    // TextWriter draw-node default scale (0.665 @1080p = 1.0 @720p; 0.8 is the
    // smallest legible size after re-upscaling).
    inline constexpr float kDefaultDrawScale = 0.8f;

    // Toast slots (lotuskit heritage; no scrolling, staggered TTLs only).
    inline constexpr std::size_t kMaxToasts = 0x20;

    // agl's primitive drawer consumes one 256-byte uniform allocation at begin
    // and one per submitted shape. The retail default is 0x32000 bytes. Consumers
    // may raise it before hook installation, but the backing allocation must stay
    // bounded and aligned to the allocator's actual block size.
    inline constexpr std::uint32_t kPrimitiveUniformBlockBytes = 256;
    inline constexpr std::uint32_t kPrimitiveUniformDefaultBytes = 0x32000;
    // A worst-case four-sided Porter Survey beam frame needs about 8.2 MiB.
    // Four in-flight frames plus shared primitive slack remain bounded below
    // this ceiling; callers still have to request an aligned explicit size.
    // ⚠ RAISED 2026-08-18 (porter-bridges-redux v0.54.0): was 32 MiB, which the
    // Survey's own frame bound had grown to meet. This is a BOUND, not an
    // allocation: no consumer's memory changes unless it asks for more. The
    // backing ring is carved out of the game's debug-drawer heap at boot by
    // `agl::init_debug_drawers`, so a consumer that raises its request past what
    // that heap can serve fails at boot rather than at draw time.
    inline constexpr std::uint32_t kPrimitiveUniformMaxBytes = 48u * 1024u * 1024u;
} // namespace tuning

inline bool isSanePrimitiveUniformBufferBytes(std::uint32_t bytes) {
    return bytes >= tuning::kPrimitiveUniformDefaultBytes &&
           bytes <= tuning::kPrimitiveUniformMaxBytes &&
           bytes % tuning::kPrimitiveUniformBlockBytes == 0;
}

// ── presentation configuration (finishing-touches addendum §3) ──────────────────
// Plain engine-free layout data a consumer may substitute for the canonical
// banner/HUD appearance. This is configuration, not a theme system: draw-list
// selection, heap size, hook addresses, lock policy, and font paths are NOT here.
// The engine facade (overlay::configurePresentation) accepts one of these before
// hook installation / the first frame and never again; invalid values fail closed
// to the canonical defaults. Buffer values are LOGICAL caps: static storage in the
// engine layer stays at the canonical compile-time size, so a configured buffer
// may shrink but never grow past kBannerBuf / kHudBuf.

struct Rgba {
    float r, g, b, a;
};

struct BannerPresentation {
    float scale;
    float glyphAdvance;
    float topY;
    float rightMargin;
    std::size_t bufferBytes;
    Rgba defaultColor;
};

struct HudPresentation {
    float scale;
    float lineHeight;
    float x;
    float bottomMargin;
    std::size_t bufferBytes;
    std::size_t maxLines;
    Rgba color;
};

struct OverlayPresentation {
    BannerPresentation banner;
    HudPresentation hud;
};

// The canonical appearance - exactly the calibrated tuning constants above.
constexpr OverlayPresentation canonicalPresentation() {
    return OverlayPresentation{
        BannerPresentation{
            tuning::kBannerScale,
            tuning::kBannerCharW,
            tuning::kBannerTopY,
            tuning::kBannerRightMargin,
            tuning::kBannerBuf,
            Rgba{1.0f, 1.0f, 1.0f, 1.0f},
        },
        HudPresentation{
            tuning::kHudScale,
            tuning::kHudLineH,
            tuning::kHudX,
            tuning::kHudBottomMargin,
            tuning::kHudBuf,
            tuning::kHudMaxLines,
            Rgba{1.0f, 1.0f, 1.0f, 1.0f},
        },
    };
}

inline bool isSaneColor(const Rgba& c) {
    const float parts[4] = {c.r, c.g, c.b, c.a};
    for (float v : parts) {
        if (!std::isfinite(v) || v < 0.0f || v > 1.0f) { return false; }
    }
    return true;
}

// Fail-closed validation. Bounds are deliberately generous around the calibrated
// values - they reject nonsense (non-finite, off-screen, zero/oversized buffers,
// impossible line limits), not taste.
inline bool isSanePresentation(const OverlayPresentation& p) {
    const BannerPresentation& b = p.banner;
    if (!std::isfinite(b.scale) || b.scale <= 0.0f || b.scale > 8.0f) { return false; }
    if (!std::isfinite(b.glyphAdvance) || b.glyphAdvance <= 0.0f || b.glyphAdvance > 64.0f) { return false; }
    if (!std::isfinite(b.topY) || b.topY < 0.0f || b.topY >= tuning::kScreenH) { return false; }
    if (!std::isfinite(b.rightMargin) || b.rightMargin < 0.0f || b.rightMargin >= tuning::kScreenW) { return false; }
    if (b.bufferBytes < 16 || b.bufferBytes > tuning::kBannerBuf) { return false; }
    if (!isSaneColor(b.defaultColor)) { return false; }

    const HudPresentation& h = p.hud;
    if (!std::isfinite(h.scale) || h.scale <= 0.0f || h.scale > 8.0f) { return false; }
    if (!std::isfinite(h.lineHeight) || h.lineHeight <= 0.0f || h.lineHeight > 64.0f) { return false; }
    if (!std::isfinite(h.x) || h.x < 0.0f || h.x >= tuning::kScreenW) { return false; }
    if (!std::isfinite(h.bottomMargin) || h.bottomMargin < 0.0f || h.bottomMargin >= tuning::kScreenH) { return false; }
    if (h.bufferBytes < 16 || h.bufferBytes > tuning::kHudBuf) { return false; }
    if (h.maxLines < 1 || h.maxLines > tuning::kHudMaxLines) { return false; }
    if (!isSaneColor(h.color)) { return false; }
    return true;
}

// ── banner measurement and anchoring (matrix O1-O3) ─────────────────────────────

// Longest DISPLAY line of a composed (possibly multi-line) text, in characters.
// White Whale v0.1.3: measuring the whole string as one line made multi-line cards
// wider than the screen and pushed the anchor off-screen left ("cards never appear").
inline int measureLongestDisplayLine(const char* text) {
    int maxChars = 0;
    int run = 0;
    for (const char* p = text; ; ++p) {
        if (*p == '\n' || *p == '\0') {
            if (run > maxChars) { maxChars = run; }
            run = 0;
            if (*p == '\0') { break; }
        } else {
            ++run;
        }
    }
    return maxChars;
}

// Display line count of a composed text: 1 + embedded newlines (1 for empty text,
// matching the donor hud::end()).
inline int countDisplayLines(const char* text) {
    int lines = 1;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '\n') { ++lines; }
    }
    return lines;
}

// Top-left X of a right-aligned text block. The clamp keeps an over-long line
// clipping at the right edge instead of vanishing off-screen left.
inline float bannerLeftX(int maxChars, float charW, float scale,
                         float screenW, float rightMargin) {
    const float blockW = (float)maxChars * charW * scale;
    float x = screenW - rightMargin - blockW;
    if (x < 0.0f) { x = 0.0f; }
    return x;
}

// Left edge that places a natively measured text rectangle around an exact
// horizontal anchor.
inline float centeredTextLeftX(float anchorX, float measuredWidth) {
    return anchorX - measuredWidth * 0.5f;
}

// DebugFont is monospace. Its 8.5-pixel base advance is already the
// boot-calibrated banner metric; use it for centered draw nodes because TotK
// does not export TextWriter::calcFormatStringRect to subsdk modules.
inline float monospaceTextWidth(const char* text, float glyphAdvance, float scale) {
    return static_cast<float>(measureLongestDisplayLine(text)) * glyphAdvance * scale;
}

// ── draw-node scale rule (matrix T12; donor TextWriter::drawFrame) ──────────────
// requested 0 → the calibrated default; requested -1 → keep the writer's current
// scale; anything else → the requested value.
inline float resolveDrawScale(float requested, float current) {
    if (requested == 0.0f) { return tuning::kDefaultDrawScale; }
    if (requested == -1.0f) { return current; }
    return requested;
}

// ── charge meter text (matrix O4) ───────────────────────────────────────────────
// ASCII on purpose: avoids unsupported glyphs and the historical '%' format
// artifact. Pips render at most 5 cells ("[#]" x5 fills the 16-byte pip buffer);
// steps clamp to threshold; threshold 0 is treated as 1. Output is truncated to
// cap-1 and always NUL-terminated. Returns the number of characters written.
inline std::size_t buildChargeMeterText(char* out, std::size_t cap,
                                        const char* friendlyName,
                                        unsigned steps, unsigned threshold,
                                        bool full) {
    if (cap == 0) { return 0; }
    if (threshold == 0) { threshold = 1; }
    if (steps > threshold) { steps = threshold; }

    char pips[16] = {};
    unsigned at = 0;
    for (unsigned i = 0; i < threshold && at + 3 < sizeof(pips); ++i) {
        pips[at++] = '[';
        pips[at++] = i < steps ? '#' : '.';
        pips[at++] = ']';
    }
    pips[at] = '\0';

    // ">--<pips> <name>[ READY]" - same composition as the donor SNPrintf.
    std::size_t n = 0;
    auto append = [&](const char* s) {
        while (*s != '\0' && n + 1 < cap) { out[n++] = *s++; }
    };
    append(">--");
    append(pips);
    append(" ");
    append(friendlyName != nullptr ? friendlyName : "relic");
    if (full) { append(" READY"); }
    out[n] = '\0';
    return n;
}

// ── debug HUD text assembly (matrix O6-O7) ──────────────────────────────────────

// Append one already-formatted line to the HUD buffer. Exact donor hud::linef
// semantics: a full buffer drops the line whole (returns false); otherwise lines
// are joined with '\n' and the copy truncates to the remaining space.
inline bool hudAppendLine(char* buf, std::size_t cap, const char* line) {
    std::size_t len = 0;
    while (buf[len] != '\0') { ++len; }
    if (len + 2 >= cap) { return false; }               // buffer full - drop the line
    if (len > 0) { buf[len++] = '\n'; buf[len] = '\0'; }
    std::size_t i = 0;
    for (; line[i] != '\0' && len + i + 1 < cap; ++i) { buf[len + i] = line[i]; }
    buf[len + i] = '\0';
    return true;
}

// Copy the next display line (up to outCap-1 chars) from *cursor into out and
// advance the cursor. A line longer than outCap-1 is split: the remainder becomes
// the next line (exact donor tick() behavior - it costs an extra HUD slot rather
// than dropping text). Returns the number of characters copied.
inline std::size_t hudNextLine(const char*& cursor, char* out, std::size_t outCap) {
    std::size_t n = 0;
    while (cursor[n] != '\0' && cursor[n] != '\n' && n < outCap - 1) {
        out[n] = cursor[n];
        ++n;
    }
    out[n] = '\0';
    cursor += n;
    if (*cursor == '\n') { ++cursor; }
    return n;
}

// Publish per-line screen Ys for a bottom-anchored HUD block. Every slot gets a
// value (render callbacks index the whole array); lineCount clamps to slotCount.
// Exact donor hud::end() math.
inline void hudLineYs(float* ys, std::size_t slotCount, int lineCount,
                      float lineH, float scale, float screenH, float bottomMargin) {
    if (lineCount > (int)slotCount) { lineCount = (int)slotCount; }
    const float step = lineH * scale;
    const float top  = screenH - bottomMargin - (float)lineCount * step;
    for (std::size_t i = 0; i < slotCount; ++i) { ys[i] = top + (float)i * step; }
}

// ── toast lifetime and slot policy (matrix T6; donor TextWriter toast lane) ─────
// A toast posted with ttlFrames = N is drawn for exactly N frames: drawToasts
// skips ttl == 0, and resetFrame reaps a slot at ttl == 0, else decrements.

enum class ToastAge : std::uint8_t {
    Reap,       // slot is expired: free the node, clear the slot
    Decrement,  // still live: ttl -= 1
};

inline bool toastVisible(std::uint32_t ttlFrames) { return ttlFrames != 0; }

inline ToastAge classifyToastAge(std::uint32_t ttlFrames) {
    return ttlFrames == 0 ? ToastAge::Reap : ToastAge::Decrement;
}

// First free slot, or -1 when all slots are occupied (the engine then frees the
// candidate node instead of leaking it from the stolen heap).
inline int firstFreeToastSlot(const bool* occupied, std::size_t slotCount) {
    for (std::size_t i = 0; i < slotCount; ++i) {
        if (!occupied[i]) { return (int)i; }
    }
    return -1;
}

} // namespace totk::ui
