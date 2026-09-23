#include <lib.hpp>

#include "engine/LinkedStickOffsets.hpp"
#include "feature/LinkedStickRuntime.hpp"
#include "totk/engine/Pointer.hpp"

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

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        Orig(device);
        linked_stick::feature::runtime().tick(device);
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
}

extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();
    const uintptr_t mainBase = exl::util::modules::GetTargetStart();
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
    ActorPresenceIsCalcHook::InstallAtOffset(
        offsets::kActorPresenceIsCalc);
    ActorPresenceIsUnloadHook::InstallAtOffset(
        offsets::kActorPresenceIsUnload);
    ActorPresenceIsDeleteHook::InstallAtOffset(
        offsets::kActorPresenceIsDelete);
    NpadCalcHook::InstallAtOffset(offsets::kNpadCalc);
    if (totk::engine::readMemory<std::uint32_t>(mainBase + offsets::kPhysicsProcessRequests) ==
        0xD10303FF) {
        PhysicsRequestsHook::InstallAtOffset(offsets::kPhysicsProcessRequests);
    } else {
        Logging.Log("[fleet-sync] physics hook signature mismatch; formation unavailable");
    }
    Logging.Log(
        "[fleet-sync] fleet-row-11 hooks installed "
        "formation=pre-solve-havok trace=periodic-independent controls=tap-link-hold-clear audio=role-chimes text=none");
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
