// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ParasailHandoff.hpp"

#include "ClimbCapture.hpp"
#include "HookshotLog.hpp"
#include "TransportController.hpp"

namespace zonai_hookshot::parasail {
using namespace zonai_hookshot::pure;

void onHandoffArmed(HookshotRuntime& runtime) {
    // Near the anchor: stop the fast drive and enable the Fall selector's own Parasail entry.
    runtime.drive.active.store(0, std::memory_order_release);
    runtime.transport.handoffTick = runtime.session.tick;
    runtime.transport.updatesAtHandoff =
        runtime.drive.updates.load(std::memory_order_relaxed);
    runtime.drive.forceEntry.store(1, std::memory_order_release);
    ZHLOG("HANDOFF_ARM rem_dm=%d cruise_ticks=%d applied=%u",
          (int)(bitsToFloat(runtime.drive.remainingBits.load(
                    std::memory_order_relaxed)) *
                10.0f),
          (int)(runtime.session.tick - runtime.transport.cruiseStartTick),
          runtime.drive.applied.load(std::memory_order_relaxed));
    note("opening the paraglider...");
}

void onGlideEntered(HookshotRuntime& runtime) {
    // Native Parasail admitted Link; release the forced predicate.
    runtime.drive.forceEntry.store(0, std::memory_order_release);
    runtime.transport.glideStartTick = runtime.session.tick;
    runtime.transport.leavesAtGlide =
        runtime.drive.leaves.load(std::memory_order_relaxed);
    ZHLOG("GLIDE_ENTER latency=%d forced=%u",
          (int)(runtime.session.tick - runtime.transport.handoffTick),
          runtime.drive.predForced.load(std::memory_order_relaxed));
    note("gliding in - the paraglider has it from here");
}

void onGlideRefused(HookshotRuntime& runtime) {
    transport::stopDrive(runtime, "handoff refused");
    ZHLOG("GLIDE_REFUSED forced=%u rem_dm=%d",
          runtime.drive.predForced.load(std::memory_order_relaxed),
          (int)(bitsToFloat(runtime.drive.remainingBits.load(
                    std::memory_order_relaxed)) *
                10.0f));
    note("the paraglider did not open - zip ended in a fall");
}

void onGlideEnded(HookshotRuntime& runtime) {
    transport::stopDrive(runtime, "glide closed");
    ZHLOG("GLIDE_END alive_ticks=%d updates=%u",
          (int)(runtime.session.tick - runtime.transport.glideStartTick),
          runtime.drive.updates.load(std::memory_order_relaxed) -
              runtime.transport.updatesAtHandoff);
    note("zip complete: the paraglider closed");
}

void serviceGlideHandoff(HookshotRuntime& runtime, MachineInputs& inputs) {
    // Entry evidence: the native Parasail update ran since the handoff.
    inputs.glideEntered =
        runtime.drive.updates.load(std::memory_order_relaxed) !=
        runtime.transport.updatesAtHandoff;
}

void serviceGlideTerminal(HookshotRuntime& runtime, MachineInputs& inputs) {
    // The trip completes when the untouched native glide closes itself.
    inputs.glideDone = runtime.drive.leaves.load(std::memory_order_relaxed) !=
                       runtime.transport.leavesAtGlide;
}

void onEnterHook(void*) {
    HookshotRuntime& rt = runtime();
    rt.drive.enters.fetch_add(1, std::memory_order_relaxed);
    rt.drive.parasailActive.store(1, std::memory_order_release);
    if (rt.drive.admissionWanted.load(std::memory_order_acquire)) {
        rt.drive.forceEntry.store(0, std::memory_order_release);
        ZHLOG("COMPOSE_GLIDE_ENTER arms=%u forced=%u",
              rt.drive.admissionArms.load(std::memory_order_relaxed),
              rt.drive.predForced.load(std::memory_order_relaxed));
    }
}

void onUpdateHook(void* action) {
    HookshotRuntime& rt = runtime();
    rt.drive.updates.fetch_add(1, std::memory_order_relaxed);
    rt.drive.parasailActive.store(1, std::memory_order_release);
    capture::applyTerminalVelocity(action);
}

void onLeaveHook(void*) {
    HookshotRuntime& rt = runtime();
    rt.drive.leaves.fetch_add(1, std::memory_order_relaxed);
    rt.drive.parasailActive.store(0, std::memory_order_release);
    // Fail-closed: the fast drive never survives into a glide.
    rt.drive.active.store(0, std::memory_order_release);
    rt.drive.captureActive.store(0, std::memory_order_release);
}

bool onGlideEntryPredicate(bool originalResult) {
    if (originalResult) return true;
    HookshotRuntime& rt = runtime();
    if (!rt.drive.forceEntry.load(std::memory_order_acquire)) return false;
    const std::uint32_t forced =
        rt.drive.predForced.fetch_add(1, std::memory_order_relaxed) + 1;
    if (rt.drive.admissionWanted.load(std::memory_order_acquire) && forced == 1)
        ZHLOG("COMPOSE_GLIDE_PREDICATE native=0 forced=1");
    return true;
}

}  // namespace zonai_hookshot::parasail
