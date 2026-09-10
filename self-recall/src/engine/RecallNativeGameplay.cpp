#include "RecallNativeGameplay.hpp"
#include <atomic>
#include <lib.hpp>
#include "RecallActorModelView.hpp"
#include "RecallFrameTicket.hpp"
#include "RecallGameClock.hpp"
#include "RecallPoseSession.hpp"

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
std::atomic<std::uint64_t> g_fallSuppressed{0}, g_velocitySuppressed{0};

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
        g_velocitySuppressed.fetch_add(1, std::memory_order_relaxed);
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
            const auto count = g_fallSuppressed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 3 || count % 300 == 0)
                Logging.Log("[self-recall] RECALL_FALL_SUPPRESSED count=%llu",
                            static_cast<unsigned long long>(count));
            return;
        }
        Orig(calculator, parameters, damage);
    }
};
} // namespace

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
    Logging.Log("[self-recall] RECALL_STAMINA_BEGIN rate=28 normal=%.3f bonus=%.3f",
        read<float>(component, kStaminaCalculator + 0x5C), read<float>(component, kStaminaCalculator + 0x60));
    return true;
}

void release() {
    pure::GameTimeSnapshot clock;
    if (game_clock::snapshot(clock))
        g_releaseUntil.store(clock.elapsedNanoseconds + pure::kRecallReleaseProtectionNs);
    Logging.Log("[self-recall] RECALL_GAMEPLAY_END velocity_requests=%llu fall_events=%llu release_ms=200",
        static_cast<unsigned long long>(g_velocitySuppressed.load()),
        static_cast<unsigned long long>(g_fallSuppressed.load()));
}

void reset() {
    g_releaseUntil.store(0);
    g_lastDrainSerial.store(0);
    g_fallSuppressed.store(0);
    g_velocitySuppressed.store(0);
    (void)g_owner.publish({});
}

void install() {
    StaminaUpdateHook::InstallAtOffset(0x01623C14);
    PlayerStaminaHook::InstallAtOffset(0x009E6484);
    PlayerVelocityHook::InstallAtOffset(0x01621CBC);
    FallHeightHook::InstallAtOffset(0x009DF480);
    RegisterInternalDamageHook::InstallAtOffset(0x009E4838);
}
} // namespace self_recall::native_gameplay
