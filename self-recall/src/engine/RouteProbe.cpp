
#include "RecallModelEngine.hpp"
#include <lib.hpp>

namespace self_recall::probe {
namespace {

RouteProbeBatch g_batch{};

}

void arm(ProbeState& state, std::span<const pure::HistorySample> samples,
         const ArmRequest& request) {
    if (!request.rewinding || !request.havePlayer || state.obstructed || state.unavailable) return;
    if (state.armed || request.rewindIndex < state.evaluatedThrough) return;
    const auto plan = pure::planRouteProbe(samples, request.climbActive);
    if (plan.kind == pure::RouteProbeKind::Unavailable) {
        state.unavailable = true;
        return;
    }
    const auto through = request.rewindIndex + plan.through;
    if (plan.kind != pure::RouteProbeKind::Cast) {
        state.evaluatedThrough = through;
        if (plan.kind != pure::RouteProbeKind::Stationary) ++state.bypasses;
        return;
    }

    if (!g_batch.begin(plan, kSolidMask, request.tick, request.generation)) return;
    state.plan = plan;
    state.armed = true;
    state.requestedThrough = through;
}

ServiceResult service(ProbeState& state, bool rewinding, std::uint64_t tick) {
    if (!state.armed) return ServiceResult::Quiet;

    const auto poll = g_batch.poll(tick, kTimeoutTicks);
    switch (poll.status) {
        case totk::engine::RaycastPollStatus::Pending:
            return ServiceResult::Quiet;
        case totk::engine::RaycastPollStatus::TimedOut:
            state.armed = false;
            ++state.timeouts;
            state.unavailable = true;
            return ServiceResult::TimedOut;
        case totk::engine::RaycastPollStatus::Ready:
            state.armed = false;
            if (rewinding && poll.hit.hit) {
                state.hitPosition = poll.hit.position;
                state.obstructed = true;
            } else if (rewinding) {
                state.evaluatedThrough = state.requestedThrough;
            }
            if (state.timeouts) state.timeouts = 0;
            return ServiceResult::Consumed;
        case totk::engine::RaycastPollStatus::Idle:
        case totk::engine::RaycastPollStatus::GenerationMismatch:
            Logging.Log("[self-recall] ROUTE_MAILBOX_UNAVAILABLE status=%u", static_cast<unsigned>(poll.status));
            state.armed = false;
            state.unavailable = true;
            return ServiceResult::Quiet;
    }
    return ServiceResult::Quiet;
}

void cancel(ProbeState& state) {
    if (state.armed) g_batch.cancel();
    state.armed = false;
    state.unavailable = false;
    state.evaluatedThrough = state.requestedThrough = state.bypasses = 0;
}

void observe(totk::engine::RaycastFunction original, const void* from,
             const void* object) {
    g_batch.observe(original, from, object);
}

}
