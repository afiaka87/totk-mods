// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis
#include <lib.hpp>

#include "Audio.hpp"
#include "GlyphRenderer.hpp"
#include "ModVersion.hpp"
#include "ScanBatching.hpp"
#include "SurveyStartup.hpp"
#include "AtlasStartup.hpp"
#if SURVEY_STARTUP_DIAGNOSTIC
#include "SurveyStartupDiagnostic.hpp"
#endif
#include "modules/zonai-survey/ZonaiSurveyModule.hpp"
#include "totk/engine/Totk121Offsets.hpp"
#include "FidelityPlayground.hpp"
#include "FidelityPolicy.hpp"

namespace {
using totk::engine::Totk121Offsets;
const wwpg::Module* g_module{};
bool surveyReady() {
    return zonai_survey::engine::g_atlasInputReady.load(std::memory_order_acquire);
}

HOOK_DEFINE_TRAMPOLINE(RayCastWorkerHook) {
    static u64 OriginalThunk(const void* from, const void* to, const void* object,
                             const void* out, u32 mask, u32 flag) {
        return Orig(from, to, object, out, mask, flag);
    }

    static u64 Callback(const void* from, const void* to, const void* object, const void* out,
                        u32 mask, u32 flag) {
        const u64 result = Orig(from, to, object, out, mask, flag);
        if (!surveyReady()) return result;
        const auto& module = wwpg::modules::zonaiSurvey();
        if (module.onRaycast) {
            module.onRaycast(&OriginalThunk, from, to, object, out, mask, flag);
        }
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
#if SURVEY_STARTUP_DIAGNOSTIC
        static std::atomic<unsigned> visits{};
        const auto visit = surveyReady() ? visits.fetch_add(1) : 8;
        if (visit < 8) zonai_survey::engine::startup_diagnostic::mark("input-original-before", visit);
#endif
        Orig(device);
#if SURVEY_STARTUP_DIAGNOSTIC
        if (visit < 8) zonai_survey::engine::startup_diagnostic::mark("input-original-after", visit);
#endif
        if (surveyReady()) {
            g_module->tick(device);
#if SURVEY_STARTUP_DIAGNOSTIC
            if (visit < 8) zonai_survey::engine::startup_diagnostic::mark("survey-tick-after", visit);
#endif
        }
    }
};

#if SURVEY_STARTUP_DIAGNOSTIC
HOOK_DEFINE_INLINE(SurveyObserveHeapHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        zonai_survey::engine::g_atlasObservedHeap=reinterpret_cast<sead::Heap*>(ctx->X[22]);
    }
};
HOOK_DEFINE_INLINE(SurveyEarlyGraphicsHook) {
    static constexpr auto s_name = "survey::early_graphics_arguments";
    static void Callback(exl::hook::InlineCtx* ctx) {
        zonai_survey::engine::startup_diagnostic::captureCreateArgs(ctx->X[20], ctx->X[1]);
    }
};

HOOK_DEFINE_INLINE(SurveyLateGraphicsHook) {
    static constexpr auto s_name = "survey::late_graphics_headroom";
    static void Callback(exl::hook::InlineFloatCtx*) {
        zonai_survey::engine::startup_diagnostic::lateGraphicsInit();
    }
};
#endif

}

extern "C" void exl_main(void*, void*) {
    const uintptr_t mainBase = exl::util::modules::GetTargetStart();
    if (!zonai_survey::engine::startupImageSupported(mainBase)) return;
    exl::hook::Initialize();

    constexpr auto ringBytes = survey_fidelity::kTextOnlyRingBytes;
    constexpr bool ringRaised=false;
#if SURVEY_STARTUP_DIAGNOSTIC
    SurveyObserveHeapHook::InstallAtOffset(0x007F61D0);
    zonai_survey::engine::startup_diagnostic::installFailureHooks(mainBase);
    SurveyEarlyGraphicsHook::InstallAtOffset(0x00A9123C);
    SurveyLateGraphicsHook::InstallAtOffset(0x00A9177C);
#endif

    g_module = &wwpg::modules::zonaiSurvey();
    g_module->init(mainBase);
    g_module->enter();
    NpadCalcHook::InstallAtOffset(Totk121Offsets::kNpadCalc.value);
    audio::installHooks(mainBase);
    survey_fidelity::install(mainBase);

    Logging.Log("[zonai-survey] %s hooks installed; graphics pending DRAW_RING=%u(%d)",
                zonai_survey::kModVersion, ringBytes,
                ringRaised ? 1 : 0);
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
