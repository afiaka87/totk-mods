// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "FlightDiagnostics.hpp"
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"
#include "../../../pure/FlightDiagnostics.hpp"
#include "../../../engine/ModelVisibility.hpp"
#include "../../../engine/FlightClock.hpp"
#include <lib.hpp>

namespace arrowbound::diagnostics {
namespace {
using namespace pure;
std::atomic_flag lock = ATOMIC_FLAG_INIT;
std::atomic<std::uintptr_t> token{0};
std::atomic<std::uint32_t> id{0}, dropped{0};
std::atomic<std::uint32_t> sampleCount{0};
std::atomic<bool> active{false};
struct Trace {
    bool pending{}, enabled{};
    std::uint32_t samples{};
    std::uint64_t start{};
    Vec3 origin{}, previous{};
    float maxSpeed{}, maxStep{};
} trace;
struct Guard {
    bool held = !lock.test_and_set(std::memory_order_acquire);
    Guard() { if (!held) dropped.fetch_add(1, std::memory_order_relaxed); }
    ~Guard() { if (held) lock.clear(std::memory_order_release); }
};
void end(const char* reason) {
    if (trace.pending || token.load(std::memory_order_relaxed))
        ZHLOG("TRACE_END id=%u reason=%s n=%u max_speed_cm_s=%d max_step_cm=%d dropped=%u clock=%llu",
            id.load(), reason, trace.samples, traceNumber(trace.maxSpeed), traceNumber(trace.maxStep),
            dropped.load(), (unsigned long long)svcGetSystemTick());
    token.store(0, std::memory_order_release);
    active.store(false, std::memory_order_release);
    trace.pending = false;
}
}

void release(bool enabled) {
    Guard guard;
    if (!guard.held) return;
    end("next_release");
    trace = {};
    trace.pending = true;
    trace.enabled = enabled;
    sampleCount.store(0);
    active.store(true, std::memory_order_release);
    trace.start = svcGetSystemTick();
    dropped.store(0);
    ZHLOG("TRACE_RELEASE id=%u enabled=%u clock=%llu schema=1 max_samples=1800 stride=6",
          id.fetch_add(1) + 1, (unsigned)enabled, (unsigned long long)trace.start);
    model_trace::begin(id.load());
}

void claim(void* controller, std::uint32_t state, bool playerOwned) {
    Guard guard;
    if (!guard.held || !active.load(std::memory_order_acquire) ||
        !trace.pending || !playerOwned || !pure::arrowClaimIsNew(state)) return;
    trace.pending = false;
    token.store(reinterpret_cast<std::uintptr_t>(controller), std::memory_order_release);
    ZHLOG("TRACE_CLAIM id=%u controller=%p state=%u enabled=%u", id.load(), controller,
          state, (unsigned)trace.enabled);
}

bool observes(void* controller) {
    return controller && active.load(std::memory_order_acquire) &&
        token.load(std::memory_order_acquire) == reinterpret_cast<std::uintptr_t>(controller);
}

void sample(void* controller, pure::Vec3 position, pure::Vec3 velocity, float rate, float dt) {
    Guard guard;
    if (!guard.held || !observes(controller)) return;
    const auto n = ++trace.samples;
    sampleCount.store(n, std::memory_order_release);
    if (n == 1) trace.origin = trace.previous = position;
    const auto delta = sub(position, trace.origin);
    const float speed = length(velocity), step = distance(position, trace.previous);
    if (std::isfinite(speed) && speed > trace.maxSpeed) trace.maxSpeed = speed;
    if (std::isfinite(step) && step > trace.maxStep) trace.maxStep = step;
    trace.previous = position;
    if (traceSampleDue(n)) {
        ZHLOG("TRACE_ARROW id=%u n=%u clock=%llu elapsed_counter=%llu dt_bits=%08x rate_bits=%08x",
              id.load(), n, (unsigned long long)svcGetSystemTick(),
              (unsigned long long)(svcGetSystemTick() - trace.start),
              floatToBits(dt), floatToBits(rate));
        ZHLOG("TRACE_ARROW_POS id=%u n=%u pos_cm=(%d,%d,%d) delta_cm=(%d,%d,%d) step_cm=%d",
              id.load(), n, traceNumber(position.x), traceNumber(position.y), traceNumber(position.z),
              traceNumber(delta.x), traceNumber(delta.y), traceNumber(delta.z), traceNumber(step));
        ZHLOG("TRACE_ARROW_VEL id=%u n=%u raw_cm_s=(%d,%d,%d) speed_cm_s=%d",
              id.load(), n, traceNumber(velocity.x), traceNumber(velocity.y),
              traceNumber(velocity.z), traceNumber(speed));
    }
    if (n >= 1800) end("sample_budget");
}

void impact(void* controller, const pure::ArrowImpact& hit) {
    Guard guard;
    if (!guard.held || !observes(controller)) return;
    ZHLOG("TRACE_HIT id=%u source=%u type=%d water=%u wall=%u pos_cm=(%d,%d,%d)",
          id.load(), (unsigned)hit.source, hit.sensorType, (unsigned)hit.water,
          (unsigned)hit.wallCandidate(), traceNumber(hit.contact.x),
          traceNumber(hit.contact.y), traceNumber(hit.contact.z));
    end("native_impact");
}

void gameplay(const HookshotRuntime& rt) {
    static std::uint32_t lastId{}, lastSamples{};
    static std::uint64_t lastActivity{};
    const auto currentId = id.load(), currentSamples = sampleCount.load(std::memory_order_acquire);
    if (currentId != lastId || currentSamples != lastSamples || rt.session.tick < lastActivity) {
        lastId = currentId;
        lastSamples = currentSamples;
        lastActivity = rt.session.tick;
    }
    if (active.load(std::memory_order_acquire) && rt.session.tick - lastActivity > 180)
        reset("no_native_samples_180_gameplay_ticks");
    if ((!token.load(std::memory_order_acquire) && rt.arrowTrip.phase == pure::ArrowPhase::Idle) ||
        rt.session.tick % 6 != 0 || !world::ready()) return;
    const auto player = world::playerPosition(), camera = world::cameraPosition();
    const auto& trip = rt.arrowTrip;
    pure::FlightTime clock{};
    if (game_clock::snapshot(clock))
        ZHLOG("TRACE_TIME id=%u tick=%llu serial=%llu ns=%llu status=%u scale_milli=%d",
              id.load(),(unsigned long long)rt.session.tick,(unsigned long long)clock.serial,
              (unsigned long long)clock.nanoseconds,unsigned(clock.status),traceNumber(clock.scale,1000));
    ZHLOG("TRACE_PLAYER id=%u shot=%u tick=%llu clock=%llu phase=%u glider=%u pose=%08x pos_cm=(%d,%d,%d)",
          id.load(), trip.shotSeqSeen, (unsigned long long)rt.session.tick,
          (unsigned long long)svcGetSystemTick(), (unsigned)trip.phase,
          rt.drive.parasailActive.load(), rt.drive.poseState.load(),
          traceNumber(player.x), traceNumber(player.y), traceNumber(player.z));
    ZHLOG("TRACE_CAMERA id=%u tick=%llu pos_cm=(%d,%d,%d) player_dist_cm=%d",
          id.load(), (unsigned long long)rt.session.tick, traceNumber(camera.x),
          traceNumber(camera.y), traceNumber(camera.z), traceNumber(distance(player, camera)));
    ZHLOG("TRACE_CARRIER id=%u tick=%llu valid=%u target_cm=(%d,%d,%d) error_cm=%d age=%llu",
          id.load(), (unsigned long long)rt.session.tick, (unsigned)trip.haveRequestedPosition,
          traceNumber(trip.lastRequestedPosition.x), traceNumber(trip.lastRequestedPosition.y),
          traceNumber(trip.lastRequestedPosition.z),
          trip.haveRequestedPosition ? traceNumber(distance(player, trip.lastRequestedPosition)) : -1,
          (unsigned long long)(rt.session.tick - trip.lastSampleTick));
    float basis[9]{};
    if (world::readPlayerRotation(basis))
        ZHLOG("TRACE_BASIS id=%u tick=%llu player_milli=(%d,%d,%d,%d,%d,%d,%d,%d,%d)",
              id.load(), (unsigned long long)rt.session.tick,
              traceNumber(basis[0],1000), traceNumber(basis[1],1000), traceNumber(basis[2],1000),
              traceNumber(basis[3],1000), traceNumber(basis[4],1000), traceNumber(basis[5],1000),
              traceNumber(basis[6],1000), traceNumber(basis[7],1000), traceNumber(basis[8],1000));
    if (world::readCameraRotation(basis))
        ZHLOG("TRACE_VIEW id=%u tick=%llu camera_milli=(%d,%d,%d,%d,%d,%d,%d,%d,%d)",
              id.load(), (unsigned long long)rt.session.tick,
              traceNumber(basis[0],1000), traceNumber(basis[1],1000), traceNumber(basis[2],1000),
              traceNumber(basis[3],1000), traceNumber(basis[4],1000), traceNumber(basis[5],1000),
              traceNumber(basis[6],1000), traceNumber(basis[7],1000), traceNumber(basis[8],1000));
}

void reset(const char* reason) {
    Guard guard;
    if (guard.held) end(reason);
    else {
        active.store(false, std::memory_order_release);
        token.store(0, std::memory_order_release);
    }
}
}
#endif
