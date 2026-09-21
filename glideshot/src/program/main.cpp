// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <lib.hpp>
#include <arrowbound/Module.hpp>

#include "HookshotInput.hpp"
#include "TraversalOwnership.hpp"
#include "modules/zonai-hookshot/ChainRenderer.hpp"
#include "modules/zonai-hookshot/ChainShaderPass.hpp"
#include "modules/zonai-hookshot/HookshotHookInstaller.hpp"
#include "modules/zonai-hookshot/HookshotHooks.hpp"
#include "modules/zonai-hookshot/HookshotIntegration.hpp"

namespace {

constexpr ptrdiff_t kNpadCalc = 0x02A267BC;
constexpr ptrdiff_t kRayCastWorker = 0x00858590;
constexpr std::uint64_t kButtonZR = 1ull << 9;
totk::engine::NpadReader g_input;
std::uint64_t g_lastButtons = 0;

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
        arrowbound::onRaycast(&OriginalThunk, from, object);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        Orig(device);
        namespace manual = zonai_hookshot::integration;
        namespace input = zonai_hookshot::input;
        const auto frame = g_input.read(device);
        const auto& snapshot = frame.snapshot();
        const bool fresh = snapshot.freshSampleCount > 0;
        if (fresh) g_lastButtons = snapshot.buttons;
        const auto decision = zonai_hookshot::pure::traversalOwnership({
            manual::ownsMovement(), arrowbound::movementEngaged(),
            fresh, (g_lastButtons & kButtonZR) != 0,
            (g_lastButtons & input::kAimChord) == input::kAimChord,
            (g_lastButtons & input::kButtonB) != 0});
        zonai_hookshot::pure::dispatchTraversal(decision,
            [] { manual::yieldMovement(); },
            [] { arrowbound::yieldMovement(); },
            [device](bool allowed) { arrowbound::tick(device, allowed); },
            [device](bool allowed) {
                manual::allowActivation(allowed);
                wwpg::modules::zonaiHookshot().tick(device);
                zonai_hookshot::hooks::afterNpad(device);
            });
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
    arrowbound::initialize(mainBase);
    arrowbound::enter();
    RayCastWorkerHook::InstallAtOffset(kRayCastWorker);
    NpadCalcHook::InstallAtOffset(kNpadCalc);
    zonai_hookshot::hooks::installUnique(mainBase, {
        arrowbound::wall_grip::onControllerUpdate,
        arrowbound::parasail::onEnterHook,
        arrowbound::parasail::onUpdateHook,
        arrowbound::parasail::onLeaveHook,
        arrowbound::parasail::onFallEnterHook,
        arrowbound::parasail::onFallUpdateHook,
        arrowbound::parasail::onFallLeaveHook,
        arrowbound::wall_grip::onClimbUpdate,
        arrowbound::parasail::onGlideEntryPredicate,
        arrowbound::sharedGripHooksReady});
    arrowbound::installFeatureHooks(mainBase);
    Logging.Log("[glideshot] 0.9.1 + Arrowbound loaded (shared hooks, exclusive traversal)");
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
