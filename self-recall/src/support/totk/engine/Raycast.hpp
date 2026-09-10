
#pragma once

#include "totk/core/Result.hpp"
#include "totk/core/Units.hpp"
#include "totk/engine/Totk121Offsets.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace totk::engine {

using RaycastFunction = std::uint64_t (*)(const void*, const void*, const void*,
                                          const void*, std::uint32_t, std::uint32_t);

struct RaycastTicket {
    std::uint32_t value = 0;
    core::SceneGeneration generation{};

    [[nodiscard]] friend constexpr bool operator==(RaycastTicket, RaycastTicket) = default;
};

struct RaycastRequest {
    core::WorldPosition from{};
    core::WorldPosition to{};
    core::WorldPosition relevancePoint{};
    core::DistanceMeters relevanceRadius{8.0F};
    std::uint32_t mask = 0;
    core::TickCount submittedAt{};
    core::SceneGeneration generation{};
};

struct RaycastHit {
    bool hit = false;
    core::WorldPosition position{};
    core::SurfaceNormal normal{};
    core::DistanceMeters distance{};
    std::uint64_t engineResult = 0;
};

enum class RaycastBeginError : std::uint8_t {
    Busy,
    NonFiniteRequest,
    InvalidRadius,
};

enum class RaycastPollStatus : std::uint8_t {
    Idle,
    Pending,
    Ready,
    TimedOut,
    GenerationMismatch,
};

struct RaycastPoll {
    RaycastPollStatus status = RaycastPollStatus::Idle;
    RaycastTicket ticket{};
    RaycastHit result{};
};

class RaycastMailbox {
public:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "raycast mailbox requires lock-free 32-bit atomics");

    [[nodiscard]] core::Result<RaycastTicket, RaycastBeginError>
    begin(const RaycastRequest& request) {
        if (!finite(request.from) || !finite(request.to) ||
            !finite(request.relevancePoint)) {
            return core::Result<RaycastTicket, RaycastBeginError>::failure(
                RaycastBeginError::NonFiniteRequest);
        }
        if (!std::isfinite(request.relevanceRadius.value) ||
            request.relevanceRadius.value <= 0.0F) {
            return core::Result<RaycastTicket, RaycastBeginError>::failure(
                RaycastBeginError::InvalidRadius);
        }

        auto expected = State::Idle;
        if (!state_.compare_exchange_strong(expected, State::Working,
                                            std::memory_order_acq_rel)) {
            return core::Result<RaycastTicket, RaycastBeginError>::failure(
                RaycastBeginError::Busy);
        }

        request_ = request;
        ++nextTicket_;
        if (nextTicket_ == 0) ++nextTicket_;
        ticket_ = RaycastTicket{nextTicket_, request.generation};
        cancelledTicket_.store(0, std::memory_order_release);
        state_.store(State::Pending, std::memory_order_release);
        return core::Result<RaycastTicket, RaycastBeginError>::success(ticket_);
    }

    void observe(RaycastFunction original, const void* liveFrom,
                 const void* liveObject) {
        if (!original || !liveFrom || !liveObject) return;

        auto expected = State::Pending;
        if (!state_.compare_exchange_strong(expected, State::Working,
                                            std::memory_order_acq_rel)) {
            return;
        }

        core::WorldPosition castFrom{};
        std::memcpy(&castFrom, liveFrom, sizeof(castFrom));
        const float radius = request_.relevanceRadius.value;
        if (distanceSquared(castFrom, request_.relevancePoint) >= radius * radius) {
            publishAfterWorker(State::Pending);
            return;
        }

        std::memcpy(queryObject_, liveObject, sizeof(queryObject_));
        queryObject_[layout::kRaycastHit] = 0;
        engineResult_ =
            original(&request_.from, &request_.to, queryObject_, nullptr,
                     request_.mask, 0U);
        publishAfterWorker(State::Ready);
    }

    [[nodiscard]] RaycastPoll poll(RaycastTicket expectedTicket,
                                   core::TickCount now,
                                   std::uint64_t timeoutTicks) {
        const auto state = state_.load(std::memory_order_acquire);
        if (state == State::Idle) {
            return RaycastPoll{RaycastPollStatus::Idle, expectedTicket, {}};
        }
        if (ticket_ != expectedTicket ||
            request_.generation != expectedTicket.generation) {
            cancel();
            return RaycastPoll{
                RaycastPollStatus::GenerationMismatch, expectedTicket, {}};
        }
        if (state == State::Pending &&
            core::elapsedTicks(now, request_.submittedAt) > timeoutTicks) {
            auto pending = State::Pending;
            if (state_.compare_exchange_strong(pending, State::Idle,
                                               std::memory_order_acq_rel)) {
                return RaycastPoll{
                    RaycastPollStatus::TimedOut, expectedTicket, {}};
            }
        }
        if (state_.load(std::memory_order_acquire) != State::Ready) {
            return RaycastPoll{RaycastPollStatus::Pending, expectedTicket, {}};
        }

        RaycastHit hit{};
        hit.hit = queryObject_[layout::kRaycastHit] != 0;
        std::memcpy(&hit.position, queryObject_ + layout::kRaycastPosition,
                    sizeof(hit.position));
        std::memcpy(&hit.normal, queryObject_ + layout::kRaycastNormal,
                    sizeof(hit.normal));
        std::memcpy(&hit.distance.value, queryObject_ + layout::kRaycastDistance,
                    sizeof(hit.distance.value));
        hit.engineResult = engineResult_;
        state_.store(State::Idle, std::memory_order_release);
        return RaycastPoll{RaycastPollStatus::Ready, expectedTicket, hit};
    }

    void cancel() {
        const auto ticketValue = ticket_.value;
        cancelledTicket_.store(ticketValue, std::memory_order_release);
        auto state = state_.load(std::memory_order_acquire);
        while (state == State::Pending || state == State::Ready) {
            if (state_.compare_exchange_weak(state, State::Idle,
                                             std::memory_order_acq_rel)) {
                return;
            }
        }
    }

    [[nodiscard]] bool busy() const {
        return state_.load(std::memory_order_acquire) != State::Idle;
    }

private:
    enum class State : std::uint32_t { Idle, Pending, Working, Ready };

    [[nodiscard]] static bool finite(const core::WorldPosition& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    }

    [[nodiscard]] static float distanceSquared(const core::WorldPosition& left,
                                               const core::WorldPosition& right) {
        const float x = left.x - right.x;
        const float y = left.y - right.y;
        const float z = left.z - right.z;
        return x * x + y * y + z * z;
    }

    void publishAfterWorker(State next) {
        const bool cancelled =
            cancelledTicket_.load(std::memory_order_acquire) == ticket_.value;
        state_.store(cancelled ? State::Idle : next, std::memory_order_release);
    }

    std::atomic<State> state_{State::Idle};
    std::atomic<std::uint32_t> cancelledTicket_{0};
    RaycastRequest request_{};
    RaycastTicket ticket_{};
    std::uint32_t nextTicket_ = 0;
    alignas(16) unsigned char queryObject_[layout::kRaycastObjectSize]{};
    std::uint64_t engineResult_ = 0;
};

} // namespace totk::engine
