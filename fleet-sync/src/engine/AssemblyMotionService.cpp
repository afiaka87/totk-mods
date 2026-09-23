#include "engine/AssemblyMotionService.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include "lib/program/loggers.hpp"

#include "engine/LinkedStickOffsets.hpp"
#include "pure/VehicleMatch.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace linked_stick::engine {
namespace math = pure::matched;
namespace {
constexpr std::uint32_t kDynamicMotionType = 2;
constexpr float kMaximumPlausibleSpeed = math::kMaximumMotionSpeed;
constexpr float kMaximumCorrectionDelta = math::kMaximumMemberDelta;

struct EngineActorReference {
    void* actor = nullptr;
    std::uint8_t ownsReference = 0;
    std::uint8_t padding[7]{};

    // A non-trivial return preserves the engine function's hidden-X8 ABI.
    ~EngineActorReference() {}
};
static_assert(sizeof(EngineActorReference) == 16);

[[nodiscard]] bool finiteVelocity(pure::TelemetryVector value) {
    return pure::isFinite(value) && std::fabs(value.x) <= kMaximumPlausibleSpeed &&
           std::fabs(value.y) <= kMaximumPlausibleSpeed &&
           std::fabs(value.z) <= kMaximumPlausibleSpeed;
}

[[nodiscard]] float lengthSquared(pure::TelemetryVector value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

[[nodiscard]] constexpr bool plausibleNameIdentity(std::uintptr_t address) {
    return totk::engine::isPlausibleStringAddress(address);
}

// Actor-name identities may be unaligned; keep this fixture odd.
static_assert(plausibleNameIdentity(0x1001));
static_assert(!totk::engine::isPlausibleAddress(0x1001));
int milli(float x) {
    if (!std::isfinite(x)) return std::numeric_limits<int>::min();
    return static_cast<int>(std::clamp(static_cast<double>(x) * 1000, -2147483647.0, 2147483647.0));
}
}

void AssemblyMotionService::configure(std::uintptr_t mainBase) {
    *this = AssemblyMotionService{};
    if (!totk::engine::isPlausibleAddress(mainBase) ||
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kHavokReadVelocities) !=
            0xA9BD7BFD ||
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kBodyNextLinearVelocity) !=
            0xA9BD7BFD ||
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kBodyNextAngularVelocity) !=
            0xFC1C0FEA ||
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kBodyCenterOfMassWorld) !=
            0xD100C3FF ||
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kRequestAngularVelocity) !=
            0xFC1B0FEA ||
        totk::engine::readMemory<std::uint32_t>(
            mainBase + totk::engine::Totk121Offsets::kRequestSetLinearVelocity.value) !=
            0xFC1B0FEA) {
        return;
    }
    mainBase_ = mainBase;
    collisionReady_ =
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kRequestLayerHitMask) == 0xA9BC7BFD &&
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kHavokBodyIsValid) == 0xA9BE7BFD &&
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kHavokGetBody) == 0xA9BE7BFD;
    Logging.Log("[fleet-sync][collision] ABI ready=%u mask=%x valid=%x body=%x", collisionReady_ ? 1u : 0u,
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kRequestLayerHitMask),
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kHavokBodyIsValid),
        totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kHavokGetBody));
    getMotionType_ = reinterpret_cast<GetMotionTypeFunction>(
        mainBase + totk::engine::Totk121Offsets::kGetMotionType.value);
    requestLinearVelocity_ = reinterpret_cast<RequestLinearVelocityFunction>(
        mainBase + totk::engine::Totk121Offsets::kRequestSetLinearVelocity.value);
    requestAngularVelocity_ = reinterpret_cast<RequestLinearVelocityFunction>(
        mainBase + offsets::kRequestAngularVelocity);
    readLinear_ = reinterpret_cast<ReadVectorFunction>(mainBase + offsets::kBodyNextLinearVelocity);
    readAngular_ =
        reinterpret_cast<ReadAngularFunction>(mainBase + offsets::kBodyNextAngularVelocity);
    readCenter_ = reinterpret_cast<ReadVectorFunction>(mainBase + offsets::kBodyCenterOfMassWorld);
}

bool AssemblyMotionService::beginPhysics(std::uintptr_t framework) {
    evidence_ = {};
    evidence_.reason = "service or framework unavailable";
    havokWorld_ = 0;
    if (!mainBase_ || !totk::engine::isPlausibleAddress(framework)) return false;
    const auto world =
        totk::engine::readMemory<std::uintptr_t>(framework + offsets::kPhysicsEntityWorld);
    evidence_.world = world;
    evidence_.reason = "entity world manager unavailable";
    if (!totk::engine::isPlausibleAddress(world)) return false;
    havokWorld_ = totk::engine::readMemory<std::uintptr_t>(world + offsets::kWorldHavok);
    evidence_.world = havokWorld_;
    evidence_.reason = "Havok world unavailable";
    return totk::engine::isPlausibleAddress(havokWorld_);
}

bool AssemblyMotionService::collisionBodyCurrent(pure::CollisionBody body) const {
    if (!collisionReady_ || !havokWorld_ || body.world != havokWorld_ || !body.body) return false;
    using Valid = bool (*)(void*, std::uint64_t);
    using Get = std::uintptr_t (*)(void*, std::uint64_t);
    auto* world = reinterpret_cast<void*>(havokWorld_);
    // Validate generation, capacity and allocation; GetBody alone is unsafe.
    if (!reinterpret_cast<Valid>(mainBase_ + offsets::kHavokBodyIsValid)(world, body.id)) return false;
    const auto native = reinterpret_cast<Get>(mainBase_ + offsets::kHavokGetBody)(world, body.id);
    if (!totk::engine::isPlausibleAddress(native) ||
        totk::engine::readMemory<std::uintptr_t>(native + offsets::kHavokBodyPhive) != body.body) return false;
    const auto sdk = totk::engine::readMemory<std::uintptr_t>(body.body + offsets::kBodySdkInstance);
    return totk::engine::isPlausibleAddress(sdk) &&
        totk::engine::readMemory<std::uint64_t>(sdk + offsets::kSdkBodyId) == body.id;
}
std::uint32_t AssemblyMotionService::motionIdentity(std::uintptr_t body, std::uint64_t& id) const {
    const auto sdk = totk::engine::readMemory<std::uintptr_t>(body + offsets::kBodySdkInstance);
    if (!totk::engine::isPlausibleAddress(sdk)) return 0xffffffffu;
    id = totk::engine::readMemory<std::uint64_t>(sdk + offsets::kSdkBodyId);
    if (!collisionBodyCurrent({havokWorld_, body, id})) return 0xffffffffu;
    using Get = std::uintptr_t (*)(void*, std::uint64_t);
    const auto native = reinterpret_cast<Get>(mainBase_ + offsets::kHavokGetBody)(reinterpret_cast<void*>(havokWorld_), id);
    return totk::engine::readMemory<std::uint32_t>(native + offsets::kHavokBodyMotionId);
}
bool AssemblyMotionService::readCollisionMask(pure::CollisionBody body, std::uint32_t& mask) const {
    if (!collisionBodyCurrent(body)) return false;
    using totk::engine::readMemory;
    mask = readMemory<std::uint32_t>(body.body + offsets::kBodyLayerHitMask);
    const auto request = readMemory<std::uintptr_t>(body.body + offsets::kBodyChangeRequest);
    if (totk::engine::isPlausibleAddress(request) &&
        (readMemory<std::uint32_t>(request + offsets::kChangeRequestFlags) & 0x4000))
        mask = (mask | readMemory<std::uint32_t>(request + offsets::kPendingLayerAdd)) &
            ~readMemory<std::uint32_t>(request + offsets::kPendingLayerRemove);
    return true;
}
bool AssemblyMotionService::writeCollisionMask(pure::CollisionBody body, std::uint32_t mask) {
    if (!collisionBodyCurrent(body)) { collisionEvent("write identity refused", body, 0, mask); return false; }
    using Request = void (*)(void*, std::uint32_t);
    reinterpret_cast<Request>(mainBase_ + offsets::kRequestLayerHitMask)(reinterpret_cast<void*>(body.body), mask);
    std::uint32_t actual = 0;
    const bool queued = readCollisionMask(body, actual) && actual == mask;
    collisionEvent(queued ? "queued" : "queue mismatch", body, actual, mask);
    if (collisionWriteCount_ < collisionWrites_.size()) collisionWrites_[collisionWriteCount_++] = {body, mask};
    return queued;
}
void AssemblyMotionService::collisionEvent(const char* event, pure::CollisionBody body,
                                          std::uint32_t before, std::uint32_t after) const {
    Logging.Log("[fleet-sync][collision] event=%s world=%p body=%p id=%llx before=%x after=%x",
        event, reinterpret_cast<void*>(body.world), reinterpret_cast<void*>(body.body),
        static_cast<unsigned long long>(body.id), before, after);
}
void AssemblyMotionService::beginCollisionFrame() { collisionDesiredCount_ = collisionWriteCount_ = 0; }
void AssemblyMotionService::allowObstaclePassThrough(const AssemblyMotionSnapshot& snapshot) {
    if (!snapshot.valid()) return;
    for (unsigned i = 0; i < snapshot.memberCount; ++i) {
        const auto body = snapshot.members[i].rigidBody;
        const auto sdk = totk::engine::readMemory<std::uintptr_t>(body + offsets::kBodySdkInstance);
        if (!totk::engine::isPlausibleAddress(sdk)) { collisionEvent("SDK refused", {havokWorld_, body, 0}, 0, 0); continue; }
        if (collisionDesiredCount_ >= collisionDesired_.size()) { collisionEvent("desired capacity refused", {havokWorld_, body, 0}, 0, 0); break; }
        collisionDesired_[collisionDesiredCount_++] = {havokWorld_, body,
            totk::engine::readMemory<std::uint64_t>(sdk + offsets::kSdkBodyId)};
    }
}
void AssemblyMotionService::finishCollisionFrame(bool logSummary) {
    obstacleLeases_.update(havokWorld_, collisionDesired_, collisionDesiredCount_, *this);
    if (logSummary) Logging.Log("[fleet-sync][collision] SUMMARY ready=%u desired=%u leases=%u writes=%u terrain=solid",
        collisionReady_ ? 1u : 0u, collisionDesiredCount_, obstacleLeases_.size(), collisionWriteCount_);
}
void AssemblyMotionService::verifyCollisionDelivery(bool logSummary) const {
    for (unsigned i = 0; i < collisionWriteCount_; ++i) {
        const auto& w = collisionWrites_[i];
        if (!collisionBodyCurrent(w.body)) { collisionEvent("delivery identity retired", w.body, 0, w.mask); continue; }
        const auto actual = totk::engine::readMemory<std::uint32_t>(w.body.body + offsets::kBodyLayerHitMask);
        collisionEvent(actual == w.mask ? "delivered" : "delivery mismatch", w.body, actual, w.mask);
    }
    if (!logSummary) return;
    for (unsigned i = 0; i < collisionDesiredCount_; ++i) {
        const auto body = collisionDesired_[i];
        if (!collisionBodyCurrent(body)) { collisionEvent("active identity retired", body, 0, 0); continue; }
        const auto filter = totk::engine::readMemory<std::uintptr_t>(body.body + offsets::kBodyCollisionFilter);
        if (!totk::engine::isPlausibleAddress(filter)) { collisionEvent("filter refused", body, 0, 0); continue; }
        const auto actual = totk::engine::readMemory<std::uint32_t>(filter + offsets::kFilterLayerHitMask);
        collisionEvent("active filter", body, actual, pure::kPassThroughLayers);
    }
}

bool AssemblyMotionService::readSimulated(std::uintptr_t body, math::Vec& linear,
                                          math::Vec& angular) const {
    evidence_ = {};
    evidence_.body = body;
    evidence_.world = havokWorld_;
    evidence_.reason = "world or body unavailable";
    if (!havokWorld_ || !totk::engine::isPlausibleAddress(body)) return false;
    const auto flags = totk::engine::readMemory<std::uint64_t>(body + offsets::kBodyFlags);
    evidence_.flags = flags;
    evidence_.reason = "sensor, absent or paused body";
    // Query SDK bodies only in the active, unpaused entity world.
    if ((flags & 0x20) || !(flags & 0x400) || (flags & 0x400000)) return false;
    const auto sdk = totk::engine::readMemory<std::uintptr_t>(body + offsets::kBodySdkInstance);
    evidence_.sdk = sdk;
    evidence_.reason = "SDK body unavailable";
    if (!totk::engine::isPlausibleAddress(sdk)) return false;
    const auto vtable = totk::engine::readMemory<std::uintptr_t>(havokWorld_);
    evidence_.reason = "Havok vtable unavailable";
    if (!totk::engine::isPlausibleAddress(vtable)) return false;
    const auto function =
        totk::engine::readMemory<std::uintptr_t>(vtable + offsets::kHavokReadVelocitiesVtable);
    evidence_.function = function;
    evidence_.reason = "velocity reader target mismatch";
    // Native code requires 4-byte alignment, unlike 8-byte object pointers.
    if (function != mainBase_ + offsets::kHavokReadVelocities) return false;
    struct alignas(16) Vector4 {
        float x, y, z, w;
    } v{}, w{};
    using Read = void (*)(void*, std::uint64_t, Vector4*, Vector4*);
    evidence_.bodyId = totk::engine::readMemory<std::uint64_t>(sdk + offsets::kSdkBodyId);
    reinterpret_cast<Read>(function)(reinterpret_cast<void*>(havokWorld_), evidence_.bodyId, &v, &w);
    linear = {v.x, v.y, v.z};
    angular = {w.x, w.y, w.z};
    evidence_.linear = linear; evidence_.angular = angular;
    const bool valid = finiteVelocity(linear) && pure::isFinite(angular) && math::length(angular) < math::kMaximumAngularSpeed;
    evidence_.reason = valid ? "read OK" : "invalid body velocity";
    return valid;
}

bool AssemblyMotionService::readEffective(std::uintptr_t body, math::Vec& linear,
                                          math::Vec& angular) const {
    if (!readSimulated(body, linear, angular)) return false;
    math::Vec queuedLinear{}, queuedAngular{};
    std::uint32_t flags = 0;
    const auto request =
        totk::engine::readMemory<std::uintptr_t>(body + offsets::kBodyChangeRequest);
    if (totk::engine::isPlausibleAddress(request)) {
        flags = totk::engine::readMemory<std::uint32_t>(request + offsets::kChangeRequestFlags);
        if (flags & 0x80) readLinear_(reinterpret_cast<void*>(body), &queuedLinear);
        if (flags & 0x100) queuedAngular = readAngular_(reinterpret_cast<void*>(body));
    }
    linear = math::effectiveVelocity(linear, queuedLinear, (flags & 0x80) != 0);
    angular = math::effectiveVelocity(angular, queuedAngular, (flags & 0x100) != 0);
    return finiteVelocity(linear) && pure::isFinite(angular) && math::length(angular) < math::kMaximumAngularSpeed;
}

bool AssemblyMotionService::verifyDelivery(const AssemblyMotionSnapshot& snapshot,
                                           float& worstLinear, float& worstAngular) const {
    bool delivered = true;
    MotionReadEvidence failure{};
    worstLinear = worstAngular = 0;
    for (std::uint32_t i = 0; i < snapshot.memberCount; ++i) {
        const auto& member = snapshot.members[i];
        math::Vec v{}, w{};
        if (!memberStillCurrent(member)) {
            evidence_.body = member.rigidBody;
            evidence_.reason = "delivery body identity changed";
            return false;
        }
        if (!readSimulated(member.rigidBody, v, w)) return false;
        worstLinear = std::max(worstLinear, math::length(math::sub(v, member.targetVelocity)));
        worstAngular = std::max(worstAngular, math::length(math::sub(w, member.targetAngular)));
        if (!math::velocityDelivered(member.targetVelocity, member.targetAngular, v, w)) {
            failure = evidence_;
            failure.reason = "delivery velocity mismatch";
            failure.targetLinear = member.targetVelocity;
            failure.targetAngular = member.targetAngular;
            delivered = false;
        }
    }
    if (!delivered) evidence_ = failure;
    return delivered;
}

void AssemblyMotionService::logDeliveryEvidence(const AssemblyMotionSnapshot& snapshot, unsigned receiver) const {
    for (unsigned i = 0; i < snapshot.memberCount; ++i) {
        const auto& a = snapshot.members[i];
        math::Vec v{}, w{};
        if (!memberStillCurrent(a) || !readSimulated(a.rigidBody, v, w)) {
            Logging.Log("[fleet-sync][matched] PART refused receiver=%u member=%u body=%p reason=%s",
                receiver, i, reinterpret_cast<void*>(a.rigidBody), evidence_.reason);
            continue;
        }
        std::uint64_t id = 0;
        const auto motion = motionIdentity(a.rigidBody, id);
        Logging.Log("[fleet-sync][matched] LINEAR_PART receiver=%u member=%u body=%p id=%llx,%llx motion=%u,%u "
                    "before_m=%d,%d,%d pending_m=%d,%d,%d raw_m=%d,%d,%d target_m=%d,%d,%d after_m=%d,%d,%d",
            receiver, i, reinterpret_cast<void*>(a.rigidBody), static_cast<unsigned long long>(a.bodyId),
            static_cast<unsigned long long>(id), a.motionId, motion,
            milli(a.velocity.x), milli(a.velocity.y), milli(a.velocity.z),
            milli(a.pendingVelocity.x), milli(a.pendingVelocity.y), milli(a.pendingVelocity.z),
            milli(a.rawTargetVelocity.x), milli(a.rawTargetVelocity.y), milli(a.rawTargetVelocity.z),
            milli(a.targetVelocity.x), milli(a.targetVelocity.y), milli(a.targetVelocity.z), milli(v.x), milli(v.y), milli(v.z));
        Logging.Log("[fleet-sync][matched] ANGULAR_PART receiver=%u member=%u "
                    "before_m=%d,%d,%d pending_m=%d,%d,%d raw_m=%d,%d,%d target_m=%d,%d,%d after_m=%d,%d,%d",
            receiver, i, milli(a.angular.x), milli(a.angular.y), milli(a.angular.z),
            milli(a.pendingAngular.x), milli(a.pendingAngular.y), milli(a.pendingAngular.z),
            milli(a.rawTargetAngular.x), milli(a.rawTargetAngular.y), milli(a.rawTargetAngular.z),
            milli(a.targetAngular.x), milli(a.targetAngular.y), milli(a.targetAngular.z), milli(w.x), milli(w.y), milli(w.z));
    }
}

std::uintptr_t AssemblyMotionService::resolveActorLink(std::uintptr_t actorLink) const {
    if (!mainBase_ || !totk::engine::isPlausibleAddress(actorLink)) return 0;

    using GetReferenceFunction = EngineActorReference (*)(void*);
    const auto getReference =
        reinterpret_cast<GetReferenceFunction>(mainBase_ + offsets::kActorLinkGetReference);
    EngineActorReference reference = getReference(reinterpret_cast<void*>(actorLink));
    const std::uintptr_t actor = reinterpret_cast<std::uintptr_t>(reference.actor);
    if (reference.ownsReference && totk::engine::isPlausibleAddress(actor)) {
        auto* count = reinterpret_cast<std::uint32_t*>(actor + offsets::kActorReferenceCount);
        if (__atomic_load_n(count, __ATOMIC_RELAXED) > 0) {
            __atomic_fetch_sub(count, 1u, __ATOMIC_RELAXED);
        }
    }
    reference.actor = nullptr;
    reference.ownsReference = 0;
    return actor;
}

std::uintptr_t AssemblyMotionService::rigidBodyOf(std::uintptr_t actor) const {
    if (!totk::engine::isPlausibleAddress(actor)) return 0;
    const auto registry = totk::engine::readMemory<std::uintptr_t>(
        actor + totk::engine::layout::kActorComponentRegistry);
    if (!totk::engine::isPlausibleAddress(registry)) return 0;
    const auto physics = totk::engine::readMemory<std::uintptr_t>(
        registry + totk::engine::layout::kPhysicsFromRegistry);
    if (!totk::engine::isPlausibleAddress(physics)) return 0;
    const auto set = totk::engine::readMemory<std::uintptr_t>(
        physics + totk::engine::layout::kRigidBodySetFromPhysics);
    if (!totk::engine::isPlausibleAddress(set)) return 0;
    const auto body =
        totk::engine::readMemory<std::uintptr_t>(set + totk::engine::layout::kRigidBodyFromSet);
    return totk::engine::isPlausibleAddress(body) ? body : 0;
}

AssemblyMotionSnapshot AssemblyMotionService::capture(std::uintptr_t receiverComponent) const {
    AssemblyMotionSnapshot result{};
    result.receiver = receiverComponent;
    if (!mainBase_ || !getMotionType_ || !requestLinearVelocity_) {
        result.status = AssemblyMotionStatus::ServiceUnavailable;
        return result;
    }
    if (!totk::engine::isPlausibleAddress(receiverComponent)) {
        result.status = AssemblyMotionStatus::ReceiverUnavailable;
        return result;
    }

    result.integrator = totk::engine::readMemory<std::uintptr_t>(receiverComponent +
                                                                 offsets::kCombinedActorIntegrator);
    if (totk::engine::isPlausibleAddress(result.integrator)) {
        const auto count = totk::engine::readMemory<std::uint32_t>(
            result.integrator + offsets::kCombinedActorMemberCount);
        if (!count) {
            result.status = AssemblyMotionStatus::IntegratorUnavailable;
            return result;
        }
        if (count > AssemblyMotionSnapshot::kMaximumMembers) {
            result.status = AssemblyMotionStatus::MemberLimitExceeded;
            return result;
        }
        const auto links = totk::engine::readMemory<std::uintptr_t>(
            result.integrator + offsets::kCombinedActorMemberLinks);
        if (!totk::engine::isPlausibleAddress(links)) {
            result.status = AssemblyMotionStatus::MemberListUnavailable;
            return result;
        }
        result.memberCount = count;
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto link =
                totk::engine::readMemory<std::uintptr_t>(links + index * sizeof(std::uintptr_t));
            const auto actor = resolveActorLink(link);
            if (!totk::engine::isPlausibleAddress(actor)) {
                result.status = AssemblyMotionStatus::MemberReferenceUnavailable;
                return result;
            }
            result.members[index].actor = actor;
        }
    } else {
        const auto actor = totk::engine::readMemory<std::uintptr_t>(receiverComponent +
                                                                    offsets::kComponentOwnerActor);
        if (!totk::engine::isPlausibleAddress(actor)) {
            result.status = AssemblyMotionStatus::IntegratorUnavailable;
            return result;
        }
        result.memberCount = 1;
        result.members[0].actor = actor;
    }

    const auto stick =
        totk::engine::readMemory<std::uintptr_t>(receiverComponent + offsets::kComponentOwnerActor);
    if (!totk::engine::isPlausibleAddress(stick)) {
        result.status = AssemblyMotionStatus::ReceiverUnavailable;
        return result;
    }
    const auto stickPosition =
        totk::engine::readMemory<math::Vec>(stick + totk::engine::layout::kActorPosition);
    result.motion.rotation =
        totk::engine::readMemory<math::Mat>(stick + totk::engine::layout::kActorRotation);
    if (!math::validRotation(result.motion.rotation) || !pure::isFinite(stickPosition)) {
        result.status = AssemblyMotionStatus::CorrectionRejected;
        return result;
    }
    const auto inverse = math::transpose(result.motion.rotation);
    pure::TelemetryVector velocitySum{};
    math::Vec centerSum{}, angularSum{};
    for (std::uint32_t index = 0; index < result.memberCount; ++index) {
        auto& member = result.members[index];
        member.nameIdentity = totk::engine::readMemory<std::uintptr_t>(
            member.actor + totk::engine::layout::kActorNamePointer);
        if (!plausibleNameIdentity(member.nameIdentity)) {
            result.status = AssemblyMotionStatus::MemberIdentityUnavailable;
            return result;
        }
        member.rigidBody = rigidBodyOf(member.actor);
        if (!member.rigidBody) {
            result.status = AssemblyMotionStatus::PhysicsBodyUnavailable;
            return result;
        }
        if (getMotionType_(reinterpret_cast<void*>(member.rigidBody)) != kDynamicMotionType) {
            result.status = AssemblyMotionStatus::NonDynamicMember;
            return result;
        }
        const auto sdkBody =
            totk::engine::readMemory<std::uintptr_t>(member.rigidBody + offsets::kBodySdkInstance);
        if (!totk::engine::isPlausibleAddress(sdkBody)) {
            result.status = AssemblyMotionStatus::PhysicsBodyUnavailable;
            return result;
        }
        if (!readSimulated(member.rigidBody, member.velocity, member.angular)) {
            result.status = AssemblyMotionStatus::PhysicsBodyUnavailable;
            return result;
        }
        readCenter_(reinterpret_cast<void*>(member.rigidBody), &member.center);
        if (!finiteVelocity(member.velocity) || !pure::isFinite(member.angular) ||
            math::length(member.angular) > math::kMaximumAngularSpeed || !pure::isFinite(member.center)) {
            result.status = AssemblyMotionStatus::NonFiniteVelocity;
            return result;
        }
        const auto position = totk::engine::readMemory<math::Vec>(
            member.actor + totk::engine::layout::kActorPosition);
        const auto rotation = totk::engine::readMemory<math::Mat>(
            member.actor + totk::engine::layout::kActorRotation);
        if (!pure::isFinite(position) || !math::validRotation(rotation)) {
            result.status = AssemblyMotionStatus::CorrectionRejected;
            return result;
        }
        member.shape.position = math::rotate(inverse, math::sub(position, stickPosition));
        member.shape.rotation = math::product(inverse, rotation);
        member.shape.kind = pure::vehiclePartKind(reinterpret_cast<const char*>(member.nameIdentity));
        result.shape.members[index] = member.shape;
        centerSum = math::add(centerSum, member.center);
        angularSum = math::add(angularSum, member.angular);
        velocitySum.x += member.velocity.x;
        velocitySum.y += member.velocity.y;
        velocitySum.z += member.velocity.z;
    }

    const float inverseCount = 1.0f / static_cast<float>(result.memberCount);
    result.averageVelocity = {
        velocitySum.x * inverseCount,
        velocitySum.y * inverseCount,
        velocitySum.z * inverseCount,
    };
    result.shape.count = result.memberCount;
    result.motion.position = math::mul(centerSum, inverseCount);
    result.motion.velocity = result.averageVelocity;
    result.motion.angular = math::mul(angularSum, inverseCount);
    result.motion.radius = 1;
    for (std::uint32_t i = 0; i < result.memberCount; ++i) {
        result.motion.radius = std::max(
            result.motion.radius,
            math::length(math::sub(result.members[i].center, result.motion.position)) + 0.5f);
    }
    result.status = AssemblyMotionStatus::Ready;
    return result;
}

bool AssemblyMotionService::memberStillCurrent(const AssemblyMemberMotion& member) const {
    if (!totk::engine::isPlausibleAddress(member.actor) ||
        !plausibleNameIdentity(member.nameIdentity) ||
        !totk::engine::isPlausibleAddress(member.rigidBody)) {
        return false;
    }
    if (totk::engine::readMemory<std::uintptr_t>(
            member.actor + totk::engine::layout::kActorNamePointer) != member.nameIdentity) {
        return false;
    }
    return rigidBodyOf(member.actor) == member.rigidBody;
}

AssemblyMotionStatus AssemblyMotionService::applyCommonVelocityDelta(
    const AssemblyMotionSnapshot& snapshot, pure::TelemetryVector delta) const {
    auto copy = snapshot;
    return applyCorrection(copy, delta, {});
}

AssemblyMotionStatus AssemblyMotionService::applyCorrection(
    AssemblyMotionSnapshot& snapshot, pure::TelemetryVector delta,
    pure::TelemetryVector angularDelta) const {
    if (!snapshot.valid() || !requestLinearVelocity_ || !getMotionType_) {
        return AssemblyMotionStatus::ServiceUnavailable;
    }
    if (!pure::isFinite(delta) || !pure::isFinite(angularDelta) ||
        math::length(angularDelta) > math::kMaximumAngularDelta ||
        lengthSquared(delta) > kMaximumCorrectionDelta * kMaximumCorrectionDelta) {
        Logging.Log("[fleet-sync][matched] COMMAND_REFUSED dv_m=%d,%d,%d dw_m=%d,%d,%d max_dv_m=%d max_dw_m=%d",
            milli(delta.x), milli(delta.y), milli(delta.z), milli(angularDelta.x), milli(angularDelta.y),
            milli(angularDelta.z), milli(kMaximumCorrectionDelta), milli(math::kMaximumAngularDelta));
        return AssemblyMotionStatus::CorrectionRejected;
    }

    const auto currentIntegrator = totk::engine::readMemory<std::uintptr_t>(
        snapshot.receiver + offsets::kCombinedActorIntegrator);
    if (currentIntegrator != snapshot.integrator) {
        return AssemblyMotionStatus::MembershipChanged;
    }
    if (snapshot.integrator &&
        totk::engine::readMemory<std::uint32_t>(
            snapshot.integrator + offsets::kCombinedActorMemberCount) != snapshot.memberCount) {
        return AssemblyMotionStatus::MembershipChanged;
    }

    std::array<pure::TelemetryVector, AssemblyMotionSnapshot::kMaximumMembers> targets{};
    std::array<math::Vec, AssemblyMotionSnapshot::kMaximumMembers> angularTargets{};
    for (std::uint32_t index = 0; index < snapshot.memberCount; ++index) {
        auto& member = snapshot.members[index];
        if (!memberStillCurrent(member)) {
            return AssemblyMotionStatus::MembershipChanged;
        }
        if (getMotionType_(reinterpret_cast<void*>(member.rigidBody)) != kDynamicMotionType) {
            return AssemblyMotionStatus::NonDynamicMember;
        }
        // Re-resolve the complete link roster, including equal-count replacements.
        if (snapshot.integrator) {
            const auto links = totk::engine::readMemory<std::uintptr_t>(
                snapshot.integrator + offsets::kCombinedActorMemberLinks);
            if (!totk::engine::isPlausibleAddress(links) ||
                resolveActorLink(totk::engine::readMemory<std::uintptr_t>(
                    links + index * sizeof(std::uintptr_t))) != member.actor)
                return AssemblyMotionStatus::MembershipChanged;
        }
        math::Vec velocity{}, center{};
        math::Vec angular{};
        if (!readEffective(member.rigidBody, velocity, angular))
            return AssemblyMotionStatus::PhysicsBodyUnavailable;
        readCenter_(reinterpret_cast<void*>(member.rigidBody), &center);
        if (!finiteVelocity(velocity) || !pure::isFinite(angular) || !pure::isFinite(center)) {
            return AssemblyMotionStatus::NonFiniteVelocity;
        }
        const auto correction =
            math::memberDelta(delta, angularDelta, center, snapshot.motion.position);
        targets[index] = math::add(velocity, correction);
        angularTargets[index] = math::add(angular, angularDelta);
        member.pendingVelocity = velocity;
        member.pendingAngular = angular;
        member.rawTargetVelocity = targets[index];
        member.rawTargetAngular = angularTargets[index];
        member.motionId = motionIdentity(member.rigidBody, member.bodyId);
        if (!finiteVelocity(targets[index]) || math::length(correction) > kMaximumCorrectionDelta ||
            !pure::isFinite(angularTargets[index]) || math::length(angularTargets[index]) > math::kMaximumAngularSpeed) {
            Logging.Log("[fleet-sync][matched] MEMBER_COMMAND_REFUSED member=%u body=%p dv_m=%d "
                        "target_v_m=%d,%d,%d target_w_m=%d,%d,%d max_dv_m=%d max_w_m=%d",
                index, reinterpret_cast<void*>(member.rigidBody), milli(math::length(correction)),
                milli(targets[index].x), milli(targets[index].y), milli(targets[index].z),
                milli(angularTargets[index].x), milli(angularTargets[index].y), milli(angularTargets[index].z),
                milli(kMaximumCorrectionDelta), milli(math::kMaximumAngularSpeed));
            return AssemblyMotionStatus::CorrectionRejected;
        }
    }

    // Readback proves current delivery, not survival through the next physics step.
    bool queued = true;
    for (std::uint32_t index = 0; index < snapshot.memberCount; ++index) {
        float velocity[3] = {targets[index].x, targets[index].y, targets[index].z};
        requestLinearVelocity_(reinterpret_cast<void*>(snapshot.members[index].rigidBody),
                               velocity);
        float angular[3] = {angularTargets[index].x, angularTargets[index].y,
                            angularTargets[index].z};
        requestAngularVelocity_(reinterpret_cast<void*>(snapshot.members[index].rigidBody),
                                angular);
        snapshot.members[index].targetVelocity = {velocity[0], velocity[1], velocity[2]};
        snapshot.members[index].targetAngular = {angular[0], angular[1], angular[2]};
        math::Vec readback{};
        readLinear_(reinterpret_cast<void*>(snapshot.members[index].rigidBody), &readback);
        const auto angularReadback =
            readAngular_(reinterpret_cast<void*>(snapshot.members[index].rigidBody));
        queued =
            queued && pure::isFinite(readback) && pure::isFinite(angularReadback) &&
            math::length(math::sub(readback, {velocity[0], velocity[1], velocity[2]})) < 0.05f &&
            math::length(math::sub(angularReadback, {angular[0], angular[1], angular[2]})) < 0.02f;
    }
    return queued ? AssemblyMotionStatus::Ready : AssemblyMotionStatus::RequestReadbackMismatch;
}

const char* assemblyMotionStatusName(AssemblyMotionStatus status) {
    switch (status) {
        case AssemblyMotionStatus::Ready:
            return "ready";
        case AssemblyMotionStatus::ServiceUnavailable:
            return "service";
        case AssemblyMotionStatus::ReceiverUnavailable:
            return "receiver";
        case AssemblyMotionStatus::IntegratorUnavailable:
            return "integrator";
        case AssemblyMotionStatus::MemberLimitExceeded:
            return "member-limit";
        case AssemblyMotionStatus::MemberListUnavailable:
            return "member-list";
        case AssemblyMotionStatus::MemberReferenceUnavailable:
            return "member-ref";
        case AssemblyMotionStatus::MemberIdentityUnavailable:
            return "identity";
        case AssemblyMotionStatus::PhysicsBodyUnavailable:
            return "body";
        case AssemblyMotionStatus::NonDynamicMember:
            return "non-dynamic";
        case AssemblyMotionStatus::NonFiniteVelocity:
            return "velocity";
        case AssemblyMotionStatus::MembershipChanged:
            return "membership";
        case AssemblyMotionStatus::CorrectionRejected:
            return "correction";
        case AssemblyMotionStatus::RequestReadbackMismatch:
            return "request-readback-mismatch";
    }
    return "unknown";
}

}
