#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "pure/RiderInputRecord.hpp"

namespace linked_stick::feature {

struct RiderInputCopyToken {
    void* source = nullptr;
    std::uint64_t tick = 0;
    std::uint32_t requestedSeat = 0;
    std::uint32_t sourceSeat = 0;
    std::uint8_t endpoint = 0xFF;
    bool redirected = false;
    bool staleEndpoint = false;

    [[nodiscard]] bool traced() const { return endpoint != 0xFF; }
};

struct RiderInputTraceSnapshot {
    std::uint64_t calls = 0;
    std::uint64_t longestGapTicks = 0;
    std::uint64_t valueMismatchFrames = 0;
    std::uint32_t endpointDrifts = 0;
    std::uint32_t droppedEvents = 0;
};

class RiderInputDiagnostics {
public:
    static constexpr std::size_t kEndpointCount = 5;

    void enter();
    void onPairPublished(std::uint32_t receiverCount,
                         std::uint64_t tick);
    void onPairCleared(std::uint64_t tick);
    void tick(std::uint64_t tick, std::uint32_t receiverCount,
              bool controllerActive);
    void recordCopy(const RiderInputCopyToken& token, const void* output);
    void noteEndpointDrift(std::size_t endpoint,
                           std::uintptr_t published,
                           std::uintptr_t observed);

    [[nodiscard]] RiderInputTraceSnapshot diagnostics() const;

private:
    static constexpr std::size_t kQueueCapacity = 512;

    struct CopyEvent {
        std::uint64_t sequence = 0;
        std::uint64_t tick = 0;
        pure::RiderInputValues values{};
        std::uint32_t requestedSeat = 0;
        std::uint32_t sourceSeat = 0;
        std::uint8_t endpoint = 0xFF;
        bool redirected = false;
        bool staleEndpoint = false;
    };

    struct EndpointFrame {
        pure::RiderInputValues first{};
        pure::RiderInputValues last{};
        std::uint32_t calls = 0;
        std::uint32_t validCalls = 0;
        std::uint32_t intraFrameDifferences = 0;
    };

    struct EndpointTotals {
        std::uint64_t calls = 0;
        std::uint64_t validCalls = 0;
        std::uint64_t lastCallTick = 0;
        std::uint64_t longestGapTicks = 0;
        std::uint64_t missingFrames = 0;
        std::uint64_t comparableFrames = 0;
        std::uint64_t valueMismatchFrames = 0;
        std::uint64_t cadenceMismatchFrames = 0;
        std::uint64_t noGuideFrames = 0;
        std::uint64_t intraFrameDifferences = 0;
        std::uint64_t redirectedCalls = 0, staleCalls = 0;
        pure::RiderInputValues last{};
        bool lastStale = false;
    };

    void resetPairState();
    void beginRun(std::uint64_t tick, const char* reason);
    void finishRun(std::uint64_t tick, const char* reason);
    void emitSummary(std::uint64_t tick, const char* reason);
    void drainEvents();
    bool popEvent(CopyEvent& event);
    void processEvent(const CopyEvent& event);
    void finalizeThrough(std::uint64_t targetTick);
    void finalizeFrame(std::uint64_t tick);

    std::array<CopyEvent, kQueueCapacity> queue_{};
    std::atomic_flag queueLock_ = ATOMIC_FLAG_INIT;
    std::size_t queueRead_ = 0;
    std::size_t queueWrite_ = 0;
    std::size_t queueCount_ = 0;
    std::atomic<std::uint64_t> nextSequence_{1};
    std::atomic<std::uint32_t> droppedEvents_{0};

    std::array<EndpointFrame, kEndpointCount> frame_{};
    std::array<EndpointTotals, kEndpointCount> totals_{};
    std::uint64_t pendingTick_ = 0;
    std::uint64_t totalCalls_ = 0;
    std::uint32_t pairReceiverCount_ = 0;
    std::uint32_t activeReceiverCount_ = 0;
    std::uint32_t runId_ = 0;
    std::uint32_t endpointDrifts_ = 0;
    bool pairPublished_ = false;
    bool runActive_ = false;
    std::uint64_t summaryAt_ = 0, frameAt_ = 0;
    bool emitFrame_ = false;
};

}
