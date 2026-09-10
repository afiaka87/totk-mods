#include "RecallGliderRelease.hpp"

#include <lib.hpp>

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
} // namespace

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
} // namespace self_recall::glider_release
