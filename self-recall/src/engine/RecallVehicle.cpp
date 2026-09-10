#include "RecallVehicle.hpp"
#include "RecallActorModelView.hpp"
#include "RecallActorReference.hpp"
#include "RecallVehiclePolicy.hpp"
#include <atomic>
#include <lib.hpp>

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
            const bool entered = !g_native.active(id); \
            g_native.enter(id); \
            const auto result = Orig(action, a2, a3); \
            if (Leaving) g_native.leave(id); \
            if (id && (entered || Leaving)) \
                Logging.Log("[self-recall] VEHICLE_ACTION actor=%u active=%u", id, !Leaving); \
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
    static std::atomic<unsigned> attempts{0};
    const auto count = attempts.fetch_add(1) + 1;
    if (count <= 8 || count % 300 == 0)
        Logging.Log("[self-recall] VEHICLE_DETACH result=%u detached=%u count=%u",
                    static_cast<unsigned>(result), detached, count);
    return detached;
}
}
