// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Hook installation shared by the standalone host and Fieldworks.
#include "HookshotHookInstaller.hpp"

#include <lib.hpp>

#include "ClimbCapture.hpp"
#include "ParasailHandoff.hpp"
#include "TransportController.hpp"

namespace {
namespace transport = zonai_hookshot::transport;
namespace parasail = zonai_hookshot::parasail;
namespace capture = zonai_hookshot::capture;

constexpr ptrdiff_t kNpadControllerCalcImpl = 0x024789BC;
constexpr ptrdiff_t kParasailEnter = 0x01D6E54C;
constexpr ptrdiff_t kParasailUpdate = 0x01D6E810;
constexpr ptrdiff_t kParasailLeave = 0x01D6F2A0;
constexpr ptrdiff_t kFallEnter = 0x01D61428;
constexpr ptrdiff_t kFallUpdate = 0x01D61790;
constexpr ptrdiff_t kFallLeave = 0x01D61988;
constexpr ptrdiff_t kClimbUpdate = 0x01D56D50;
constexpr ptrdiff_t kJumpMultiplierSelect = 0x0107EA8C;
constexpr ptrdiff_t kJumpHeightClampSetup = 0x0107EA94;
constexpr u32 kJumpMultiplierSelectWord = 0x1E281C21;
constexpr u32 kJumpHeightClampWord = 0x1E229001;
constexpr ptrdiff_t kGlideEntryPredicate = 0x01722210;

HOOK_DEFINE_TRAMPOLINE(NpadControllerCalcImplHook) {
    static void Callback(void* controller) {
        Orig(controller);
        capture::applyContinuousForward(controller);
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailEnterHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onEnterHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailUpdateHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onUpdateHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ParasailLeaveHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onLeaveHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallEnterHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        transport::onFallEnterHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallUpdateHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        transport::onFallUpdateHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallLeaveHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        transport::onFallLeaveHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ClimbUpdateHook) {
    static u64 Callback(void* a1, void* a2) {
        capture::onClimbUpdateHook(a1);
        return Orig(a1, a2);
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
        return parasail::onGlideEntryPredicate(native) ? 1 : 0;
    }
};

bool hookWordMatches(uintptr_t base, ptrdiff_t offset, u32 expected,
                     const char* label) {
    const u32 actual = *reinterpret_cast<const u32*>(base + offset);
    if (actual == expected) return true;
    Logging.Log(
        "[zonai-hookshot] JUMP BOOST DISABLED %s: main+%p word %08x != "
        "expected %08x (version/cheat conflict)",
        label, reinterpret_cast<void*>(offset), actual, expected);
    return false;
}

}  // namespace

namespace zonai_hookshot::hooks {
void installUnique(std::uintptr_t mainBase) {
    NpadControllerCalcImplHook::InstallAtOffset(kNpadControllerCalcImpl);
    ParasailEnterHook::InstallAtOffset(kParasailEnter);
    ParasailUpdateHook::InstallAtOffset(kParasailUpdate);
    ParasailLeaveHook::InstallAtOffset(kParasailLeave);
    FallEnterHook::InstallAtOffset(kFallEnter);
    FallUpdateHook::InstallAtOffset(kFallUpdate);
    FallLeaveHook::InstallAtOffset(kFallLeave);
    ClimbUpdateHook::InstallAtOffset(kClimbUpdate);
    GlideEntryPredicateHook::InstallAtOffset(kGlideEntryPredicate);
    if (hookWordMatches(mainBase, kJumpMultiplierSelect,
                        kJumpMultiplierSelectWord, "jump source") &&
        hookWordMatches(mainBase, kJumpHeightClampSetup, kJumpHeightClampWord,
                        "jump hook")) {
        JumpBoostHook::InstallAtOffset(kJumpHeightClampSetup);
        Logging.Log(
            "[zonai-hookshot] jump boost hook installed @ main+%p "
            "(one-shot 4x; inactive=vanilla)",
            reinterpret_cast<void*>(kJumpHeightClampSetup));
    }
}

void afterNpad(void* device) { transport::applyLaunchInjection(device); }

}  // namespace zonai_hookshot::hooks
