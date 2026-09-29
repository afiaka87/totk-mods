// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis
#include <lib.hpp>

#include "Audio.hpp"
#include "ModVersion.hpp"
#include "PulseRenderer.hpp"
#include "SurveyStartup.hpp"
#include "modules/zonai-survey/ZonaiSurveyModule.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace {
using totk::engine::Totk121Offsets;
const wwpg::Module* g_module{};

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        Orig(device);
        g_module->tick(device);
    }
};
}

extern "C" void exl_main(void*, void*) {
    const uintptr_t mainBase = exl::util::modules::GetTargetStart();
    if (!zonai_survey::engine::startupImageSupported(mainBase)) return;
    exl::hook::Initialize();

    g_module = &wwpg::modules::zonaiSurvey();
    g_module->init(mainBase);
    g_module->enter();
    NpadCalcHook::InstallAtOffset(Totk121Offsets::kNpadCalc.value);
    audio::init(mainBase);
    zonai_survey::pulse::install(mainBase);

    Logging.Log("[zonai-survey] %s hooks installed", zonai_survey::kModVersion);
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
