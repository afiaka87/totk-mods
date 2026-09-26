// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <lib.hpp>
#include <arrowbound/ActiveGame.hpp>

#include "modules/arrowbound/ArrowboundHookInstaller.hpp"
#include "modules/arrowbound/ArrowboundModule.hpp"

namespace {

HOOK_DEFINE_TRAMPOLINE(RayCastWorkerHook) {
    static u64 OriginalThunk(const void* from, const void* to,
                             const void* object, const void* out, u32 mask,
                             u32 flag) {
        return Orig(from, to, object, out, mask, flag);
    }

    static u64 Callback(const void* from, const void* to, const void* object,
                        const void* out, u32 mask, u32 flag) {
        const u64 result = Orig(from, to, object, out, mask, flag);
        arrowbound::onRaycast(&OriginalThunk, from, object);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        Orig(device);
        arrowbound::tick(device);
    }
};
}

extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();
    const uintptr_t mainBase = exl::util::modules::GetTargetStart();
    const auto* game = arrowbound::profiles::activate(
        mainBase, exl::util::GetMainModuleInfo().m_Text.m_Size);
    if (!game) {
        Logging.Log("[arrowbound] 0.2.3 unknown game build; nothing installed");
        return;
    }
    arrowbound::initialize(mainBase);
    arrowbound::enter();
    if (arrowbound::profiles::entryHookable(mainBase, game->hooks.raycastWorker, "raycast"))
        RayCastWorkerHook::InstallAtOffset(game->hooks.raycastWorker.offset);
    if (arrowbound::profiles::entryHookable(mainBase, game->hooks.npadCalc, "npad"))
        NpadCalcHook::InstallAtOffset(game->hooks.npadCalc.offset);
    arrowbound::hooks::install(mainBase);
    Logging.Log("[arrowbound] 0.2.3 loaded for TotK %s", game->name);
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
