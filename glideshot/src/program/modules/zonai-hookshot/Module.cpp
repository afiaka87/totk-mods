// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// The coordinator: tick order, the pure machine's inputs, and event dispatch. Behaviour lives in
// the named services; only src/engine touches guest memory.
#include "HookshotHooks.hpp"
#include "HookshotIntegration.hpp"

#include "ActionContext.hpp"
#include "AimRaycaster.hpp"
#include "ChainPresentationService.hpp"
#include "FacingController.hpp"
#include "HookshotAudio.hpp"
#include "HookshotCues.hpp"
#include "HookshotInput.hpp"
#include "HookshotLog.hpp"
#include "HookshotRuntime.hpp"
#include "HookshotWorld.hpp"
#include "TargetingService.hpp"

namespace {
using namespace zonai_hookshot;
using namespace zonai_hookshot::pure;

// A post-press confirmation ray needs a full timeout window before the confirmation gives up.
static_assert(MachineConfig{}.confirmTimeoutTicks > aim::kTimeoutTicks);

// Sound state lives beside the tick so the runtime header stays free of engine types.
struct AudioState {
    AimFeedbackState aim{};
    std::uint32_t generation = 0;
    TravelCueGate travelGate{};
};
AudioState g_audio;
bool g_allowActivation = true;

void silenceAudio() {
    audio::resetAbilityCues();
    g_audio.travelGate.reset();
    g_audio.aim = {};
}

void serviceAudio(HookshotRuntime& rt) {
    // Look for the speaker occasionally until it turns up.
    if (world::ready() && !audio::ready() && (rt.session.tick % 60) == 0)
        audio::prime();

    if (g_audio.generation != rt.session.worldGen) {
        silenceAudio();
        g_audio.generation = rt.session.worldGen;
    }
    audio::updateAbilityCues(world::playerPosition());
    const auto feedback = aimFeedbackInput(rt.machine.phase, rt.aim.verdict);
    const auto cues = g_audio.aim.update(feedback, (std::uint32_t)rt.session.tick);
    const char* aimCues[]{cues.entry, cues.verdict};
    for (const char* cue : aimCues) {
        if (cue) {
            const bool emitted = audio::playCue(cue);
            ZHLOG("AUDIO_EMIT kind=verdict cue=%s result=%u phase=%u", cue,
                  (unsigned)emitted, (unsigned)rt.machine.phase);
        }
    }

    if (g_audio.travelGate.shouldEmit(travelCueWanted(rt.machine.phase)))
        audio::playAbilityCue(false, world::playerPosition());
}

void handleEvent(HookshotRuntime& rt, Event event) {
    if (const char* cue = event == Event::TargetingEntered ? nullptr : cueForEvent(event)) {
        if (cue == kCueArrive) {
            audio::playAbilityCue(true, world::playerPosition());
        } else {
            const bool emitted = audio::playCue(cue);
            ZHLOG("AUDIO_EMIT kind=event cue=%s result=%u event=%u phase=%u",
                  cue, (unsigned)emitted, (unsigned)event,
                  (unsigned)rt.machine.phase);
        }
    }

    switch (event) {
        case Event::TargetingEntered: targeting::onTargetingEntered(rt); break;
        case Event::ArmingAbandoned:  targeting::onArmingAbandoned(rt); break;
        case Event::ConfirmStarted:   targeting::onConfirmStarted(rt); break;
        case Event::Committed:        targeting::onCommitted(rt); break;
        case Event::Refused:          targeting::onRefused(rt); break;
        case Event::Latched:          targeting::onLatched(rt); break;

        case Event::PositionZipStarted:
            if (transport::startPositionZip(rt))
                note("ZIP! opening the paraglider automatically; B bails out");
            else
                note("position zip refused: invalid start or target");
            break;
        case Event::PositionZipEnded:  transport::onPositionZipEnded(rt); break;
        case Event::PositionZipFailed: transport::onPositionZipFailed(rt); break;

        case Event::CaptureStarted: capture::onCaptureStarted(rt); break;
        case Event::ClimbAcquired:  capture::onClimbAcquired(rt); break;
        case Event::CaptureFailed:  capture::onCaptureFailed(rt); break;

        case Event::Cancelled:
            note("cancelled");
            ZHLOG("CANCEL");
            break;
        case Event::Cleared:
            // B from any launched or transport phase: never keep driving past a player cancel.
            transport::stopDrive(rt, "cancel");
            note("chain cleared");
            ZHLOG("CLEAR");
            break;

        case Event::DetachRequested: transport::onDetachRequested(rt); break;
        case Event::DetachRefused:   transport::onDetachRefused(rt); break;
        case Event::FallEntered:     transport::onFallEntered(rt); break;
        case Event::TripAborted:     transport::onTripAborted(rt); break;

        case Event::HandoffArmed: parasail::onHandoffArmed(rt); break;
        case Event::GlideEntered: parasail::onGlideEntered(rt); break;
        case Event::GlideRefused: parasail::onGlideRefused(rt); break;
        case Event::GlideEnded:   parasail::onGlideEnded(rt); break;

        case Event::Reset:
            resetSession("world");
            break;
        case Event::CooldownDone:
            note("hold ZL + right-stick click to aim");
            break;
        default:
            break;
    }
}

// A world change also retires an unclaimed ray request.
void onWorldLost(const char* reason) {
    resetSession(reason);
    aim::abandonPending();
}

void runtimeTick(void* device) {
    HookshotRuntime& rt = runtime();
    ++rt.session.tick;
    if (rt.aim.latchFeedbackTicks > 0) --rt.aim.latchFeedbackTicks;
    world::refresh(onWorldLost);
    targeting::serviceAimRay(rt);
    transport::reportJumpBoostEdge(rt);

    const bool ready = world::ready();
    const bool aiming = rt.machine.phase == Phase::Targeting ||
                        rt.machine.phase == Phase::Confirming;
    if (rt.aim.starved >= targeting::kStarveResetLimit && aiming) {
        resetSession("menu");
        note("aiming paused: the world stopped answering (menu/loading)");
    }

    const SampleContext context = sampleContext(rt.aim.sample, rt.aim.sampleTick,
                                                rt.session.worldGen, rt.session.tick);
    rt.aim.verdict = validate(rt.aim.sample, context);

    // No fresh samples: no edges, no hold advance.
    const totk::engine::NpadFrame frame = input::readFrame(device);
    const bool fresh = frame.snapshot().freshSampleCount > 0;
    // Publish the live Npad id before Controller::calc runs later in the same pass.
    const input::LiveSlot live = input::newestLiveSlot(device);
    if (live.valid() && live.samplingNumber > 0) {
        rt.walk.targetNpadId.store((std::uint32_t)live.index,
                                   std::memory_order_relaxed);
        rt.walk.targetNpadValid.store(1, std::memory_order_release);
    }
    const std::uint64_t buttons =
        fresh ? frame.snapshot().buttons : rt.lastButtons;
    MachineInputs in{};
    in.worldReady = ready;
    in.freshInput = fresh;
    in.chordHeld = g_allowActivation && (buttons & input::kAimChord) == input::kAimChord;
    in.stickClickHeld = (buttons & input::kButtonRStick) != 0;

    const bool ownAim = in.chordHeld || rt.machine.phase == Phase::Targeting ||
                        rt.machine.phase == Phase::Confirming;
    if (fresh && rt.haveButtons) {
        in.aEdge = (buttons & input::kButtonA) &&
                   !(rt.prevButtons & input::kButtonA);
        in.bEdge = (buttons & input::kButtonB) &&
                   !(rt.prevButtons & input::kButtonB);
    }
    if (fresh) {
        rt.prevButtons = rt.lastButtons = buttons;
        rt.haveButtons = true;
    }

    if (rt.machine.phase == Phase::Confirming)
        in.confirm = confirmDecision(rt.aim.sample, context,
                                     rt.aim.confirmFloorSeq);
    if (rt.machine.phase == Phase::ChainLaunch)
        in.launchComplete = stepLaunch(rt.launch);

    // Transport signals, computed before step so transitions land this tick.
    if (rt.machine.phase == Phase::PositionCruise) {
        facing::startOnParasailIfReady(rt);
        in.positionZipDone = rt.positionDrive.completed;
        in.positionZipFailed = rt.positionDrive.failed;
        // Hold the endpoint until the yaw is fully locked so capture never inherits a partial
        // turn.
        in.glideEntered =
            rt.drive.parasailActive.load(std::memory_order_acquire) != 0 &&
            rt.positionDrive.yaw.finished;
        // A very short trip can reach its standoff before Parasail admits Link; hold the endpoint
        // while the acquisition window finishes.
        if (rt.positionDrive.completed && !in.glideEntered) {
            if (++rt.positionDrive.handoffWaitTicks >= 90) {
                rt.positionDrive.failed = true;
                in.positionZipFailed = true;
                ZHLOG("COMPOSE_GLIDE_REFUSED wait=%d fall=%u arms=%u forced=%u",
                      rt.positionDrive.handoffWaitTicks,
                      rt.drive.fallActive.load(std::memory_order_relaxed),
                      rt.drive.admissionArms.load(std::memory_order_relaxed),
                      rt.drive.predForced.load(std::memory_order_relaxed));
            }
        }
    }
    if (rt.machine.phase == Phase::DetachRequest) {
        // Fall evidence: the native Fall update ran since the arm.
        in.fallEntered =
            rt.drive.fallUpdates.load(std::memory_order_relaxed) !=
            rt.transport.fallUpdatesAtRequest;
    }
    if (rt.machine.phase == Phase::FallCruise)
        transport::serviceFallCruise(rt, in);
    if (rt.machine.phase == Phase::GlideHandoff)
        parasail::serviceGlideHandoff(rt, in);
    if (rt.machine.phase == Phase::GlideTerminal)
        parasail::serviceGlideTerminal(rt, in);
    if (rt.machine.phase == Phase::Capture) capture::serviceCapture(rt, in);

    const StepOutput out = step(rt.machine, in);
    if (out.event != Event::None) handleEvent(rt, out.event);

    capture::serviceInputTelemetry(rt);

    // The carrier runs only while its phase owns Link; B or a reset transitions out before this
    // call.
    if (ready && rt.machine.phase == Phase::PositionCruise) {
        if (rt.positionDrive.completed) transport::holdPositionEndpoint(rt);
        else transport::positionDriveTick(rt);
    }

    // Issue the next aim ray after stepping so the press-tick confirmation floor matches the first
    // post-press request.
    if (ready && (rt.machine.phase == Phase::Targeting ||
                  rt.machine.phase == Phase::Confirming)) {
        targeting::requestAimRay(rt);
    }
    if (ready && rt.machine.phase == Phase::FallCruise)
        transport::requestCruiseProbe(rt);

    serviceAudio(rt);
    presentation::publishChain(rt);
    // Mask exactly what this tick consumed.
    std::uint64_t mask =
        (out.consumed.a ? input::kButtonA : 0ull) |
        (out.consumed.b ? input::kButtonB : 0ull) |
        (out.consumed.stickClick ? input::kButtonRStick : 0ull);
    if (ownAim) mask |= input::kAimChord;
    if (mask) frame.maskOwnedButtons(mask);
}

// module contract

void moduleInit(std::uintptr_t base) {
    runtime().session.base = base;
    audio::initialize(base);
    world::initialize(base);
    aim::initialize(base);
    action::initialize(base);
}

void moduleEnter() {
    HookshotRuntime& rt = runtime();
    silenceAudio();
    const bool latch = rt.machine.stickClickLatched;  // quarantine survives re-entry
    rt.machine = {};
    rt.machine.stickClickLatched = latch;
    rt.prevButtons = 0;
    rt.lastButtons = 0;
    rt.haveButtons = false;
    rt.walk.targetNpadValid.store(0, std::memory_order_release);
}

bool moduleExit() {
    resetSession("module exit");
    silenceAudio();
    presentation::publishInvisible();
    return true;
}

const char* moduleStatus() { return phaseName(runtime().machine.phase); }

const char* moduleAim() { return ""; }

void moduleRaycast(wwpg::RaycastFn original, const void* from,
                   const void* /*to*/, const void* object, const void* /*out*/,
                   std::uint32_t /*mask*/, std::uint32_t /*flag*/) {
    aim::observe(original, from, object);
}

const wwpg::Module kModule{
    "ZONAI HOOKSHOT",
    "Hold ZL+R3 to aim; A fires and B cancels",
    "Green accepts, red refuses; traversal ends in native Climb",
    "Complete: target, fire, zip, and enter native Climb.",
    moduleInit,
    moduleEnter,
    runtimeTick,
    moduleExit,
    moduleStatus,
    moduleRaycast,
    moduleAim,
};

}  // namespace

namespace wwpg::modules {
const Module& zonaiHookshot() { return kModule; }
}  // namespace wwpg::modules

namespace zonai_hookshot::integration {
bool beginTargeting() {
    HookshotRuntime& rt = runtime();
    if (!g_allowActivation || !world::ready() || rt.machine.phase != pure::Phase::Idle) return false;

    rt.machine.phase = pure::Phase::Targeting;
    rt.machine.armTicks = 0;
    rt.machine.stickClickLatched = false;
    // The first post-wheel sample establishes the edge baseline so a held face button cannot fire.
    rt.prevButtons = 0;
    rt.lastButtons = 0;
    rt.haveButtons = false;
    handleEvent(rt, pure::Event::TargetingEntered);
    ZHLOG("FIELDWORKS_BEGIN_TARGETING");
    return true;
}

bool movementEngaged() {
    const HookshotRuntime& rt = runtime();
    return rt.machine.phase != pure::Phase::Idle || rt.machine.stickClickLatched;
}

bool worldReady() { return world::ready(); }

bool ownsMovement() {
    const auto phase = runtime().machine.phase;
    return phase != pure::Phase::Idle && phase != pure::Phase::Cooldown;
}

void allowActivation(bool allowed) { g_allowActivation = allowed; }

void yieldMovement() {
    auto& rt = runtime();
    transport::stopDrive(rt, "bow aim");
    aim::abandonPending();
    silenceAudio();
    const bool latch = rt.machine.stickClickLatched;
    rt.machine = {};
    rt.machine.stickClickLatched = latch;
    rt.launch = {};
    rt.positionDrive = {};
    rt.capture = {};
    rt.aim = {};
    presentation::publishInvisible();
    ZHLOG("MANUAL_DETACH reason=bow_aim");
}

}  // namespace zonai_hookshot::integration
