#pragma once
#include <array>
#include <atomic>
#include <cstdint>

#include "engine/AssemblyMotionService.hpp"
#include "feature/FleetTelemetry.hpp"
#include "pure/MatchedFormation.hpp"

namespace linked_stick::feature {
enum class FormationAssistPhase : std::uint8_t { Idle, Acquiring, Assisting };
struct FormationAssistDiagnostics {
    FormationAssistPhase phase = FormationAssistPhase::Idle;
    std::uint32_t requested = 0, peerGuards = 0, saturations = 0, skips = 0;
    float error = 0, heightError = 0;
    bool measurementValid = false;
    const char* status = "ready";
};
class FormationAssist {
   public:
    void configure(std::uintptr_t mainBase);
    void enter();
    void onPairPublished();
    void onPairCleared();
    bool beginPhysics(std::uintptr_t framework, const FleetTelemetryEndpoints& endpoints,
                      bool enabled);
    void endPhysics();
    void heartbeat(const FleetTelemetryEndpoints& endpoints, bool enabled);
    FormationAssistDiagnostics diagnostics() const;

   private:
    void reset();
    void report(std::size_t slot, const char* status);
    void tick(const FleetTelemetryEndpoints& endpoints, bool enabled, float dt);
    void publishDiagnostics();
    void logSnapshot(std::size_t slot, const char* role,
                     const engine::AssemblyMotionSnapshot& snapshot);
    void logBodyEvidence(std::size_t slot);
    engine::AssemblyMotionService service_{};
    std::array<engine::AssemblyMotionSnapshot, 5> assemblies_{};
    std::array<engine::AssemblyMotionSnapshot, 5> captured_{};
    std::array<pure::matched::Controller, 4> controllers_{};
    std::array<pure::matched::FormationHealth, 4> health_{};
    std::array<pure::matched::Command, 4> commands_{};
    std::array<bool, 4> issued_{};
    std::array<const char*, 5> statuses_{};
    std::array<bool, 4> matched_{};
    std::array<std::uint64_t, 5> bindingHashes_{};
    FormationAssistDiagnostics diagnostics_{};
    std::uint64_t frequency_ = 0, lastLogTime_ = 0;
    std::atomic_flag stateLock_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> resetPending_{true};
    std::atomic<const char*> publishedStatus_{"mount controller"};
    std::atomic<std::uint32_t> publishedRequested_{0}, publishedSkips_{0}, publishedLimits_{0};
    std::atomic<std::uint32_t> publishedGuards_{0};
    std::atomic<FormationAssistPhase> publishedPhase_{FormationAssistPhase::Idle};
    std::atomic<float> publishedError_{0}, publishedHeight_{0};
    std::atomic<bool> publishedMeasurementValid_{false};
    std::atomic<std::uint64_t> publishedAt_{0}, measuredAt_{0};
    std::atomic<std::uint32_t> physicsCalls_{0}, admittedCalls_{0}, modeRejects_{0}, busyRejects_{0};
    std::atomic<std::uint32_t> frameworkRejects_{0}, lastMode_{0}, lastWorld_{0};
    std::atomic<bool> lastModeValid_{false}, lastWorldValid_{false};
    std::atomic<float> lastDt_{0};
    std::uint64_t heartbeatAt_ = 0, evidenceAt_ = 0;
    float stepSeconds_ = 0;
    float simulationSeconds_ = 0;
    std::uint32_t delivered_ = 0, deliveryFailures_ = 0;
    bool logStep_ = false;
    const char* stickyStatus_ = nullptr;
    float stickySeconds_ = 0;
    std::uint32_t receiverCount_ = 0;
    std::uint32_t faultSlot_ = 0;
    bool active_ = false, admitted_ = false, constructionFault_ = false, evidenceStep_ = false;
};
}
