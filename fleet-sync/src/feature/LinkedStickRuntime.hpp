#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "engine/StickWorld.hpp"
#include "feature/FleetTelemetry.hpp"
#include "feature/FormationAssist.hpp"
#include "feature/RiderInputDiagnostics.hpp"
#include "pure/PairPolicy.hpp"
#include "pure/LinkInput.hpp"
#include "totk/engine/Npad.hpp"

namespace linked_stick::feature {

class LinkedStickRuntime {
public:
    static constexpr std::size_t kMaxReceivers = 4;

    void initialize(std::uintptr_t mainBase);
    void enter();
    void tick(void* npadDevice);
    bool beginPhysics(void* framework);
    void endPhysics();

    // Hooks use release/acquire addresses, never tick-owned handles.
    void* controlSource(void* queriedReceiver, bool countProxy = true);
    void observeControlAxis(void* queriedReceiver, ControlAxis axis,
                            float value);
    void* activationTarget(void* mountedActor);
    void* releaseTarget(void* mountedActor);
    void completeRelease(void* mountedActor, void* releaseTarget);
    [[nodiscard]] std::uint32_t publishedReceiverCount() const;
    std::uint32_t initialActivationFanoutCount(
        void* oneShot, void* actor) const;
    bool beginInitialActivationPass(void* oneShot, std::uint32_t index);
    void endInitialActivationFanout();
    void* initialActivationActor(void* oneShot, void* actor);
    void extendRemoteEnergyRange(void* gearControl);
    bool shouldForceStandaloneReceiverCalc(
        void* actor, bool vanillaCalculates);
    bool shouldKeepRemoteAssemblyResidentAtUnload(void* preActor);
    bool shouldKeepRemoteAssemblyResidentAtDelete(void* actor);
    void* riddenSource(void* queriedRidable);
    void* ridableRiderTypeSource(void* queriedRidable);
    void* riderTypeSource(void* queriedSeat);
    RiderInputCopyToken beginRiderInputCopy(void* queriedRidable,
                                            std::uint32_t seatIndex);
    void completeRiderInputCopy(const RiderInputCopyToken& token,
                                const void* output);

private:
    static constexpr std::uint32_t kRemoteEnergyRangeBits = 0x461C4000;
    static constexpr std::uint32_t kRemoteEnergyRangeMeters = 10000;
    static constexpr std::uint32_t kInvalidIntegratorIndex = 0xFFFFFFFF;
    static constexpr std::uint32_t kGuardDiagnosticCheckThreshold = 64;
    enum class PublishedEndpointKind : std::uint8_t {
        Ridable,
        Seat,
    };

    struct ReceiverPublication {
        std::atomic<std::uintptr_t> actor{0};
        std::atomic<std::uintptr_t> receiver{0};
        std::atomic<std::uintptr_t> ridable{0};
        std::atomic<std::uintptr_t> observedRidable{0};
        std::atomic<std::uintptr_t> seat{0};
        std::atomic<std::uintptr_t> integrator{0};
        std::atomic<std::uint32_t> integratorIndex{0xFFFFFFFF};
    };

    struct PairPublication {
        std::atomic<std::uintptr_t> controllerActor{0};
        std::atomic<std::uintptr_t> controllerReceiver{0};
        std::atomic<std::uintptr_t> controllerRidable{0};
        std::atomic<std::uintptr_t> observedControllerRidable{0};
        std::atomic<std::uintptr_t> controllerSeat{0};
        std::array<ReceiverPublication, kMaxReceivers> receivers{};
        std::atomic<std::uint32_t> receiverCount{0};
        std::atomic<std::uintptr_t> activeController{0};
        std::atomic<std::uintptr_t> activePrimaryReceiver{0};
        std::atomic<std::uintptr_t> initialWakeOneShot{0};
        std::atomic<std::uintptr_t> initialWakeReceiver{0};
    };

    struct Counters {
        std::atomic<std::uint32_t> getterProxies{0};
        std::atomic<std::uint32_t> riddenProxies{0};
        std::atomic<std::uint32_t> riderTypeProxies{0};
        std::atomic<std::uint32_t> riderInputProxies{0};
        std::atomic<std::uint32_t> activationProxies{0};
        std::atomic<std::uint32_t> initialWakeProxies{0};
        std::atomic<std::uint32_t> initialWakeFanoutPasses{0};
        std::atomic<std::uint32_t> rangeProxies{0};
        std::atomic<std::uint32_t> rangeChecks{0};
        std::atomic<std::uint32_t> rangeDirectMatches{0};
        std::atomic<std::uint32_t> rangePointerMatches{0};
        std::atomic<std::uint32_t> rangeIndexMatches{0};
        std::atomic<std::uint32_t> assemblyRefreshes{0};
        std::atomic<std::uint32_t> unloadGuards{0};
        std::atomic<std::uint32_t> unloadChecks{0};
        std::atomic<std::uint32_t> deleteGuards{0};
        std::atomic<std::uint32_t> deleteChecks{0};
        std::atomic<std::uint32_t> residencyDirectMatches{0};
        std::atomic<std::uint32_t> residencyPointerMatches{0};
        std::atomic<std::uint32_t> residencyIndexMatches{0};
        std::atomic<std::uint32_t> standaloneCalcChecks{0};
        std::atomic<std::uint32_t> standaloneCalcGuards{0};
        std::atomic<std::uint32_t> releaseProxies{0};
    };

    void refreshWorld();
    void refreshPublishedReceiverState();
    void refreshRiderInputEndpointObservations();
    void publishReceiverAssemblyIdentity(
        std::size_t index, std::uintptr_t receiverComponent,
        bool reportChange);
    pure::RemoteAssemblyMatch classifyPublishedReceiverAssembly(
        std::uintptr_t actor) const;
    bool shouldKeepRemoteAssemblyResident(
        std::uintptr_t actor, pure::PresenceBoundary boundary);
    void resetPairDiagnostics();
    void processInput(void* npadDevice);
    void flushHookDiagnostics();
    bool selectController();
    bool selectReceiver();
    bool refuseSelection(const char* role, const char* reason);
    bool publishPair();
    void unpublishPair();
    void clearPair(const char* reason);
    void setEvent(const char* text);
    [[nodiscard]] FleetTelemetryEndpoints fleetTelemetryEndpoints() const;
    void* redirectPublishedEndpoint(
        void* queried, PublishedEndpointKind endpointKind,
        const std::atomic<std::uintptr_t>& controller,
        std::atomic<std::uint32_t>& counter, const char* seam);
    [[nodiscard]] bool receiverSelected(std::uintptr_t actor) const;

    engine::StickWorld world_{};
    std::uintptr_t mainBase_ = 0;
    totk::engine::NpadReader npad_{};
    FleetTelemetry fleetTelemetry_{};
    FormationAssist formationAssist_{};
    RiderInputDiagnostics riderInputDiagnostics_{};
    engine::WorldSnapshot worldSnapshot_{};
    totk::engine::ActorHandle controller_{};
    std::array<totk::engine::ActorHandle, kMaxReceivers> receivers_{};
    std::size_t receiverCount_ = 0;
    pure::LinkInput input_{};
    std::atomic<std::uint64_t> guideCopySerial_{0};
    PairPublication publication_{};
    Counters counters_{};
    bool rangeHitLogged_ = false;
    bool rangeMissLogged_ = false;
    bool residencyHitLogged_ = false;
    bool residencyMissLogged_ = false;
    bool standaloneCalcObservedLogged_ = false;
    bool standaloneCalcHitLogged_ = false;
    bool controllerRidableDriftLogged_ = false;
    std::array<bool, kMaxReceivers> receiverRidableDriftLogged_{};
};

LinkedStickRuntime& runtime();

}
