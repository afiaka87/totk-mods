// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ArrowboundHookInstaller.hpp"

#include <lib.hpp>

#include "ArrowHookshotService.hpp"
#include "ArrowModeService.hpp"
#include "CarrierSlotAppearance.hpp"
#include "ParasailHandoff.hpp"
#include "HookshotLog.hpp"
#include "WallGripService.hpp"

namespace {
namespace parasail = arrowbound::parasail;

constexpr ptrdiff_t kParasailEnter = 0x01D6E54C;
constexpr ptrdiff_t kParasailLoopEntry = 0x01D6E5BC;
constexpr ptrdiff_t kParasailUpdate = 0x01D6E810;
constexpr ptrdiff_t kParasailLeave = 0x01D6F2A0;
constexpr ptrdiff_t kFallEnter = 0x01D61428;
constexpr ptrdiff_t kFallUpdate = 0x01D61790;
constexpr ptrdiff_t kFallLeave = 0x01D61988;
constexpr ptrdiff_t kGlideEntryPredicate = 0x01722210;
constexpr ptrdiff_t kBowGetResult = 0x01D2E890;
constexpr ptrdiff_t kSerializeSaveData = 0x0241469C;
constexpr ptrdiff_t kNpadControllerCalcImpl = 0x024789BC;
constexpr ptrdiff_t kClimbUpdate = 0x01D56D50;

constexpr ptrdiff_t kEquipmentReleaseArrow = 0x015D65BC;
constexpr ptrdiff_t kArrowControllerUpdate = 0x0173B034;
constexpr ptrdiff_t kArrowImpactClassify = 0x0173C920;
constexpr ptrdiff_t kArrowWorldSweep = 0x0173E298;
constexpr ptrdiff_t kPouchOnSelectSlot = 0x01B16650;
constexpr ptrdiff_t kPouchHandleAction = 0x01B1241C;
constexpr ptrdiff_t kPouchSetSlot = 0x01A58EE0;
constexpr ptrdiff_t kPouchSelectionDeal = 0x01B171AC;
constexpr ptrdiff_t kFindMessage = 0x00D7AF44;

constexpr u32 kEquipmentReleaseArrowWord = 0xD10283FF;
constexpr u32 kArrowControllerUpdateWord = 0xD10283FF;
constexpr u32 kArrowImpactClassifyWord = 0xFC160FEE;
constexpr u32 kArrowWorldSweepWord = 0x6DB63BEF;
constexpr u32 kPouchOnSelectSlotWord = 0xD10343FF;
constexpr u32 kPouchHandleActionWord = 0xFC190FEA;
constexpr u32 kPouchSetSlotWord = 0xD107C3FF;
constexpr u32 kBowGetResultWord = 0xA9BD7BFD;
constexpr u32 kSerializeSaveDataWord = 0xD10103FF;
constexpr u32 kParasailLoopEntryWord = 0xF0014628;
constexpr u32 kNpadControllerCalcImplWord = 0x6DB923E9;
constexpr u32 kClimbUpdateWord = 0xA9BE7BFD;
constexpr u32 kPouchSelectionDealWord = 0x910083E0;
constexpr u32 kFindMessageWord = 0xD10583FF;

HOOK_DEFINE_INLINE(PouchSelectionDealHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        ctx->X[1] = arrowbound::arrow_mode::selectionDeal(ctx->X[1]);
    }
};

HOOK_DEFINE_TRAMPOLINE(FindMessageHook) {
    static u64 Callback(void* manager, void* output, const char* const* table, const char* const* key) {
        if (arrowbound::arrow_mode::replaceMessage(output, table, key)) return 0;
        return Orig(manager, output, table, key);
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadControllerCalcImplHook) {
    static void Callback(void* controller) {
        Orig(controller);
        arrowbound::wall_grip::onControllerUpdate(controller);
    }
};

HOOK_DEFINE_TRAMPOLINE(ClimbUpdateHook) {
    static u64 Callback(void* action, void* context) {
        arrowbound::wall_grip::onClimbUpdate(action);
        return Orig(action, context);
    }
};

HOOK_DEFINE_INLINE(ParasailLoopEntryHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        // Integer-only: ANDS W24,W0,#1 follows the relocated ADRP at this site.
        ctx->X[0] = arrowbound::pure::arrowFlightParasailEntry(
            ctx->X[0], arrowbound::arrow_hookshot::keepsParagliderPresented());
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
        parasail::onFallEnterHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallUpdateHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onFallUpdateHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(FallLeaveHook) {
    static u64 Callback(void* a1, void* a2, void* a3) {
        const u64 result = Orig(a1, a2, a3);
        parasail::onFallLeaveHook(a1);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(GlideEntryPredicateHook) {
    static u64 Callback(void* a1, float a2) {
        const bool native = (Orig(a1, a2) & 1) != 0;
        return parasail::onGlideEntryPredicate(native) ? 1 : 0;
    }
};

HOOK_DEFINE_TRAMPOLINE(BowGetResultHook) {
    static u64 Callback(void* action) {
        const u64 native = Orig(action);
        const auto result = arrowbound::pure::arrowFlightBowResult(
            native, arrowbound::arrow_hookshot::keepsParagliderPresented());
        if (result != native) ZHLOG("ARROW_BOW_FINISH native=%llu result=%llu", native, result);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(SerializeSaveDataHook) {
    static u64 Callback(void* manager, u32 fileIndex) {
        const auto result = Orig(manager, fileIndex);
        arrowbound::arrow_mode::filterSerializedSave(manager, fileIndex);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(EquipmentReleaseArrowHook) {
    static u64 Callback(void* equipmentUser) {
        arrowbound::arrow_hookshot::onArrowRelease(equipmentUser);
        return Orig(equipmentUser);
    }
};

HOOK_DEFINE_TRAMPOLINE(ArrowControllerUpdateHook) {
    static u64 Callback(void* controller, float* deltaTime) {
        arrowbound::arrow_hookshot::onArrowUpdate(controller);
        const u64 result = Orig(controller, deltaTime);
        arrowbound::arrow_hookshot::onArrowSample(controller);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ArrowImpactClassifyHook) {
    static u64 Callback(void* controller, int* hitType, void* hitId16,
                        void* motionContext, float* adjustedHit, float deltaTime) {
        const u64 result = Orig(controller, hitType, hitId16, motionContext,
                                adjustedHit, deltaTime);
        arrowbound::arrow_hookshot::onArrowImpact(
            controller, (result & 1u) != 0, hitType ? *hitType : 0,
            adjustedHit, motionContext);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ArrowWorldSweepHook) {
    static u64 Callback(void* controller, bool* a2, bool* a3, unsigned char* a4,
                        unsigned char* a5, unsigned char* a6, bool* a7, float* a8,
                        float step, float unscaledStep, void* motionContext,
                        unsigned char a12) {
        const u64 result = Orig(controller, a2, a3, a4, a5, a6, a7, a8,
                                step, unscaledStep, motionContext, a12);
        arrowbound::arrow_hookshot::onArrowWorldImpact(
            controller, (result & 1u) != 0, a6 && a7 && *a6 && *a7);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PouchOnSelectSlotHook) {
    static u64 Callback(void* screen, u32 category, u32 index, void* arg4) {
        arrowbound::arrow_mode::beginSelection(category, index);
        const u64 result = Orig(screen, category, index, arg4);
        arrowbound::arrow_mode::endSelection(screen);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PouchHandleActionHook) {
    static u64 Callback(void* screen, int action) {
        const bool owned = arrowbound::arrow_mode::beginAction(action);
        const u64 result = Orig(screen, owned ? -1 : action);
        if (owned) arrowbound::arrow_mode::refreshSelectedSlot(screen);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PouchSetSlotHook) {
    static u64 Callback(void* list, void* slot, void* options) {
        const arrowbound::engine::CarrierSlotAppearance appearance(
            options, arrowbound::arrow_mode::isCarrierSlot(options),
            arrowbound::arrow_mode::enabled());
        return Orig(list, slot, options);
    }
};

bool hookWordMatches(uintptr_t base, ptrdiff_t offset, u32 expected,
                     const char* label) {
    const u32 actual = *reinterpret_cast<const u32*>(base + offset);
    if (actual == expected) return true;
    Logging.Log(
        "[arrowbound] HOOK DISABLED %s: main+%p word %08x != expected %08x "
        "(version/cheat conflict)",
        label, reinterpret_cast<void*>(offset), actual, expected);
    return false;
}
}

namespace arrowbound::hooks {
void install(std::uintptr_t mainBase, bool installShared) {
    if (installShared) {
        ParasailEnterHook::InstallAtOffset(kParasailEnter);
        ParasailUpdateHook::InstallAtOffset(kParasailUpdate);
        ParasailLeaveHook::InstallAtOffset(kParasailLeave);
        FallEnterHook::InstallAtOffset(kFallEnter);
        FallUpdateHook::InstallAtOffset(kFallUpdate);
        FallLeaveHook::InstallAtOffset(kFallLeave);
        GlideEntryPredicateHook::InstallAtOffset(kGlideEntryPredicate);
        if (hookWordMatches(mainBase, kNpadControllerCalcImpl, kNpadControllerCalcImplWord, "grip input") &&
            hookWordMatches(mainBase, kClimbUpdate, kClimbUpdateWord, "grip climb release")) {
            NpadControllerCalcImplHook::InstallAtOffset(kNpadControllerCalcImpl);
            ClimbUpdateHook::InstallAtOffset(kClimbUpdate);
            arrowbound::runtime().grip.hooksReady.store(1, std::memory_order_release);
        }
    }

#define INSTALL_VERIFIED(Hook, Offset, Word, Label)                         \
    do {                                                                    \
        if (hookWordMatches(mainBase, Offset, Word, Label))                 \
            Hook::InstallAtOffset(Offset);                                  \
    } while (false)

    INSTALL_VERIFIED(EquipmentReleaseArrowHook, kEquipmentReleaseArrow,
                     kEquipmentReleaseArrowWord, "arrow release");
    INSTALL_VERIFIED(ArrowControllerUpdateHook, kArrowControllerUpdate,
                     kArrowControllerUpdateWord, "arrow update");
    INSTALL_VERIFIED(ArrowImpactClassifyHook, kArrowImpactClassify,
                     kArrowImpactClassifyWord, "arrow impact");
    INSTALL_VERIFIED(ArrowWorldSweepHook, kArrowWorldSweep,
                     kArrowWorldSweepWord, "arrow world impact");
    // Install the menu and its action interception together, never a native sage action alone.
    const bool emblemReady =
        hookWordMatches(mainBase, kPouchOnSelectSlot, kPouchOnSelectSlotWord, "pouch selection") &&
        hookWordMatches(mainBase, kPouchHandleAction, kPouchHandleActionWord, "pouch action") &&
        hookWordMatches(mainBase, kPouchSetSlot, kPouchSetSlotWord, "emblem appearance") &&
        hookWordMatches(mainBase, kPouchSelectionDeal, kPouchSelectionDealWord, "emblem menu type") &&
        hookWordMatches(mainBase, kFindMessage, kFindMessageWord, "emblem literal text") &&
        hookWordMatches(mainBase, kSerializeSaveData, kSerializeSaveDataWord, "unsaved emblem carrier");
    if (emblemReady) {
        PouchOnSelectSlotHook::InstallAtOffset(kPouchOnSelectSlot);
        PouchHandleActionHook::InstallAtOffset(kPouchHandleAction);
        PouchSetSlotHook::InstallAtOffset(kPouchSetSlot);
        PouchSelectionDealHook::InstallAtOffset(kPouchSelectionDeal);
        FindMessageHook::InstallAtOffset(kFindMessage);
        SerializeSaveDataHook::InstallAtOffset(kSerializeSaveData);
    }
    arrowbound::arrow_mode::setCodeOnlyReady(emblemReady);
    Logging.Log("[arrowbound] CODE_ONLY_EMBLEM ready=%u", emblemReady);
    INSTALL_VERIFIED(BowGetResultHook, kBowGetResult,
                      kBowGetResultWord, "released bow completion");
    INSTALL_VERIFIED(ParasailLoopEntryHook, kParasailLoopEntry,
                      kParasailLoopEntryWord, "arrow steady-glide entry");

#undef INSTALL_VERIFIED
}
}
