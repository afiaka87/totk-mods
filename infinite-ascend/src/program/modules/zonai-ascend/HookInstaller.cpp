#include "HookInstaller.hpp"

#include <lib.hpp>

#include "Module.hpp"

static_assert(TOTK_VERSION == 121,
              "Zonai Ascend supports only TotK 1.2.1");

namespace zonai_ascend::hooks {
namespace {

namespace off {
constexpr ptrdiff_t PrimarySpanSource = 0x0175CA00;
constexpr ptrdiff_t PrimarySpanHook = 0x0175CA04;
constexpr ptrdiff_t SurroundSpanSource = 0x0175CC24;
constexpr ptrdiff_t SurroundSpanHook = 0x0175CC28;
constexpr ptrdiff_t QueryValid = 0x0175C99C;
constexpr ptrdiff_t MaxHeight = 0x01C64CE4;
constexpr ptrdiff_t MaxHeightRet = 0x01C64CE8;
constexpr ptrdiff_t MarkerSpanSource = 0x00E50444;
constexpr ptrdiff_t MarkerSpanHook = 0x00E50448;
constexpr ptrdiff_t CeilingClipperPostCalc = 0x00E501DC;
constexpr ptrdiff_t ELinkSetPosition = 0x00829FD8;
constexpr ptrdiff_t ELinkSetPosAndScale = 0x01DAF314;
}  // namespace off

namespace word {
constexpr u32 PrimarySpanSource = 0x52A83388;
constexpr u32 PrimarySpanHook = 0xBD42B70A;
constexpr u32 SurroundSpanSource = 0x52A8339B;
constexpr u32 SurroundSpanHook = 0x5283EEBC;
constexpr u32 QueryValid = 0xD10483FF;
constexpr u32 MaxHeight = 0x1E269000;
constexpr u32 MaxHeightRet = 0xD65F03C0;
constexpr u32 MarkerSpanSource = 0xBD42A903;
constexpr u32 MarkerSpanHook = 0xBD42BEE2;
constexpr u32 CeilingClipperPostCalc = 0xA9BB7BFD;
constexpr u32 ELinkSetPosition = 0x79800408;
constexpr u32 ELinkSetPosAndScale = 0xA9BD7BFD;
}  // namespace word

std::uintptr_t g_mainBase = 0;

bool wordMatches(ptrdiff_t offset, u32 expected, const char* label) {
    const u32 actual = *reinterpret_cast<const u32*>(g_mainBase + offset);
    if (actual == expected) return true;
    Logging.Log(
        "[zonai-ascend] GUARD FAILED %s main+%p actual=%08x expected=%08x",
        label, reinterpret_cast<void*>(offset), actual, expected);
    return false;
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

}  // namespace

InstallStatus install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;

    InstallStatus status{};
    status.range =
        wordMatches(off::PrimarySpanSource, word::PrimarySpanSource,
                    "primary span source") &&
        wordMatches(off::PrimarySpanHook, word::PrimarySpanHook,
                    "primary span hook") &&
        wordMatches(off::SurroundSpanSource, word::SurroundSpanSource,
                    "surround span source") &&
        wordMatches(off::SurroundSpanHook, word::SurroundSpanHook,
                    "surround span hook") &&
        wordMatches(off::MaxHeight, word::MaxHeight,
                    "max-height getter") &&
        wordMatches(off::MaxHeightRet, word::MaxHeightRet,
                    "max-height return") &&
        wordMatches(off::MarkerSpanSource, word::MarkerSpanSource,
                    "marker span source") &&
        wordMatches(off::MarkerSpanHook, word::MarkerSpanHook,
                    "marker span hook");

    if (status.range) {
        PrimarySpanHook::InstallAtOffset(off::PrimarySpanHook);
        SurroundingSpanHook::InstallAtOffset(off::SurroundSpanHook);
        MaxHeightHook::InstallAtOffset(off::MaxHeight);
        MarkerSpanHook::InstallAtOffset(off::MarkerSpanHook);
    }

    status.leniency =
        wordMatches(off::QueryValid, word::QueryValid, "queryValid entry");
    if (status.leniency) {
        QueryValidHook::InstallAtOffset(off::QueryValid);
    }

    status.markerScale =
        wordMatches(off::CeilingClipperPostCalc, word::CeilingClipperPostCalc,
                    "CeilingClipper postCalc") &&
        wordMatches(off::ELinkSetPosition, word::ELinkSetPosition,
                    "ELink setPosition") &&
        wordMatches(off::ELinkSetPosAndScale, word::ELinkSetPosAndScale,
                    "ELink setPosAndScale");
    if (status.markerScale) {
        CeilingClipperPostCalcHook::InstallAtOffset(
            off::CeilingClipperPostCalc);
        ELinkSetPositionHook::InstallAtOffset(off::ELinkSetPosition);
    }

    zonai_ascend::init(mainBase, status.leniency, status.markerScale);
    Logging.Log(
        "[zonai-ascend] v0.4.0 release installed range10km=%u lenient=%u markerScale=%u controls=0 overlay=0 guide=0 speed=0 diagnostics=0",
        status.range ? 1u : 0u, status.leniency ? 1u : 0u,
        status.markerScale ? 1u : 0u);
    return status;
}

}  // namespace zonai_ascend::hooks
