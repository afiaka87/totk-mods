#include "feature/RiderInputDiagnostics.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <lib.hpp>
#include <nn/os.h>

#include "lib/program/loggers.hpp"

namespace linked_stick::feature {
namespace {
constexpr float kMilliScale = 1000.0f;

[[nodiscard]] std::int32_t scaledInteger(std::uint32_t bits) {
    const float value = std::bit_cast<float>(bits);
    if (!std::isfinite(value)) return std::numeric_limits<std::int32_t>::min();
    const float scaled = value * kMilliScale;
    if (scaled <= static_cast<float>(
                      std::numeric_limits<std::int32_t>::min() + 1)) {
        return std::numeric_limits<std::int32_t>::min() + 1;
    }
    if (scaled >= static_cast<float>(
                      std::numeric_limits<std::int32_t>::max())) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(scaled);
}

}

void RiderInputDiagnostics::enter() {
    if (runActive_) finishRun(pendingTick_, "module-enter");
    runId_ = 0;
    endpointDrifts_ = 0;
    resetPairState();
}

void RiderInputDiagnostics::onPairPublished(std::uint32_t receiverCount,
                                             std::uint64_t tick) {
    if (runActive_) finishRun(tick, "pair-republished");
    resetPairState();
    pairPublished_ = true;
    pairReceiverCount_ = std::min<std::uint32_t>(
        receiverCount, static_cast<std::uint32_t>(kEndpointCount - 1));
    Logging.Log(
        "[fleet-sync][input] PAIR_READY format=2 tick=%llu receivers=%u "
        "trace=periodic-independent forward_offset=0x8 steering_offset=0xc",
        static_cast<unsigned long long>(tick), pairReceiverCount_);
}

void RiderInputDiagnostics::onPairCleared(std::uint64_t tick) {
    drainEvents();
    if (runActive_) {
        finalizeThrough(tick);
        finishRun(tick, "pair-cleared");
    }
    resetPairState();
}

void RiderInputDiagnostics::tick(std::uint64_t tick,
                                 std::uint32_t receiverCount,
                                 bool controllerActive) {
    pairReceiverCount_ = std::min<std::uint32_t>(
        receiverCount, static_cast<std::uint32_t>(kEndpointCount - 1));
    if (pairPublished_ && controllerActive && !runActive_) beginRun(tick, "controller-active");

    drainEvents();
    const auto now = svcGetSystemTick();
    const auto frequency = nn::os::GetSystemTickFrequency();
    const bool summaryDue = !summaryAt_ || (frequency && now - summaryAt_ >= frequency);
    emitFrame_ = !frameAt_ || (frequency && now - frameAt_ >= frequency / 2);
    if (emitFrame_) frameAt_ = now;
    if (runActive_ && tick) finalizeThrough(tick - 1);
    if (summaryDue) {
        summaryAt_ = now;
        emitSummary(tick, "periodic");
    }
    if (!runActive_) return;

    // Finalize tick N-1 to allow late cross-thread CopyRiderInput publication.
    if (!controllerActive) {
        finalizeThrough(tick);
        finishRun(tick, "controller-inactive");
    }
}

void RiderInputDiagnostics::recordCopy(const RiderInputCopyToken& token,
                                       const void* output) {
    if (!token.traced()) return;

    CopyEvent event{};
    event.sequence = nextSequence_.fetch_add(1, std::memory_order_relaxed);
    event.tick = token.tick;
    event.values = pure::readRiderInputValues(output);
    event.requestedSeat = token.requestedSeat;
    event.sourceSeat = token.sourceSeat;
    event.endpoint = token.endpoint;
    event.redirected = token.redirected;
    event.staleEndpoint = token.staleEndpoint;

    while (queueLock_.test_and_set(std::memory_order_acquire)) {
    }
    if (queueCount_ == kQueueCapacity) {
        droppedEvents_.fetch_add(1, std::memory_order_relaxed);
        queueLock_.clear(std::memory_order_release);
        return;
    }
    queue_[queueWrite_] = event;
    queueWrite_ = (queueWrite_ + 1) % kQueueCapacity;
    ++queueCount_;
    queueLock_.clear(std::memory_order_release);
}

void RiderInputDiagnostics::noteEndpointDrift(
    std::size_t endpoint, std::uintptr_t published,
    std::uintptr_t observed) {
    ++endpointDrifts_;
    Logging.Log(
        "[fleet-sync][input] ENDPOINT_DRIFT endpoint=%u published=%p "
        "observed=%p routing_uses=published",
        static_cast<unsigned>(endpoint),
        reinterpret_cast<void*>(published),
        reinterpret_cast<void*>(observed));
}

RiderInputTraceSnapshot RiderInputDiagnostics::diagnostics() const {
    std::uint64_t longestGap = 0;
    std::uint64_t mismatches = 0;
    for (std::size_t endpoint = 1; endpoint < kEndpointCount; ++endpoint) {
        longestGap = std::max(longestGap,
                              totals_[endpoint].longestGapTicks);
        mismatches += totals_[endpoint].valueMismatchFrames;
    }
    return {
        totalCalls_,
        longestGap,
        mismatches,
        endpointDrifts_,
        droppedEvents_.load(std::memory_order_relaxed),
    };
}

void RiderInputDiagnostics::resetPairState() {
    while (queueLock_.test_and_set(std::memory_order_acquire)) {
    }
    queueRead_ = 0;
    queueWrite_ = 0;
    queueCount_ = 0;
    queueLock_.clear(std::memory_order_release);
    droppedEvents_.store(0, std::memory_order_relaxed);
    frame_ = {};
    totals_ = {};
    pendingTick_ = 0;
    totalCalls_ = 0;
    pairReceiverCount_ = 0;
    activeReceiverCount_ = 0;
    endpointDrifts_ = 0;
    pairPublished_ = false;
    runActive_ = false;
}

void RiderInputDiagnostics::beginRun(std::uint64_t tick,
                                     const char* reason) {
    ++runId_;
    runActive_ = true;
    activeReceiverCount_ = pairReceiverCount_;
    pendingTick_ = 0;
    frame_ = {};
    totals_ = {};
    totalCalls_ = 0;
    Logging.Log(
        "[fleet-sync][input] BEGIN format=2 run=%u tick=%llu "
        "receivers=%u trace=periodic-independent reason=%s",
        runId_, static_cast<unsigned long long>(tick),
        activeReceiverCount_, reason);
}

void RiderInputDiagnostics::finishRun(std::uint64_t tick,
                                      const char* reason) {
    emitSummary(tick, reason);
    Logging.Log("[fleet-sync][input] END run=%u tick=%llu reason=%s", runId_,
                static_cast<unsigned long long>(tick), reason);
    runActive_ = false;
    pendingTick_ = 0;
    frame_ = {};
}

void RiderInputDiagnostics::emitSummary(std::uint64_t tick, const char* reason) {
    const std::uint32_t dropped =
        droppedEvents_.load(std::memory_order_relaxed);
    for (std::size_t endpoint = 0;
         endpoint <= std::max(pairReceiverCount_, activeReceiverCount_); ++endpoint) {
        const auto& totals = totals_[endpoint];
        Logging.Log(
            "[fleet-sync][input] ENDPOINT_SUMMARY run=%u endpoint=%u "
            "calls=%llu valid=%llu missing_frames=%llu "
            "longest_gap_ticks=%llu comparable=%llu value_diff=%llu "
            "cadence_diff=%llu no_guide=%llu intra_diff=%llu "
            "redirected=%llu stale=%llu last_tick=%llu last_valid=%u last_m=%d,%d reason=%s",
            runId_, static_cast<unsigned>(endpoint),
            static_cast<unsigned long long>(totals.calls),
            static_cast<unsigned long long>(totals.validCalls),
            static_cast<unsigned long long>(totals.missingFrames),
            static_cast<unsigned long long>(totals.longestGapTicks),
            static_cast<unsigned long long>(totals.comparableFrames),
            static_cast<unsigned long long>(totals.valueMismatchFrames),
            static_cast<unsigned long long>(
                totals.cadenceMismatchFrames),
            static_cast<unsigned long long>(totals.noGuideFrames),
            static_cast<unsigned long long>(totals.intraFrameDifferences),
            static_cast<unsigned long long>(totals.redirectedCalls),
            static_cast<unsigned long long>(totals.staleCalls),
            static_cast<unsigned long long>(totals.lastCallTick), totals.last.finite ? 1u : 0u,
            scaledInteger(totals.last.forwardBits), scaledInteger(totals.last.steeringBits), reason);
    }
    Logging.Log(
        "[fleet-sync][input] PROGRESS run=%u tick=%llu calls=%llu "
        "dropped=%u endpoint_drifts=%u reason=%s paired=%u active=%u",
        runId_, static_cast<unsigned long long>(tick),
        static_cast<unsigned long long>(totalCalls_), dropped,
        endpointDrifts_, reason, pairPublished_ ? 1u : 0u, runActive_ ? 1u : 0u);
}

void RiderInputDiagnostics::drainEvents() {
    CopyEvent event{};
    while (popEvent(event)) processEvent(event);
}

bool RiderInputDiagnostics::popEvent(CopyEvent& event) {
    while (queueLock_.test_and_set(std::memory_order_acquire)) {
    }
    if (!queueCount_) {
        queueLock_.clear(std::memory_order_release);
        return false;
    }
    event = queue_[queueRead_];
    queueRead_ = (queueRead_ + 1) % kQueueCapacity;
    --queueCount_;
    queueLock_.clear(std::memory_order_release);
    return true;
}

void RiderInputDiagnostics::processEvent(const CopyEvent& event) {
    if (!runActive_) beginRun(event.tick, "copy-observed");
    if (event.endpoint >= kEndpointCount) return;

    if (!pendingTick_) pendingTick_ = event.tick;
    if (event.tick < pendingTick_) {
        Logging.Log(
            "[fleet-sync][input] LATE_CALL run=%u seq=%llu tick=%llu "
            "pending=%llu endpoint=%u",
            runId_, static_cast<unsigned long long>(event.sequence),
            static_cast<unsigned long long>(event.tick),
            static_cast<unsigned long long>(pendingTick_),
            static_cast<unsigned>(event.endpoint));
        return;
    }
    if (event.tick > pendingTick_) finalizeThrough(event.tick - 1);

    auto& endpoint = frame_[event.endpoint];
    if (!endpoint.calls) {
        endpoint.first = event.values;
        endpoint.last = event.values;
    } else {
        if (!pure::sameRiderInput(endpoint.last, event.values)) {
            ++endpoint.intraFrameDifferences;
        }
        endpoint.last = event.values;
    }
    ++endpoint.calls;
    if (event.values.finite) ++endpoint.validCalls;
    ++totalCalls_;
    auto& totals = totals_[event.endpoint];
    totals.redirectedCalls += event.redirected ? 1 : 0;
    totals.staleCalls += event.staleEndpoint ? 1 : 0;
    const auto previous = totals.last;
    totals.last = event.values;
    const bool material = previous.readable != event.values.readable ||
        previous.finite != event.values.finite || event.staleEndpoint != totals.lastStale ||
        scaledInteger(event.values.steeringBits) / 100 != scaledInteger(previous.steeringBits) / 100 ||
        scaledInteger(event.values.forwardBits) / 100 != scaledInteger(previous.forwardBits) / 100;
    totals.lastStale = event.staleEndpoint;
    if (material) Logging.Log(
        "[fleet-sync][input] CALL run=%u seq=%llu tick=%llu endpoint=%u "
        "redirected=%u stale=%u requested_seat=%u source_seat=%u "
        "valid=%u forward_m=%d steering_m=%d forward_bits=%08x "
        "steering_bits=%08x",
        runId_, static_cast<unsigned long long>(event.sequence),
        static_cast<unsigned long long>(event.tick),
        static_cast<unsigned>(event.endpoint),
        event.redirected ? 1u : 0u,
        event.staleEndpoint ? 1u : 0u, event.requestedSeat,
        event.sourceSeat, event.values.finite ? 1u : 0u,
        scaledInteger(event.values.forwardBits),
        scaledInteger(event.values.steeringBits),
        event.values.forwardBits, event.values.steeringBits);
}

void RiderInputDiagnostics::finalizeThrough(std::uint64_t targetTick) {
    if (!pendingTick_ || pendingTick_ > targetTick) return;
    while (pendingTick_ <= targetTick) {
        finalizeFrame(pendingTick_);
        frame_ = {};
        ++pendingTick_;
    }
}

void RiderInputDiagnostics::finalizeFrame(std::uint64_t tick) {
    const auto& guide = frame_[0];
    auto& guideTotals = totals_[0];
    guideTotals.calls += guide.calls;
    guideTotals.validCalls += guide.validCalls;
    guideTotals.intraFrameDifferences += guide.intraFrameDifferences;
    if (guide.calls) guideTotals.lastCallTick = tick;

    for (std::size_t endpoint = 1;
         endpoint <= activeReceiverCount_; ++endpoint) {
        const auto& receiver = frame_[endpoint];
        auto& totals = totals_[endpoint];
        totals.calls += receiver.calls;
        totals.validCalls += receiver.validCalls;
        totals.intraFrameDifferences += receiver.intraFrameDifferences;

        if (receiver.calls) {
            const std::uint64_t gap = pure::missingTicksBetween(
                totals.lastCallTick, tick);
            totals.longestGapTicks =
                std::max(totals.longestGapTicks, gap);
            totals.lastCallTick = tick;
        } else {
            if (guide.calls) ++totals.missingFrames;
            if (totals.lastCallTick) {
                totals.longestGapTicks = std::max(
                    totals.longestGapTicks, tick - totals.lastCallTick);
            }
        }

        int valueEqual = -1;
        if (guide.calls && receiver.calls) {
            ++totals.comparableFrames;
            valueEqual =
                !guide.intraFrameDifferences &&
                        !receiver.intraFrameDifferences &&
                        pure::sameRiderInput(guide.first,
                                             receiver.first) &&
                        pure::sameRiderInput(guide.last,
                                             receiver.last)
                    ? 1
                    : 0;
            if (!valueEqual) ++totals.valueMismatchFrames;
            if (guide.calls != receiver.calls) {
                ++totals.cadenceMismatchFrames;
            }
        } else if (receiver.calls && !guide.calls) {
            ++totals.noGuideFrames;
        }

        if (emitFrame_) Logging.Log(
            "[fleet-sync][input] FRAME run=%u tick=%llu receiver=%u "
            "guide_calls=%u receiver_calls=%u guide_m=%d,%d "
            "receiver_m=%d,%d value_equal=%d cadence_equal=%u "
            "intra_diff=%u longest_gap_ticks=%llu",
            runId_, static_cast<unsigned long long>(tick),
            static_cast<unsigned>(endpoint), guide.calls,
            receiver.calls, scaledInteger(guide.last.forwardBits),
            scaledInteger(guide.last.steeringBits),
            scaledInteger(receiver.last.forwardBits),
            scaledInteger(receiver.last.steeringBits), valueEqual,
            guide.calls == receiver.calls ? 1u : 0u,
            guide.intraFrameDifferences +
                receiver.intraFrameDifferences,
            static_cast<unsigned long long>(totals.longestGapTicks));
    }
    emitFrame_ = false;
}

}
