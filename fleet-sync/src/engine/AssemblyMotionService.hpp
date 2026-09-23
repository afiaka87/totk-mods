#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pure/FleetTelemetryMath.hpp"
#include "pure/MatchedFormation.hpp"
#include "pure/ObstaclePassThrough.hpp"

namespace linked_stick::engine {

enum class AssemblyMotionStatus : std::uint8_t {
    Ready,
    ServiceUnavailable,
    ReceiverUnavailable,
    IntegratorUnavailable,
    MemberLimitExceeded,
    MemberListUnavailable,
    MemberReferenceUnavailable,
    MemberIdentityUnavailable,
    PhysicsBodyUnavailable,
    NonDynamicMember,
    NonFiniteVelocity,
    MembershipChanged,
    CorrectionRejected,
    RequestReadbackMismatch,
};

struct AssemblyMemberMotion {
    std::uintptr_t actor = 0;
    std::uintptr_t nameIdentity = 0;
    std::uintptr_t rigidBody = 0;
    pure::TelemetryVector velocity{};
    pure::TelemetryVector angular{}, center{};
    pure::TelemetryVector targetVelocity{}, targetAngular{};
    pure::TelemetryVector pendingVelocity{}, pendingAngular{}, rawTargetVelocity{}, rawTargetAngular{};
    std::uint64_t bodyId = 0;
    std::uint32_t motionId = 0xffffffffu;
    pure::matched::ShapeMember shape{};
};

struct AssemblyMotionSnapshot {
    static constexpr std::size_t kMaximumMembers = 21;

    std::uintptr_t receiver = 0;
    std::uintptr_t integrator = 0;
    std::array<AssemblyMemberMotion, kMaximumMembers> members{};
    std::uint32_t memberCount = 0;
    pure::TelemetryVector averageVelocity{};
    pure::matched::Motion motion{};
    pure::matched::Shape shape{};
    AssemblyMotionStatus status = AssemblyMotionStatus::ServiceUnavailable;

    [[nodiscard]] bool valid() const {
        return status == AssemblyMotionStatus::Ready && memberCount > 0;
    }
};

struct MotionReadEvidence {
    const char* reason = "not read";
    std::uintptr_t world = 0, body = 0, sdk = 0, function = 0;
    std::uint64_t flags = 0, bodyId = 0;
    pure::TelemetryVector linear{}, angular{}, targetLinear{}, targetAngular{};
};

class AssemblyMotionService {
   public:
    void configure(std::uintptr_t mainBase);
    bool beginPhysics(std::uintptr_t framework);
    void beginCollisionFrame();
    void allowObstaclePassThrough(const AssemblyMotionSnapshot& snapshot);
    void finishCollisionFrame(bool logSummary);
    void verifyCollisionDelivery(bool logSummary) const;
    bool readCollisionMask(pure::CollisionBody body, std::uint32_t& mask) const;
    bool writeCollisionMask(pure::CollisionBody body, std::uint32_t mask);
    void collisionEvent(const char* event, pure::CollisionBody body,
                        std::uint32_t before, std::uint32_t after) const;
    const MotionReadEvidence& evidence() const { return evidence_; }
    bool verifyDelivery(const AssemblyMotionSnapshot& snapshot, float& worstLinear,
                        float& worstAngular) const;
    void logDeliveryEvidence(const AssemblyMotionSnapshot& snapshot, unsigned receiver) const;

    [[nodiscard]] AssemblyMotionSnapshot capture(std::uintptr_t receiverComponent) const;
    [[nodiscard]] AssemblyMotionStatus applyCommonVelocityDelta(
        const AssemblyMotionSnapshot& snapshot, pure::TelemetryVector delta) const;
    [[nodiscard]] AssemblyMotionStatus applyCorrection(AssemblyMotionSnapshot& snapshot,
                                                       pure::TelemetryVector linear,
                                                       pure::TelemetryVector angular) const;

   private:
    using GetMotionTypeFunction = std::uint32_t (*)(void*);
    using RequestLinearVelocityFunction = void (*)(void*, float*);
    using ReadVectorFunction = void (*)(void*, pure::TelemetryVector*);
    // Three floats return in S0/S1/S2 under the AAPCS64 homogeneous-aggregate ABI.
    using ReadAngularFunction = pure::TelemetryVector (*)(void*);

    [[nodiscard]] std::uintptr_t resolveActorLink(std::uintptr_t actorLink) const;
    [[nodiscard]] std::uintptr_t rigidBodyOf(std::uintptr_t actor) const;
    [[nodiscard]] bool memberStillCurrent(const AssemblyMemberMotion& member) const;
    bool readSimulated(std::uintptr_t body, pure::TelemetryVector& linear,
                       pure::TelemetryVector& angular) const;
    bool readEffective(std::uintptr_t body, pure::TelemetryVector& linear,
                       pure::TelemetryVector& angular) const;
    bool collisionBodyCurrent(pure::CollisionBody body) const;
    std::uint32_t motionIdentity(std::uintptr_t body, std::uint64_t& id) const;

    std::uintptr_t mainBase_ = 0;
    mutable MotionReadEvidence evidence_{};
    std::uintptr_t havokWorld_ = 0;
    pure::ObstaclePassThrough obstacleLeases_{};
    pure::ObstaclePassThrough::Bodies collisionDesired_{};
    unsigned collisionDesiredCount_ = 0, collisionWriteCount_ = 0;
    struct CollisionWrite { pure::CollisionBody body{}; std::uint32_t mask = 0; };
    std::array<CollisionWrite, pure::ObstaclePassThrough::capacity * 2> collisionWrites_{};
    bool collisionReady_ = false;
    GetMotionTypeFunction getMotionType_ = nullptr;
    RequestLinearVelocityFunction requestLinearVelocity_ = nullptr;
    RequestLinearVelocityFunction requestAngularVelocity_ = nullptr;
    ReadVectorFunction readLinear_ = nullptr;
    ReadVectorFunction readCenter_ = nullptr;
    ReadAngularFunction readAngular_ = nullptr;
};

[[nodiscard]] const char* assemblyMotionStatusName(AssemblyMotionStatus status);

}
