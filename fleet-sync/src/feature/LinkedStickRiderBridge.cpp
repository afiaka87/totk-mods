#include "feature/LinkedStickRuntime.hpp"

#include <lib.hpp>

namespace linked_stick::feature {

void* LinkedStickRuntime::redirectPublishedEndpoint(
    void* queried, PublishedEndpointKind endpointKind,
    const std::atomic<std::uintptr_t>& controller,
    std::atomic<std::uint32_t>& counter, const char* seam) {
    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!count) return queried;
    const std::uintptr_t queriedEndpoint =
        reinterpret_cast<std::uintptr_t>(queried);
    std::uintptr_t remoteEndpoint = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& receiver = publication_.receivers[index];
        const std::uintptr_t candidate =
            endpointKind == PublishedEndpointKind::Seat
                ? receiver.seat.load(std::memory_order_acquire)
                : receiver.ridable.load(std::memory_order_acquire);
        if (candidate != queriedEndpoint) continue;
        remoteEndpoint = candidate;
        break;
    }
    if (!remoteEndpoint) return queried;
    const std::uintptr_t controllerEndpoint =
        controller.load(std::memory_order_acquire);
    const std::uintptr_t redirected = pure::redirectEndpoint(
        reinterpret_cast<std::uintptr_t>(queried), remoteEndpoint,
        controllerEndpoint);
    if (redirected == reinterpret_cast<std::uintptr_t>(queried)) {
        return queried;
    }
    const std::uint32_t prior =
        counter.fetch_add(1, std::memory_order_relaxed);
    if (!prior) {
        Logging.Log(
            "[fleet-sync] RIDER_CONTEXT_BRIDGED seam=%s "
            "receiver=%p controller=%p",
            seam, queried, reinterpret_cast<void*>(redirected));
    }
    return reinterpret_cast<void*>(redirected);
}

void* LinkedStickRuntime::riddenSource(void* queriedRidable) {
    return redirectPublishedEndpoint(
        queriedRidable, PublishedEndpointKind::Ridable,
        publication_.controllerRidable, counters_.riddenProxies,
        "is_ridden");
}

void* LinkedStickRuntime::ridableRiderTypeSource(void* queriedRidable) {
    return redirectPublishedEndpoint(
        queriedRidable, PublishedEndpointKind::Ridable,
        publication_.controllerRidable, counters_.riderTypeProxies,
        "ridable_rider_type");
}

void* LinkedStickRuntime::riderTypeSource(void* queriedSeat) {
    return redirectPublishedEndpoint(
        queriedSeat, PublishedEndpointKind::Seat,
        publication_.controllerSeat, counters_.riderTypeProxies,
        "rider_type");
}

RiderInputCopyToken LinkedStickRuntime::beginRiderInputCopy(
    void* queriedRidable, std::uint32_t seatIndex) {
    RiderInputCopyToken token{};
    token.source = queriedRidable;
    token.tick = fleetTelemetry_.currentTick();
    token.requestedSeat = seatIndex;
    token.sourceSeat = seatIndex;

    const std::uint32_t count =
        publication_.receiverCount.load(std::memory_order_acquire);
    if (!count || count > kMaxReceivers) return token;

    const std::uintptr_t queried =
        reinterpret_cast<std::uintptr_t>(queriedRidable);
    const std::uintptr_t controller =
        publication_.controllerRidable.load(std::memory_order_acquire);
    const std::uintptr_t observedController =
        publication_.observedControllerRidable.load(
            std::memory_order_acquire);
    if (queried &&
        (queried == controller || queried == observedController)) {
        token.endpoint = 0;
        token.staleEndpoint = queried != controller;
        return token;
    }

    for (std::size_t index = 0; index < count; ++index) {
        const auto& receiver = publication_.receivers[index];
        const std::uintptr_t published =
            receiver.ridable.load(std::memory_order_acquire);
        const std::uintptr_t observed =
            receiver.observedRidable.load(std::memory_order_acquire);
        if (queried != published && queried != observed) continue;

        token.endpoint = static_cast<std::uint8_t>(index + 1);
        token.staleEndpoint = queried != published;
        if (token.staleEndpoint || !controller) return token;

        token.source = reinterpret_cast<void*>(controller);
        token.redirected = true;
        const std::uint32_t prior = counters_.riderInputProxies.fetch_add(
            1, std::memory_order_relaxed);
        if (!prior) {
            Logging.Log(
                "[fleet-sync] RIDER_CONTEXT_BRIDGED seam=rider_input "
                "receiver=%p controller=%p requested_seat=%u",
                queriedRidable, token.source, seatIndex);
        }
        return token;
    }
    return token;
}

void LinkedStickRuntime::completeRiderInputCopy(
    const RiderInputCopyToken& token, const void* output) {
    if (token.endpoint==0 && !token.staleEndpoint)
        guideCopySerial_.fetch_add(1,std::memory_order_release);
    if (publication_.activeController.load(std::memory_order_acquire))
        riderInputDiagnostics_.recordCopy(token, output);
}

}
