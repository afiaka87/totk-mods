
#include "RouteSafetyService.hpp"

#include "PlayerBridge.hpp"
#include "RecallLog.hpp"
#include "RecallPoseSession.hpp"
#include "RecallVehicle.hpp"

namespace self_recall::safety {
namespace {
constexpr std::uint32_t kStateOrdinary = 0;
constexpr std::uint32_t kStateRiding = 6;
}  // namespace

Unsafe classify(RecallRuntime& runtime) {
    auto& safety = runtime.safety;
    safety.controlStickActiveNow = false;
    if (!world::havePlayer() ||
        world::state().stablePlayerTicks < kWarmupTicks) {
        return Unsafe::Loading;
    }

    const bool climbNow = world::nativeClimbing() || world::climbSensorEngaged();
    if (climbNow) safety.lastClimbSeen = runtime.session.tick;
    safety.climbActiveNow =
        climbNow ||
        (safety.lastClimbSeen != 0 &&
         runtime.session.tick - safety.lastClimbSeen <=
             static_cast<std::uint64_t>(kClimbGraceTicks));
    if (safety.climbActiveNow) return Unsafe::None;

    safety.controlStickActiveNow = vehicle::controlStickActive(
        world::state().mainBase, world::playerActor());
    if (safety.controlStickActiveNow) return Unsafe::None;

    std::uint32_t state = 0;
    if (!world::readUiPlayerState(state)) return Unsafe::StateUnreadable;
    safety.uiStateRaw = state;
    if (state == kStateRiding)
        return (vehicle::controlStickRiding(world::state().mainBase, world::playerActor()) ||
                (pose_session::active() && vehicle::unmounted(world::playerActor())))
             ? Unsafe::None : Unsafe::Riding;
    if (state != kStateOrdinary) return Unsafe::SpecialState;

    float upright = 0.0f;
    if (!world::readUprightY(upright) || !std::isfinite(upright) ||
        upright < kUprightMin) {
        return Unsafe::Tumbling;
    }
    return Unsafe::None;
}

void serviceProbe(RecallRuntime& runtime) {
    auto& probeState = runtime.safety.probe;
    const auto result = probe::service(probeState, runtime.playback.rewinding,
                                       runtime.session.tick);
    if (result == probe::ServiceResult::TimedOut &&
        (probeState.timeouts <= 3 || (probeState.timeouts % 60) == 0)) {
        SRLOG("ROUTE_RAY_TIMEOUT count=%u", probeState.timeouts);
    }
}

void armProbe(RecallRuntime& runtime) {
    const auto* cursor = pose_session::playback();
    if (!cursor || cursor->index() + 1 >= cursor->count()) return;
    auto& state = runtime.safety.probe;
    if (state.armed || state.obstructed || state.unavailable) return;
    const auto start = pure::nextRouteProbeStart(cursor->index(), state.evaluatedThrough, cursor->count(),
        runtime.session.speed.rate() != pure::PlaybackRate::Normal);
    if (start == UINT32_MAX) return;
    pure::HistorySample samples[pure::kProbeWindowMaxSamples + 1];
    std::size_t count = 0;
    for (; count <= pure::kProbeWindowMaxSamples && start + count < cursor->count(); ++count) {
        auto frame = cursor->frameAt(static_cast<std::uint32_t>(start + count));
        if (!frame) {
            SRLOG("ROUTE_HISTORY_UNAVAILABLE index=%u offset=%u", start, static_cast<unsigned>(count));
            state.unavailable = true;
            return;
        }
        samples[count] = frame.get()->header.route;
    }
    probe::ArmRequest request{};
    request.rewindIndex = start;
    request.rewinding = runtime.playback.rewinding;
    request.havePlayer = world::havePlayer();
    request.climbActive = runtime.safety.climbActiveNow;
    request.tick = runtime.session.tick;
    request.generation = runtime.session.worldGeneration;
    probe::arm(state, {samples, count}, request);
    if (state.unavailable && count >= 2) {
        SRLOG("ROUTE_SAMPLE_REFUSED index=%u flags=%u,%u upright=%.3f,%.3f live_climb=%u native_climb=%u ui=%u",
            cursor->index(), samples[0].flags, samples[1].flags,
            samples[0].pose.rotation.values[4], samples[1].pose.rotation.values[4],
            request.climbActive, world::nativeClimbing(), runtime.safety.uiStateRaw);
    }
}

void observeRaycast(wwpg::RaycastFn original, const void* from,
                    const void* object) {
    probe::observe(original, from, object);
}

}  // namespace self_recall::safety
