// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "HookshotRuntime.hpp"

#include "ChainPresentationService.hpp"
#include "HookshotLog.hpp"
#include "TransportController.hpp"

namespace zonai_hookshot {
namespace {
HookshotRuntime g_runtime{};
}  // namespace

HookshotRuntime& runtime() { return g_runtime; }

void note(const char* text) { ZHLOG("NOTE %s", text); }

void resetSession(const char* reason) {
    const bool wasActive = g_runtime.machine.phase != pure::Phase::Idle;
    // The stick-click quarantine is input state: it survives every reset and clears only on a
    // physical release.
    const bool latch = g_runtime.machine.stickClickLatched;
    g_runtime.machine = {};
    g_runtime.machine.stickClickLatched = latch;
    g_runtime.launch = {};
    g_runtime.positionDrive = {};
    g_runtime.capture = {};
    g_runtime.aim.sample = {};
    g_runtime.aim.confirmFloorSeq = 0;
    g_runtime.aim.starved = 0;
    g_runtime.aim.latchFeedbackTicks = 0;
    transport::stopDrive(g_runtime, "reset");
    // Fail-closed on world loss: the native-ownership trackers may be stale.
    auto& drive = g_runtime.drive;
    drive.fallActive.store(0, std::memory_order_release);
    drive.parasailActive.store(0, std::memory_order_release);
    drive.directionSamples.store(0, std::memory_order_relaxed);
    drive.directionAlignBits.store(floatToBits(0.0f), std::memory_order_relaxed);
    drive.directionSideBits.store(floatToBits(0.0f), std::memory_order_relaxed);
    drive.directionLateralBits.store(floatToBits(0.0f),
                                     std::memory_order_relaxed);
    const int tier = g_runtime.transport.tier;  // a bench setting, keep it
    g_runtime.transport = {};
    g_runtime.transport.tier = tier;
    ++g_runtime.session.worldGen;
    presentation::publishInvisible();
    if (wasActive) {
        ZHLOG("RESET reason=%s", reason);
        note("reset: the world changed under the hookshot");
    }
}

}  // namespace zonai_hookshot
