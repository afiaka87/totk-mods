// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "HookshotRuntime.hpp"

#include "AimRaycaster.hpp"
#include "ArrowHookshotService.hpp"
#include "ArrowModeService.hpp"
#include "HookshotLog.hpp"

namespace arrowbound {
namespace {
HookshotRuntime g_runtime{};
}

HookshotRuntime& runtime() { return g_runtime; }

void note(const char* text) { ZHLOG("NOTE %s", text); }

void resetSession(const char* reason) {
    const bool wasActive = arrow_hookshot::engaged(g_runtime);
    arrow_hookshot::reset(g_runtime);
    g_runtime.inputOwnership = {};
    g_runtime.grip.npadValid.store(0, std::memory_order_release);
    arrow_mode::onWorldReset(g_runtime.session.worldGen + 1);
    aim::abandonPending();
    g_runtime.aim = {};
    auto& drive = g_runtime.drive;
    drive.active.store(0, std::memory_order_release);
    drive.forceEntry.store(0, std::memory_order_release);
    drive.admissionWanted.store(0, std::memory_order_release);
    drive.captureActive.store(0, std::memory_order_release);
    drive.presentParaglider.store(0, std::memory_order_release);
    drive.fallActive.store(0, std::memory_order_release);
    drive.parasailActive.store(0, std::memory_order_release);
    drive.poseState.store(0xFFFFFFFFu, std::memory_order_relaxed);
    drive.poseEntryUpdates.store(0, std::memory_order_relaxed);
    ++g_runtime.session.worldGen;
    if (wasActive) ZHLOG("RESET reason=%s", reason);
}

}  // namespace arrowbound
