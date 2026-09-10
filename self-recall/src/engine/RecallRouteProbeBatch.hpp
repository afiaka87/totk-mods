#pragma once
#include "RecallRouteProbePlan.hpp"
#include "RecallRouteContact.hpp"
#include "totk/engine/Raycast.hpp"
#include <array>

namespace self_recall::probe {

struct BatchPoll {
    totk::engine::RaycastPollStatus status = totk::engine::RaycastPollStatus::Idle;
    totk::engine::RaycastHit hit{};
    unsigned segment = 0;
};

class RouteProbeBatch {
public:
    bool begin(const pure::RouteProbePlan& plan, std::uint32_t mask,
               std::uint64_t tick, std::uint32_t generation) {
        if (pending_ || !plan.count || plan.count > boxes_.size()) return false;
        for (const auto& box : boxes_) if (box.busy()) return false;
        for (unsigned i = 0; i < plan.count; ++i) {
            totk::engine::RaycastRequest request{};
            request.from = plan.segments[i].from;
            request.to = plan.segments[i].to;
            request.relevancePoint = {(request.from.x + request.to.x) * 0.5f,
                                      (request.from.y + request.to.y) * 0.5f,
                                      (request.from.z + request.to.z) * 0.5f};
            request.mask = mask;
            request.submittedAt = {tick};
            request.generation = {generation};
            const auto begun = boxes_[i].begin(request);
            if (!begun) { cancel(); return false; }
            tickets_[i] = begun.value;
            segments_[i] = plan.segments[i];
            pending_ |= 1u << i;
        }
        return true;
    }

    BatchPoll poll(std::uint64_t tick, std::uint64_t timeout) {
        using Status = totk::engine::RaycastPollStatus;
        if (!pending_) return {};
        for (unsigned i = 0; i < boxes_.size(); ++i) {
            if (!(pending_ & (1u << i))) continue;
            const auto result = boxes_[i].poll(tickets_[i], {tick}, timeout);
            if (result.status == Status::Pending) continue;
            pending_ &= ~(1u << i);
            if (result.status == Status::Ready && result.result.hit &&
                pure::tolerableRouteContact(segments_[i], result.result.position,
                    {result.result.normal.x, result.result.normal.y, result.result.normal.z})) continue;
            if (result.status != Status::Ready || result.result.hit) {
                cancel();
                return {result.status, result.result, i};
            }
        }
        return {pending_ ? Status::Pending : Status::Ready, {}, 0};
    }

    void observe(totk::engine::RaycastFunction original, const void* from, const void* object) {
        for (auto& box : boxes_) box.observe(original, from, object);
    }
    void cancel() {
        for (auto& box : boxes_) if (box.busy()) box.cancel();
        pending_ = 0;
    }

private:
    std::array<totk::engine::RaycastMailbox, pure::kProbeWindowMaxSamples> boxes_{};
    std::array<totk::engine::RaycastTicket, pure::kProbeWindowMaxSamples> tickets_{};
    std::array<pure::RouteProbeSegment, pure::kProbeWindowMaxSamples> segments_{};
    unsigned pending_ = 0;
};
} // namespace self_recall::probe
