// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <lib.hpp>

#include "modules/arrowbound/ArrowboundHookInstaller.hpp"
#include "modules/arrowbound/ArrowboundModule.hpp"

namespace {
constexpr ptrdiff_t kNpadCalc = 0x02A267BC;
constexpr ptrdiff_t kRayCastWorker = 0x00858590;

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
    arrowbound::initialize(mainBase);
    arrowbound::enter();
    RayCastWorkerHook::InstallAtOffset(kRayCastWorker);
    NpadCalcHook::InstallAtOffset(kNpadCalc);
    arrowbound::hooks::install(mainBase);
    Logging.Log("[arrowbound] 0.1.0 loaded");
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
