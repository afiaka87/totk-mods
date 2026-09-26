// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Hook installation shared by the standalone host and Fieldworks.
#include "HookshotHookInstaller.hpp"

#include <lib.hpp>

#include "ClimbCapture.hpp"
#include "ParasailHandoff.hpp"
#include "TransportController.hpp"
#include "GripHooks.hpp"
#include <arrowbound/ActiveGame.hpp>

namespace {
namespace transport = zonai_hookshot::transport;
namespace parasail = zonai_hookshot::parasail;
namespace capture = zonai_hookshot::capture;
zonai_hookshot::hooks::Observers g_observers{};

HOOK_DEFINE_TRAMPOLINE(NpadControllerCalcImplHook) {
    static void Callback(void* controller) {
        Orig(controller);
        capture::applyContinuousForward(controller);
        if (g_observers.controller) g_observers.controller(controller);
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailEnterHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onEnterHook(a1);
        if (g_observers.parasailEnter) g_observers.parasailEnter(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailUpdateHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onUpdateHook(a1);
        if (g_observers.parasailUpdate) g_observers.parasailUpdate(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailLeaveHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onLeaveHook(a1);
        if (g_observers.parasailLeave) g_observers.parasailLeave(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallEnterHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        transport::onFallEnterHook(a1);
        if (g_observers.fallEnter) g_observers.fallEnter(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallUpdateHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        transport::onFallUpdateHook(a1);
        if (g_observers.fallUpdate) g_observers.fallUpdate(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallLeaveHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        transport::onFallLeaveHook(a1);
        if (g_observers.fallLeave) g_observers.fallLeave(a1);
        return result;
    }
};

struct ClimbUpdateHook {
    inline static zonai_hookshot::hooks::ClimbCallback previous{};
    static std::uint64_t Callback(void* a1, void* a2, void* a3) {
        capture::onClimbUpdateHook(a1);
        if (g_observers.climbUpdate) g_observers.climbUpdate(a1);
        return previous(a1, a2, a3);
    }
};

HOOK_DEFINE_INLINE(JumpBoostHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        if (transport::consumeJumpBoost()) ctx->S[0] *= 4.0f;
    }
};

HOOK_DEFINE_TRAMPOLINE(GlideEntryPredicateHook) {
    static u64 Callback(void* a1, float a2) {
        const bool native = (Orig(a1, a2) & 1) != 0;
        const bool manual = parasail::onGlideEntryPredicate(native);
        return (g_observers.glideEntry ? g_observers.glideEntry(manual) : manual) ? 1 : 0;
    }
};

bool hookWordMatches(uintptr_t base, const arrowbound::profiles::Site& site,
                     const char* label) {
    const u32 actual = *reinterpret_cast<const u32*>(base + site.offset);
    if (actual == site.word) return true;
    Logging.Log(
        "[zonai-hookshot] HOOK DISABLED %s: main+%p word %08x != "
        "expected %08x (version/cheat conflict)",
        label, reinterpret_cast<void*>(site.offset), actual, site.word);
    return false;
}

}  // namespace

namespace zonai_hookshot::hooks {
void installUnique(std::uintptr_t mainBase, const Observers& observers) {
    const auto* game = arrowbound::profiles::active();
    if (!game) return;
    const auto& h = game->hooks;
    g_observers = observers;
    static std::uintptr_t s_gripController{};
    s_gripController = h.gripController.offset;
    const GripSites grip{std::uintptr_t(h.gripController.offset), h.gripController.word,
                         std::uintptr_t(h.climbUpdate.offset), h.climbUpdate.word};
    const bool gripReady = installGripHooks(mainBase, grip, ClimbUpdateHook::Callback,
        ClimbUpdateHook::previous, [] {
            NpadControllerCalcImplHook::InstallAtOffset(s_gripController);
        });
    if (g_observers.gripHooksReady) g_observers.gripHooksReady(gripReady);
    const auto entry = [mainBase](const arrowbound::profiles::Site& site, const char* label) {
        return arrowbound::profiles::entryHookable(mainBase, site, label);
    };
    if (entry(h.parasailEnter, "parasail enter"))
        ParasailEnterHook::InstallAtOffset(h.parasailEnter.offset);
    if (entry(h.parasailUpdate, "parasail update"))
        ParasailUpdateHook::InstallAtOffset(h.parasailUpdate.offset);
    if (entry(h.parasailLeave, "parasail leave"))
        ParasailLeaveHook::InstallAtOffset(h.parasailLeave.offset);
    if (entry(h.fallEnter, "fall enter")) FallEnterHook::InstallAtOffset(h.fallEnter.offset);
    if (entry(h.fallUpdate, "fall update")) FallUpdateHook::InstallAtOffset(h.fallUpdate.offset);
    if (entry(h.fallLeave, "fall leave")) FallLeaveHook::InstallAtOffset(h.fallLeave.offset);
    if (entry(h.glideEntry, "glide admission"))
        GlideEntryPredicateHook::InstallAtOffset(h.glideEntry.offset);
    if (hookWordMatches(mainBase, h.jumpSelect, "jump source") &&
        hookWordMatches(mainBase, h.jumpHook, "jump hook")) {
        JumpBoostHook::InstallAtOffset(h.jumpHook.offset);
        Logging.Log(
            "[zonai-hookshot] jump boost hook installed @ main+%p "
            "(one-shot 4x; inactive=vanilla)",
            reinterpret_cast<void*>(h.jumpHook.offset));
    }
}

void afterNpad(void* device) { transport::applyLaunchInjection(device); }

}  // namespace zonai_hookshot::hooks
