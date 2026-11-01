#include "HookInstaller.hpp"

#include <lib.hpp>

#include "Module.hpp"
#include "RemoteTargeting.hpp"
#include "TallSpeed.hpp"
#include "../../../pure/GameProfiles.hpp"
#include "../../../pure/RemoteProfile121.hpp"

namespace zonai_ascend::hooks {
namespace {

using profiles::Site;
namespace remote121 = remote::profile121;

std::uintptr_t g_remoteMain = 0;
bool queryRemoteLayers(remote::Layers* layers, const remote::Vec3* position) {
    const auto slot = *reinterpret_cast<const std::uintptr_t*>(
        g_remoteMain + remote121::kVolumeStatsSlot);
    if (!slot) return false;
    void* module = *reinterpret_cast<void* const*>(slot);
    if (!module) return false;
    using Query = bool (*)(void*, remote::Layers*, const remote::Vec3*);
    return reinterpret_cast<Query>(g_remoteMain+remote121::kLayersQuery)(module, layers, position);
}

exl::hook::InlineCtx* floatGeneralRegisters(exl::hook::InlineFloatCtx* ctx) {
    // Vendored inline_asm.s passes Q0-Q31 first, followed by InlineCtx. S[] compensates; X[] does not.
    static_assert(sizeof(ctx->m_Fr) == 0x200 && offsetof(exl::hook::InlineCtx, m_Gpr.m_Lr) == 0xF0);
    return reinterpret_cast<exl::hook::InlineCtx*>(reinterpret_cast<unsigned char*>(ctx)+sizeof(ctx->m_Fr));
}

HOOK_DEFINE_INLINE(ForwardSpeedAfterCapHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        const auto regs = floatGeneralRegisters(ctx);
        void* player = reinterpret_cast<void*>(regs->X[0]);
        ctx->S[8] = speed::scaleForwardStep(player, ctx->S[8],
                                           regs->X[30] == g_remoteMain+remote121::kTravelCommandReturn);
    }
};

HOOK_DEFINE_INLINE(ApproachLookaheadHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        void* node = reinterpret_cast<void*>(floatGeneralRegisters(ctx)->X[19]);
        ctx->S[1] = speed::scaleApproachLookahead(node, ctx->S[1]);
    }
};

HOOK_DEFINE_TRAMPOLINE(TravelSpeedHook) {
    static void* Callback(void* node) {
        void* result = Orig(node);
        speed::extendBraking(node);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ApproachPhysicsHook) {
    static u64 Callback(void* node) {
        speed::ApproachScope scope(node);
        return Orig(node);
    }
};

ptrdiff_t offset(const profiles::GameProfile& game, Site site) {
    return game.at(site).offset;
}

HOOK_DEFINE_INLINE(PrimarySpanHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        ctx->W[8] = zonai_ascend::validationSpanBits();
    }
};

HOOK_DEFINE_INLINE(SurroundingSpanHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        ctx->W[27] = zonai_ascend::validationSpanBits();
    }
};

HOOK_DEFINE_TRAMPOLINE(MaxHeightHook) {
    static float Callback() {
        return zonai_ascend::currentReach();
    }
};

HOOK_DEFINE_INLINE(MarkerSpanHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        ctx->S[3] = zonai_ascend::markerSpan();
    }
};

HOOK_DEFINE_TRAMPOLINE(QueryValidHook) {
    static bool Callback(void* manager, void* query) {
        remote::beginQuery(manager);
        const bool nativePassed = Orig(manager, query);
        return zonai_ascend::resolveQueryValid(manager, query, nativePassed);
    }
};

HOOK_DEFINE_TRAMPOLINE(QueryFloorHook) {
    static bool Callback(void* manager, void* query, const remote::Vec3* origin,
                         remote::Vec3* output, bool* failed) {
        const bool nativePassed = Orig(manager, query, origin, output, failed);
        return remote::resolveFloor(manager, query, origin, output, failed, nativePassed);
    }
};

HOOK_DEFINE_TRAMPOLINE(TravelResultHook) {
    static u64 Callback(void* node) {
        if (remote::cancelUnsafeArrival(node)) return 2;
        return Orig(node);
    }
};

HOOK_DEFINE_TRAMPOLINE(CommitFloorHook) {
    static u64 Callback(void* manager) {
        const u64 result = Orig(manager);
        remote::commit(manager);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(CancelInputHook) {
    static u64 Callback(void* selector) {
        const u64 result = Orig(selector);
        remote::finishApproachCancel(selector);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(CeilingClipperPostCalcHook) {
    static u64 Callback(void* manager, void* updateContext) {
        zonai_ascend::beginMarkerPostCalc(manager, updateContext);
        const u64 result = Orig(manager, updateContext);
        zonai_ascend::endMarkerPostCalc(manager);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ELinkSetPositionHook) {
    static u64 Callback(void* handle, const void* position) {
        position = zonai_ascend::markerPosition(handle, position);
        const u64 result = Orig(handle, position);
        zonai_ascend::applyMarkerScale(handle, position);
        return result;
    }
};

}

InstallStatus install(std::uintptr_t mainBase) {
    InstallStatus status{};
    remote::bind({});
    speed::bind(nullptr);
    const auto textSize = exl::util::GetMainModuleInfo().m_Text.m_Size;
    const auto* game = profiles::select(textSize, [mainBase](ptrdiff_t at) {
        return *reinterpret_cast<const u32*>(mainBase + at);
    });
    if (!game) {
        Logging.Log("[zonai-ascend] unknown or changed main; all hooks disabled; text=%p",
                    reinterpret_cast<void*>(textSize));
        zonai_ascend::init(mainBase, false, false, 0, 0);
        return status;
    }

    const bool is121 = offset(*game, Site::QueryValid) == 0x175C99C;
    const bool remoteHealthy = is121 && remote121::matches(textSize, [mainBase](ptrdiff_t at) {
        return *reinterpret_cast<const u32*>(mainBase+at);
    });
    if (is121 && !remoteHealthy) {
        for (const auto& guard : remote121::kGuards) {
            const auto actual = *reinterpret_cast<const u32*>(mainBase+guard.offset);
            if (actual == guard.original) continue;
            Logging.Log("[zonai-ascend] remote disabled: offset=%x actual=%08x expected=%08x",
                        static_cast<unsigned>(guard.offset), actual, guard.original);
            break;
        }
    }

    PrimarySpanHook::InstallAtOffset(offset(*game, Site::PrimarySpanHook));
    SurroundingSpanHook::InstallAtOffset(offset(*game, Site::SurroundSpanHook));
    MaxHeightHook::InstallAtOffset(offset(*game, Site::MaxHeight));
    MarkerSpanHook::InstallAtOffset(offset(*game, Site::MarkerSpanHook));
    QueryValidHook::InstallAtOffset(offset(*game, Site::QueryValid));
    CeilingClipperPostCalcHook::InstallAtOffset(
        offset(*game, Site::CeilingClipperPostCalc));
    ELinkSetPositionHook::InstallAtOffset(offset(*game, Site::ELinkSetPosition));

    status.range = true;
    status.leniency = true;
    status.markerScale = true;
    zonai_ascend::init(mainBase, true, true,
                       offset(*game, Site::ELinkSetPosAndScale),
                       game->actorPositionOffset);
    if (remoteHealthy) {
        g_remoteMain = mainBase;
        remote::Bindings bindings{};
        bindings.queryLayers = queryRemoteLayers;
        bindings.blockedExit = reinterpret_cast<decltype(bindings.blockedExit)>(mainBase+remote121::kBlockedExit);
        bindings.isUnderground = reinterpret_cast<decltype(bindings.isUnderground)>(mainBase+remote121::kUnderground);
        bindings.getActor = reinterpret_cast<decltype(bindings.getActor)>(mainBase+remote121::kGetActor);
        bindings.getPlayer = reinterpret_cast<decltype(bindings.getPlayer)>(mainBase+remote121::kGetPlayer);
        bindings.getSelectorChild = reinterpret_cast<decltype(bindings.getSelectorChild)>(mainBase+remote121::kSelectorChild);
        bindings.snapFloor = reinterpret_cast<decltype(bindings.snapFloor)>(mainBase+remote121::kSnapFloor);
        remote::bind(bindings);
        speed::bind(bindings.getPlayer);
        QueryFloorHook::InstallAtOffset(remote121::kFloorQuery);
        TravelResultHook::InstallAtOffset(remote121::kTravelResult);
        CommitFloorHook::InstallAtOffset(remote121::kCommitFloor);
        CancelInputHook::InstallAtOffset(remote121::kCancelInput);
        ForwardSpeedAfterCapHook::InstallAtOffset(remote121::kForwardSpeedAfterCap);
        ApproachLookaheadHook::InstallAtOffset(remote121::kApproachLookahead);
        TravelSpeedHook::InstallAtOffset(remote121::kTravelSpeed);
        ApproachPhysicsHook::InstallAtOffset(remote121::kApproachPhysics);
        exl::patch::CodePatcher patch(0);
        for (auto at : remote121::kVoxelLimits) {
            patch.Seek(at);
            patch.WriteInst(exl::armv8::inst::Movz(exl::armv8::reg::W4, 10000));
        }
    }
    Logging.Log(
        "[zonai-ascend] game=%s range10km=1 lenient=1 markerScale=1 controls=0 overlay=0 guide=0 speed=%d remote=%d diagnostics=0",
        game->version, remoteHealthy, remoteHealthy);
    return status;
}

}
