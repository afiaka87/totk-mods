#include "feature/FormationAssist.hpp"

#include <nn/os.h>

#include <cstring>
#include <limits>
#include <lib.hpp>

#include "engine/LinkedStickOffsets.hpp"
#include "pure/AssemblyContinuity.hpp"
#include "lib/program/loggers.hpp"
#include "totk/engine/Pointer.hpp"

namespace linked_stick::feature {
namespace m = pure::matched;
namespace {
const char* stateName(m::State state) {
    switch (state) {
        case m::State::Waiting:
            return "waiting";
        case m::State::Following:
            return "correcting";
        case m::State::TooClose:
            return "space bikes further apart";
        case m::State::Invalid:
            return "invalid motion";
    }
    return "invalid";
}
int milli(float x) {
    if (!std::isfinite(x)) return std::numeric_limits<int>::min();
    const double scaled = static_cast<double>(x) * 1000;
    return static_cast<int>(std::clamp(scaled, -2147483647.0, 2147483647.0));
}
}
void FormationAssist::configure(std::uintptr_t base) {
    service_.configure(base);
    frequency_ = nn::os::GetSystemTickFrequency();
}
void FormationAssist::reset() {
    assemblies_ = {};
    captured_ = {};
    controllers_ = {};
    health_ = {};
    commands_ = {};
    issued_ = {};
    statuses_ = {};
    matched_ = {};
    bindingHashes_ = {};
    constructionFault_ = false;
    lastLogTime_ = 0;
    active_ = false;
    admitted_ = false;
    receiverCount_ = 0;
    diagnostics_ = {};
    delivered_ = deliveryFailures_ = 0;
    simulationSeconds_ = 0;
    stickyStatus_ = nullptr;
    stickySeconds_ = 0;
}
void FormationAssist::enter() { resetPending_.store(true, std::memory_order_release); }
void FormationAssist::onPairPublished() { enter(); }
void FormationAssist::onPairCleared() { enter(); }
FormationAssistDiagnostics FormationAssist::diagnostics() const {
    FormationAssistDiagnostics out;
    out.status = publishedStatus_.load(std::memory_order_acquire);
    out.requested = publishedRequested_.load(std::memory_order_relaxed);
    out.skips = publishedSkips_.load(std::memory_order_relaxed);
    out.saturations = publishedLimits_.load(std::memory_order_relaxed);
    out.peerGuards = publishedGuards_.load(std::memory_order_relaxed);
    out.phase = publishedPhase_.load(std::memory_order_relaxed);
    out.error = publishedError_.load(std::memory_order_relaxed);
    out.heightError = publishedHeight_.load(std::memory_order_relaxed);
    const auto now = svcGetSystemTick();
    const auto measured = measuredAt_.load(std::memory_order_acquire);
    out.measurementValid = publishedMeasurementValid_.load(std::memory_order_acquire) &&
        !resetPending_.load(std::memory_order_acquire) &&
        frequency_ && now >= measured && now - measured <= frequency_ / 2;
    const auto published = publishedAt_.load(std::memory_order_acquire);
    if (out.requested && frequency_ && now > published && now - published > frequency_)
        out.status = "physics paused/unavailable";
    return out;
}
void FormationAssist::publishDiagnostics() {
    publishedRequested_.store(diagnostics_.requested, std::memory_order_relaxed);
    publishedSkips_.store(diagnostics_.skips, std::memory_order_relaxed);
    publishedLimits_.store(diagnostics_.saturations, std::memory_order_relaxed);
    publishedGuards_.store(diagnostics_.peerGuards, std::memory_order_relaxed);
    publishedPhase_.store(diagnostics_.phase, std::memory_order_relaxed);
    publishedError_.store(diagnostics_.error, std::memory_order_relaxed);
    publishedHeight_.store(diagnostics_.heightError, std::memory_order_relaxed);
    publishedMeasurementValid_.store(diagnostics_.measurementValid, std::memory_order_release);
    if (diagnostics_.measurementValid) measuredAt_.store(svcGetSystemTick(), std::memory_order_release);
    publishedStatus_.store(diagnostics_.status, std::memory_order_release);
    publishedAt_.store(svcGetSystemTick(), std::memory_order_release);
}
void FormationAssist::heartbeat(const FleetTelemetryEndpoints& endpoints, bool enabled) {
    const auto now = svcGetSystemTick();
    if (!frequency_ || (heartbeatAt_ && now - heartbeatAt_ < frequency_)) return;
    heartbeatAt_ = now;
    const auto d = diagnostics();
    Logging.Log("[fleet-sync][matched] HEARTBEAT enabled=%u active=%u receivers=%u "
                "calls=%u admitted=%u mode_reject=%u busy=%u bad_framework=%u "
                "mode=%u mode_valid=%u world=%u world_valid=%u dt_us=%d "
                "state=%s requested=%u skips=%u limits=%u measurement_valid=%u drift_mm=%d height_mm=%d",
                enabled ? 1u : 0u, endpoints.controllerActive ? 1u : 0u, endpoints.receiverCount,
                physicsCalls_.load(), admittedCalls_.load(), modeRejects_.load(), busyRejects_.load(),
                frameworkRejects_.load(), lastMode_.load(), lastModeValid_.load() ? 1u : 0u,
                lastWorld_.load(), lastWorldValid_.load() ? 1u : 0u,
                milli(lastDt_.load() * 1000),
                d.status, d.requested, d.skips, d.saturations, d.measurementValid ? 1u : 0u,
                milli(d.error), milli(d.heightError));
}
bool FormationAssist::beginPhysics(std::uintptr_t framework,
                                   const FleetTelemetryEndpoints& endpoints, bool enabled) {
    namespace o = engine::offsets;
    using totk::engine::readMemory;
    ++physicsCalls_;
    if (!totk::engine::isPlausibleAddress(framework)) { ++frameworkRejects_; return false; }
    const float dt = readMemory<float>(framework + o::kPhysicsStepSeconds);
    const bool modeValid = readMemory<std::uint8_t>(framework + o::kPhysicsModeValid) != 0;
    const bool worldValid = readMemory<std::uint8_t>(framework + o::kPhysicsWorldIndexValid) != 0;
    const auto mode = readMemory<std::uint32_t>(framework + o::kPhysicsMode);
    const auto world = readMemory<std::uint32_t>(framework + o::kPhysicsWorldIndex);
    lastDt_.store(dt); lastMode_.store(mode); lastWorld_.store(world);
    lastModeValid_.store(modeValid); lastWorldValid_.store(worldValid);
    if (!m::simulationPass(modeValid, mode, worldValid, world, dt)) { ++modeRejects_; return false; }
    // The physics owner holds mutable state; input callbacks publish only atomic intent.
    if (stateLock_.test_and_set(std::memory_order_acquire)) { ++busyRejects_; return false; }
    ++admittedCalls_;
    if (resetPending_.exchange(false, std::memory_order_acq_rel)) reset();
    stepSeconds_ = dt;
    stickySeconds_ = std::max(0.0f, stickySeconds_ - dt);
    issued_ = {};
    logStep_ = false;
    diagnostics_.measurementValid = false;
    diagnostics_.phase = FormationAssistPhase::Acquiring;
    const auto now = svcGetSystemTick();
    evidenceStep_ = !evidenceAt_ || (frequency_ && now - evidenceAt_ >= frequency_);
    if (evidenceStep_) evidenceAt_ = now;
    service_.beginCollisionFrame();
    if (service_.beginPhysics(framework)) {
        tick(endpoints, enabled, dt);
        service_.finishCollisionFrame(evidenceStep_);
    } else {
        if (evidenceStep_ || !statuses_[0] || std::strcmp(statuses_[0], "physics unavailable"))
            logBodyEvidence(0);
        report(0, "physics unavailable");
    }
    return true;  // Retain the lock through the native drain and endPhysics readback.
}
void FormationAssist::endPhysics() {
    service_.verifyCollisionDelivery(evidenceStep_);
    for (std::uint32_t i = 0; i < receiverCount_; ++i) {
        if (!issued_[i]) continue;
        float linearError = 0, angularError = 0;
        const bool delivered =
            service_.verifyDelivery(assemblies_[i + 1], linearError, angularError);
        if (!delivered && (evidenceStep_ || !statuses_[i + 1] ||
                          std::strcmp(statuses_[i + 1], "physics mismatch")))
            logBodyEvidence(i + 1);
        if (delivered)
            ++delivered_;
        else
            ++deliveryFailures_;
        report(i + 1, health_[i].step(commands_[i], delivered, stepSeconds_));
        if (logStep_)
            service_.logDeliveryEvidence(assemblies_[i + 1], i + 1);
        if (logStep_)
            Logging.Log(
                "[fleet-sync][matched] DELIVERY receiver=%u havok=%u dv_error_m=%d dw_error_m=%d "
                "verified=%u failed=%u dt_us=%u",
                i + 1, delivered ? 1u : 0u, milli(linearError), milli(angularError), delivered_,
                deliveryFailures_, static_cast<unsigned>(stepSeconds_ * 1000000));
    }
    if (evidenceStep_)
        Logging.Log("[fleet-sync][matched] PROGRESS active=%u admitted=%u requested=%u skips=%u "
                    "havok=%u failed=%u sim_ms=%d state=%s measurement_valid=%u",
                    active_ ? 1u : 0u, admitted_ ? 1u : 0u, diagnostics_.requested,
                    diagnostics_.skips, delivered_, deliveryFailures_, milli(simulationSeconds_),
                    diagnostics_.status, diagnostics_.measurementValid ? 1u : 0u);
    publishDiagnostics();
    stateLock_.clear(std::memory_order_release);
}
void FormationAssist::logBodyEvidence(std::size_t slot) {
    const auto& b = service_.evidence();
    Logging.Log("[fleet-sync][matched] BODY endpoint=%u reason=%s world=%p body=%p "
                "flags=%llx sdk=%p id=%llx reader=%p v_m=%d,%d,%d w_m=%d,%d,%d "
                "target_v_m=%d,%d,%d target_w_m=%d,%d,%d",
                static_cast<unsigned>(slot), b.reason, reinterpret_cast<void*>(b.world),
                reinterpret_cast<void*>(b.body), static_cast<unsigned long long>(b.flags),
                reinterpret_cast<void*>(b.sdk), static_cast<unsigned long long>(b.bodyId),
                reinterpret_cast<void*>(b.function), milli(b.linear.x), milli(b.linear.y), milli(b.linear.z),
                milli(b.angular.x), milli(b.angular.y), milli(b.angular.z),
                milli(b.targetLinear.x), milli(b.targetLinear.y), milli(b.targetLinear.z),
                milli(b.targetAngular.x), milli(b.targetAngular.y), milli(b.targetAngular.z));
}
void FormationAssist::logSnapshot(std::size_t slot, const char* role,
                                  const engine::AssemblyMotionSnapshot& s) {
    Logging.Log("[fleet-sync][matched] ROSTER endpoint=%u role=%s component=%p integrator=%p "
                "members=%u status=%s p_mm=%d,%d,%d v_m=%d,%d,%d w_m=%d,%d,%d",
                static_cast<unsigned>(slot), role, reinterpret_cast<void*>(s.receiver),
                reinterpret_cast<void*>(s.integrator), s.memberCount,
                engine::assemblyMotionStatusName(s.status), milli(s.motion.position.x),
                milli(s.motion.position.y), milli(s.motion.position.z), milli(s.motion.velocity.x),
                milli(s.motion.velocity.y), milli(s.motion.velocity.z), milli(s.motion.angular.x),
                milli(s.motion.angular.y), milli(s.motion.angular.z));
    for (unsigned i = 0; i < s.memberCount && i < s.members.size(); ++i) {
        const auto& a = s.members[i];
        Logging.Log("[fleet-sync][matched] MEMBER endpoint=%u role=%s index=%u actor=%p name=%p "
                    "body=%p kind=%llx local_mm=%d,%d,%d rotation_m=%d,%d,%d,%d,%d,%d,%d,%d,%d",
                    static_cast<unsigned>(slot), role, i, reinterpret_cast<void*>(a.actor),
                    reinterpret_cast<void*>(a.nameIdentity), reinterpret_cast<void*>(a.rigidBody),
                    static_cast<unsigned long long>(a.shape.kind), milli(a.shape.position.x),
                    milli(a.shape.position.y), milli(a.shape.position.z),
                    milli(a.shape.rotation[0]), milli(a.shape.rotation[1]), milli(a.shape.rotation[2]),
                    milli(a.shape.rotation[3]), milli(a.shape.rotation[4]), milli(a.shape.rotation[5]),
                    milli(a.shape.rotation[6]), milli(a.shape.rotation[7]), milli(a.shape.rotation[8]));
    }
}
void FormationAssist::report(std::size_t slot, const char* status) {
    const bool healthy = std::strcmp(status, "holding") == 0 ||
                         std::strcmp(status, "correcting") == 0 ||
                         std::strcmp(status, "drifting") == 0;
    if (!healthy) {
        stickyStatus_ = status;
        stickySeconds_ = 1.0f;
    }
    diagnostics_.status = stickySeconds_ > 0 && stickyStatus_ ? stickyStatus_ : status;
    if (statuses_[slot] && std::strcmp(statuses_[slot], status) == 0 && !evidenceStep_) return;
    statuses_[slot] = status;
    const auto& snapshot = assemblies_[slot];
    Logging.Log("[fleet-sync][matched] STATE endpoint=%u state=%s integrator=%p members=%u",
                static_cast<unsigned>(slot), status, reinterpret_cast<void*>(snapshot.integrator),
                snapshot.memberCount);
}
void FormationAssist::tick(const FleetTelemetryEndpoints& endpoints, bool enabled, float dt) {
    if (!enabled || !endpoints.controllerActive || !endpoints.receiverCount ||
        endpoints.receiverCount > 4) {
        if (active_) {
            Logging.Log("[fleet-sync][matched] END requested=%u skips=%u havok=%u failed=%u",
                        diagnostics_.requested, diagnostics_.skips, delivered_, deliveryFailures_);
            reset();
        }
        diagnostics_.status = enabled ? "mount controller" : "off";
        return;
    }
    const auto now = svcGetSystemTick();
    if (!frequency_) {
        report(0, "clock unavailable");
        return;
    }
    if (!active_) {
        active_ = true;
        receiverCount_ = endpoints.receiverCount;
        diagnostics_.phase = FormationAssistPhase::Acquiring;
        Logging.Log(
            "[fleet-sync][matched] BEGIN format=7 body-feedback=havok "
            "correction=pre-solve-recovery receivers=%u",
            receiverCount_);
        Logging.Log("[fleet-sync][matched] BUDGET catchup_m=%d height_m=%d orbit_m=%d total_m=%d "
                    "align_rate_m=%d align_accel_m=%d continuous_recovery=1",
                    milli(m::kCatchUpAcceleration), milli(m::kHeightAcceleration),
                    milli(m::kOrbitAcceleration), milli(m::kTotalHorizontalAcceleration),
                    milli(m::kAlignmentRate), milli(m::kAlignmentAcceleration));
    }
    if (receiverCount_ != endpoints.receiverCount) {
        reset();
        return;
    }
    simulationSeconds_ += dt;
    bool snapshotsValid = true;
    for (std::uint32_t slot = 0; slot <= receiverCount_; ++slot) {
        const auto receiver =
            slot ? endpoints.receiverComponents[slot - 1] : endpoints.controllerReceiver;
        assemblies_[slot] = service_.capture(receiver);
        if (!assemblies_[slot].valid()) {
            if (evidenceStep_ || !statuses_[slot] ||
                std::strcmp(statuses_[slot], engine::assemblyMotionStatusName(assemblies_[slot].status))) {
                logSnapshot(slot, "capture-refused", assemblies_[slot]);
                logBodyEvidence(slot);
            }
            ++diagnostics_.skips;
            report(slot, engine::assemblyMotionStatusName(assemblies_[slot].status));
            snapshotsValid = false;
            continue;
        }
        if (evidenceStep_) {
            const auto& a = assemblies_[slot].motion;
            Logging.Log("[fleet-sync][matched] MOTION endpoint=%u sim_ms=%d p_mm=%d,%d,%d "
                        "v_m=%d,%d,%d w_m=%d,%d,%d forward_m=%d,%d,%d",
                        slot, milli(simulationSeconds_), milli(a.position.x), milli(a.position.y),
                        milli(a.position.z), milli(a.velocity.x), milli(a.velocity.y), milli(a.velocity.z),
                        milli(a.angular.x), milli(a.angular.y), milli(a.angular.z),
                        milli(a.rotation[2]), milli(a.rotation[5]), milli(a.rotation[8]));
        }
    }
    if (!snapshotsValid) {
        for (auto& controller : controllers_) controller.pauseTracking();
        return;
    }
    if (!admitted_) {
        captured_ = assemblies_;
        for (std::uint32_t slot = 0; slot <= receiverCount_; ++slot) {
            logSnapshot(slot, "original", captured_[slot]);
            bindingHashes_[slot] = m::bindingFingerprint(captured_[slot]);
        }
        for (std::uint32_t i = 0; i < receiverCount_; ++i) {
            matched_[i] = assemblies_[0].integrator != assemblies_[i + 1].integrator &&
                          m::sameShape(assemblies_[0].shape, assemblies_[i + 1].shape);
            Logging.Log(
                "[fleet-sync][matched] SHAPE receiver=%u match=%u guide_members=%u "
                "receiver_members=%u",
                i + 1, matched_[i] ? 1u : 0u, assemblies_[0].memberCount,
                assemblies_[i + 1].memberCount);
        }
        admitted_ = true;
    }
    for (std::uint32_t slot = 0; slot <= receiverCount_; ++slot) {
        const auto continuity = m::constructionContinuity(captured_[slot], assemblies_[slot]);
        const auto hash = m::bindingFingerprint(assemblies_[slot]);
        if (!continuity.safe || hash != bindingHashes_[slot]) {
            if (evidenceStep_ || !constructionFault_ || hash != bindingHashes_[slot]) {
                Logging.Log("[fleet-sync][matched] CONTINUITY endpoint=%u safe=%u reason=%s "
                            "handle_changed=%u order_changed=%u body_changed=%u before=%u after=%u",
                            slot, continuity.safe ? 1u : 0u, continuity.reason,
                            continuity.handleChanged ? 1u : 0u, continuity.orderChanged ? 1u : 0u,
                            continuity.bodyChanged ? 1u : 0u, continuity.before, continuity.after);
                logSnapshot(slot, "original", captured_[slot]);
                logSnapshot(slot, "current", assemblies_[slot]);
            }
            bindingHashes_[slot] = hash;
        }
        if (!continuity.safe && !constructionFault_) {
            faultSlot_ = slot;
            constructionFault_ = true;
        }
    }
    if (constructionFault_) {
        ++diagnostics_.skips;
        report(faultSlot_, "construction changed: remount");
        return;
    }
    diagnostics_.phase = FormationAssistPhase::Acquiring;
    float worstErrorThisStep = 0;
    for (std::uint32_t i = 0; i < receiverCount_; ++i) {
        if (!matched_[i]) {
            report(i + 1, "different builds: input only");
            continue;
        }
        service_.allowObstaclePassThrough(assemblies_[i + 1]);
        auto command = controllers_[i].step(assemblies_[0].motion, assemblies_[i + 1].motion, dt,
                                             receiverCount_ == 1);
        if (command.capturedLane) {
            Logging.Log("[fleet-sync][matched] LANE receiver=%u initial_local_mm=%d,%d,%d target_local_mm=%d,%d,%d",
                        i + 1, milli(command.initialOffset.x), milli(command.initialOffset.y),
                        milli(command.initialOffset.z), milli(command.laneOffset.x),
                        milli(command.laneOffset.y), milli(command.laneOffset.z));
        }
        if (!command.apply) {
            if (m::length(command.error) > 0) {
                diagnostics_.error = m::length(command.error);
                diagnostics_.heightError = -command.error.y;
            }
            report(i + 1, stateName(command.state));
            ++diagnostics_.skips;
            continue;
        }
        for (std::uint32_t peer = 1; peer <= receiverCount_; ++peer) {
            if (peer == i + 1) continue;
            const auto apart =
                m::sub(assemblies_[i + 1].motion.position, assemblies_[peer].motion.position);
            const float distance = m::length(apart);
            const float safe =
                assemblies_[i + 1].motion.radius + assemblies_[peer].motion.radius + 0.5f;
            if (distance > 0.001f && distance < safe) {
                const auto normal = m::mul(apart, 1 / distance);
                const float inward = m::dot(command.linear, normal);
                if (inward < 0) {
                    command.linear = m::sub(command.linear, m::mul(normal, inward));
                    ++diagnostics_.peerGuards;
                }
            }
        }
        const auto status =
            service_.applyCorrection(assemblies_[i + 1], command.linear, command.angular);
        commands_[i] = command;
        issued_[i] = status == engine::AssemblyMotionStatus::Ready ||
                     status == engine::AssemblyMotionStatus::RequestReadbackMismatch;
        if (m::length(command.error) >= worstErrorThisStep) {
            worstErrorThisStep = m::length(command.error);
            diagnostics_.error = m::length(command.error);
            diagnostics_.heightError = -command.error.y;
        }
        diagnostics_.measurementValid = true;
        if (status == engine::AssemblyMotionStatus::Ready ||
            status == engine::AssemblyMotionStatus::RequestReadbackMismatch)
            ++diagnostics_.requested;
        if (status == engine::AssemblyMotionStatus::Ready) {
            diagnostics_.phase = FormationAssistPhase::Assisting;
        } else {
            ++diagnostics_.skips;
            if (!issued_[i]) report(i + 1, engine::assemblyMotionStatusName(status));
        }
        if (command.limited) ++diagnostics_.saturations;
        if (now - lastLogTime_ >= frequency_ / 2) {
            logStep_ = true;
            Logging.Log("[fleet-sync][matched] TURN receiver=%u yaw_m=%d alpha_m=%d "
                        "orbit_m=%d,%d,%d feedback_m=%d,%d,%d speed_error_m=%d",
                        i + 1, milli(command.yawRate), milli(command.yawAcceleration),
                        milli(command.turnAcceleration.x), milli(command.turnAcceleration.y),
                        milli(command.turnAcceleration.z), milli(command.feedbackAcceleration.x),
                        milli(command.feedbackAcceleration.y), milli(command.feedbackAcceleration.z),
                        milli(command.speedError));
            const auto guideVelocity = assemblies_[0].motion.velocity;
            const auto followerVelocity = assemblies_[i + 1].motion.velocity;
            const auto guidePosition = assemblies_[0].motion.position;
            const auto followerPosition = assemblies_[i + 1].motion.position;
            Logging.Log(
                "[fleet-sync][matched] SAMPLE receiver=%u err_mm=%d,%d,%d angle_mrad=%d "
                "dv_m=%d,%d,%d dw_m=%d,%d,%d vertical=%u limited=%u request=%s "
                "guide_v_m=%d,%d,%d receiver_v_m=%d,%d,%d sim_ms=%d "
                "guide_p_mm=%d,%d,%d receiver_p_mm=%d,%d,%d",
                i + 1, milli(command.error.x), milli(command.error.y), milli(command.error.z),
                milli(command.angle), milli(command.linear.x), milli(command.linear.y),
                milli(command.linear.z), milli(command.angular.x), milli(command.angular.y),
                milli(command.angular.z), command.vertical ? 1u : 0u, command.limited ? 1u : 0u,
                engine::assemblyMotionStatusName(status), milli(guideVelocity.x),
                milli(guideVelocity.y), milli(guideVelocity.z), milli(followerVelocity.x),
                milli(followerVelocity.y), milli(followerVelocity.z), milli(simulationSeconds_),
                milli(guidePosition.x), milli(guidePosition.y), milli(guidePosition.z),
                milli(followerPosition.x), milli(followerPosition.y), milli(followerPosition.z));
        }
    }
    if (now - lastLogTime_ >= frequency_ / 2) lastLogTime_ = now;
}
}
