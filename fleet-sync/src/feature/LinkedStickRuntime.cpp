#include "feature/LinkedStickRuntime.hpp"

#include <lib.hpp>
#include <algorithm>
#include <nn/os.h>

#include "engine/LinkedStickOffsets.hpp"
#include "engine/LinkAudio.hpp"
#include "pure/LinkFeedback.hpp"
#include "totk/engine/Pointer.hpp"

namespace linked_stick::feature {
namespace {

struct ResolvedReceiverEndpoints {
    std::uintptr_t actor = 0;
    std::uintptr_t receiver = 0;
    std::uintptr_t ridable = 0;
    std::uintptr_t seat = 0;
};

LinkedStickRuntime runtimeStorage;
}

LinkedStickRuntime& runtime() { return runtimeStorage; }

void LinkedStickRuntime::initialize(std::uintptr_t mainBase) {
    mainBase_ = mainBase;
    engine::audio::initialize(mainBase);
    world_.configure(mainBase);
    formationAssist_.configure(mainBase);
}

void LinkedStickRuntime::enter() {
    input_ = {};
    guideCopySerial_.store(0,std::memory_order_relaxed);
    fleetTelemetry_.enter();
    formationAssist_.enter();
    riderInputDiagnostics_.enter();
}

void LinkedStickRuntime::setEvent(const char* text) {
    Logging.Log("[fleet-sync] EVENT %s", text);
}

void LinkedStickRuntime::unpublishPair() {
    // Clear receiverCount first: it is the hook-publication sentinel.
    publication_.receiverCount.store(0, std::memory_order_release);
    publication_.initialWakeOneShot.store(0, std::memory_order_release);
    publication_.initialWakeReceiver.store(0, std::memory_order_relaxed);
    publication_.activePrimaryReceiver.store(0, std::memory_order_release);
    publication_.activeController.store(0, std::memory_order_relaxed);
    for (auto& receiver : publication_.receivers) {
        receiver.receiver.store(0, std::memory_order_release);
        receiver.integratorIndex.store(
            kInvalidIntegratorIndex, std::memory_order_relaxed);
        receiver.integrator.store(0, std::memory_order_relaxed);
        receiver.seat.store(0, std::memory_order_relaxed);
        receiver.observedRidable.store(0, std::memory_order_relaxed);
        receiver.ridable.store(0, std::memory_order_relaxed);
        receiver.actor.store(0, std::memory_order_relaxed);
    }
    publication_.controllerSeat.store(0, std::memory_order_relaxed);
    publication_.observedControllerRidable.store(
        0, std::memory_order_relaxed);
    publication_.controllerRidable.store(0, std::memory_order_relaxed);
    publication_.controllerReceiver.store(0, std::memory_order_release);
    publication_.controllerActor.store(0, std::memory_order_relaxed);
}

void LinkedStickRuntime::publishReceiverAssemblyIdentity(
    std::size_t index, std::uintptr_t receiverComponent,
    bool reportChange) {
    if (index >= kMaxReceivers) return;
    auto& receiver = publication_.receivers[index];
    const std::uintptr_t integrator =
        totk::engine::isPlausibleAddress(receiverComponent)
            ? totk::engine::readMemory<std::uintptr_t>(
                  receiverComponent +
                  engine::offsets::kCombinedActorIntegrator)
            : 0;
    const std::uintptr_t publishedIntegrator =
        totk::engine::isPlausibleAddress(integrator) ? integrator : 0;
    const std::uint32_t integratorIndex = publishedIntegrator
        ? totk::engine::readMemory<std::uint32_t>(
              publishedIntegrator +
              engine::offsets::kCombinedActorIntegratorIndex)
        : kInvalidIntegratorIndex;
    const std::uintptr_t previousIntegrator =
        receiver.integrator.load(std::memory_order_acquire);
    const std::uint32_t previousIndex =
        receiver.integratorIndex.load(std::memory_order_relaxed);

    // Publish index before pointer; racing hooks may skip one check, never invent an endpoint.
    receiver.integratorIndex.store(
        integratorIndex, std::memory_order_relaxed);
    receiver.integrator.store(
        publishedIntegrator, std::memory_order_release);

    if (!reportChange ||
        (previousIntegrator == publishedIntegrator &&
         previousIndex == integratorIndex)) {
        return;
    }
    counters_.assemblyRefreshes.fetch_add(1, std::memory_order_relaxed);
    Logging.Log(
        "[fleet-sync] RECEIVER_ASSEMBLY_REFRESHED "
        "slot=%u integrator=%p->%p index=%u->%u",
        static_cast<unsigned>(index + 1),
        reinterpret_cast<void*>(previousIntegrator),
        reinterpret_cast<void*>(publishedIntegrator), previousIndex,
        integratorIndex);
}

void LinkedStickRuntime::refreshPublishedReceiverState() {
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!count || count != receiverCount_) return;
    for (std::size_t index = 0; index < count; ++index) {
        if (!world_.current(receivers_[index])) {
            clearPair("receiver identity changed; links cleared");
            return;
        }
        const std::uintptr_t currentReceiver =
            world_.receiverComponent(receivers_[index]);
        const std::uintptr_t publishedReceiver =
            publication_.receivers[index].receiver.load(
                std::memory_order_acquire);
        if (!currentReceiver || currentReceiver != publishedReceiver) {
            clearPair("receiver component changed; links cleared");
            return;
        }
        publishReceiverAssemblyIdentity(index, currentReceiver, true);
    }
    refreshRiderInputEndpointObservations();
}

void LinkedStickRuntime::refreshRiderInputEndpointObservations() {
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!count || count != receiverCount_) return;
    const auto observe = [this](const totk::engine::ActorHandle& actor,
                               const std::atomic<std::uintptr_t>& ridable,
                               std::atomic<std::uintptr_t>& observedRidable,
                               bool& driftLogged, std::size_t slot) {
        const auto published = ridable.load(std::memory_order_acquire);
        const auto observed = world_.current(actor) ? world_.ridableComponent(actor) : 0;
        observedRidable.store(observed, std::memory_order_release);
        const bool drift = observed && published && observed != published;
        if (drift && !driftLogged) {
            driftLogged = true;
            riderInputDiagnostics_.noteEndpointDrift(slot, published, observed);
        } else if (!drift) {
            driftLogged = false;
        }
    };
    observe(controller_, publication_.controllerRidable,
            publication_.observedControllerRidable, controllerRidableDriftLogged_, 0);
    for (std::size_t index = 0; index < count; ++index) {
        auto& endpoint = publication_.receivers[index];
        observe(receivers_[index], endpoint.ridable, endpoint.observedRidable,
                receiverRidableDriftLogged_[index], index + 1);
    }
}

bool LinkedStickRuntime::publishPair() {
    if (!world_.current(controller_) || !receiverCount_) {
        setEvent("pair refused: endpoint identity changed");
        return false;
    }
    const std::uintptr_t controllerReceiver =
        world_.receiverComponent(controller_);
    const std::uintptr_t controllerRidable =
        world_.ridableComponent(controller_);
    const std::uintptr_t controllerSeat =
        world_.riderSeat(controllerRidable, 0);
    if (!controllerReceiver || !controllerRidable || !controllerSeat) {
        setEvent("pair refused: controller component missing");
        Logging.Log(
            "[fleet-sync] publish refused controller_spr=%p "
            "controller_ridable=%p controller_seat=%p",
            reinterpret_cast<void*>(controllerReceiver),
            reinterpret_cast<void*>(controllerRidable),
            reinterpret_cast<void*>(controllerSeat));
        return false;
    }

    const auto controllerVehicle = world_.vehicleShape(controller_);
    std::array<ResolvedReceiverEndpoints, kMaxReceivers> resolved{};
    std::array<std::uintptr_t, kMaxReceivers> assemblies{};
    for (std::size_t index = 0; index < receiverCount_; ++index) {
        if (!world_.current(receivers_[index])) {
            setEvent("pair refused: receiver identity changed");
            return false;
        }
        auto& endpoint = resolved[index];
        const auto vehicle = world_.vehicleShape(receivers_[index]);
        const char* failure = pure::vehicleMatchFailure(controllerVehicle, vehicle);
        for (std::size_t previous = 0; previous < index && !failure; ++previous)
            if (assemblies[previous] == vehicle.assembly) failure = "receiver_vehicle_already_linked";
        Logging.Log("[fleet-sync] VEHICLE_CHECK slot=%u controller_parts=%u/%u receiver_parts=%u/%u result=%s",
                    static_cast<unsigned>(index + 1), controllerVehicle.shape.count, controllerVehicle.expected,
                    vehicle.shape.count, vehicle.expected, failure ? failure : "matched");
        if (failure) return refuseSelection("RECEIVER", failure);
        assemblies[index] = vehicle.assembly;
        endpoint.actor = receivers_[index].address;
        endpoint.receiver = world_.receiverComponent(receivers_[index]);
        endpoint.ridable = world_.ridableComponent(receivers_[index]);
        endpoint.seat = world_.riderSeat(endpoint.ridable, 0);
        if (endpoint.receiver && endpoint.ridable && endpoint.seat) continue;
        setEvent("pair refused: receiver component missing");
        Logging.Log(
            "[fleet-sync] publish refused slot=%u receiver_spr=%p "
            "receiver_ridable=%p receiver_seat=%p",
            static_cast<unsigned>(index + 1),
            reinterpret_cast<void*>(endpoint.receiver),
            reinterpret_cast<void*>(endpoint.ridable),
            reinterpret_cast<void*>(endpoint.seat));
        return false;
    }

    unpublishPair();
    resetPairDiagnostics();
    publication_.controllerActor.store(controller_.address,
                                       std::memory_order_relaxed);
    publication_.controllerReceiver.store(controllerReceiver,
                                          std::memory_order_relaxed);
    publication_.controllerRidable.store(controllerRidable,
                                         std::memory_order_relaxed);
    publication_.observedControllerRidable.store(
        controllerRidable, std::memory_order_relaxed);
    publication_.controllerSeat.store(controllerSeat,
                                      std::memory_order_relaxed);
    for (std::size_t index = 0; index < receiverCount_; ++index) {
        auto& published = publication_.receivers[index];
        const auto& endpoint = resolved[index];
        published.actor.store(endpoint.actor, std::memory_order_relaxed);
        published.ridable.store(endpoint.ridable, std::memory_order_relaxed);
        published.observedRidable.store(endpoint.ridable,
                                        std::memory_order_relaxed);
        published.seat.store(endpoint.seat, std::memory_order_relaxed);
        publishReceiverAssemblyIdentity(index, endpoint.receiver, false);
        published.receiver.store(endpoint.receiver,
                                 std::memory_order_release);
    }
    publication_.receiverCount.store(
        static_cast<std::uint32_t>(receiverCount_),
        std::memory_order_release);
    fleetTelemetry_.onPairPublished();
    riderInputDiagnostics_.onPairPublished(
        static_cast<std::uint32_t>(receiverCount_),
        fleetTelemetry_.currentTick());
    formationAssist_.onPairPublished();

    Logging.Log(
        "[fleet-sync] PAIR_PUBLISHED controller=%p receivers=%u "
        "controller_spr=%p controller_ridable=%p controller_seat=%p",
        reinterpret_cast<void*>(controller_.address),
        static_cast<unsigned>(receiverCount_),
        reinterpret_cast<void*>(controllerReceiver),
        reinterpret_cast<void*>(controllerRidable),
        reinterpret_cast<void*>(controllerSeat));
    for (std::size_t index = 0; index < receiverCount_; ++index) {
        const auto& endpoint = publication_.receivers[index];
        Logging.Log(
            "[fleet-sync] RECEIVER_PUBLISHED slot=%u actor=%p spr=%p "
            "ridable=%p seat=%p integrator=%p index=%u",
            static_cast<unsigned>(index + 1),
            reinterpret_cast<void*>(
                endpoint.actor.load(std::memory_order_relaxed)),
            reinterpret_cast<void*>(
                endpoint.receiver.load(std::memory_order_relaxed)),
            reinterpret_cast<void*>(
                endpoint.ridable.load(std::memory_order_relaxed)),
            reinterpret_cast<void*>(
                endpoint.seat.load(std::memory_order_relaxed)),
            reinterpret_cast<void*>(
                endpoint.integrator.load(std::memory_order_acquire)),
            endpoint.integratorIndex.load(std::memory_order_relaxed));
    }
    return true;
}

void LinkedStickRuntime::clearPair(const char* reason) {
    riderInputDiagnostics_.onPairCleared(fleetTelemetry_.currentTick());
    fleetTelemetry_.onPairCleared();
    formationAssist_.onPairCleared();
    unpublishPair();
    controller_ = {};
    receivers_ = {};
    receiverCount_ = 0;
    setEvent(reason);
    Logging.Log("[fleet-sync] PAIR_CLEARED reason=%s", reason);
}

void LinkedStickRuntime::refreshWorld() {
    const bool hadPair = controller_.address || receiverCount_;
    worldSnapshot_ = world_.refresh();
    if (worldSnapshot_.observation == engine::WorldObservation::Unavailable) {
        if (hadPair) clearPair("world unavailable; pair cleared");
        return;
    }
    if (worldSnapshot_.observation == engine::WorldObservation::ChangedScene &&
        hadPair) {
        clearPair("scene changed; pair cleared");
    }
    bool receiverChanged = false;
    for (std::size_t index = 0; index < receiverCount_; ++index) {
        if (!world_.current(receivers_[index])) {
            receiverChanged = true;
            break;
        }
    }
    if ((controller_.address && !world_.current(controller_)) ||
        receiverChanged) {
        clearPair("endpoint identity changed; pair cleared");
    }
}

bool LinkedStickRuntime::refuseSelection(const char* role, const char* reason) {
    const auto& scan = worldSnapshot_.scan;
    Logging.Log("[fleet-sync] SELECT_%s refused=%s status=%u walked=%u matching=%u count=%u",
                role, reason, static_cast<unsigned>(scan.status), scan.visited,
                scan.matchingSticks, static_cast<unsigned>(receiverCount_));
    return false;
}

bool LinkedStickRuntime::selectController() {
    if (publication_.activeController.load(std::memory_order_acquire))
        return refuseSelection("CONTROLLER", "controller_active");
    switch (pure::evaluateControllerSelection(worldSnapshot_.scan.ready(),
                                               worldSnapshot_.nearest.actor.address != 0)) {
        case pure::ControllerSelection::DiscoveryUnavailable:
            return refuseSelection("CONTROLLER", "scanner_unavailable");
        case pure::ControllerSelection::NoNearbyStick:
            return refuseSelection("CONTROLLER", "no_nearby_stick");
        case pure::ControllerSelection::Accepted: break;
    }
    const auto vehicle = world_.vehicleShape(worldSnapshot_.nearest.actor);
    if (!vehicle.valid()) {
        Logging.Log("[fleet-sync] VEHICLE_CHECK controller_parts=%u/%u complete=%u",
                    vehicle.shape.count, vehicle.expected, vehicle.complete ? 1u : 0u);
        return refuseSelection("CONTROLLER", "controller_vehicle_unavailable");
    }
    controller_ = worldSnapshot_.nearest.actor;
    receivers_ = {};
    receiverCount_ = 0;
    unpublishPair();
    Logging.Log("[fleet-sync] CONTROLLER_SELECTED actor=%p candidate_kind=%u",
                reinterpret_cast<void*>(controller_.address),
                static_cast<unsigned>(worldSnapshot_.nearest.kind));
    return true;
}

bool LinkedStickRuntime::receiverSelected(std::uintptr_t actor) const {
    return actor && std::any_of(receivers_.begin(), receivers_.begin() + receiverCount_,
                               [actor](const auto& receiver) { return receiver.address == actor; });
}

bool LinkedStickRuntime::selectReceiver() {
    const auto actor = worldSnapshot_.nearest.actor;
    const pure::ReceiverSelectionContext context{
        world_.current(controller_),
        publication_.activeController.load(std::memory_order_acquire) != 0,
        worldSnapshot_.scan.ready(), actor.address != 0,
        actor.address && actor.address == controller_.address,
        receiverSelected(actor.address), receiverCount_ >= kMaxReceivers
    };
    using Verdict = pure::ReceiverSelection;
    const auto verdict = pure::evaluateReceiverSelection(context);
    switch (verdict) {
        case Verdict::ControllerUnavailable: return refuseSelection("RECEIVER", "controller_unavailable");
        case Verdict::ControllerActive: return refuseSelection("RECEIVER", "controller_active");
        case Verdict::DiscoveryUnavailable: return refuseSelection("RECEIVER", "scanner_unavailable");
        case Verdict::NoNearbyStick: return refuseSelection("RECEIVER", "no_nearby_stick");
        case Verdict::SameAsController: return refuseSelection("RECEIVER", "same_as_controller");
        case Verdict::AlreadySelected: return refuseSelection("RECEIVER", "already_selected");
        case Verdict::ReceiverLimitReached: return refuseSelection("RECEIVER", "limit");
        case Verdict::Accepted: break;
    }
    receivers_[receiverCount_++] = actor;
    if (!publishPair()) {
        receivers_[--receiverCount_] = {};
        return false;
    }
    Logging.Log("[fleet-sync] RECEIVER_SELECTED slot=%u actor=%p candidate_kind=%u imaginary_seen=%u",
                static_cast<unsigned>(receiverCount_), reinterpret_cast<void*>(actor.address),
                static_cast<unsigned>(worldSnapshot_.nearest.kind), worldSnapshot_.scan.imaginarySticks);
    return true;
}

void LinkedStickRuntime::processInput(void* npadDevice) {
    const auto frame = npad_.read(npadDevice);
    const auto& buttons = frame.snapshot();
    if (!buttons.freshSampleCount) return;
    const auto ticks = svcGetSystemTick();
    const auto frequency = nn::os::GetSystemTickFrequency();
    const auto nowMs = frequency > 0 ? ticks / frequency * 1000 +
        ticks % frequency * 1000 / frequency : 0;
    const bool available = frequency > 0 &&
        worldSnapshot_.observation != engine::WorldObservation::Unavailable &&
        (controller_.address || worldSnapshot_.nearest.actor.address);
    const auto action = input_.step(buttons.buttons, available, controller_.address != 0, nowMs);
    frame.maskOwnedButtons(input_.ownedButtons());
    if (action == pure::LinkAction::None) return;
    bool accepted = false;
    switch (action) {
        case pure::LinkAction::Controller: accepted = selectController(); break;
        case pure::LinkAction::Receiver: accepted = selectReceiver(); break;
        case pure::LinkAction::Clear: clearPair("cleared by player"); accepted = true; break;
        case pure::LinkAction::None: break;
    }
    engine::audio::play(pure::selectionCue(action, accepted));
    Logging.Log("[fleet-sync] INPUT action=%u accepted=%u",
                static_cast<unsigned>(action), accepted ? 1u : 0u);
}

void LinkedStickRuntime::tick(void* npadDevice) {
    refreshWorld();
    refreshPublishedReceiverState();
    processInput(npadDevice);
    flushHookDiagnostics();
    const auto endpoints = fleetTelemetryEndpoints();
    formationAssist_.heartbeat(endpoints, true);
    riderInputDiagnostics_.tick(
        fleetTelemetry_.currentTick(), endpoints.receiverCount,
        endpoints.controllerActive);
    fleetTelemetry_.tick(endpoints);
}

bool LinkedStickRuntime::beginPhysics(void* framework) {
    return formationAssist_.beginPhysics(reinterpret_cast<std::uintptr_t>(framework),
        fleetTelemetryEndpoints(), true);
}
void LinkedStickRuntime::endPhysics() { formationAssist_.endPhysics(); }

FleetTelemetryEndpoints LinkedStickRuntime::fleetTelemetryEndpoints() const {
    FleetTelemetryEndpoints endpoints{};
    endpoints.controllerReceiver =
        publication_.controllerReceiver.load(std::memory_order_acquire);
    endpoints.receiverCount =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (endpoints.receiverCount > kMaxReceivers) {
        endpoints.receiverCount = 0;
        return endpoints;
    }
    for (std::size_t index = 0; index < endpoints.receiverCount; ++index) {
        endpoints.receiverComponents[index] =
            publication_.receivers[index].receiver.load(
                std::memory_order_acquire);
    }
    endpoints.controllerActive =
        publication_.activeController.load(std::memory_order_acquire) != 0;
    return endpoints;
}

}
