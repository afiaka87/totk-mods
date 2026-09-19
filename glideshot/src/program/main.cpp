// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <lib.hpp>

#include "modules/zonai-hookshot/ChainRenderer.hpp"
#include "modules/zonai-hookshot/ChainShaderPass.hpp"
#include "modules/zonai-hookshot/HookshotHookInstaller.hpp"
#include "modules/zonai-hookshot/HookshotHooks.hpp"

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
        const auto& module = wwpg::modules::zonaiHookshot();
        if (module.onRaycast) {
            module.onRaycast(&OriginalThunk, from, to, object, out, mask, flag);
        }
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        Orig(device);
        wwpg::modules::zonaiHookshot().tick(device);
        zonai_hookshot::hooks::afterNpad(device);
    }
};

}  // namespace

extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();
    const uintptr_t mainBase = exl::util::modules::GetTargetStart();
    zonai_hookshot::render::configure(mainBase);
    zonai_hookshot::render::installShaderPass(mainBase);
    const auto& module = wwpg::modules::zonaiHookshot();
    module.init(mainBase);
    module.enter();
    RayCastWorkerHook::InstallAtOffset(kRayCastWorker);
    NpadCalcHook::InstallAtOffset(kNpadCalc);
    zonai_hookshot::hooks::installUnique(mainBase);
    Logging.Log("[glideshot] zonai-hookshot 0.8.1 loaded");
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
