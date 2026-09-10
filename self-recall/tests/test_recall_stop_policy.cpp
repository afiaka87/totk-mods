#include "RecallStopPolicy.hpp"
#include "../src/program/modules/self-recall/RecallStopText.hpp"
#include <ostream>
#include "doctest.h"
#include <array>
#include <string_view>
using namespace self_recall;

TEST_CASE("typed playback exits preserve candidate10 messages and cleanup decisions") {
    struct Case { pure::PlaybackStop stop; const char* reason; const char* event; };
    const std::array cases{
        Case{pure::PlaybackStop::PoseApplyFailed, "POSE_APPLY_FAILED", "stopped: player pose unavailable"},
        Case{pure::PlaybackStop::ClockUnavailable, "CLOCK_UNAVAILABLE", "stopped: gameplay clock unavailable"},
        Case{pure::PlaybackStop::RayBlocked, "RAY_BLOCKED", "path blocked - stopped in place"},
        Case{pure::PlaybackStop::RouteUnavailable, "ROUTE_UNAVAILABLE", "stopped: route check unavailable"},
        Case{pure::PlaybackStop::NonfiniteCurrent, "NONFINITE_CURRENT", "stopped: pose became invalid"},
        Case{pure::PlaybackStop::Blocked, "BLOCKED", "path blocked - stopped in place"},
        Case{pure::PlaybackStop::Finished, "FINISHED", "route fully rewound"},
        Case{pure::PlaybackStop::UnsafeSample, "UNSAFE_SAMPLE", "unsafe move reached - stopped in place"},
        Case{pure::PlaybackStop::PoseUnavailable, "POSE_UNAVAILABLE", "stopped: recorded animation unavailable"},
        Case{pure::PlaybackStop::NonfiniteSample, "NONFINITE_SAMPLE", "bad sample - stopped in place"},
        Case{pure::PlaybackStop::NativePathFailed, "NATIVE_PATH_FAILED", "stopped: Recall path unavailable"},
        Case{pure::PlaybackStop::PaletteFailed, "PALETTE_FAILED", "stopped: Recall colors unavailable"},
        Case{pure::PlaybackStop::AnimationRenderFailed, "ANIMATION_RENDER_FAILED", "stopped: recorded animation unavailable"},
        Case{pure::PlaybackStop::UnsafeLiveState, "UNSAFE_LIVE_STATE", "another move took over - stopped in place"},
        Case{pure::PlaybackStop::CancelledByB, "CANCELLED_BY_B", "cancelled"},
        Case{pure::PlaybackStop::StaminaExhausted, "STAMINA_EXHAUSTED", "Recall ended: stamina depleted"},
        Case{pure::PlaybackStop::StaminaUnavailable, "STAMINA_UNAVAILABLE", "stopped: stamina unavailable"},
    };
    CHECK(cases.size() == static_cast<unsigned>(pure::PlaybackStop::Count));
    for (const auto& item : cases) {
        const auto text = playback::stopText(item.stop);
        CHECK(std::string_view(text.reason) == item.reason);
        CHECK(std::string_view(text.event) == item.event);
        const bool wasGraceful = std::string_view(item.reason) == "FINISHED" ||
                                 std::string_view(item.reason) == "CANCELLED_BY_B" ||
                                 std::string_view(item.reason) == "STAMINA_EXHAUSTED";
        for (unsigned safe = 0; safe <= 1; ++safe) {
            for (unsigned flags = 0; flags < 8; ++flags) {
                const auto exit = pure::playbackExitPlan(item.stop, safe != 0, static_cast<std::uint8_t>(flags));
                const bool wasGliderRelease = wasGraceful && safe &&
                    std::string_view(item.reason) != "STAMINA_EXHAUSTED" &&
                    (flags & pure::SampleNativeGlide) && (flags & pure::SampleAdmissible);
                CHECK(exit.graceful == wasGraceful);
                CHECK(exit.releaseGlider == wasGliderRelease);
            }
        }
    }
}
