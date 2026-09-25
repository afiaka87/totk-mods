#include "HookInstaller.hpp"

#include <lib.hpp>

#include "Module.hpp"
#include "../../../pure/GameProfiles.hpp"

namespace zonai_ascend::hooks {
namespace {

using profiles::Site;

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
        const bool nativePassed = Orig(manager, query);
        return zonai_ascend::resolveQueryValid(manager, nativePassed);
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
        const u64 result = Orig(handle, position);
        zonai_ascend::applyMarkerScale(handle, position);
        return result;
    }
};

}

InstallStatus install(std::uintptr_t mainBase) {
    InstallStatus status{};
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
    Logging.Log(
        "[zonai-ascend] game=%s range10km=1 lenient=1 markerScale=1 controls=0 overlay=0 guide=0 speed=0 diagnostics=0",
        game->version);
    return status;
}

}
