#pragma once

namespace zonai_ascend::pure {

inline constexpr float kNativeReach = 20.0f;
inline constexpr float kReleaseReach = 10000.0f;
inline constexpr float kValidationStartAbovePlayer = 0.5f;
inline constexpr float kMarkerStartAbovePlayer = 1.8f;

// Release policy (LENIENT): waive only the two local surface-shape disagreements; all else stays native.
inline constexpr unsigned kAngleReason = 0x0008u;
inline constexpr unsigned kQueryBaselineReason = 0x0040u;
inline constexpr unsigned kSurfaceDisagreementReason = 0x0400u;
inline constexpr unsigned kLenientReasonMask =
    kAngleReason | kSurfaceDisagreementReason;

// Marker growth starts after 40 m, adds 1x per 240 m, and caps at 4x.
inline constexpr float kMarkerNativeDistance = 40.0f;
inline constexpr float kMarkerGrowthMetresPerScale = 240.0f;
inline constexpr float kMarkerScaleCap = 4.0f;

struct DerivedReach {
    float total;
    float validationSpan;
    float markerSpan;
};

constexpr bool validReach(float reach) {
    return reach == reach && reach >= kNativeReach &&
           reach <= kReleaseReach;
}

constexpr DerivedReach deriveReach(float reach) {
    if (!validReach(reach)) reach = kNativeReach;
    return {
        reach,
        reach - kValidationStartAbovePlayer,
        reach - kMarkerStartAbovePlayer,
    };
}

constexpr bool canRelaxQueryFailure(unsigned reason) {
    const unsigned rejection = reason & ~kQueryBaselineReason;
    return (rejection & kLenientReasonMask) != 0 &&
           (rejection & ~kLenientReasonMask) == 0;
}

constexpr unsigned clearLenientReasons(unsigned reason) {
    return reason & ~kLenientReasonMask;
}

constexpr float markerScale(float distance) {
    if (!(distance == distance) || distance < 0.0f ||
        distance > kReleaseReach)
        return 1.0f;
    if (distance <= kMarkerNativeDistance) return 1.0f;

    float scale =
        1.0f +
        (distance - kMarkerNativeDistance) /
            kMarkerGrowthMetresPerScale;
    if (scale > kMarkerScaleCap) scale = kMarkerScaleCap;
    return scale;
}

} // namespace zonai_ascend::pure
