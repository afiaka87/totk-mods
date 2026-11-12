// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

#include <lib.hpp>
#include <arrowbound/ActiveGame.hpp>
#include <arrowbound/Module.hpp>

#include "ActivationButtons.hpp"
#include "HookshotInput.hpp"
#include "TraversalOwnership.hpp"
#include "modules/zonai-hookshot/ChainRenderer.hpp"
#include "modules/zonai-hookshot/ChainShaderPass.hpp"
#include "modules/zonai-hookshot/HookshotHookInstaller.hpp"
#include "modules/zonai-hookshot/HookshotHooks.hpp"
#include "modules/zonai-hookshot/HookshotIntegration.hpp"
#include "ActionContext.hpp"

namespace {

constexpr std::uint64_t kButtonZR = 1ull << 9;
totk::engine::NpadReader g_input;
std::uint64_t g_lastButtons = 0;
// Zonai Fieldworks clears this when the player switches the hookshot off; the standalone module is always on.
bool g_switchedOn = true;

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

void updateTraversal(void* device) {
    namespace manual = zonai_hookshot::integration;
    namespace input = zonai_hookshot::input;
    const auto frame = g_input.read(device);
    const auto& snapshot = frame.snapshot();
    const bool fresh = snapshot.freshSampleCount > 0;
    if (fresh) g_lastButtons = snapshot.buttons;
    const auto decision = zonai_hookshot::pure::traversalOwnership({
        manual::ownsMovement(), arrowbound::movementEngaged(),
        fresh, (g_lastButtons & kButtonZR) != 0,
        (g_lastButtons & zonai_hookshot::input_policy::kAimChord) ==
            zonai_hookshot::input_policy::kAimChord,
        (g_lastButtons & input::kButtonB) != 0});
    zonai_hookshot::pure::dispatchTraversal(decision,
        [] { manual::yieldMovement(); },
        [] { arrowbound::yieldMovement(); },
        [device](bool allowed) { arrowbound::tick(device, allowed && g_switchedOn); },
        [device](bool allowed) {
            // Diagnostic (Zonai Fieldworks d5): once a second while the aim buttons are held and the
            // hookshot is not allowed to start, who holds the movement.
            static unsigned refusedTicks = 0;
            const bool aimHeld = (g_lastButtons & zonai_hookshot::input_policy::kAimChord) ==
                                 zonai_hookshot::input_policy::kAimChord;
            if (aimHeld && !(allowed && g_switchedOn)) {
                if (refusedTicks++ % 60 == 0)
                    Logging.Log("[zonai-hookshot] AIM_REFUSED allowed=%u switched_on=%u arrow_engaged=%u "
                                "hookshot_owns=%u",
                                unsigned(allowed), unsigned(g_switchedOn),
                                unsigned(arrowbound::movementEngaged()), unsigned(manual::ownsMovement()));
            } else {
                refusedTicks = 0;
            }
            manual::allowActivation(allowed && g_switchedOn);
            wwpg::modules::zonaiHookshot().tick(device);
            zonai_hookshot::hooks::afterNpad(device);
        });
#ifndef TOTK_SUITE_MEMBER
    zonai_hookshot::log::sdLogService();  // Fieldworks logs to the emulator log only (design 06)
#endif
}

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        Orig(device);
        updateTraversal(device);
    }
};

}  // namespace

namespace zonai_hookshot::suite {

// Shared by the standalone module and Zonai Fieldworks; false = unknown game build, nothing installed.
bool install(std::uintptr_t mainBase) {
    const auto textSize = exl::util::GetMainModuleInfo().m_Text.m_Size;
    const auto* game = arrowbound::profiles::activate(mainBase, textSize);
    if (!game) {
        Logging.Log("[zonai-hookshot] " HOOKSHOT_VERSION " unknown game build main_text=%zx; nothing installed",
                    textSize);
        return false;
    }
    Logging.Log("[zonai-hookshot] " HOOKSHOT_VERSION " selected TotK %s main_text=%zx self=%lx",
                game->name, textSize, exl::util::modules::GetSelfStart() - mainBase);
    zonai_hookshot::action::useGameProfile(game->version);
    zonai_hookshot::render::configure(mainBase);
    zonai_hookshot::render::installShaderPass(mainBase);
    const auto& module = wwpg::modules::zonaiHookshot();
    module.init(mainBase);
    module.enter();
    arrowbound::initialize(mainBase);
    arrowbound::enter();
    if (arrowbound::profiles::entryHookable(mainBase, game->hooks.raycastWorker, "raycast"))
        RayCastWorkerHook::InstallAtOffset(game->hooks.raycastWorker.offset);
    if (arrowbound::profiles::entryHookable(mainBase, game->hooks.npadCalc, "npad"))
        NpadCalcHook::InstallAtOffset(game->hooks.npadCalc.offset);
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
    Logging.Log("[zonai-hookshot] " HOOKSHOT_VERSION " + Arrowbound loaded for TotK %s (shared hooks, exclusive traversal)",
                game->name);
    return true;
}

#ifdef TOTK_SUITE_MEMBER
// Zonai Fieldworks, on the controller thread. Off ends a hookshot or arrow trip as B would and refuses new
// ones; the presentation hooks run a little longer to show the glider and arm again (design 06, section 3a).
void enabled(bool on) {
    g_switchedOn = on;
    const auto& module = wwpg::modules::zonaiHookshot();
    if (on) {
        module.enter();
        arrowbound::enter();
    } else {
        module.requestExit();
        arrowbound::enter();  // ends the arrow trip and its sound
    }
    g_lastButtons = 0;
    Logging.Log("[zonai-hookshot] SWITCHED %s", on ? "on" : "off");
}
#endif

}  // namespace zonai_hookshot::suite

#ifndef TOTK_SUITE_MEMBER
extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();
    (void)zonai_hookshot::suite::install(exl::util::modules::GetTargetStart());
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
#endif
