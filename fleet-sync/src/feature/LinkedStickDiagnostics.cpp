#include "feature/LinkedStickRuntime.hpp"
#include <lib.hpp>
#include "engine/LinkedStickOffsets.hpp"
#include "totk/engine/Pointer.hpp"

namespace linked_stick::feature {
void LinkedStickRuntime::resetPairDiagnostics() {
    counters_.getterProxies.store(0, std::memory_order_relaxed);
    counters_.riddenProxies.store(0, std::memory_order_relaxed);
    counters_.riderTypeProxies.store(0, std::memory_order_relaxed);
    counters_.riderInputProxies.store(0, std::memory_order_relaxed);
    counters_.activationProxies.store(0, std::memory_order_relaxed);
    counters_.initialWakeProxies.store(0, std::memory_order_relaxed);
    counters_.initialWakeFanoutPasses.store(0, std::memory_order_relaxed);
    counters_.rangeProxies.store(0, std::memory_order_relaxed);
    counters_.rangeChecks.store(0, std::memory_order_relaxed);
    counters_.rangeDirectMatches.store(0, std::memory_order_relaxed);
    counters_.rangePointerMatches.store(0, std::memory_order_relaxed);
    counters_.rangeIndexMatches.store(0, std::memory_order_relaxed);
    counters_.assemblyRefreshes.store(0, std::memory_order_relaxed);
    counters_.unloadGuards.store(0, std::memory_order_relaxed);
    counters_.unloadChecks.store(0, std::memory_order_relaxed);
    counters_.deleteGuards.store(0, std::memory_order_relaxed);
    counters_.deleteChecks.store(0, std::memory_order_relaxed);
    counters_.residencyDirectMatches.store(0, std::memory_order_relaxed);
    counters_.residencyPointerMatches.store(0, std::memory_order_relaxed);
    counters_.residencyIndexMatches.store(0, std::memory_order_relaxed);
    counters_.standaloneCalcChecks.store(0, std::memory_order_relaxed);
    counters_.standaloneCalcGuards.store(0, std::memory_order_relaxed);
    counters_.releaseProxies.store(0, std::memory_order_relaxed);
    rangeHitLogged_ = false;
    rangeMissLogged_ = false;
    residencyHitLogged_ = false;
    residencyMissLogged_ = false;
    standaloneCalcObservedLogged_ = false;
    standaloneCalcHitLogged_ = false;
    controllerRidableDriftLogged_ = false;
    receiverRidableDriftLogged_ = {};
}

void LinkedStickRuntime::flushHookDiagnostics() {
    const std::uint32_t rangeHits =
        counters_.rangeProxies.load(std::memory_order_acquire);
    const std::uint32_t rangeChecks =
        counters_.rangeChecks.load(std::memory_order_acquire);
    if (!rangeHitLogged_ && rangeHits) {
        rangeHitLogged_ = true;
        Logging.Log(
            "[fleet-sync] REMOTE_ENERGY_RANGE_BRIDGED remote_m=%u",
            kRemoteEnergyRangeMeters);
    }
    if (!rangeMissLogged_ && !rangeHits &&
        rangeChecks >= kGuardDiagnosticCheckThreshold) {
        rangeMissLogged_ = true;
        Logging.Log(
            "[fleet-sync] REMOTE_ENERGY_RANGE_UNMATCHED "
            "checks=%u direct=%u pointer=%u index=%u "
            "receivers=%u primary_integrator=%p primary_index=%u "
            "refreshes=%u",
            rangeChecks,
            counters_.rangeDirectMatches.load(
                std::memory_order_relaxed),
            counters_.rangePointerMatches.load(
                std::memory_order_relaxed),
            counters_.rangeIndexMatches.load(
                std::memory_order_relaxed),
            publication_.receiverCount.load(std::memory_order_acquire),
            reinterpret_cast<void*>(
                publication_.receivers[0].integrator.load(
                    std::memory_order_acquire)),
            publication_.receivers[0].integratorIndex.load(
                std::memory_order_relaxed),
            counters_.assemblyRefreshes.load(
                std::memory_order_relaxed));
    }

    const std::uint32_t unloadGuards =
        counters_.unloadGuards.load(std::memory_order_acquire);
    const std::uint32_t deleteGuards =
        counters_.deleteGuards.load(std::memory_order_acquire);
    const std::uint32_t unloadChecks =
        counters_.unloadChecks.load(std::memory_order_acquire);
    const std::uint32_t deleteChecks =
        counters_.deleteChecks.load(std::memory_order_acquire);
    if (!residencyHitLogged_ && (unloadGuards || deleteGuards)) {
        residencyHitLogged_ = true;
        Logging.Log(
            "[fleet-sync] REMOTE_ASSEMBLY_RESIDENCY_GUARDED "
            "unload=%u delete=%u",
            unloadGuards, deleteGuards);
    }
    if (!residencyMissLogged_ && !unloadGuards && !deleteGuards &&
        unloadChecks + deleteChecks >= kGuardDiagnosticCheckThreshold) {
        residencyMissLogged_ = true;
        Logging.Log(
            "[fleet-sync] REMOTE_ASSEMBLY_RESIDENCY_UNMATCHED "
            "unload_checks=%u delete_checks=%u "
            "direct=%u pointer=%u index=%u",
            unloadChecks, deleteChecks,
            counters_.residencyDirectMatches.load(
                std::memory_order_relaxed),
            counters_.residencyPointerMatches.load(
                std::memory_order_relaxed),
            counters_.residencyIndexMatches.load(
                std::memory_order_relaxed));
    }

    const std::uint32_t standaloneCalcChecks =
        counters_.standaloneCalcChecks.load(std::memory_order_acquire);
    const std::uint32_t standaloneCalcGuards =
        counters_.standaloneCalcGuards.load(std::memory_order_acquire);
    if (!standaloneCalcObservedLogged_ &&
        standaloneCalcChecks >= kGuardDiagnosticCheckThreshold) {
        standaloneCalcObservedLogged_ = true;
        Logging.Log(
            "[fleet-sync] STANDALONE_RECEIVER_CALC_OBSERVED "
            "checks=%u guards=%u active=%p integrator=%p",
            standaloneCalcChecks, standaloneCalcGuards,
            reinterpret_cast<void*>(
                publication_.activeController.load(
                    std::memory_order_acquire)),
            reinterpret_cast<void*>(
                publication_.receivers[0].integrator.load(
                    std::memory_order_acquire)));
    }
    if (!standaloneCalcHitLogged_ && standaloneCalcGuards) {
        standaloneCalcHitLogged_ = true;
        Logging.Log(
            "[fleet-sync] STANDALONE_RECEIVER_CALC_GUARDED "
            "checks=%u guards=%u",
            standaloneCalcChecks, standaloneCalcGuards);
    }
}
}
