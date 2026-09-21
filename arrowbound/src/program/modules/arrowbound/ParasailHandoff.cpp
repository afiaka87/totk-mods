// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ParasailHandoff.hpp"

#include "HookshotLog.hpp"
#include "HookshotRuntime.hpp"
#include "WallGripService.hpp"
#include "totk/engine/Pointer.hpp"

namespace arrowbound::parasail {
namespace {
void observePose(void* action, bool entered) {
    auto& rt = runtime();
    auto& drive = rt.drive;
    if (!action || !drive.presentParaglider.load(std::memory_order_acquire)) return;
    const auto phase = *reinterpret_cast<const std::uint32_t*>(
        reinterpret_cast<std::uintptr_t>(action) + 0x20);
    using GetControllerFn = std::uintptr_t (*)(void*);
    using GetRagdollFn = std::uintptr_t (*)(std::uintptr_t);
    const auto controller = reinterpret_cast<GetControllerFn>(
        rt.session.base + 0x01B6766C)(action);
    const auto ragdoll = totk::engine::isPlausibleAddress(controller) ?
        reinterpret_cast<GetRagdollFn>(rt.session.base + 0x00830D24)(controller) : 0;
    const bool valid = totk::engine::isPlausibleAddress(ragdoll);
    const std::uint32_t enabled = valid ? *reinterpret_cast<const std::uint8_t*>(ragdoll + 0x15C) : 0;
    const std::uint32_t mode = valid ? *reinterpret_cast<const std::uint32_t*>(ragdoll + 0x14C) : 0xFF;
    const auto state = (phase & 0xFF) | ((enabled & 0xFF) << 8) | ((mode & 0xFF) << 16);
    const auto previous = drive.poseState.exchange(state, std::memory_order_relaxed);
    if (entered || state != previous)
        ZHLOG("GLIDE_POSE shot=%u update=%u phase=%u ragdoll_valid=%u enabled=%u mode=%u entry=%u",
              rt.arrow.shotSeq.load(std::memory_order_relaxed),
              drive.updates.load(std::memory_order_relaxed) -
                  drive.poseEntryUpdates.load(std::memory_order_relaxed),
              phase, (unsigned)valid, enabled, mode, (unsigned)entered);
}
}

void onFallEnterHook(void*) {
    auto& drive = runtime().drive;
    drive.fallEnters.fetch_add(1, std::memory_order_relaxed);
    drive.fallActive.store(1, std::memory_order_release);
    if (drive.admissionWanted.load(std::memory_order_acquire)) {
        const auto arms = drive.admissionArms.fetch_add(1, std::memory_order_relaxed) + 1;
        drive.forceEntry.store(1, std::memory_order_release);
        ZHLOG("GLIDE_ARM source=fall_enter arms=%u", arms);
    }
}

void onFallUpdateHook(void*) {
    auto& drive = runtime().drive;
    drive.fallUpdates.fetch_add(1, std::memory_order_relaxed);
    drive.fallActive.store(1, std::memory_order_release);
}

void onFallLeaveHook(void*) {
    auto& drive = runtime().drive;
    drive.fallLeaves.fetch_add(1, std::memory_order_relaxed);
    drive.fallActive.store(0, std::memory_order_release);
}

void onEnterHook(void* action) {
    auto& drive = runtime().drive;
    drive.enters.fetch_add(1, std::memory_order_relaxed);
    drive.parasailActive.store(1, std::memory_order_release);
    drive.poseEntryUpdates.store(drive.updates.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
    drive.poseState.store(0xFFFFFFFFu, std::memory_order_relaxed);
    if (drive.admissionWanted.load(std::memory_order_acquire)) {
        drive.forceEntry.store(0, std::memory_order_release);
        ZHLOG("GLIDE_ENTER arms=%u forced=%u",
              drive.admissionArms.load(std::memory_order_relaxed),
              drive.predForced.load(std::memory_order_relaxed));
    }
    observePose(action, true);
}

void onUpdateHook(void* action) {
    auto& drive = runtime().drive;
    drive.updates.fetch_add(1, std::memory_order_relaxed);
    drive.parasailActive.store(1, std::memory_order_release);
    observePose(action, false);
    wall_grip::onParasailUpdate(action);
}

void onLeaveHook(void*) {
    auto& drive = runtime().drive;
    drive.leaves.fetch_add(1, std::memory_order_relaxed);
    drive.parasailActive.store(0, std::memory_order_release);
    drive.active.store(0, std::memory_order_release);
    drive.captureActive.store(0, std::memory_order_release);
}

bool onGlideEntryPredicate(bool originalResult) {
    if (originalResult) return true;
    auto& drive = runtime().drive;
    if (!drive.forceEntry.load(std::memory_order_acquire)) return false;
    const auto forced = drive.predForced.fetch_add(1, std::memory_order_relaxed) + 1;
    if (drive.admissionWanted.load(std::memory_order_acquire) && forced == 1)
        ZHLOG("GLIDE_PREDICATE native=0 forced=1");
    return true;
}

}  // namespace arrowbound::parasail
