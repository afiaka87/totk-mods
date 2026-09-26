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
#include "ArrowSettings.hpp"
#include <arrowbound/ActiveGame.hpp>
#include "../../../engine/FlightClock.hpp"
#include <nn/util.h>
#include <array>

namespace arrowbound {
namespace {
void onWorldLost(const char* reason) { resetSession(reason); }

void saveFlightStatus(const HookshotRuntime& rt, bool allowed) {
    // Bounded idle-only reporting: never do SD I/O while following an arrow.
    static unsigned attempts{};
    static std::array<std::uint64_t,8> last{};
    if (attempts>=32 || rt.session.tick%120 || !world::ready() ||
        rt.arrowTrip.phase!=pure::ArrowPhase::Idle) return;
    pure::FlightTime time{};
    if (!game_clock::snapshot(time)) return;
    const auto& a=rt.arrow; const auto& d=rt.drive;
    const std::array<std::uint64_t,8> key{a.releaseObserved.load(),a.shotSeq.load(),
        a.followBegins.load(),a.clockRejects.load(),a.carrierWrites.load(),
        a.modeEnabled.load(),unsigned(allowed),time.serial ? unsigned(time.status)+1u : 0u};
    if (attempts && key==last) return;
    last=key; ++attempts;
    char text[1024]{};
    nn::util::SNPrintf(text,sizeof(text),
        "Glideshot embedded Arrowbound clock-coexistence\n"
        "tick=%llu enabled=%u allowed=%u accept=%u phase=%u\n"
        "clock_serial=%llu clock_status=%u clock_rejects=%u\n"
        "release_observed=%u accepted_shots=%u follow_begins=%u carrier_writes=%u\n"
        "samples=%u owner_misses=%u body_misses=%u pending=%u\n"
        "glider_enters=%u glider_updates=%u forced_predicate=%u present=%u\n",
        (unsigned long long)rt.session.tick,a.modeEnabled.load(),unsigned(allowed),a.acceptShots.load(),unsigned(rt.arrowTrip.phase),
        (unsigned long long)time.serial,unsigned(time.status),a.clockRejects.load(),
        a.releaseObserved.load(),a.shotSeq.load(),a.followBegins.load(),a.carrierWrites.load(),
        a.sampleSeq.load(),a.claimOwnerMisses.load(),a.sampleBodyMisses.load(),a.pendingShot.load(),
        d.enters.load(),d.updates.load(),d.predForced.load(),d.presentParaglider.load());
    if (!settings::writeFlightStatus(text,sizeof(text)))
        ZHLOG("FLIGHT_STATUS_WRITE_FAILED attempt=%u",attempts);
}

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
    if (const auto* game = profiles::active()) {
        action::useGameProfile(game->version);
        arrow_mode::useGameProfile(game->version);
    }
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
    saveFlightStatus(rt,allowShots);
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
