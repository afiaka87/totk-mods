#include "RecallRuntimeEngine.hpp"
#include "RecallModelEngine.hpp"
#include "RecallVisual.hpp"
#include "RecallBase.hpp"

#include <atomic>
#include <cmath>
#include <lib.hpp>

namespace self_recall::camera {
namespace {
template <class T>
T read(const void* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
    return value;
}

void alignResolvedCamera(void* state, float* camera) {
    if (!pose_session::active()) return;
    static std::atomic<unsigned> refusals{0};
    unsigned refusal = 0;
    pure::Pose shown;
    pure::CameraVerticalCorrection correction;
    if (!state || !camera) {
        refusal = 1;
    } else {
        const auto* component = static_cast<const std::byte*>(state) - 1096;
        const auto* target = read<const void*>(component, 3144);
        void* actor = target ? read<void*>(target, 24) : nullptr;
        if (!actor || !pose_recorder::presentationPose(actor, shown)) {
            refusal = 2;
        } else if (!pure::alignCameraHeight(camera, shown.position.y,
                       read<float>(state, 156), read<float>(state, 840), correction)) {
            refusal = 3;
        }
    }
    if (refusal) {
        const auto n = refusals.fetch_add(1) + 1;
        if (n <= 3 || n % 300 == 0)
            Logging.Log("[self-recall] CAMERA_FRAME_REFUSED reason=%u count=%u", refusal, n);
        return;
    }
}

HOOK_DEFINE_TRAMPOLINE(ResolveCameraHook) {
    static void Callback(void* state, float* camera, float frameScale) {
        Orig(state, camera, frameScale);
        alignResolvedCamera(state, camera);
    }
};

HOOK_DEFINE_TRAMPOLINE(FollowTargetHook) {
    static void Callback(void* action, float* current, const float* target,
                         const float* cushion, bool predict, float amount, float delta) {
        Orig(action, current, target, cushion, predict, amount, delta);
        if (!pose_session::active() || !current || !target || !std::isfinite(target[1])) return;
        current[1] = target[1];
    }
};

HOOK_DEFINE_TRAMPOLINE(FinalTargetHook) {
    static float Callback(void* action, float* target, const void* direction) {
        const auto result = Orig(action, target, direction);
        if (!pose_session::active() || !action || !target || !std::isfinite(target[1])) return result;
        auto* height = static_cast<std::byte*>(action) + 0x98;
        std::memcpy(height, target + 1, sizeof(float));
        return result;
    }
};
}
void install() {
    FollowTargetHook::InstallAtOffset(0x00AE59C8);
    FinalTargetHook::InstallAtOffset(0x00AE675C);
    ResolveCameraHook::InstallAtOffset(0x00A6DF60);
}
}

namespace self_recall::vehicle {
namespace {
using actor_model::read;
std::uintptr_t g_mainBase = 0;
pure::NativeVehicleState g_native;
std::uint32_t actorId(const void* actor) { return actor ? read<std::uint32_t>(actor, 0x10) : 0; }
void* actionActor(void* action) {
    using GetActor = void* (*)(void*);
    return action ? reinterpret_cast<GetActor>(g_mainBase + 0x00BC7610)(action) : nullptr;
}
void* rider(const void* player) {
    const auto* registry = player ? actor_model::registry(player) : nullptr;
    return registry ? read<void*>(registry, 0x410) : nullptr;
}
bool isControlStick(std::uintptr_t base, const void* component) {
    if (!base || !component || read<std::uint32_t>(component, 0x38) != 1 ||
        read<std::int32_t>(component, 0x30) == -1) return false;
    model::ScopedActorReference target(base, static_cast<const std::byte*>(component) + 0x20);
    if (!target) return false;
    const auto* registry = actor_model::registry(target.get());
    const auto* ridable = registry ? read<const void*>(registry, 0x408) : nullptr;
    if (!ridable) return false;
    using SeatType = unsigned (*)(const void*, unsigned);
    return reinterpret_cast<SeatType>(base + 0x01370680)(ridable,
        read<unsigned>(component, 0x3C)) == 5;
}
#define RECALL_VEHICLE_HOOK(Name, Leaving) \
    HOOK_DEFINE_TRAMPOLINE(Name) { \
        static u64 Callback(void* action, void* a2, void* a3) { \
            const auto id = actorId(actionActor(action)); \
            g_native.enter(id); \
            const auto result = Orig(action, a2, a3); \
            if (Leaving) g_native.leave(id); \
            return result; \
        } \
    }
RECALL_VEHICLE_HOOK(ManipulateEnterHook, false);
RECALL_VEHICLE_HOOK(ManipulateUpdateHook, false);
RECALL_VEHICLE_HOOK(ManipulateLeaveHook, true);
#undef RECALL_VEHICLE_HOOK
}
void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    ManipulateEnterHook::InstallAtOffset(0x01D6A1CC);
    ManipulateUpdateHook::InstallAtOffset(0x01D6A280);
    ManipulateLeaveHook::InstallAtOffset(0x01D6A710);
}
void resetWorld() { g_native.clear(); }
bool controlStickActive(std::uintptr_t base, const void* player) {
    return g_native.active(actorId(player)) || controlStickRiding(base, player);
}
bool controlStickRiding(std::uintptr_t base, const void* player) {
    return isControlStick(base, rider(player));
}
bool unmounted(const void* player) {
    const auto* component = rider(player);
    return component && read<std::uint32_t>(component, 0x38) == 0;
}
bool detachControlStick(std::uintptr_t base, void* player) {
    auto* component = rider(player);
    if (!controlStickActive(base, player)) return true;
    if (!component) {
        static std::atomic<unsigned> missing{0};
        if (missing.fetch_add(1) < 8)
            Logging.Log("[self-recall] VEHICLE_DETACH missing_rider actor=%u", actorId(player));
        return false;
    }
    if (read<std::uint32_t>(component, 0x38) == 0) return true;
    using Unride = std::uint64_t (*)(void*, unsigned, bool);
    const auto result = reinterpret_cast<Unride>(base + 0x008096A8)(component, 1, true);
    const bool detached = read<std::uint32_t>(component, 0x38) == 0;
    if (!detached) {
        static std::atomic<unsigned> failures{0};
        const auto count = failures.fetch_add(1) + 1;
        if (count <= 8 || count % 300 == 0)
            Logging.Log("[self-recall] VEHICLE_DETACH_FAILED result=%u count=%u",
                        static_cast<unsigned>(result), count);
    }
    return detached;
}
}

namespace self_recall::glider_release {
namespace {
std::uintptr_t g_mainBase = 0;
pure::NativeTraversalState g_native;
pure::GliderRelease g_release;
std::uint64_t g_reported = 0;

struct ActorIdentity { std::uintptr_t actor = 0; std::uint32_t id = 0; };
ActorIdentity identify(void* actor) {
    if (!actor) return {};
    std::uint32_t id;
    std::memcpy(&id, static_cast<const std::byte*>(actor) + 0x10, sizeof(id));
    return {reinterpret_cast<std::uintptr_t>(actor), id};
}
ActorIdentity actionActor(void* action) {
    using GetActor = void* (*)(void*);
    return g_mainBase && action ? identify(reinterpret_cast<GetActor>(g_mainBase + 0x00BC7610)(action))
                                : ActorIdentity{};
}
pure::GliderReleaseContext context(void* actor, std::uint32_t generation,
                                  std::uint64_t tick, bool allowed) {
    const auto identity = identify(actor);
    return {identity.actor, identity.id, generation, tick, allowed};
}
void report() {
    const auto outcome = g_release.result();
    if (!outcome.serial || outcome.serial <= g_reported) return;
    g_reported = outcome.serial;
    Logging.Log("[self-recall] GLIDE_HANDOFF_END serial=%llu reason=%u forced=%u",
        static_cast<unsigned long long>(outcome.serial), static_cast<unsigned>(outcome.reason), outcome.forcedCalls);
}

#define RECALL_TRAVERSAL_HOOK(Name, Method) \
    HOOK_DEFINE_TRAMPOLINE(Name) { \
        static u64 Callback(void* a1, void* a2, void* a3) { \
            const auto actor = actionActor(a1); \
            const u64 result = Orig(a1, a2, a3); \
            if (actor.actor) { Method; } \
            return result; \
        } \
    }
RECALL_TRAVERSAL_HOOK(FallEnterHook, g_native.enterFall(actor.id));
RECALL_TRAVERSAL_HOOK(FallUpdateHook, g_native.enterFall(actor.id));
RECALL_TRAVERSAL_HOOK(FallLeaveHook, g_native.leaveFall(actor.id));
RECALL_TRAVERSAL_HOOK(GlideEnterHook, g_native.enterGlide(actor.id); g_release.entered(actor.actor, actor.id));
RECALL_TRAVERSAL_HOOK(GlideUpdateHook, g_native.enterGlide(actor.id); g_release.entered(actor.actor, actor.id));
RECALL_TRAVERSAL_HOOK(GlideLeaveHook, g_native.leaveGlide(actor.id));
RECALL_TRAVERSAL_HOOK(ClimbEnterHook, g_native.enterClimb(actor.id));
RECALL_TRAVERSAL_HOOK(ClimbUpdateHook, g_native.enterClimb(actor.id));
RECALL_TRAVERSAL_HOOK(ClimbLeaveHook, g_native.leaveClimb(actor.id));
#undef RECALL_TRAVERSAL_HOOK

HOOK_DEFINE_TRAMPOLINE(GlideEntryPredicateHook) {
    static u64 Callback(void* actor, float height) {
        const u64 original = Orig(actor, height);
        if (original & 1u) return original;
        const auto identity = identify(actor);
        if (!identity.actor) return original;
        const auto ticket = g_release.forceTicket(identity.actor, identity.id, g_native);
        if (!ticket) return original;
        using CheckIsGet = u64 (*)(std::uint32_t);
        if (!(reinterpret_cast<CheckIsGet>(g_mainBase + 0x00B60C00)(1274277390u) & 1u)) {
            g_release.cancelTicket(ticket, pure::GliderReleaseEnd::Unavailable);
            return original;
        }
        if (g_release.forceTicket(identity.actor, identity.id, g_native) != ticket ||
            !g_release.confirmForce(ticket)) return original;
        return 1;
    }
};
}

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    FallEnterHook::InstallAtOffset(0x01D61428);
    FallUpdateHook::InstallAtOffset(0x01D61790);
    FallLeaveHook::InstallAtOffset(0x01D61988);
    GlideEnterHook::InstallAtOffset(0x01D6E54C);
    GlideUpdateHook::InstallAtOffset(0x01D6E810);
    GlideLeaveHook::InstallAtOffset(0x01D6F2A0);
    ClimbEnterHook::InstallAtOffset(0x01D565E4);
    ClimbUpdateHook::InstallAtOffset(0x01D56D50);
    ClimbLeaveHook::InstallAtOffset(0x01D58970);
    GlideEntryPredicateHook::InstallAtOffset(0x01722210);
}
bool nativeGliding(std::uint32_t actorId) { return g_native.gliding(actorId); }
bool nativeClimbing(std::uint32_t actorId) { return g_native.climbing(actorId); }
void request(void* player, std::uint32_t generation, std::uint64_t tick, bool recordedNativeGlide) {
    report();
    const auto serial = g_release.arm(context(player, generation, tick, true), recordedNativeGlide, g_native);
    if (serial) Logging.Log("[self-recall] GLIDE_HANDOFF_ARM serial=%llu generation=%u ticks=%u",
        static_cast<unsigned long long>(serial), generation, static_cast<unsigned>(pure::GliderRelease::kAcquireTicks));
}
void service(void* player, std::uint32_t generation, std::uint64_t tick, bool allowed, bool userCancelled) {
    g_release.service(context(player, generation, tick, allowed), userCancelled);
    report();
}
void cancel(pure::GliderReleaseEnd reason) { g_release.cancel(reason); report(); }
void resetWorld() { cancel(pure::GliderReleaseEnd::ContextChanged); g_native.clear(); }
}

namespace self_recall::native_gameplay {
namespace {
using actor_model::read;
constexpr std::size_t kStaminaCalculator = 0x1388;
struct Owner {
    const void* actor = nullptr;
    const void* component = nullptr;
    std::uint32_t actorId = 0;
};
class OwnerMailbox {
    std::atomic<std::uint64_t> serial_{0};
    std::atomic<const void*> actor_{nullptr}, component_{nullptr};
    std::atomic<std::uint32_t> id_{0};
public:
    bool publish(Owner owner) {
        serial_.fetch_add(1);
        actor_.store(owner.actor);
        component_.store(owner.component);
        id_.store(owner.actorId);
        serial_.fetch_add(1);
        return true;
    }
    bool snapshot(Owner& out) const {
        const auto serial = serial_.load();
        if (serial & 1) return false;
        out = {actor_.load(), component_.load(), id_.load()};
        return serial_.load() == serial;
    }
} g_owner;
std::atomic<std::uint64_t> g_releaseUntil{0}, g_lastDrainSerial{0};

const void* playerComponent(const void* player) {
    const auto* registry = player ? actor_model::registry(player) : nullptr;
    return registry ? read<const void*>(registry, 0x3A8) : nullptr;
}

bool activeOwner(Owner& out) {
    return pose_session::active() && g_owner.snapshot(out) && out.actor && out.component;
}

bool protectedActor(const void* actor) {
    Owner owner;
    if (!actor || !g_owner.snapshot(owner) || owner.actor != actor ||
        read<std::uint32_t>(actor, actor_model::kActorId) != owner.actorId) return false;
    if (pose_session::active()) return true;
    pure::GameTimeSnapshot clock;
    return game_clock::snapshot(clock) &&
        pure::releaseProtectionActive(clock.elapsedNanoseconds, g_releaseUntil.load());
}

struct StaminaRequest { float rate; std::uint32_t kind; };
HOOK_DEFINE_TRAMPOLINE(StaminaUpdateHook) {
    static std::uintptr_t Callback(void* calculator, const StaminaRequest* request) {
        Owner owner;
        if (request && request->rate > 0 && activeOwner(owner) &&
            calculator == static_cast<const std::byte*>(owner.component) + kStaminaCalculator)
            return reinterpret_cast<std::uintptr_t>(calculator);
        return Orig(calculator, request);
    }
};

HOOK_DEFINE_TRAMPOLINE(PlayerStaminaHook) {
    static std::uintptr_t Callback(void* component) {
        Owner owner;
        pure::GameTimeSnapshot clock;
        if (activeOwner(owner) && owner.component == component &&
            read<const void*>(component, 0x18) == owner.actor &&
            read<std::uint32_t>(owner.actor, actor_model::kActorId) == owner.actorId &&
            game_clock::snapshot(clock) && clock.status == pure::GameTimeStatus::Running &&
            g_lastDrainSerial.exchange(clock.serial) != clock.serial) {
            const StaminaRequest request{pure::kRecallStaminaPerSecond, 0};
            StaminaUpdateHook::Orig(static_cast<std::byte*>(component) + kStaminaCalculator, &request);
        }
        return Orig(component);
    }
};

HOOK_DEFINE_TRAMPOLINE(PlayerVelocityHook) {
    static void Callback(void* component, const float* velocity, bool perFrame) {
        if (!protectedActor(read<const void*>(component, 0x18))) {
            Orig(component, velocity, perFrame);
            return;
        }
        const float zero[3]{};
        Orig(component, zero, false);
    }
};

HOOK_DEFINE_TRAMPOLINE(FallHeightHook) {
    static void Callback(void* calculator) {
        const auto* actor = read<const void*>(calculator, 8);
        if (!protectedActor(actor)) { Orig(calculator); return; }
        const float height = read<float>(actor, 0x2B8);
        const float zero = 0;
        std::memcpy(static_cast<std::byte*>(calculator) + 0x560, &height, sizeof(height));
        std::memcpy(static_cast<std::byte*>(calculator) + 0x564, &zero, sizeof(zero));
    }
};

HOOK_DEFINE_TRAMPOLINE(RegisterInternalDamageHook) {
    static void Callback(void* calculator, const void* parameters, const void* damage) {
        if (damage && read<std::uint32_t>(damage, 0x14) == 4 &&
            protectedActor(read<const void*>(calculator, 8))) {
            return;
        }
        Orig(calculator, parameters, damage);
    }
};
}

pure::StaminaStatus stamina(const void* player) {
    const auto* component = playerComponent(player);
    if (!component) return pure::StaminaStatus::Unavailable;
    return pure::staminaStatus(read<float>(component, kStaminaCalculator + 0x5C),
                               read<float>(component, kStaminaCalculator + 0x60));
}

bool begin(const void* player) {
    reset();
    const auto* component = playerComponent(player);
    if (!component || !g_owner.publish({player, component,
            read<std::uint32_t>(player, actor_model::kActorId)})) {
        Logging.Log("[self-recall] RECALL_GAMEPLAY_OWNER_UNAVAILABLE player=%p component=%p", player, component);
        return false;
    }
    return true;
}

void release() {
    pure::GameTimeSnapshot clock;
    if (game_clock::snapshot(clock))
        g_releaseUntil.store(clock.elapsedNanoseconds + pure::kRecallReleaseProtectionNs);
}

void reset() {
    g_releaseUntil.store(0);
    g_lastDrainSerial.store(0);
    (void)g_owner.publish({});
}

void install() {
    StaminaUpdateHook::InstallAtOffset(0x01623C14);
    PlayerStaminaHook::InstallAtOffset(0x009E6484);
    PlayerVelocityHook::InstallAtOffset(0x01621CBC);
    FallHeightHook::InstallAtOffset(0x009DF480);
    RegisterInternalDamageHook::InstallAtOffset(0x009E4838);
}
}
