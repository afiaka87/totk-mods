// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Chain geometry: preview diamond while targeting, full-length chain from the moment the shot
// commits, near end following Link while latched.
#pragma once

#include <cstdint>

#include "TargetValidator.hpp"
#include "Vec3.hpp"

namespace zonai_hookshot::pure {
enum class ChainStyle : uint8_t {
    None,
    PreviewValid,
    PreviewInvalid,
    Launch,
    Latched,
};

struct ChainSnapshot {
    bool visible = false;
    Vec3 nearPoint{};
    Vec3 farPoint{};
    ChainStyle style = ChainStyle::None;
};

inline bool chainBodyVisible(ChainStyle style) {
    return style == ChainStyle::Launch || style == ChainStyle::Latched;
}

// Spans past the plausibility ceiling draw nothing, which hides the chain inside shrines.
constexpr float kMinimumSpan = 0.05f;
constexpr float kMaximumSpan = 400.0f;

struct LaunchState {
    Vec3 fireOrigin{};  // captured at commit
    Vec3 anchor{};      // immutable for the whole attempt
    float advanced = 0.0f;
    int ticks = 0;
};

// The chain is at the anchor as soon as the shot commits, so ChainLaunch lasts one tick.
inline bool stepLaunch(LaunchState& s) {
    ++s.ticks;
    const float total = distance(s.fireOrigin, s.anchor);
    s.advanced = std::isfinite(total) ? total : 0.0f;
    return true;
}

inline Vec3 launchTip(const LaunchState& s) { return s.anchor; }

inline bool plausibleSpan(Vec3 a, Vec3 b, float maxSpan = kMaximumSpan) {
    if (!finite3(a) || !finite3(b)) return false;
    const float span = distance(a, b);
    return std::isfinite(span) && span >= kMinimumSpan && span <= maxSpan;
}

// Preview: valid targets draw the teal diamond, refused hits the red one,
// Pending/Miss/InvalidFinite nothing.
inline ChainSnapshot preview(Vec3 origin, const TargetSample& sample, Verdict verdict,
                             float maxSpan = kMaximumSpan) {
    ChainSnapshot snap{};
    if (verdict == Verdict::Pending || verdict == Verdict::Miss ||
        verdict == Verdict::InvalidFinite)
        return snap;
    if (!plausibleSpan(origin, sample.position, maxSpan)) return snap;
    snap.visible = true;
    snap.nearPoint = origin;
    snap.farPoint = sample.position;
    snap.style = verdict == Verdict::Valid ? ChainStyle::PreviewValid
                                           : ChainStyle::PreviewInvalid;
    return snap;
}

inline ChainSnapshot launchSnapshot(Vec3 origin, const LaunchState& s, float maxSpan = kMaximumSpan) {
    ChainSnapshot snap{};
    const Vec3 tip = launchTip(s);
    if (!plausibleSpan(origin, tip, maxSpan)) return snap;
    snap.visible = true;
    snap.nearPoint = origin;
    snap.farPoint = tip;
    snap.style = ChainStyle::Launch;
    return snap;
}

inline ChainSnapshot latched(Vec3 origin, Vec3 anchor, float maxSpan = kMaximumSpan) {
    ChainSnapshot snap{};
    if (!plausibleSpan(origin, anchor, maxSpan)) return snap;
    snap.visible = true;
    snap.nearPoint = origin;
    snap.farPoint = anchor;
    snap.style = ChainStyle::Latched;
    return snap;
}

}  // namespace zonai_hookshot::pure
