#include "feature/LinkedStickRuntime.hpp"
#include <lib.hpp>
#include "engine/LinkedStickOffsets.hpp"
#include "totk/engine/Pointer.hpp"

namespace linked_stick::feature {
void* LinkedStickRuntime::controlSource(void* queriedReceiver,
                                        bool countProxy) {
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!count) return queriedReceiver;
    const std::uintptr_t queried =
        reinterpret_cast<std::uintptr_t>(queriedReceiver);
    bool matched = false;
    for (std::size_t index = 0; index < count; ++index) {
        if (publication_.receivers[index].receiver.load(
                std::memory_order_acquire) == queried) {
            matched = true;
            break;
        }
    }
    if (!matched) return queriedReceiver;
    const std::uintptr_t source =
        publication_.controllerReceiver.load(std::memory_order_acquire);
    if (!source) return queriedReceiver;
    if (countProxy) {
        const std::uint32_t prior =
            counters_.getterProxies.fetch_add(1, std::memory_order_relaxed);
        if (!prior) {
            Logging.Log(
                "[fleet-sync] SPECIAL_RECEIVER_INPUT_BRIDGED "
                "receiver=%p controller=%p",
                queriedReceiver, reinterpret_cast<void*>(source));
        }
    }
    return reinterpret_cast<void*>(source);
}

void LinkedStickRuntime::observeControlAxis(
    void* queriedReceiver, ControlAxis axis, float value) {
    if (!publication_.activeController.load(std::memory_order_acquire)) return;
    const std::uintptr_t queried =
        reinterpret_cast<std::uintptr_t>(queriedReceiver);
    if (publication_.controllerReceiver.load(
            std::memory_order_acquire) == queried) {
        fleetTelemetry_.recordInput(0, axis, value);
        return;
    }
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    for (std::size_t index = 0; index < count; ++index) {
        if (publication_.receivers[index].receiver.load(
                std::memory_order_acquire) != queried) {
            continue;
        }
        fleetTelemetry_.recordInput(index + 1, axis, value);
        return;
    }
}

void* LinkedStickRuntime::activationTarget(void* mountedActor) {
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    const std::uintptr_t controller =
        publication_.controllerActor.load(std::memory_order_acquire);
    const std::uintptr_t receiver =
        count ? publication_.receivers[0].actor.load(
                    std::memory_order_acquire)
              : 0;
    if (!count || !controller || !receiver ||
        reinterpret_cast<std::uintptr_t>(mountedActor) != controller) {
        return mountedActor;
    }
    publication_.activeController.store(controller, std::memory_order_relaxed);
    publication_.activePrimaryReceiver.store(
        receiver, std::memory_order_release);
    counters_.activationProxies.fetch_add(1, std::memory_order_relaxed);
    Logging.Log(
        "[fleet-sync] ACTIVE_STICK_BRIDGED mounted=%p receiver=%p",
        mountedActor, reinterpret_cast<void*>(receiver));
    return reinterpret_cast<void*>(receiver);
}

void* LinkedStickRuntime::releaseTarget(void* mountedActor) {
    const std::uintptr_t controller =
        publication_.activeController.load(std::memory_order_acquire);
    const std::uintptr_t receiver =
        publication_.activePrimaryReceiver.load(std::memory_order_acquire);
    if (!controller || !receiver ||
        reinterpret_cast<std::uintptr_t>(mountedActor) != controller) {
        return mountedActor;
    }
    counters_.releaseProxies.fetch_add(1, std::memory_order_relaxed);
    return reinterpret_cast<void*>(receiver);
}

void LinkedStickRuntime::completeRelease(void* mountedActor,
                                         void* releaseTarget) {
    const std::uintptr_t controller =
        publication_.activeController.load(std::memory_order_acquire);
    const std::uintptr_t receiver =
        publication_.activePrimaryReceiver.load(std::memory_order_acquire);
    if (reinterpret_cast<std::uintptr_t>(mountedActor) != controller ||
        reinterpret_cast<std::uintptr_t>(releaseTarget) != receiver) {
        return;
    }
    publication_.activePrimaryReceiver.store(
        0, std::memory_order_release);
    publication_.activeController.store(0, std::memory_order_release);
    Logging.Log(
        "[fleet-sync] ACTIVE_STICK_RELEASED mounted=%p receiver=%p",
        mountedActor, releaseTarget);
}

std::uint32_t LinkedStickRuntime::publishedReceiverCount() const {
    return publication_.receiverCount.load(std::memory_order_acquire);
}

std::uint32_t LinkedStickRuntime::initialActivationFanoutCount(
    void* oneShot, void* actor) const {
    if (!oneShot || !actor || !mainBase_) return 0;
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!count) return 0;
    const std::uintptr_t expectedVtable =
        mainBase_ + engine::offsets::kRideOnCombinedControlStickVtable;
    const std::uintptr_t actualVtable =
        totk::engine::readMemory<std::uintptr_t>(
            reinterpret_cast<std::uintptr_t>(oneShot));
    const std::uintptr_t controller =
        publication_.controllerActor.load(std::memory_order_acquire);
    const std::uintptr_t primaryReceiver =
        publication_.receivers[0].actor.load(std::memory_order_acquire);
    return pure::shouldRedirectInitialWake(
               true, actualVtable, expectedVtable,
               reinterpret_cast<std::uintptr_t>(actor), controller,
               primaryReceiver)
        ? count
        : 0;
}

bool LinkedStickRuntime::beginInitialActivationPass(
    void* oneShot, std::uint32_t index) {
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!oneShot || index >= count) return false;
    const std::uintptr_t receiver =
        publication_.receivers[index].actor.load(std::memory_order_acquire);
    if (!receiver) return false;

    publication_.initialWakeReceiver.store(
        receiver, std::memory_order_relaxed);
    publication_.initialWakeOneShot.store(
        reinterpret_cast<std::uintptr_t>(oneShot),
        std::memory_order_release);
    counters_.initialWakeFanoutPasses.fetch_add(
        1, std::memory_order_relaxed);
    Logging.Log(
        "[fleet-sync] INITIAL_WAKE_FANOUT_PASS slot=%u/%u receiver=%p",
        index + 1, count, reinterpret_cast<void*>(receiver));
    return true;
}

void LinkedStickRuntime::endInitialActivationFanout() {
    publication_.initialWakeOneShot.store(0, std::memory_order_release);
    publication_.initialWakeReceiver.store(0, std::memory_order_relaxed);
}

void* LinkedStickRuntime::initialActivationActor(void* oneShot,
                                                 void* actor) {
    if (!oneShot || !actor || !mainBase_) return actor;
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!count) return actor;
    const std::uintptr_t expectedVtable =
        mainBase_ + engine::offsets::kRideOnCombinedControlStickVtable;
    const std::uintptr_t actualVtable =
        totk::engine::readMemory<std::uintptr_t>(
            reinterpret_cast<std::uintptr_t>(oneShot));
    const std::uintptr_t controller =
        publication_.controllerActor.load(std::memory_order_acquire);
    std::uintptr_t receiver =
        publication_.receivers[0].actor.load(std::memory_order_acquire);
    if (!pure::shouldRedirectInitialWake(
            true, actualVtable, expectedVtable,
            reinterpret_cast<std::uintptr_t>(actor), controller,
            receiver)) {
        return actor;
    }

    const std::uintptr_t overrideOneShot =
        publication_.initialWakeOneShot.load(std::memory_order_acquire);
    if (overrideOneShot == reinterpret_cast<std::uintptr_t>(oneShot)) {
        const std::uintptr_t overrideReceiver =
            publication_.initialWakeReceiver.load(
                std::memory_order_relaxed);
        if (overrideReceiver) receiver = overrideReceiver;
    }

    const std::uint32_t prior =
        counters_.initialWakeProxies.fetch_add(
            1, std::memory_order_relaxed);
    if (!prior) {
        Logging.Log(
            "[fleet-sync] INITIAL_WAKE_BRIDGED controller=%p receiver=%p",
            actor, reinterpret_cast<void*>(receiver));
    }
    return reinterpret_cast<void*>(receiver);
}

pure::RemoteAssemblyMatch
LinkedStickRuntime::classifyPublishedReceiverAssembly(
    std::uintptr_t actor) const {
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!totk::engine::isPlausibleAddress(actor) || !count) return {};

    std::uintptr_t actorIntegrator = 0;
    std::uint32_t actorIntegratorIndex = kInvalidIntegratorIndex;
    const std::uintptr_t registry =
        totk::engine::readMemory<std::uintptr_t>(
            actor + engine::offsets::kComponentRegistry);
    const std::uintptr_t receiver =
        totk::engine::isPlausibleAddress(registry)
            ? totk::engine::readMemory<std::uintptr_t>(
                  registry + engine::offsets::kSpecialPowerReceiver)
            : 0;
    actorIntegrator =
        totk::engine::isPlausibleAddress(receiver)
            ? totk::engine::readMemory<std::uintptr_t>(
                  receiver + engine::offsets::kCombinedActorIntegrator)
            : 0;
    if (totk::engine::isPlausibleAddress(actorIntegrator)) {
        actorIntegratorIndex =
            totk::engine::readMemory<std::uint32_t>(
                actorIntegrator +
                engine::offsets::kCombinedActorIntegratorIndex);
    }

    pure::RemoteAssemblyMatch aggregate{};
    for (std::size_t index = 0; index < count; ++index) {
        const auto& receiver = publication_.receivers[index];
        const auto match = pure::classifyRemoteAssemblyMatch(
            actor,
            receiver.actor.load(std::memory_order_acquire),
            actorIntegrator,
            receiver.integrator.load(std::memory_order_acquire),
            actorIntegratorIndex,
            receiver.integratorIndex.load(std::memory_order_relaxed),
            kInvalidIntegratorIndex);
        aggregate.directActor |= match.directActor;
        aggregate.integratorPointer |= match.integratorPointer;
        aggregate.integratorIndex |= match.integratorIndex;
    }
    return aggregate;
}

void LinkedStickRuntime::extendRemoteEnergyRange(void* gearControl) {
    if (!totk::engine::isPlausibleAddress(
            reinterpret_cast<std::uintptr_t>(gearControl))) {
        return;
    }
    if (!publication_.receiverCount.load(std::memory_order_acquire)) return;
    counters_.rangeChecks.fetch_add(1, std::memory_order_relaxed);

    const std::uintptr_t gearAddress =
        reinterpret_cast<std::uintptr_t>(gearControl);
    const std::uintptr_t owner = totk::engine::readMemory<std::uintptr_t>(
        gearAddress + engine::offsets::kComponentOwnerActor);
    const pure::RemoteAssemblyMatch match =
        classifyPublishedReceiverAssembly(owner);
    if (match.directActor) {
        counters_.rangeDirectMatches.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (match.integratorPointer) {
        counters_.rangePointerMatches.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (match.integratorIndex) {
        counters_.rangeIndexMatches.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (!match.matches()) return;

    counters_.rangeProxies.fetch_add(1, std::memory_order_release);
    totk::engine::writeMemory<std::uint32_t>(
        gearAddress + engine::offsets::kZonauGearEnergyRange,
        kRemoteEnergyRangeBits);
}

bool LinkedStickRuntime::shouldForceStandaloneReceiverCalc(
    void* actor, bool vanillaCalculates) {
    const bool pairPublished =
        publication_.receiverCount.load(std::memory_order_acquire) != 0;
    if (!pairPublished) return false;

    const std::uintptr_t candidateActor =
        reinterpret_cast<std::uintptr_t>(actor);
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_relaxed);
    std::uintptr_t receiverActor = 0;
    std::uintptr_t receiverIntegrator = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& receiver = publication_.receivers[index];
        if (receiver.actor.load(std::memory_order_acquire) != candidateActor)
            continue;
        receiverActor = candidateActor;
        receiverIntegrator =
            receiver.integrator.load(std::memory_order_acquire);
        break;
    }
    if (!receiverActor) return false;

    counters_.standaloneCalcChecks.fetch_add(
        1, std::memory_order_relaxed);
    const std::uintptr_t activeController =
        publication_.activeController.load(std::memory_order_acquire);
    const pure::StandaloneCalcContext context{
        vanillaCalculates,
        pairPublished,
        candidateActor,
        receiverActor,
        activeController,
        activeController ? candidateActor : 0,
        receiverIntegrator,
    };
    if (!pure::shouldForceStandaloneReceiverCalc(context)) return false;

    counters_.standaloneCalcGuards.fetch_add(
        1, std::memory_order_release);
    return true;
}

bool LinkedStickRuntime::shouldKeepRemoteAssemblyResidentAtUnload(
    void* preActor) {
    if (!publication_.receiverCount.load(std::memory_order_acquire))
        return false;
    const std::uintptr_t candidate =
        reinterpret_cast<std::uintptr_t>(preActor);
    if (!totk::engine::isPlausibleAddress(candidate)) return false;

    const std::uintptr_t preActorLiveActor =
        totk::engine::readMemory<std::uintptr_t>(
            candidate + engine::offsets::kPreActorLiveActor);
    return shouldKeepRemoteAssemblyResident(
        pure::selectPresenceActor(
            pure::PresenceBoundary::Unload, candidate,
            preActorLiveActor),
        pure::PresenceBoundary::Unload);
}

bool LinkedStickRuntime::shouldKeepRemoteAssemblyResidentAtDelete(
    void* actor) {
    const std::uintptr_t candidate = reinterpret_cast<std::uintptr_t>(actor);
    return shouldKeepRemoteAssemblyResident(
        pure::selectPresenceActor(
            pure::PresenceBoundary::Delete, candidate, 0),
        pure::PresenceBoundary::Delete);
}

bool LinkedStickRuntime::shouldKeepRemoteAssemblyResident(
    std::uintptr_t actor, pure::PresenceBoundary boundary) {
    if (!publication_.receiverCount.load(std::memory_order_acquire))
        return false;

    const bool isDelete = boundary == pure::PresenceBoundary::Delete;
    auto& checks = isDelete ? counters_.deleteChecks
                            : counters_.unloadChecks;
    checks.fetch_add(1, std::memory_order_relaxed);
    if (!totk::engine::isPlausibleAddress(actor)) return false;

    const pure::RemoteAssemblyMatch match =
        classifyPublishedReceiverAssembly(actor);
    if (match.directActor) {
        counters_.residencyDirectMatches.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (match.integratorPointer) {
        counters_.residencyPointerMatches.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (match.integratorIndex) {
        counters_.residencyIndexMatches.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (!match.matches()) return false;

    auto& guards = isDelete ? counters_.deleteGuards
                            : counters_.unloadGuards;
    guards.fetch_add(1, std::memory_order_release);
    return true;
}
}
