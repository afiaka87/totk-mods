#pragma once
#include "RecallHistory.hpp"

namespace self_recall::pure {
enum class PlaybackStop {
    PoseApplyFailed, ClockUnavailable, RayBlocked, RouteUnavailable,
    NonfiniteCurrent, Blocked, Finished, UnsafeSample, PoseUnavailable,
    NonfiniteSample, NativePathFailed, PaletteFailed, AnimationRenderFailed,
    UnsafeLiveState, CancelledByB, StaminaExhausted, StaminaUnavailable, Count,
};

struct PlaybackExitPlan {
    bool graceful = false;
    bool releaseGlider = false;
};

constexpr PlaybackExitPlan playbackExitPlan(PlaybackStop stop, bool liveStateSafe,
                                           std::uint8_t lastSampleFlags) {
    const bool graceful = stop == PlaybackStop::Finished || stop == PlaybackStop::CancelledByB ||
                          stop == PlaybackStop::StaminaExhausted;
    return {graceful, graceful && stop != PlaybackStop::StaminaExhausted &&
                      liveStateSafe && (lastSampleFlags & SampleNativeGlide) &&
                      (lastSampleFlags & SampleAdmissible)};
}
} // namespace self_recall::pure
