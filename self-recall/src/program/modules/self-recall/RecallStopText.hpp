#pragma once
#include "RecallStopPolicy.hpp"

namespace self_recall::playback {
struct StopText { const char* reason; const char* event; };
constexpr StopText stopText(pure::PlaybackStop stop) {
    switch (stop) {
    case pure::PlaybackStop::PoseApplyFailed: return {"POSE_APPLY_FAILED", "stopped: player pose unavailable"};
    case pure::PlaybackStop::ClockUnavailable: return {"CLOCK_UNAVAILABLE", "stopped: gameplay clock unavailable"};
    case pure::PlaybackStop::RayBlocked: return {"RAY_BLOCKED", "path blocked - stopped in place"};
    case pure::PlaybackStop::RouteUnavailable: return {"ROUTE_UNAVAILABLE", "stopped: route check unavailable"};
    case pure::PlaybackStop::NonfiniteCurrent: return {"NONFINITE_CURRENT", "stopped: pose became invalid"};
    case pure::PlaybackStop::Blocked: return {"BLOCKED", "path blocked - stopped in place"};
    case pure::PlaybackStop::Finished: return {"FINISHED", "route fully rewound"};
    case pure::PlaybackStop::UnsafeSample: return {"UNSAFE_SAMPLE", "unsafe move reached - stopped in place"};
    case pure::PlaybackStop::PoseUnavailable: return {"POSE_UNAVAILABLE", "stopped: recorded animation unavailable"};
    case pure::PlaybackStop::NonfiniteSample: return {"NONFINITE_SAMPLE", "bad sample - stopped in place"};
    case pure::PlaybackStop::NativePathFailed: return {"NATIVE_PATH_FAILED", "stopped: Recall path unavailable"};
    case pure::PlaybackStop::PaletteFailed: return {"PALETTE_FAILED", "stopped: Recall colors unavailable"};
    case pure::PlaybackStop::AnimationRenderFailed: return {"ANIMATION_RENDER_FAILED", "stopped: recorded animation unavailable"};
    case pure::PlaybackStop::UnsafeLiveState: return {"UNSAFE_LIVE_STATE", "another move took over - stopped in place"};
    case pure::PlaybackStop::CancelledByB: return {"CANCELLED_BY_B", "cancelled"};
    case pure::PlaybackStop::StaminaExhausted: return {"STAMINA_EXHAUSTED", "Recall ended: stamina depleted"};
    case pure::PlaybackStop::StaminaUnavailable: return {"STAMINA_UNAVAILABLE", "stopped: stamina unavailable"};
    case pure::PlaybackStop::Count: break;
    }
    return {"STOP", ""};
}
} // namespace self_recall::playback
