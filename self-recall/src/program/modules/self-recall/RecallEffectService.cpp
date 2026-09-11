
#include "RecallEffectService.hpp"

#include "PlayerBridge.hpp"
#include "RecallAnimation.hpp"
#include "RecallLog.hpp"
#include "RecallPresentation.hpp"
#include "RecallPoseSession.hpp"
#include "RecallNativePath.hpp"
#include "RecallMemoryProfiler.hpp"

namespace self_recall::effects {

void initialize(std::uintptr_t mainBase) {
    presentation::initialize(mainBase);
    anim::initialize(mainBase);
}

void service(RecallRuntime&) {
    presentation::service();
}

void buildRoute(RecallRuntime& runtime) {
#if SELF_RECALL_MEMORY_PROFILE
    const auto started = svcGetSystemTick();
#endif
    const auto* cursor = pose_session::playback();
    const auto count = cursor ? cursor->count() : 0;
    std::uint32_t reachable = 0;
    pure::RenderRoute route{};
    const auto result = pure::buildRoute(route, [&](pure::RouteBuilder& builder) {
        reachable = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            pure::PoseFrameHeader frame;
            if (!cursor->headerAt(i, frame) || !(frame.route.flags & pure::SampleAdmissible)) break;
            ++reachable;
            const auto& sample = frame.route;
            const std::uint16_t ordinal =
                static_cast<std::uint16_t>(count - 1 - i);
            const std::uint8_t stateBits =
                (sample.flags & pure::SampleClimb) ? std::uint8_t{1}
                                                   : std::uint8_t{0};
            pure::routeFeed(builder,
                            pure::recallTrailPosition(frame),
                            ordinal, stateBits);
        }
    });

    pure::NativePathRoute nativeRoute;
    if (cursor && count) {
        pure::PoseFrameHeader newest, oldest;
        if (cursor->headerAt(0, newest) && cursor->headerAt(count - 1, oldest)) {
            nativeRoute.anchor = cursor->anchorKey();
            nativeRoute.world = newest.worldGeneration;
            nativeRoute.historyCount = count;
            nativeRoute.newestNanoseconds = newest.elapsedNanoseconds;
            nativeRoute.oldestNanoseconds = oldest.elapsedNanoseconds;
            nativeRoute.route = route;
            const auto duration = nativeRoute.newestNanoseconds - nativeRoute.oldestNanoseconds;
            bool valid = nativeRoute.newestNanoseconds > nativeRoute.oldestNanoseconds &&
                         duration <= pure::kRecallWindowNanoseconds;
            for (unsigned i = 0; valid && i < route.count; ++i) {
                if (route.points[i].ordinal >= count) { valid = false; break; }
                pure::PoseFrameHeader frame;
                valid = cursor->headerAt(count - 1 - route.points[i].ordinal, frame) && frame.elapsedNanoseconds <= nativeRoute.newestNanoseconds &&
                        frame.elapsedNanoseconds >= nativeRoute.oldestNanoseconds;
                if (valid) nativeRoute.phases[i] = static_cast<float>(
                    static_cast<double>(nativeRoute.newestNanoseconds - frame.elapsedNanoseconds) /
                    static_cast<double>(duration));
            }
            if (!valid) nativeRoute = {};
        }
    }
    native_path::publish(nativeRoute);
#if SELF_RECALL_MEMORY_PROFILE
    SRLOG("ROUTE_BUILD_TIMING frames=%u ticks=%llu", count,
        static_cast<unsigned long long>(svcGetSystemTick() - started));
#endif
    SRLOG(
        "VISUAL_START raw=%u reachable=%u fed=%u kept=%u dedup=%u truncated=%u attempts=%d",
        (unsigned)count, (unsigned)reachable, result.fed, result.kept,
        result.duplicates, result.truncated, result.attempts);
}

bool startRewind(RecallRuntime& runtime) {
    void* player = world::playerActor();
    const auto* cursor = pose_session::playback();
    const auto generation = cursor ? cursor->selectedKey().generation : 0;
    const bool acquired = presentation::start(player, generation);
    anim::beginRewind(player);
    runtime.playback.lastAnimKind = pure::kKindNone;
    runtime.playback.lastAnimSlot = pure::kInvalidSlot;
    runtime.playback.lastAnimStickX = 0;
    runtime.playback.lastAnimStickY = 0;
    return acquired;
}

void driveAnimation(RecallRuntime& runtime) {
    anim::drive(world::playerActor(), runtime.playback.lastAnimKind,
                runtime.playback.lastAnimSlot);
}

void stopForExit(RecallRuntime& runtime, const char* reason, bool emitEnd,
                 bool graceful) {
    const auto& playback = runtime.playback;
    const unsigned completed =
        playback.rewindTotal >= playback.rewindRemaining
            ? static_cast<unsigned>(playback.rewindTotal -
                                    playback.rewindRemaining)
            : 0u;

    void* player = world::playerActor();
    anim::release(player, reason);
    presentation::stop(player, reason, emitEnd);
    SRLOG(
        "VISUAL_SUMMARY reason=%s consumed=%u/%u graceful=%d",
        reason ? reason : "STOP", completed, (unsigned)playback.rewindTotal,
        (int)graceful);
}

void abandonAll(RecallRuntime& runtime, const char* reason, bool releaseAnim) {
    void* player = world::playerActor();
    if (releaseAnim) {
        anim::release(player, reason);
        presentation::stop(player, reason, false);
    } else {
        presentation::stop(player, reason, false);
        anim::abandon();
    }
}

}  // namespace self_recall::effects
