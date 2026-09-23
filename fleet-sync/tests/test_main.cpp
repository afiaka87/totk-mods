#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstring>

#include "pure/FleetTelemetryMath.hpp"
#include "pure/PairPolicy.hpp"
#include "pure/RiderInputRecord.hpp"

namespace pure = linked_stick::pure;

TEST_CASE("rider input decoder reads the TotK 1.2.1 output fields") {
    std::array<std::byte, pure::kRiderInputRecordSize> record{};
    const float forward = -0.375f;
    const float steering = 0.625f;
    std::memcpy(record.data() + pure::kRiderInputForwardOffset,
                &forward, sizeof(forward));
    std::memcpy(record.data() + pure::kRiderInputSteeringOffset,
                &steering, sizeof(steering));

    const auto values = pure::readRiderInputValues(record.data());
    CHECK(values.readable);
    CHECK(values.finite);
    CHECK(std::bit_cast<float>(values.forwardBits) == forward);
    CHECK(std::bit_cast<float>(values.steeringBits) == steering);
}

TEST_CASE("rider input decoder preserves invalid output evidence") {
    std::array<std::byte, pure::kRiderInputRecordSize> record{};
    const std::uint32_t quietNan = 0x7FC00000;
    std::memcpy(record.data() + pure::kRiderInputForwardOffset,
                &quietNan, sizeof(quietNan));
    std::memcpy(record.data() + pure::kRiderInputSteeringOffset,
                &quietNan, sizeof(quietNan));

    const auto values = pure::readRiderInputValues(record.data());
    CHECK(values.readable);
    CHECK_FALSE(values.finite);
    CHECK(values.forwardBits == quietNan);
    CHECK(values.steeringBits == quietNan);
}

TEST_CASE("rider input gap counts only missing ticks") {
    CHECK(pure::missingTicksBetween(0, 10) == 0);
    CHECK(pure::missingTicksBetween(10, 11) == 0);
    CHECK(pure::missingTicksBetween(10, 14) == 3);
}

TEST_CASE("controller selection distinguishes scanner and nearby-stick failures") {
    CHECK(pure::evaluateControllerSelection(false, false) ==
          pure::ControllerSelection::DiscoveryUnavailable);
    CHECK(pure::evaluateControllerSelection(true, false) ==
          pure::ControllerSelection::NoNearbyStick);
    CHECK(pure::evaluateControllerSelection(true, true) ==
          pure::ControllerSelection::Accepted);
}

TEST_CASE("receiver selection distinguishes each refusal") {
    CHECK(pure::evaluateReceiverSelection({}) ==
          pure::ReceiverSelection::ControllerUnavailable);
    CHECK(pure::evaluateReceiverSelection({.controllerCurrent = true}) ==
          pure::ReceiverSelection::DiscoveryUnavailable);
    CHECK(pure::evaluateReceiverSelection(
              {.controllerCurrent = true, .discoveryReady = true}) ==
          pure::ReceiverSelection::NoNearbyStick);
    CHECK(pure::evaluateReceiverSelection(
              {.controllerCurrent = true,
               .discoveryReady = true,
               .nearbyStick = true,
               .sameAsController = true}) ==
          pure::ReceiverSelection::SameAsController);
    CHECK(pure::evaluateReceiverSelection(
              {.controllerCurrent = true,
               .discoveryReady = true,
               .nearbyStick = true}) ==
          pure::ReceiverSelection::Accepted);
    CHECK(pure::evaluateReceiverSelection(
              {.controllerCurrent = true,
               .discoveryReady = true,
               .nearbyStick = true,
               .alreadySelected = true}) ==
          pure::ReceiverSelection::AlreadySelected);
    CHECK(pure::evaluateReceiverSelection(
              {.controllerCurrent = true,
               .discoveryReady = true,
               .nearbyStick = true,
               .receiverLimitReached = true}) ==
          pure::ReceiverSelection::ReceiverLimitReached);
    CHECK(pure::evaluateReceiverSelection(
              {.controllerCurrent = true,
               .controllerActive = true,
               .discoveryReady = true,
               .nearbyStick = true}) ==
          pure::ReceiverSelection::ControllerActive);
}

TEST_CASE("bridge redirects only the published remote endpoint") {
    constexpr std::uintptr_t queried = 0x1000;
    constexpr std::uintptr_t remote = 0x2000;
    constexpr std::uintptr_t controller = 0x3000;

    CHECK(pure::redirectEndpoint(queried, remote, controller) == queried);
    CHECK(pure::redirectEndpoint(remote, remote, controller) == controller);
    CHECK(pure::redirectEndpoint(remote, 0, controller) == remote);
    CHECK(pure::redirectEndpoint(remote, remote, 0) == remote);
}

TEST_CASE("stick candidate classification keeps ordinary actors out") {
    using pure::StickCandidateKind;

    CHECK(pure::classifyStickCandidate(
              true, false, false, false, false, false) ==
          StickCandidateKind::NamedProcess);
    CHECK(pure::classifyStickCandidate(
              false, true, false, false, false, false) ==
          StickCandidateKind::NamedIdentity);
    CHECK(pure::classifyStickCandidate(
              false, false, true, true, true, true) ==
          StickCandidateKind::ImaginaryRidable);
    CHECK(pure::classifyStickCandidate(
              false, false, true, true, true, false) ==
          StickCandidateKind::None);
    CHECK(pure::classifyStickCandidate(
              false, false, false, true, true, true) ==
          StickCandidateKind::None);
}

TEST_CASE("initial wake redirect is exact to the paired control-stick event") {
    CHECK(pure::shouldRedirectInitialWake(
        true, 0x10, 0x10, 0x20, 0x20, 0x30));
    CHECK_FALSE(pure::shouldRedirectInitialWake(
        true, 0x11, 0x10, 0x20, 0x20, 0x30));
    CHECK_FALSE(pure::shouldRedirectInitialWake(
        true, 0x10, 0x10, 0x21, 0x20, 0x30));
    CHECK_FALSE(pure::shouldRedirectInitialWake(
        false, 0x10, 0x10, 0x20, 0x20, 0x30));
}

TEST_CASE("remote energy range applies only to the receiver assembly") {
    const auto direct = pure::classifyRemoteAssemblyMatch(
        0x20, 0x20, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF);
    CHECK(direct.directActor);
    CHECK_FALSE(direct.integratorPointer);
    CHECK_FALSE(direct.integratorIndex);

    const auto byIntegrator = pure::classifyRemoteAssemblyMatch(
        0x21, 0x20, 0x40, 0x40, 7, 7);
    CHECK_FALSE(byIntegrator.directActor);
    CHECK(byIntegrator.integratorPointer);
    CHECK(byIntegrator.integratorIndex);

    CHECK(pure::isRemoteAssemblyMember(
        0x20, 0x20, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF));
    CHECK(pure::isRemoteAssemblyMember(
        0x21, 0x20, 0x40, 0x40, 7, 7));
    CHECK(pure::isRemoteAssemblyMember(
        0x21, 0x20, 0x41, 0x40, 7, 7));
    CHECK_FALSE(pure::isRemoteAssemblyMember(
        0x21, 0x20, 0x41, 0x40, 8, 7));
    CHECK_FALSE(pure::isRemoteAssemblyMember(
        0x21, 0x20, 0x41, 0x40, 0xFFFFFFFF, 0xFFFFFFFF));
}

TEST_CASE("presence boundaries select their version-verified actor shape") {
    constexpr std::uintptr_t candidate = 0x1000;
    constexpr std::uintptr_t preActorLiveActor = 0x2000;

    CHECK(pure::selectPresenceActor(
              pure::PresenceBoundary::Unload, candidate,
              preActorLiveActor) == preActorLiveActor);
    CHECK(pure::selectPresenceActor(
              pure::PresenceBoundary::Delete, candidate,
              preActorLiveActor) == candidate);
    CHECK(pure::selectPresenceActor(
              pure::PresenceBoundary::Delete, 0,
              preActorLiveActor) == 0);
}

TEST_CASE("standalone calc lease excludes every working craft path") {
    const pure::StandaloneCalcContext looseRemote{
        false, true, 0x20, 0x20, 0x10, 0x20, 0};
    CHECK(pure::shouldForceStandaloneReceiverCalc(looseRemote));

    auto context = looseRemote;
    context.vanillaCalculates = true;
    CHECK_FALSE(pure::shouldForceStandaloneReceiverCalc(context));

    context = looseRemote;
    context.pairPublished = false;
    CHECK_FALSE(pure::shouldForceStandaloneReceiverCalc(context));

    context = looseRemote;
    context.candidateActor = 0x21;
    CHECK_FALSE(pure::shouldForceStandaloneReceiverCalc(context));

    context = looseRemote;
    context.activeController = 0;
    CHECK_FALSE(pure::shouldForceStandaloneReceiverCalc(context));

    context = looseRemote;
    context.activeReceiver = 0x21;
    CHECK_FALSE(pure::shouldForceStandaloneReceiverCalc(context));

    context = looseRemote;
    context.receiverIntegrator = 0x40;
    CHECK_FALSE(pure::shouldForceStandaloneReceiverCalc(context));
}
