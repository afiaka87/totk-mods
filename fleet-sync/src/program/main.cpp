#include <lib.hpp>

#include <array>
#include <atomic>
#include <cstdint>

#include "engine/LinkedStickOffsets.hpp"
#include "feature/LinkedStickRuntime.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/port/CodeCheck.hpp"

#ifdef TOTK_SUITE_MEMBER
#include "totk/suite/Member.hpp"
#endif

namespace {
namespace offsets = linked_stick::engine::offsets;

HOOK_DEFINE_TRAMPOLINE(GetControlStickXHook) {
    static float Callback(void* receiver) {
        auto& runtime = linked_stick::feature::runtime();
        void* source = runtime.controlSource(receiver);
        const float value = Orig(source);
        runtime.observeControlAxis(
            receiver, linked_stick::feature::ControlAxis::X, value);
        return value;
    }
};

HOOK_DEFINE_TRAMPOLINE(GetControlStickForwardBackHook) {
    static float Callback(void* receiver) {
        auto& runtime = linked_stick::feature::runtime();
        void* source = runtime.controlSource(receiver);
        const float value = Orig(source);
        runtime.observeControlAxis(
            receiver,
            linked_stick::feature::ControlAxis::ForwardBack, value);
        return value;
    }
};

HOOK_DEFINE_TRAMPOLINE(GetControlStickYHook) {
    static float Callback(void* receiver) {
        auto& runtime = linked_stick::feature::runtime();
        void* source = runtime.controlSource(receiver);
        const float value = Orig(source);
        runtime.observeControlAxis(
            receiver, linked_stick::feature::ControlAxis::Y, value);
        return value;
    }
};

HOOK_DEFINE_TRAMPOLINE(IsSpecialPartsOnHook) {
    static bool Callback(void* receiver) {
        return Orig(
            linked_stick::feature::runtime().controlSource(receiver, false));
    }
};

HOOK_DEFINE_TRAMPOLINE(SetActiveControlStickHook) {
    static u64 Callback(void* module, void* actor) {
        auto& runtime = linked_stick::feature::runtime();
        return Orig(module, runtime.activationTarget(actor));
    }
};

HOOK_DEFINE_TRAMPOLINE(ReleaseActiveControlStickHook) {
    static u64 Callback(void* module, void* actor) {
        auto& runtime = linked_stick::feature::runtime();
        void* target = runtime.releaseTarget(actor);
        const u64 result = Orig(module, target);
        runtime.completeRelease(actor, target);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(EvaluateRidableIsRiddenSeatHook) {
    static u64 Callback(void* query, void* ridable, std::int32_t seatIndex) {
        void* source = linked_stick::feature::runtime().riddenSource(ridable);
        return Orig(query, source, seatIndex);
    }
};

HOOK_DEFINE_TRAMPOLINE(CopyRiderInputHook) {
    static u64 Callback(void* ridable, void* output,
                        std::uint32_t seatIndex) {
        auto& runtime = linked_stick::feature::runtime();
        const auto token = runtime.beginRiderInputCopy(ridable, seatIndex);
        const u64 result = Orig(token.source, output, token.sourceSeat);
        runtime.completeRiderInputCopy(token, output);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(RidableGetRiderTypeHook) {
    static u64 Callback(void* ridable, std::uint32_t seatIndex) {
        void* source = linked_stick::feature::runtime()
                           .ridableRiderTypeSource(ridable);
        return Orig(source, seatIndex);
    }
};

HOOK_DEFINE_TRAMPOLINE(RidableSeatGetRiderTypeHook) {
    static u64 Callback(void* seat) {
        void* source =
            linked_stick::feature::runtime().riderTypeSource(seat);
        return Orig(source);
    }
};

HOOK_DEFINE_TRAMPOLINE(OneShotBaseGetActorHook) {
    static void* Callback(void* oneShot) {
        void* actor = Orig(oneShot);
        return linked_stick::feature::runtime().initialActivationActor(
            oneShot, actor);
    }
};

HOOK_DEFINE_TRAMPOLINE(RideOnCombinedControlStickHook) {
    static u64 Callback(void* oneShot, void* context) {
        auto& runtime = linked_stick::feature::runtime();
        if (runtime.publishedReceiverCount() < 2) {
            return Orig(oneShot, context);
        }
        void* actor = OneShotBaseGetActorHook::Orig(oneShot);
        const std::uint32_t count =
            runtime.initialActivationFanoutCount(oneShot, actor);

        if (count < 2) return Orig(oneShot, context);

        u64 result = 0;
        bool invoked = false;
        for (std::uint32_t index = 0; index < count; ++index) {
            if (!runtime.beginInitialActivationPass(oneShot, index)) break;
            result = Orig(oneShot, context);
            invoked = true;
        }
        runtime.endInitialActivationFanout();
        return invoked ? result : Orig(oneShot, context);
    }
};

// Update X19's component+0x48 before the relocated S3 load, without touching live SIMD.
HOOK_DEFINE_INLINE(RemoteEnergyRangeHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        linked_stick::feature::runtime().extendRemoteEnergyRange(
            reinterpret_cast<void*>(ctx->X[19]));
    }
};

HOOK_DEFINE_TRAMPOLINE(ActorPresenceIsCalcHook) {
    static bool Callback(void* judge, void* actor) {
        const bool vanilla = Orig(judge, actor);
        const bool forced = linked_stick::feature::runtime()
                                .shouldForceStandaloneReceiverCalc(
                                    actor, vanilla);
        return vanilla || forced;
    }
};

HOOK_DEFINE_TRAMPOLINE(ActorPresenceIsUnloadHook) {
    static bool Callback(void* judge, void* preActor) {
        const bool vanilla = Orig(judge, preActor);
        if (vanilla && linked_stick::feature::runtime()
                           .shouldKeepRemoteAssemblyResidentAtUnload(
                               preActor)) {
            return false;
        }
        return vanilla;
    }
};

HOOK_DEFINE_TRAMPOLINE(ActorPresenceIsDeleteHook) {
    static bool Callback(void* judge, void* actor) {
        const bool vanilla = Orig(judge, actor);
        if (vanilla && linked_stick::feature::runtime()
                           .shouldKeepRemoteAssemblyResidentAtDelete(
                               actor)) {
            return false;
        }
        return vanilla;
    }
};

// Zonai Fieldworks clears this when the player switches Fleet Sync off; the standalone module is always on.
bool g_switchedOn = true;

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        Orig(device);
        if (g_switchedOn) linked_stick::feature::runtime().tick(device);
    }
};

// The caller holds the world lock; Orig drains requests before scheduling the solver.
HOOK_DEFINE_TRAMPOLINE(PhysicsRequestsHook) {
    static void Callback(void* framework) {
        auto& runtime = linked_stick::feature::runtime();
        const bool audit = runtime.beginPhysics(framework);
        Orig(framework);
        if (audit) runtime.endPhysics();
    }
};

// 1.4.3 only: the physics job inlines its own request routine, so two instruction hooks bracket that copy.
std::atomic<std::uintptr_t> g_inlinePhysicsFrame{0};

// Integer register `index` of the hooked code; `ctx->X[]` does not work on a float context.
std::uintptr_t inlineRegister(const exl::hook::InlineFloatCtx* ctx, unsigned index) {
    return static_cast<std::uintptr_t>(*reinterpret_cast<const std::uint64_t*>(
        reinterpret_cast<const std::uint8_t*>(ctx) + 0x200 + index * 8));
}

HOOK_DEFINE_INLINE(InlinePhysicsBeginHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        const auto frame = inlineRegister(ctx, 29);
        if (!frame) return;
        void* framework = reinterpret_cast<void*>(inlineRegister(ctx, 25));
        if (linked_stick::feature::runtime().beginPhysics(framework)) {
            g_inlinePhysicsFrame.store(frame, std::memory_order_release);
        }
    }
};

HOOK_DEFINE_INLINE(InlinePhysicsEndHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        auto frame = inlineRegister(ctx, 29);
        if (frame && g_inlinePhysicsFrame.compare_exchange_strong(
                         frame, 0, std::memory_order_acq_rel)) {
            linked_stick::feature::runtime().endPhysics();
        }
    }
};

void installInlinePhysics143(std::uintptr_t mainBase) {
    using namespace totk::port::literals;
    constexpr std::ptrdiff_t begin = 0x1807B0_v143;
    constexpr std::ptrdiff_t end = 0x180E84_v143;
    const std::uint32_t words[4] = {
        totk::suite::checkedCode(mainBase + begin), totk::suite::checkedCode(mainBase + begin + 4),
        totk::suite::checkedCode(mainBase + end), totk::suite::checkedCode(mainBase + end + 4)};
    if (words[0] != 0x3941832A || words[1] != 0x7100011F || words[2] != 0x39418328 ||
        words[3] != 0x7100011F) {
        Logging.Log("[fleet-sync] formation off on 1.4.3: the per-frame physics job does not match "
                    "(begin=%08x,%08x end=%08x,%08x)", words[0], words[1], words[2], words[3]);
        return;
    }
    InlinePhysicsBeginHook::InstallAtOffset(begin);
    InlinePhysicsEndHook::InstallAtOffset(end);
    Logging.Log("[fleet-sync] formation on 1.4.3: per-frame physics job hooked at %p and %p",
                reinterpret_cast<void*>(begin), reinterpret_cast<void*>(end));
}

// 1.4.3 only: naming the mod's callbacks in the presence table routes all three checks through them, as on 1.2.1.
struct PresenceEntry143 {
    const char* name;
    std::uintptr_t entry, routine, callback;
};

std::array<PresenceEntry143, 3> presenceEntries143(std::uintptr_t mainBase) {
    using namespace totk::port::literals;
    return {{
        {"calc", mainBase + 0x3634198_v143, mainBase + offsets::kActorPresenceIsCalc,
         reinterpret_cast<std::uintptr_t>(&ActorPresenceIsCalcHook::Callback)},
        {"delete", mainBase + 0x36341A0_v143, mainBase + offsets::kActorPresenceIsDelete,
         reinterpret_cast<std::uintptr_t>(&ActorPresenceIsDeleteHook::Callback)},
        {"unload", mainBase + 0x36341A8_v143, mainBase + offsets::kActorPresenceIsUnload,
         reinterpret_cast<std::uintptr_t>(&ActorPresenceIsUnloadHook::Callback)},
    }};
}

// True when all three table entries are writable data that still name the game's routines.
bool presenceTableReady143(std::uintptr_t mainBase) {
    for (const auto& entry : presenceEntries143(mainBase)) {
        MemoryInfo info{};
        std::uint32_t page{};
        const bool writable =
            !svcQueryMemory(&info, &page, entry.entry) && (info.perm & Perm_Rw) == Perm_Rw;
        const auto held = writable ? totk::engine::readMemory<std::uintptr_t>(entry.entry) : 0;
        if (held != entry.routine) {
            Logging.Log("[fleet-sync] keep-loaded off on 1.4.3: presence table entry %s at %p is "
                        "not the game routine (writable=%u holds=%p expected=%p)", entry.name,
                        reinterpret_cast<void*>(entry.entry - mainBase), writable ? 1u : 0u,
                        reinterpret_cast<void*>(held), reinterpret_cast<void*>(entry.routine));
            return false;
        }
    }
    return true;
}

// Call after the three presence hooks are installed, so each callback can reach the game routine.
void routePresenceTable143(std::uintptr_t mainBase) {
    for (const auto& entry : presenceEntries143(mainBase)) {
        totk::engine::writeMemory(entry.entry, entry.callback);
        Logging.Log("[fleet-sync] presence %s on 1.4.3: table entry %p was routine %p, now %p",
                    entry.name, reinterpret_cast<void*>(entry.entry - mainBase),
                    reinterpret_cast<void*>(entry.routine - mainBase),
                    reinterpret_cast<void*>(
                        totk::engine::readMemory<std::uintptr_t>(entry.entry)));
    }
}
}

namespace linked_stick::suite {

#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks, on the controller thread. Off clears the pair and stops reading the stick clicks; the
// physics hook runs a little longer to release the receivers (design 06, section 3a).
void enabled(bool on) {
    auto& runtime = linked_stick::feature::runtime();
    g_switchedOn = on;
    if (on) runtime.enter();
    else runtime.switchOff();
    Logging.Log("[fleet-sync] SWITCHED %s", on ? "on" : "off");
}

// Riding a linked stick: the game's active stick is the receiver's until the player gets off.
bool busy() { return linked_stick::feature::runtime().bridging(); }
#endif

// Shared by the standalone module and Zonai Fieldworks, on TotK 1.2.1 and 1.4.3.
bool install(std::uintptr_t mainBase) {
    if (!totk::port::supported()) return false;
    auto& runtime = linked_stick::feature::runtime();
    runtime.initialize(mainBase);
    runtime.enter();
    GetControlStickXHook::InstallAtOffset(offsets::kGetControlStickX);
    GetControlStickForwardBackHook::InstallAtOffset(
        offsets::kGetControlStickForwardBack);
    GetControlStickYHook::InstallAtOffset(offsets::kGetControlStickY);
    IsSpecialPartsOnHook::InstallAtOffset(offsets::kIsSpecialPartsOn);
    SetActiveControlStickHook::InstallAtOffset(offsets::kSetActiveControlStick);
    ReleaseActiveControlStickHook::InstallAtOffset(
        offsets::kReleaseActiveControlStick);
    EvaluateRidableIsRiddenSeatHook::InstallAtOffset(
        offsets::kEvaluateRidableIsRiddenSeat);
    CopyRiderInputHook::InstallAtOffset(offsets::kCopyRiderInput);
    RidableGetRiderTypeHook::InstallAtOffset(
        offsets::kRidableGetRiderType);
    RidableSeatGetRiderTypeHook::InstallAtOffset(
        offsets::kRidableSeatGetRiderType);
    OneShotBaseGetActorHook::InstallAtOffset(
        offsets::kOneShotBaseGetActor);
    RideOnCombinedControlStickHook::InstallAtOffset(
        offsets::kRideOnCombinedControlStickExecute);
    RemoteEnergyRangeHook::InstallAtOffset(
        offsets::kRemoteEnergyRangeLoad);
    // On 1.4.3 the presence hooks go in only together with the table entries that lead to them.
    const bool presence = !totk::port::is143() || presenceTableReady143(mainBase);
    if (presence) {
        ActorPresenceIsCalcHook::InstallAtOffset(
            offsets::kActorPresenceIsCalc);
        ActorPresenceIsUnloadHook::InstallAtOffset(
            offsets::kActorPresenceIsUnload);
        ActorPresenceIsDeleteHook::InstallAtOffset(
            offsets::kActorPresenceIsDelete);
    }
    if (presence && totk::port::is143()) routePresenceTable143(mainBase);
    NpadCalcHook::InstallAtOffset(offsets::kNpadCalc);
    if (totk::port::wordIs(mainBase, offsets::kPhysicsProcessRequests, 0xD10303FF)) {
        PhysicsRequestsHook::InstallAtOffset(offsets::kPhysicsProcessRequests);
    } else {
        Logging.Log("[fleet-sync] physics hook signature mismatch; formation unavailable");
    }
    if (totk::port::is143()) installInlinePhysics143(mainBase);
    Logging.Log(
        "[fleet-sync] fleet-row-11 hooks installed "
        "formation=pre-solve-havok trace=periodic-independent controls=tap-link-hold-clear audio=role-chimes text=none");
    return true;
}

}  // namespace linked_stick::suite

#ifndef TOTK_SUITE_MEMBER
extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();
    if (!totk::port::start("fleet-sync")) return;
    (void)linked_stick::suite::install(exl::util::modules::GetTargetStart());
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
#endif
