#pragma once

#include <cmath>
#include <cstdint>

namespace self_recall::pure {

constexpr std::uint8_t kKindNone = 0;
constexpr std::uint8_t kKindMove = 1;
constexpr std::uint8_t kKindClimbMove = 2;
constexpr std::uint8_t kKindClimbWait = 3;
constexpr std::uint8_t kKindGlide = 4;
constexpr std::uint8_t kKindFall = 5;
constexpr std::uint8_t kKindParasailGlide = 6;
constexpr std::uint8_t kKindCount = 7;
constexpr std::uint8_t kInvalidSlot = 0xff;

constexpr const char* allowlistedCommandName(std::uint8_t kind) {
    switch (kind) {
        case kKindMove: return "Move";
        case kKindClimbMove: return "ClimbMove";
        case kKindClimbWait: return "ClimbWait";
        case kKindGlide: return "Glide";
        case kKindFall: return "Fall";
        case kKindParasailGlide: return "ParasailGlide";
        default: return nullptr;
    }
}

constexpr bool isAllowlistedKind(std::uint8_t kind) {
    return allowlistedCommandName(kind) != nullptr;
}

constexpr float kForwardRateMinimum = 0.25f;
constexpr float kForwardRateMaximum = 2.0f;
constexpr float kForwardRateFallback = 1.0f;

inline float forwardAnimationRate(float recordedRate) {
    if (!std::isfinite(recordedRate)) return kForwardRateFallback;
    float rate = recordedRate < 0.0f ? -recordedRate : recordedRate;
    if (rate < kForwardRateMinimum) rate = kForwardRateMinimum;
    if (rate > kForwardRateMaximum) rate = kForwardRateMaximum;
    return rate;
}

consteval bool animationPolicyContracts() {
    if (allowlistedCommandName(kKindNone) != nullptr) return false;
    for (std::uint8_t kind = kKindMove; kind < kKindCount; ++kind) {
        if (allowlistedCommandName(kind) == nullptr) return false;
    }
    if (allowlistedCommandName(kKindCount) != nullptr) return false;
    if (allowlistedCommandName(0xff) != nullptr) return false;
    return true;
}

static_assert(animationPolicyContracts(),
              "forward-animation allowlist must remain exact");

enum class AnimLane : std::uint8_t {
    ForwardResume = 0,
    Reversed = 1,
    Plain = 2,
};

constexpr AnimLane nextAnimLane(AnimLane lane) {
    switch (lane) {
        case AnimLane::ForwardResume: return AnimLane::Reversed;
        case AnimLane::Reversed: return AnimLane::Plain;
        default: return AnimLane::ForwardResume;
    }
}

constexpr const char* animLaneName(AnimLane lane) {
    switch (lane) {
        case AnimLane::ForwardResume: return "anim: forward-resume";
        case AnimLane::Reversed: return "anim: reversed";
        default: return "anim: plain";
    }
}

enum class FrameForceResult : std::uint8_t {
    Applied = 0,
    Dead = 1,
    Bent = 2,
};

constexpr float kFrameForceTolerance = 0.6f;
constexpr float kFrameForceDeadBand = 1.0f;

constexpr FrameForceResult classifyFrameForce(float pre, float post,
                                              float target) {
    const float moved = post >= pre ? post - pre : pre - post;
    const float error = post >= target ? post - target : target - post;
    if (error <= kFrameForceTolerance) return FrameForceResult::Applied;
    if (moved <= 0.0001f) {
        const float wanted = target >= pre ? target - pre : pre - target;
        if (wanted > kFrameForceDeadBand) return FrameForceResult::Dead;
        return FrameForceResult::Applied;  // indistinguishable from success
    }
    return FrameForceResult::Bent;
}

consteval bool animLaneContracts() {
    AnimLane lane = AnimLane::ForwardResume;
    lane = nextAnimLane(lane);
    if (lane != AnimLane::Reversed) return false;
    lane = nextAnimLane(lane);
    if (lane != AnimLane::Plain) return false;
    lane = nextAnimLane(lane);
    if (lane != AnimLane::ForwardResume) return false;
    if (animLaneName(AnimLane::ForwardResume) == nullptr) return false;
    if (animLaneName(AnimLane::Reversed) == nullptr) return false;
    if (animLaneName(AnimLane::Plain) == nullptr) return false;
    if (classifyFrameForce(10.0f, 42.0f, 42.0f) !=
        FrameForceResult::Applied)
        return false;
    if (classifyFrameForce(10.0f, 10.0f, 42.0f) != FrameForceResult::Dead)
        return false;
    if (classifyFrameForce(10.0f, 3.0f, 42.0f) != FrameForceResult::Bent)
        return false;
    return true;
}

static_assert(animLaneContracts(),
              "anim lane cycle and frame-force classification must hold");

}  // namespace self_recall::pure
