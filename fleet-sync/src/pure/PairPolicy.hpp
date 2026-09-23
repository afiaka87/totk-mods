#pragma once

#include <cstdint>

namespace linked_stick::pure {

enum class ControllerSelection : std::uint8_t {
    Accepted,
    DiscoveryUnavailable,
    NoNearbyStick,
};

inline ControllerSelection evaluateControllerSelection(
    bool discoveryReady, bool nearbyStick) {
    if (!discoveryReady) return ControllerSelection::DiscoveryUnavailable;
    return nearbyStick ? ControllerSelection::Accepted
                       : ControllerSelection::NoNearbyStick;
}

enum class ReceiverSelection : std::uint8_t {
    Accepted,
    ControllerUnavailable,
    ControllerActive,
    DiscoveryUnavailable,
    NoNearbyStick,
    SameAsController,
    AlreadySelected,
    ReceiverLimitReached,
};

struct ReceiverSelectionContext {
    bool controllerCurrent = false;
    bool controllerActive = false;
    bool discoveryReady = false;
    bool nearbyStick = false;
    bool sameAsController = false;
    bool alreadySelected = false;
    bool receiverLimitReached = false;
};

inline ReceiverSelection evaluateReceiverSelection(
    const ReceiverSelectionContext& context) {
    if (!context.controllerCurrent)
        return ReceiverSelection::ControllerUnavailable;
    if (context.controllerActive) return ReceiverSelection::ControllerActive;
    if (!context.discoveryReady)
        return ReceiverSelection::DiscoveryUnavailable;
    if (!context.nearbyStick) return ReceiverSelection::NoNearbyStick;
    if (context.sameAsController) return ReceiverSelection::SameAsController;
    if (context.alreadySelected) return ReceiverSelection::AlreadySelected;
    if (context.receiverLimitReached)
        return ReceiverSelection::ReceiverLimitReached;
    return ReceiverSelection::Accepted;
}

enum class StickCandidateKind : std::uint8_t {
    None,
    NamedProcess,
    NamedIdentity,
    ImaginaryRidable,
};

inline constexpr StickCandidateKind classifyStickCandidate(
    bool processNameMatches, bool identityNameMatches,
    bool imaginaryAutobuildActor, bool hasReceiver, bool hasRidable,
    bool hasSeat) {
    if (processNameMatches) return StickCandidateKind::NamedProcess;
    if (identityNameMatches) return StickCandidateKind::NamedIdentity;
    if (imaginaryAutobuildActor && hasReceiver && hasRidable && hasSeat)
        return StickCandidateKind::ImaginaryRidable;
    return StickCandidateKind::None;
}

inline constexpr bool shouldRedirectInitialWake(
    bool pairPublished, std::uintptr_t actualVtable,
    std::uintptr_t expectedVtable, std::uintptr_t actor,
    std::uintptr_t controller, std::uintptr_t receiver) {
    return pairPublished && actualVtable == expectedVtable && actor &&
           actor == controller && receiver;
}

struct RemoteAssemblyMatch {
    bool directActor = false;
    bool integratorPointer = false;
    bool integratorIndex = false;

    [[nodiscard]] constexpr bool matches() const {
        return directActor || integratorPointer || integratorIndex;
    }
};

enum class PresenceBoundary : std::uint8_t {
    Unload,
    Delete,
};

inline constexpr std::uintptr_t selectPresenceActor(
    PresenceBoundary boundary, std::uintptr_t candidate,
    std::uintptr_t preActorLiveActor) {
    if (!candidate) return 0;
    return boundary == PresenceBoundary::Delete ? candidate
                                                : preActorLiveActor;
}

struct StandaloneCalcContext {
    bool vanillaCalculates = false;
    bool pairPublished = false;
    std::uintptr_t candidateActor = 0;
    std::uintptr_t receiverActor = 0;
    std::uintptr_t activeController = 0;
    std::uintptr_t activeReceiver = 0;
    std::uintptr_t receiverIntegrator = 0;
};

inline constexpr bool shouldForceStandaloneReceiverCalc(
    const StandaloneCalcContext& context) {
    return !context.vanillaCalculates && context.pairPublished &&
           context.candidateActor &&
           context.candidateActor == context.receiverActor &&
           context.activeController &&
           context.activeReceiver == context.receiverActor &&
           !context.receiverIntegrator;
}

inline constexpr RemoteAssemblyMatch classifyRemoteAssemblyMatch(
    std::uintptr_t owner, std::uintptr_t receiverActor,
    std::uintptr_t ownerIntegrator,
    std::uintptr_t receiverIntegrator,
    std::uint32_t ownerIntegratorIndex,
    std::uint32_t receiverIntegratorIndex,
    std::uint32_t invalidIntegratorIndex = 0xFFFFFFFF) {
    return {
        owner && owner == receiverActor,
        owner && ownerIntegrator && receiverIntegrator &&
            ownerIntegrator == receiverIntegrator,
        owner && ownerIntegrator && receiverIntegrator &&
            ownerIntegratorIndex != invalidIntegratorIndex &&
            receiverIntegratorIndex != invalidIntegratorIndex &&
            ownerIntegratorIndex == receiverIntegratorIndex,
    };
}

inline constexpr bool isRemoteAssemblyMember(
    std::uintptr_t owner, std::uintptr_t receiverActor,
    std::uintptr_t ownerIntegrator,
    std::uintptr_t receiverIntegrator,
    std::uint32_t ownerIntegratorIndex,
    std::uint32_t receiverIntegratorIndex,
    std::uint32_t invalidIntegratorIndex = 0xFFFFFFFF) {
    return classifyRemoteAssemblyMatch(
               owner, receiverActor, ownerIntegrator, receiverIntegrator,
               ownerIntegratorIndex, receiverIntegratorIndex,
               invalidIntegratorIndex)
        .matches();
}

inline constexpr std::uintptr_t redirectEndpoint(
    std::uintptr_t queried, std::uintptr_t remote,
    std::uintptr_t controller) {
    return remote && controller && queried == remote ? controller : queried;
}

}
