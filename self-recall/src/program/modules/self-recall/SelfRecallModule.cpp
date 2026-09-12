#include "SelfRecallModule.hpp"

#include <nn/util.h>

#include "RecallBase.hpp"
#include "RecallGraphicsEngine.hpp"
#include "RecallModelEngine.hpp"
#include "RecallRuntimeEngine.hpp"

namespace {

using self_recall::RecallRuntime;
using self_recall::runtime;
using self_recall::setEvent;
using self_recall::Unsafe;
namespace pure = self_recall::pure;

constexpr float kTeleportResetMeters = 30.0f;

void updateOutfitSpeed(bool playerReady) {
    pure::GameTimeSnapshot clock;
    pure::OutfitSnapshot outfit;
    if (!playerReady || !self_recall::game_clock::snapshot(clock) ||
        clock.status != pure::GameTimeStatus::Running ||
        !self_recall::world::readOutfit(pure::kRecallOutfitSpeed, outfit)) return;
    auto& rt = runtime();
    if (!rt.session.speed.update(outfit)) return;
    SRLOG("RECALL_OUTFIT set=%s count=%u mask=%u value=%s ids=%u/%u/%u rewinding=%u",
          pure::kRecallOutfitSpeed.label, rt.session.speed.count(), rt.session.speed.mask(),
          pure::playbackRateText(rt.session.speed.rate()), outfit.actorIds[0],
          outfit.actorIds[1], outfit.actorIds[2], unsigned(rt.playback.rewinding));
}

void invalidateGeneration(const char* reason) {
    RecallRuntime& rt = runtime();
    self_recall::native_gameplay::reset();
    self_recall::effects::stop(reason);
    self_recall::clearRoute(reason);
    self_recall::glider_release::resetWorld();
    self_recall::vehicle::resetWorld();
    ++rt.session.worldGeneration;
    rt.session.speed.reset();
    self_recall::world::onGenerationInvalidated();
    rt.safety.lastClimbSeen = 0;
    rt.safety.climbActiveNow = false;
    SRLOG("WORLD_RESET generation=%u reason=%s", rt.session.worldGeneration,
          reason ? reason : "unspecified");
}

std::uint64_t historyDurationNanoseconds() {
    const auto* poses = self_recall::pose_storage::history();
    if (!poses) return 0;
    auto newest = poses->newest();
    const auto count = poses->count();
    if (!newest || !count) return 0;
    auto oldest = poses->before(newest.get()->header.key, count - 1);
    if (!oldest || newest.get()->header.elapsedNanoseconds < oldest.get()->header.elapsedNanoseconds)
        return 0;
    return newest.get()->header.elapsedNanoseconds - oldest.get()->header.elapsedNanoseconds;
}

void publishRouteLine() {
    RecallRuntime& rt = runtime();
    if ((rt.session.tick % 15u) != 0u) return;
    char* line = rt.session.routeLine;
    constexpr std::size_t kSize = sizeof(self_recall::SessionState::routeLine);
    if (rt.playback.rewinding) {
        nn::util::SNPrintf(
            line, kSize,
            "Recall: %u/%u | path/engine %.1f/%.1f m/s | yaw restored",
            (unsigned)(rt.playback.rewindTotal - rt.playback.rewindRemaining),
            (unsigned)rt.playback.rewindTotal,
            (double)rt.playback.lastAppliedRecordedSpeed,
            (double)rt.playback.lastAppliedEngineSpeed);
    } else if (rt.safety.unsafeNow != Unsafe::None) {
        if (rt.safety.unsafeNow == Unsafe::SpecialState)
            nn::util::SNPrintf(
                line, kSize,
                "History: recording unsafe state %u (will stop before it)",
                rt.safety.uiStateRaw);
        else
            nn::util::SNPrintf(line, kSize,
                               "History: recording %s (will stop before it)",
                               self_recall::unsafeName(rt.safety.unsafeNow));
    } else {
        const double seconds = static_cast<double>(historyDurationNanoseconds()) / 1.0e9;
        nn::util::SNPrintf(line, kSize,
                           "History: %.1f/%u.0 s | recording movement and animation",
                           seconds, pure::kHistorySeconds);
    }
}

void moduleTick(void* device) {
    RecallRuntime& rt = runtime();
    ++rt.session.tick;

    const self_recall::world::RefreshOutcome refreshed =
        self_recall::world::refresh(invalidateGeneration, kTeleportResetMeters);
    if (!refreshed.poseFinite && rt.playback.rewinding)
        setEvent("stopped: player pose became invalid");

    const auto renderFailure = self_recall::pose_render::takeFailure();
    const auto paletteFailure = self_recall::palette::takeFailure();
    const auto pathFailure = self_recall::native_path::takeFailure();
    if (pathFailure) {
        SRLOG("NATIVE_PATH_FAILED code=%u detail=%u", static_cast<unsigned>(pathFailure >> 32),
              static_cast<unsigned>(pathFailure));
        if (rt.playback.rewinding)
            self_recall::playback::finish(rt, self_recall::pure::PlaybackStop::NativePathFailed, true);
    }
    if (paletteFailure) {
        SRLOG("PALETTE_FAILED reason=%s code=%u detail=%u",
              self_recall::palette::failureName(paletteFailure),
              static_cast<unsigned>(paletteFailure >> 32), static_cast<unsigned>(paletteFailure));
        if (rt.playback.rewinding)
            self_recall::playback::finish(rt, self_recall::pure::PlaybackStop::PaletteFailed, true);
    }
    if (renderFailure) {
        SRLOG("ANIMATION_RENDER_FAILED code=%u detail=%u",
              static_cast<unsigned>(renderFailure >> 32), static_cast<unsigned>(renderFailure));
        if (rt.playback.rewinding)
            self_recall::playback::finish(rt, self_recall::pure::PlaybackStop::AnimationRenderFailed, true);
    }

    self_recall::safety::serviceProbe(rt);
    rt.safety.unsafeNow = self_recall::safety::classify(rt);
    self_recall::pose_storage::setPreparationAllowed(
        refreshed.resolved && rt.safety.unsafeNow == Unsafe::None &&
        self_recall::world::state().stablePlayerTicks >= self_recall::safety::kWarmupTicks);

    if (rt.playback.rewinding && rt.safety.unsafeNow == Unsafe::Riding) {
        self_recall::playback::finish(
            rt, self_recall::pure::PlaybackStop::UnsafeLiveState,
            true);
    }

    updateOutfitSpeed(refreshed.resolved &&
        self_recall::world::state().stablePlayerTicks >= self_recall::safety::kWarmupTicks);

    namespace input = self_recall::input;
    const totk::engine::NpadFrame frame = input::read(device);
    const std::uint64_t buttons = frame.snapshot().buttons;
    const bool activationHeld = pure::activationHeld(buttons);
    const bool triggered = pure::holdStep(rt.playback.triggerHold, activationHeld,
                                          input::kHoldTicks);
    if (rt.playback.startPending || triggered) {
        self_recall::playback::start(rt);
    }
    frame.maskOwnedButtons(pure::activationOwnedButtons(buttons));

    const bool bHeld = (buttons & input::kButtonB) != 0;
    const bool rewindingAtB = rt.playback.rewinding;
    if (rewindingAtB && bHeld) {
        self_recall::playback::finish(rt, self_recall::pure::PlaybackStop::CancelledByB, true);
        rt.playback.swallowB = true;
    }
    if (rt.playback.startPending && bHeld) {
        self_recall::pose_session::reset(false);
        rt.playback.startPending = false;
        rt.playback.swallowB = true;
        setEvent("cancelled");
    }
    if (!bHeld) rt.playback.swallowB = false;
    if (bHeld && (rewindingAtB || rt.playback.swallowB))
        frame.maskOwnedButtons(input::kButtonB);

    self_recall::glider_release::service(self_recall::world::playerActor(), rt.session.worldGeneration,
        rt.session.tick, refreshed.resolved && rt.safety.unsafeNow == Unsafe::None &&
        !rt.playback.rewinding && !rt.playback.startPending, bHeld && !rt.playback.swallowB);

    if (rt.playback.rewinding) {
        self_recall::playback::applyRecordedInput(frame);
        self_recall::playback::step(rt);
    }
    self_recall::pose_recorder::Control capture{};
    const auto& currentWorld = self_recall::world::state();
    capture.scene = currentWorld.sceneToken.value;
    capture.player = currentWorld.playerAddress;
    capture.worldGeneration = rt.session.worldGeneration;
    capture.sampleFlags = static_cast<std::uint8_t>(
        (rt.safety.unsafeNow == Unsafe::None ? pure::SampleAdmissible : 0) |
        (rt.safety.climbActiveNow ? pure::SampleClimb : 0) |
        (rt.safety.controlStickActiveNow ? pure::SampleControlStick : 0));
    capture.climbMayAdmit = rt.safety.unsafeNow == Unsafe::None ||
        rt.safety.unsafeNow == Unsafe::Tumbling || rt.safety.unsafeNow == Unsafe::SpecialState;
    capture.vehicleMayAdmit = capture.climbMayAdmit || rt.safety.unsafeNow == Unsafe::Riding;
    capture.enabled = refreshed.resolved && !rt.playback.rewinding &&
        currentWorld.stablePlayerTicks >= self_recall::safety::kWarmupTicks;
    self_recall::pose_recorder::publishControl(capture);
    publishRouteLine();
    self_recall::presentation::service();
}

void moduleInit(std::uintptr_t base) {
    self_recall::world::initialize(base);
    self_recall::presentation::initialize(base);
}

void moduleEnter() {
    self_recall::native_gameplay::reset();
    self_recall::vehicle::resetWorld();
    self_recall::pose_recorder::publishControl({});
    RecallRuntime& rt = runtime();
    self_recall::effects::stop("module enter");
    self_recall::clearRoute("module enter");
    rt.playback.triggerHold = {};
    rt.session.speed.reset();
}

bool moduleExit() {
    self_recall::native_gameplay::reset();
    self_recall::vehicle::resetWorld();
    self_recall::pose_recorder::publishControl({});
    RecallRuntime& rt = runtime();
    self_recall::effects::stop("module exit");
    self_recall::playback::clearVelocity("module exit");
    self_recall::clearRoute("module exit");
    rt.playback.triggerHold = {};
    rt.session.speed.reset();
    return true;
}

const char* moduleStatus() {
    RecallRuntime& rt = runtime();
    const char* mode =
        rt.playback.rewinding
            ? "REWINDING"
            : (rt.playback.startPending ? "starting Recall"
               : (historyDurationNanoseconds() >= pure::kRecallMinimumNanoseconds
                      ? "route ready" : "recording"));
    nn::util::SNPrintf(rt.session.status, sizeof(rt.session.status), "%s | %s",
                       mode, rt.session.event);
    return rt.session.status;
}

const char* moduleRoute() { return runtime().session.routeLine; }

void moduleRaycast(wwpg::RaycastFn original, const void* from,
                   const void* , const void* object, const void* ,
                   std::uint32_t , std::uint32_t ) {
    self_recall::safety::observeRaycast(original, from, object);
}

const wwpg::Module kModule{
    "SELF RECALL development",
    "No spawned objects",
    "ZL+RStick rewind | B cancel | Glide clothing controls speed",
    "Recorded animation frames + golden recall ribbon (development).",
    moduleInit,
    moduleEnter,
    moduleTick,
    moduleExit,
    moduleStatus,
    moduleRaycast,
    moduleRoute,
};

}

namespace wwpg::modules {
const Module& selfRecall() { return kModule; }
}

namespace self_recall {
namespace {
RecallRuntime g_runtime{};

}

RecallRuntime& runtime() { return g_runtime; }

const char* unsafeName(Unsafe unsafe) {
    switch (unsafe) {
        case Unsafe::None: return "ordinary";
        case Unsafe::Loading: return "loading";
        case Unsafe::Riding: return "riding";
        case Unsafe::SpecialState: return "special move";
        case Unsafe::Tumbling: return "tumbling";
        case Unsafe::StateUnreadable: return "state unreadable";
    }
    return "?";
}

void setEvent(const char* text) {
    char* event = g_runtime.session.event;
    int i = 0;
    for (; i < 95 && text && text[i]; ++i) event[i] = text[i];
    event[i] = 0;
}

void clearRoute(const char* reason) {
    auto& playback = g_runtime.playback;
    const auto* poses = pose_storage::history();
    const bool material = playback.rewinding || (poses && poses->count());

    pose_session::reset();
    glider_release::cancel();

    playback.rewinding = false;
    playback.startPending = false;
    playback.selectionPending = false;
    playback.selectedEnd = false;
    playback.lastStepClockSerial = 0;
    playback.rewindRemaining = 0;
    playback.rewindTotal = 0;
    playback.fightTicks = 0;
    playback.haveApplied = false;
    playback.lastSampleFlags = 0;
    playback.haveVerified = false;
    playback.lastAppliedRecordedSpeed = 0.0f;
    playback.lastAppliedEngineSpeed = 0.0f;
    playback.lastAppliedRate = pure::PlaybackRate::Normal;

    g_runtime.safety.probe.obstructed = false;
    probe::cancel(g_runtime.safety.probe);

    if (material) SRLOG("history cleared: %s", reason ? reason : "unspecified");
}

}

namespace self_recall::effects {

void buildRoute() {
    const auto* cursor = pose_session::playback();
    const auto count = cursor ? cursor->count() : 0;
    pure::RenderRoute route{};
    pure::buildRoute(route, [&](pure::RouteBuilder& builder) {
        for (std::uint32_t i = 0; i < count; ++i) {
            pure::PoseFrameHeader frame;
            if (!cursor->headerAt(i, frame) || !(frame.route.flags & pure::SampleAdmissible)) break;
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
                valid = cursor->headerAt(
                            count - 1 - route.points[i].ordinal, frame) &&
                        frame.elapsedNanoseconds <=
                            nativeRoute.newestNanoseconds &&
                        frame.elapsedNanoseconds >= nativeRoute.oldestNanoseconds;
                if (valid) nativeRoute.phases[i] = static_cast<float>(
                    static_cast<double>(nativeRoute.newestNanoseconds - frame.elapsedNanoseconds) /
                    static_cast<double>(duration));
            }
            if (!valid) nativeRoute = {};
        }
    }
    native_path::publish(nativeRoute);
}

bool startRewind() {
    void* player = world::playerActor();
    const auto* cursor = pose_session::playback();
    const auto generation = cursor ? cursor->selectedKey().generation : 0;
    return presentation::start(player, generation);
}

void stopForExit(const char* reason, bool emitEnd) {
    presentation::stop(world::playerActor(), reason, emitEnd);
}

void stop(const char* reason) {
    presentation::stop(world::playerActor(), reason, false);
}

}

namespace self_recall::safety {
namespace {
constexpr std::uint32_t kStateOrdinary = 0;
constexpr std::uint32_t kStateRiding = 6;
}

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
        pure::PoseFrameHeader frame;
        if (!cursor->headerAt(static_cast<std::uint32_t>(start + count), frame)) {
            SRLOG("ROUTE_HISTORY_UNAVAILABLE index=%u offset=%u", start, static_cast<unsigned>(count));
            state.unavailable = true;
            return;
        }
        samples[count] = frame.route;
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

}
