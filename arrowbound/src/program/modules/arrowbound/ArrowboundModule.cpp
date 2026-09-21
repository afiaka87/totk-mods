// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ArrowboundModule.hpp"

#include "AimRaycaster.hpp"
#include "ActionContext.hpp"
#include "ArrowboundHookInstaller.hpp"
#include "ArrowHookshotService.hpp"
#include "ArrowModeService.hpp"
#include "HookshotAudio.hpp"
#include "HookshotInput.hpp"
#include "HookshotLog.hpp"
#include "HookshotRuntime.hpp"
#include "HookshotWorld.hpp"

namespace arrowbound {
namespace {
void onWorldLost(const char* reason) { resetSession(reason); }

void serviceAimMailbox(HookshotRuntime& rt) {
    pure::TargetSample sample{};
    const auto poll = aim::service(rt.session.tick, sample);
    if (poll == aim::Poll::TimedOut) {
        ++rt.aim.starved;
        return;
    }
    if (poll != aim::Poll::Ready) return;
    rt.aim.sample = sample;
    rt.aim.sampleTick = rt.session.tick;
    rt.aim.starved = 0;
}
}

void initialize(std::uintptr_t mainBase) {
    auto& rt = runtime();
    rt.session.base = mainBase;
    audio::initialize(mainBase);
    world::initialize(mainBase);
    action::initialize(mainBase);
    aim::initialize(mainBase);
    arrow_mode::initialize(mainBase);
}

void enter() {
    auto& rt = runtime();
    audio::resetAbilityCues();
    rt.prevButtons = 0;
    rt.lastButtons = 0;
    rt.haveButtons = false;
    rt.inputOwnership = {};
    arrow_hookshot::reset(rt);
    arrow_mode::onWorldReset(rt.session.worldGen);
}

void tick(void* device, bool allowShots) {
    auto& rt = runtime();
    ++rt.session.tick;
    world::refresh(onWorldLost);
    serviceAimMailbox(rt);
    arrow_mode::service(rt);

    const auto frame = input::readFrame(device);
    const int liveController = input::newestLiveController(device);
    if (liveController >= 0)
        rt.grip.npadId.store(static_cast<std::uint32_t>(liveController), std::memory_order_relaxed);
    rt.grip.npadValid.store(liveController >= 0 ? 1u : 0u, std::memory_order_release);
    const bool fresh = frame.snapshot().freshSampleCount > 0;
    const std::uint64_t buttons = fresh ? frame.snapshot().buttons : rt.lastButtons;
    bool bEdge = false;
    bool zrEdge = false;
    if (fresh && rt.haveButtons) {
        bEdge = (buttons & input::kButtonB) && !(rt.prevButtons & input::kButtonB);
        zrEdge = (buttons & input::kButtonZR) && !(rt.prevButtons & input::kButtonZR);
    }
    if (fresh) {
        rt.prevButtons = rt.lastButtons = buttons;
        rt.haveButtons = true;
    }

    const auto inputPhase = pure::arrowHasFlight(rt.arrowTrip.phase,
        rt.arrow.shotSeq.load(std::memory_order_acquire), rt.arrowTrip.shotSeqSeen) ?
        (rt.arrowTrip.phase == pure::ArrowPhase::Idle ? pure::ArrowPhase::WaitingForArrow :
                                                     rt.arrowTrip.phase) : pure::ArrowPhase::Idle;
    const auto controls = rt.inputOwnership.update(inputPhase,
        (buttons & input::kButtonB) != 0, bEdge, zrEdge);
    arrow_hookshot::service(rt, controls.cancel, controls.reaim, allowShots);
    if (controls.consumeB) frame.maskOwnedButtons(input::kButtonB);

    if (world::ready() && !audio::ready() && (rt.session.tick % 60) == 0)
        audio::prime();
    audio::updateAbilityCues(world::playerPosition());
}

void onRaycast(RaycastFn original, const void* from, const void* object) {
    aim::observe(original, from, object);
}

bool movementEngaged() {
    const auto& rt = runtime();
    return pure::arrowHasFlight(rt.arrowTrip.phase,
        rt.arrow.shotSeq.load(std::memory_order_acquire), rt.arrowTrip.shotSeqSeen);
}

void yieldMovement() {
    arrow_hookshot::reset(runtime());
    ZHLOG("ARROW_DETACH reason=manual_hookshot");
}

void installFeatureHooks(std::uintptr_t mainBase) { hooks::install(mainBase, false); }

void sharedGripHooksReady(bool ready) {
    runtime().grip.hooksReady.store(ready ? 1u : 0u, std::memory_order_release);
}

}  // namespace arrowbound
