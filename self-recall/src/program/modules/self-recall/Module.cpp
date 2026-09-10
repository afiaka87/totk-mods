#include "SelfRecallModule.hpp"

#include <nn/util.h>

#include "PlaybackController.hpp"
#include "PlayerBridge.hpp"
#include "RecallEffectService.hpp"
#include "RecallInput.hpp"
#include "RecallLog.hpp"
#include "RecallPoseStorage.hpp"
#include "RecallPoseRecorder.hpp"
#include "RecallGliderRelease.hpp"
#include "RecallGameClock.hpp"
#include "RecallPoseSession.hpp"
#include "RecallPoseRender.hpp"
#include "RecallScenePalette.hpp"
#include "RecallNativePath.hpp"
#include "RecallNativeGameplay.hpp"
#include "RecallVehicle.hpp"
#include "RecallRuntime.hpp"
#include "RouteSafetyService.hpp"

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
    self_recall::effects::abandonAll(rt, reason, /*releaseAnim=*/false);
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
                           "History: %.1f/64.0 s | recording movement and animation",
                           seconds);
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
    const totk::engine::NpadFrame frame = input::read(device, rt.input);
    const std::uint64_t buttons = frame.snapshot().buttons;
    const bool activationHeld =
        (buttons & (input::kButtonZL | input::kButtonDown)) ==
        (input::kButtonZL | input::kButtonDown);
    const bool triggered = pure::holdStep(rt.playback.triggerHold, activationHeld,
                                          input::kHoldTicks);
    if (rt.playback.startPending || triggered) {
        self_recall::playback::start(rt);
    }
    if (activationHeld) frame.maskOwnedButtons(input::kButtonDown);

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
        self_recall::playback::applyRecordedInput(rt, frame);
        self_recall::playback::step(rt);
    }
    self_recall::pose_recorder::Control capture{};
    const auto& currentWorld = self_recall::world::state();
    capture.scene = currentWorld.sceneToken.value;
    capture.player = currentWorld.playerAddress;
    capture.tick = rt.session.tick;
    capture.worldGeneration = rt.session.worldGeneration;
    capture.stickX = rt.input.latestStickX;
    capture.stickY = rt.input.latestStickY;
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
    self_recall::effects::service(rt);
}

void moduleInit(std::uintptr_t base) {
    runtime().session.mainBase = base;
    self_recall::world::initialize(base);
    self_recall::effects::initialize(base);
}

void moduleEnter() {
    self_recall::native_gameplay::reset();
    self_recall::vehicle::resetWorld();
    self_recall::pose_recorder::publishControl({});
    RecallRuntime& rt = runtime();
    self_recall::effects::abandonAll(rt, "module enter", /*releaseAnim=*/false);
    self_recall::clearRoute("module enter");
    rt.playback.triggerHold = {};
    rt.session.speed.reset();
}

bool moduleExit() {
    self_recall::native_gameplay::reset();
    self_recall::vehicle::resetWorld();
    self_recall::pose_recorder::publishControl({});
    RecallRuntime& rt = runtime();
    self_recall::effects::abandonAll(rt, "module exit", /*releaseAnim=*/true);
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
                   const void* /*to*/, const void* object, const void* /*out*/,
                   std::uint32_t /*mask*/, std::uint32_t /*flag*/) {
    self_recall::safety::observeRaycast(original, from, object);
}

const wwpg::Module kModule{
    "SELF RECALL development",
    "No spawned objects",
    "ZL+Down rewind | B cancel | Glide clothing controls speed",
    "Recorded animation frames + golden recall ribbon (development).",
    moduleInit,
    moduleEnter,
    moduleTick,
    moduleExit,
    moduleStatus,
    moduleRaycast,
    moduleRoute,
};

}  // namespace

namespace wwpg::modules {
const Module& selfRecall() { return kModule; }
}  // namespace wwpg::modules

namespace self_recall::integration {

bool movementEngaged() {
    const RecallRuntime& rt = runtime();
    return rt.playback.rewinding || rt.playback.startPending || rt.playback.swallowB ||
           rt.playback.triggerHold.ticks != 0 || rt.playback.triggerHold.fired;
}

}  // namespace self_recall::integration
